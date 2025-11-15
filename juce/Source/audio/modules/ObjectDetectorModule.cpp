#include "ObjectDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/theme/ThemeManager.h"
#include <juce_opengl/juce_opengl.h>
#include <map>
#include <unordered_map>
#endif

// YOLOv3 default input size
static constexpr int YOLO_INPUT_SIZE = 416;
// Embedded COCO-80 labels used if coco.names is missing
static const char* kCoco80[] = {
    "person","bicycle","car","motorbike","aeroplane","bus","train","truck","boat","traffic light",
    "fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
    "elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle",
    "wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange",
    "broccoli","carrot","hot dog","pizza","donut","cake","chair","sofa","pottedplant","bed",
    "diningtable","toilet","tvmonitor","laptop","mouse","remote","keyboard","cell phone","microwave","oven",
    "toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"
};

juce::AudioProcessorValueTreeState::ParameterLayout ObjectDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "targetClass", "Target Class", juce::StringArray{ "person" }, 0));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "confidence", "Confidence", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "useGpu", "Use GPU (CUDA)", defaultGpu));
    
    return { params.begin(), params.end() };
}

ObjectDetectorModule::ObjectDetectorModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(9), true)  // 5 existing + 4 zone gates
                      .withOutput("Video Out", juce::AudioChannelSet::mono(), true)    // PASSTHROUGH
                      .withOutput("Cropped Out", juce::AudioChannelSet::mono(), true)), // CROPPED
      juce::Thread("Object Detector Thread"),
      apvts(*this, nullptr, "ObjectDetectorParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    targetClassParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("targetClass"));
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    
    fifoBuffer.resize(16);
    
    loadModel();
}

ObjectDetectorModule::~ObjectDetectorModule()
{
    stopThread(5000);
}

void ObjectDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void ObjectDetectorModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void ObjectDetectorModule::loadModel()
{
    // Find assets next to executable
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir = exeFile.getParentDirectory();
    juce::File assetsDir = appDir.getChildFile("assets");
    
    juce::File weights = assetsDir.getChildFile("yolov3.weights");
    juce::File cfg     = assetsDir.getChildFile("yolov3.cfg");
    // Fallback to tiny if standard is missing
    if (!weights.existsAsFile() || !cfg.existsAsFile())
    {
        juce::File wTiny = assetsDir.getChildFile("yolov3-tiny.weights");
        juce::File cTiny = assetsDir.getChildFile("yolov3-tiny.cfg");
        if (wTiny.existsAsFile() && cTiny.existsAsFile())
        {
            weights = wTiny; cfg = cTiny;
            juce::Logger::writeToLog("[ObjectDetector] Using YOLOv3-tiny assets.");
        }
    }
    juce::File names = assetsDir.getChildFile("coco.names");
 
    juce::Logger::writeToLog("[ObjectDetector] Assets directory: " + assetsDir.getFullPathName());
    
    if (weights.existsAsFile() && cfg.existsAsFile())
    {
        try
        {
            net = cv::dnn::readNetFromDarknet(cfg.getFullPathName().toStdString(), weights.getFullPathName().toStdString());
            
            // CRITICAL: Set backend immediately after loading model
            #if WITH_CUDA_SUPPORT
                bool useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    juce::Logger::writeToLog("[ObjectDetector] ✓ Model loaded with CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[ObjectDetector] Model loaded with CPU backend");
                }
            #else
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[ObjectDetector] Model loaded with CPU backend (CUDA not compiled)");
            #endif
            
            // Load class names if available
            classNames.clear();
            if (names.existsAsFile())
            {
                std::ifstream ifs(names.getFullPathName().toStdString().c_str());
                std::string line;
                while (std::getline(ifs, line))
                    if (!line.empty()) classNames.push_back(line);
            }
            if (classNames.empty())
            {
                classNames.assign(std::begin(kCoco80), std::end(kCoco80));
                juce::Logger::writeToLog("[ObjectDetector] coco.names missing; using embedded COCO-80 labels.");
            }
            
            // Note: UI combo is populated from classNames directly
            
            modelLoaded = true;
            juce::Logger::writeToLog("[ObjectDetector] YOLOv3 model loaded successfully");
        }
        catch (const cv::Exception& e)
        {
            juce::Logger::writeToLog("[ObjectDetector] OpenCV exception: " + juce::String(e.what()));
            modelLoaded = false;
        }
    }
    else
    {
        juce::Logger::writeToLog("[ObjectDetector] FAILED: Could not find YOLO model files in " + assetsDir.getFullPathName());
        modelLoaded = false;
    }
}

void ObjectDetectorModule::run()
{
    // Resolve our logical ID once at the start
    juce::uint32 myLogicalId = storedLogicalId;
    if (myLogicalId == 0 && parentSynth != nullptr)
    {
        for (const auto& info : parentSynth->getModulesInfo())
        {
            if (parentSynth->getModuleForLogical(info.first) == this)
            {
                myLogicalId = info.first;
                storedLogicalId = myLogicalId; // Cache it
                break;
            }
        }
    }
    
    #if WITH_CUDA_SUPPORT
        bool lastGpuState = false; // Track GPU state to minimize backend switches
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        if (!modelLoaded)
        {
            wait(200);
            continue;
        }
        
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        if (!frame.empty())
        {
            // Save a clean copy for cropping (before drawing annotations)
            cv::Mat originalFrame;
            frame.copyTo(originalFrame);
            
            bool useGpu = false;
            
            #if WITH_CUDA_SUPPORT
                // Check if user wants GPU and if CUDA device is available
                useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
                {
                    useGpu = false; // Fallback to CPU
                    if (!loggedGpuWarning)
                    {
                        juce::Logger::writeToLog("[ObjectDetector] WARNING: GPU requested but no CUDA device found. Using CPU.");
                        loggedGpuWarning = true;
                    }
                }
                
                // Set DNN backend only when state changes (expensive operation)
                if (useGpu != lastGpuState)
                {
                    if (useGpu)
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                        juce::Logger::writeToLog("[ObjectDetector] ✓ Switched to CUDA backend (GPU)");
                    }
                    else
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                        juce::Logger::writeToLog("[ObjectDetector] Switched to CPU backend");
                    }
                    lastGpuState = useGpu;
                }
            #endif
            
            // Prepare input blob
            // NOTE: For DNN models, blobFromImage works on CPU
            // The GPU acceleration happens in net.forward() when backend is set to CUDA
            cv::Mat blob = cv::dnn::blobFromImage(
                frame,
                1.0 / 255.0,
                cv::Size(YOLO_INPUT_SIZE, YOLO_INPUT_SIZE),
                cv::Scalar(),
                true,
                false);
            net.setInput(blob);
            
            // Forward through YOLO (GPU-accelerated if backend is CUDA)
            std::vector<cv::Mat> outs;
            net.forward(outs, net.getUnconnectedOutLayersNames());
            
            std::vector<int> classIds;
            std::vector<float> confidences;
            std::vector<cv::Rect> boxes;
            
            int targetClassId = juce::jlimit(0, (int)juce::jmax<size_t>(1, classNames.size()) - 1, selectedClassId.load());
            float confThreshold = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.5f;
            
            for (const auto& out : outs)
            {
                const float* data = reinterpret_cast<const float*>(out.data);
                for (int i = 0; i < out.rows; ++i, data += out.cols)
                {
                    float score;
                    int classId = -1;
                    // Scores start at index 5
                    cv::Mat scores(1, out.cols - 5, CV_32F, (void*)(data + 5));
                    cv::Point classIdPoint;
                    double maxClassScore;
                    cv::minMaxLoc(scores, nullptr, &maxClassScore, nullptr, &classIdPoint);
                    score = static_cast<float>(maxClassScore);
                    classId = classIdPoint.x;
                    
                    if (score > confThreshold && classId == targetClassId)
                    {
                        int centerX = static_cast<int>(data[0] * frame.cols);
                        int centerY = static_cast<int>(data[1] * frame.rows);
                        int width   = static_cast<int>(data[2] * frame.cols);
                        int height  = static_cast<int>(data[3] * frame.rows);
                        int left    = centerX - width / 2;
                        int top     = centerY - height / 2;
                        classIds.push_back(classId);
                        confidences.push_back(score);
                        boxes.emplace_back(left, top, width, height);
                    }
                }
            }
            
            std::vector<int> indices;
            cv::dnn::NMSBoxes(boxes, confidences, confThreshold, 0.4f, indices);
            
            ObjectDetectionResult result;
            // Initialize zone hits to false
            for (int z = 0; z < 4; ++z)
                result.zoneHits[z] = false;
            
            if (indices.empty())
            {
                // Clear cropped output when no object detected
                cv::Mat emptyFrame;
                VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), emptyFrame);
            }
            else
            {
                int bestIdx = indices[0];
                const cv::Rect& box = boxes[bestIdx];
                result.detected = true;
                result.x = juce::jlimit(0.0f, 1.0f, (float)(box.x + box.width * 0.5f) / (float)frame.cols);
                result.y = juce::jlimit(0.0f, 1.0f, (float)(box.y + box.height * 0.5f) / (float)frame.rows);
                result.width  = juce::jlimit(0.0f, 1.0f, (float)box.width / (float)frame.cols);
                result.height = juce::jlimit(0.0f, 1.0f, (float)box.height / (float)frame.rows);
                
                // Check zone hits: object center (x, y) is checked against each color zone
                for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                {
                    std::vector<ZoneRect> rects;
                    loadZoneRects(colorIdx, rects);
                    
                    bool hit = false;
                    for (const auto& rect : rects)
                    {
                        // Point-in-rectangle check for object center
                        if (result.x >= rect.x && result.x <= rect.x + rect.width &&
                            result.y >= rect.y && result.y <= rect.y + rect.height)
                        {
                            hit = true;
                            break;
                        }
                    }
                    result.zoneHits[colorIdx] = hit;
                }
                
                // --- CROPPED OUTPUT LOGIC ---
                if (box.area() > 0)
                {
                    // Ensure the box is within the frame to prevent crash
                    cv::Rect validBox = box & cv::Rect(0, 0, originalFrame.cols, originalFrame.rows);
                    if (validBox.area() > 0)
                    {
                        // Create the cropped frame from original (before annotations)
                        cv::Mat cropped = originalFrame(validBox);
                        
                        // Publish it using the secondary ID
                        VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), cropped);
                    }
                }
                
                // Draw bbox and label on the main frame
                cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
                std::string label;
                if (!classNames.empty() && targetClassId >= 0 && targetClassId < (int)classNames.size())
                    label = classNames[(size_t)targetClassId];
                else
                    label = "target";
                cv::putText(frame, label, box.tl() + cv::Point(0, -5), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
            }
            
            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                    fifoBuffer[writeScope.startIndex1] = result;
            }
            
            // --- PASSTHROUGH LOGIC ---
            // Publish the annotated frame under our primary ID and update local GUI
            if (myLogicalId != 0)
                VideoFrameManager::getInstance().setFrame(myLogicalId, frame);
            updateGuiFrame(frame);
        }
        
        // YOLO is heavy; ~10 FPS
        wait(100);
    }
}

void ObjectDetectorModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() ||
        latestFrameForGui.getWidth() != bgraFrame.cols ||
        latestFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData destData(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(destData.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image ObjectDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

// Serialize zone rectangles to string: "x1,y1,w1,h1;x2,y2,w2,h2;..."
juce::String ObjectDetectorModule::serializeZoneRects(const std::vector<ZoneRect>& rects)
{
    juce::String result;
    for (size_t i = 0; i < rects.size(); ++i)
    {
        if (i > 0) result += ";";
        result += juce::String(rects[i].x, 4) + "," +
                  juce::String(rects[i].y, 4) + "," +
                  juce::String(rects[i].width, 4) + "," +
                  juce::String(rects[i].height, 4);
    }
    return result;
}

// Deserialize zone rectangles from string
std::vector<ObjectDetectorModule::ZoneRect> ObjectDetectorModule::deserializeZoneRects(const juce::String& data)
{
    std::vector<ZoneRect> rects;
    if (data.isEmpty()) return rects;
    
    juce::StringArray rectStrings;
    rectStrings.addTokens(data, ";", "");
    
    for (const auto& rectStr : rectStrings)
    {
        juce::StringArray coords;
        coords.addTokens(rectStr, ",", "");
        if (coords.size() == 4)
        {
            ZoneRect rect;
            rect.x = coords[0].getFloatValue();
            rect.y = coords[1].getFloatValue();
            rect.width = coords[2].getFloatValue();
            rect.height = coords[3].getFloatValue();
            rects.push_back(rect);
        }
    }
    return rects;
}

// Load zone rectangles for a color from APVTS state tree
void ObjectDetectorModule::loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::var value = apvts.state.getProperty(key);
    if (value.isString())
    {
        rects = deserializeZoneRects(value.toString());
    }
    else
    {
        rects.clear();
    }
}

// Save zone rectangles for a color to APVTS state tree
void ObjectDetectorModule::saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects)
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::String data = serializeZoneRects(rects);
    apvts.state.setProperty(key, data, nullptr);
}

std::vector<DynamicPinInfo> ObjectDetectorModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (9 channels: 5 existing + 4 zone gates)
    // Bus 1: Video Out (1 channel)
    // Bus 2: Cropped Out (1 channel)
    const int cvOutChannels = 9;  // 5 existing + 4 zone gates
    const int videoOutStartChannel = cvOutChannels;
    const int croppedOutStartChannel = videoOutStartChannel + 1;

    return {
        { "X", 0, PinDataType::CV },
        { "Y", 1, PinDataType::CV },
        { "Width", 2, PinDataType::CV },
        { "Height", 3, PinDataType::CV },
        { "Gate", 4, PinDataType::Gate },
        { "Red Zone Gate", 5, PinDataType::Gate },
        { "Green Zone Gate", 6, PinDataType::Gate },
        { "Blue Zone Gate", 7, PinDataType::Gate },
        { "Yellow Zone Gate", 8, PinDataType::Gate },
        { "Video Out", videoOutStartChannel, PinDataType::Video },
        { "Cropped Out", croppedOutStartChannel, PinDataType::Video }
    };
}

void ObjectDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Read Source ID from input pin
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
    {
        float sourceIdFloat = inputBuffer.getSample(0, 0);
        currentSourceId.store((juce::uint32)sourceIdFloat);
    }
    
    buffer.clear();
    
    // --- BEGIN FIX: Find our own ID if it's not set ---
    juce::uint32 myLogicalId = storedLogicalId;
    if (myLogicalId == 0 && parentSynth != nullptr)
    {
        for (const auto& info : parentSynth->getModulesInfo())
        {
            if (parentSynth->getModuleForLogical(info.first) == this)
            {
                myLogicalId = info.first;
                storedLogicalId = myLogicalId; // Cache it
                break;
            }
        }
    }
    // --- END FIX ---
    
    // Read ALL available results from FIFO to ensure latest result is used (like ColorTrackerModule)
    while (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
    }
    
    // Output channels: 0:X, 1:Y, 2:W, 3:H, 4:Gate, 5-8:Zone Gates
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    const float values[5]
    {
        lastResultForAudio.x,
        lastResultForAudio.y,
        lastResultForAudio.width,
        lastResultForAudio.height,
        lastResultForAudio.detected ? 1.0f : 0.0f
    };
    
    // Output existing CV channels (0-4)
    for (int ch = 0; ch < juce::jmin(5, cvOutBus.getNumChannels()); ++ch)
    {
        for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            cvOutBus.setSample(ch, s, values[ch]);
    }
    
    // Output zone gates (channels 5-8)
    for (int z = 0; z < 4; ++z)
    {
        int ch = 5 + z;
        if (ch < cvOutBus.getNumChannels())
        {
            float gateValue = lastResultForAudio.zoneHits[z] ? 1.0f : 0.0f;
            // Fill entire buffer with same value (gates should be steady-state)
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
                cvOutBus.setSample(ch, s, gateValue);
        }
    }
    
    // Output primary ID for passthrough video on bus 1
    auto videoOutBus = getBusBuffer(buffer, false, 1);
    if (videoOutBus.getNumChannels() > 0)
    {
        float primaryId = static_cast<float>(myLogicalId); // Use the resolved ID
        for (int s = 0; s < videoOutBus.getNumSamples(); ++s)
            videoOutBus.setSample(0, s, primaryId);
    }
    
    // Output secondary ID for cropped video on bus 2
    auto croppedOutBus = getBusBuffer(buffer, false, 2);
    if (croppedOutBus.getNumChannels() > 0)
    {
        float secondaryId = static_cast<float>(getSecondaryLogicalId());
        for (int s = 0; s < croppedOutBus.getNumSamples(); ++s)
            croppedOutBus.setSample(0, s, secondaryId);
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 ObjectDetectorModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void ObjectDetectorModule::drawParametersInNode(float itemWidth,
                                                const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    ImGui::PushItemWidth(itemWidth);
    
    // GPU ACCELERATION TOGGLE
    #if WITH_CUDA_SUPPORT
        bool cudaAvailable = (cv::cuda::getCudaEnabledDeviceCount() > 0);
        
        if (!cudaAvailable)
        {
            ImGui::BeginDisabled();
        }
        
        bool useGpu = useGpuParam->get();
        if (ImGui::Checkbox("⚡ Use GPU (CUDA)", &useGpu))
        {
            *useGpuParam = useGpu;
            onModificationEnded();
        }
        
        if (!cudaAvailable)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("No CUDA-enabled GPU detected.\nCheck that your GPU supports CUDA and drivers are installed.");
            }
        }
        else if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Enable GPU acceleration for object detection.\nRequires CUDA-capable NVIDIA GPU.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
    #endif
    
    if (targetClassParam)
    {
        int idx = juce::jlimit(0, (int)juce::jmax<size_t>(1, classNames.size()) - 1, selectedClassId.load());
        const char* currentLabel = (!classNames.empty() && juce::isPositiveAndBelow(idx, (int)classNames.size()))
            ? classNames[(size_t)idx].c_str() : "person";

        // Optional quick filter
        static char filterBuf[64] = {0};
        ImGui::InputText("##class_filter", filterBuf, sizeof(filterBuf));
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { filterBuf[0] = '\0'; }

        if (ImGui::BeginCombo("Target Class", currentLabel))
        {
            const juce::String filter = juce::String(filterBuf).toLowerCase();
            for (int i = 0; i < (int)classNames.size(); ++i)
            {
                const juce::String name = juce::String(classNames[(size_t)i]);
                if (filter.isNotEmpty() && !name.toLowerCase().contains(filter))
                    continue;
                bool selected = (idx == i);
                if (ImGui::Selectable(name.toRawUTF8(), selected))
                {
                    idx = i;
                    selectedClassId.store(idx);
                    onModificationEnded();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    
    float confidence = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.5f;
    if (ImGui::SliderFloat("Confidence", &confidence, 0.0f, 1.0f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence")))
            *p = confidence;
        onModificationEnded();
    }
    
    // Zoom controls
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    float buttonWidth = (itemWidth / 2.0f) - 4.0f;
    const bool atMin = (level <= 0);
    const bool atMax = (level >= 2);
    if (atMin) ImGui::BeginDisabled();
    if (ImGui::Button("-", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmax(0, level - 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMin) ImGui::EndDisabled();
    ImGui::SameLine();
    if (atMax) ImGui::BeginDisabled();
    if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmin(2, level + 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMax) ImGui::EndDisabled();
    
    if (modelLoaded)
    {
        ThemeText("Model: Loaded", theme.text.success);
        ImGui::Text("Classes: %d", (int)classNames.size());
    }
    else
    {
        ThemeText("Model: NOT LOADED", theme.text.error);
        ImGui::TextWrapped("Place files in assets/: yolov3.cfg, yolov3.weights, coco.names");
    }

    ImGui::Separator();
    
    // Zone color palette (4 colors)
    static const ImVec4 ZONE_COLORS[4] = {
        ImVec4(1.0f, 0.0f, 0.0f, 0.3f),  // Red - 30% opacity
        ImVec4(0.0f, 1.0f, 0.0f, 0.3f),  // Green - 30% opacity
        ImVec4(0.0f, 0.0f, 1.0f, 0.3f),  // Blue - 30% opacity
        ImVec4(1.0f, 1.0f, 0.0f, 0.3f)   // Yellow - 30% opacity
    };
    
    // Static state for mouse interaction (per-instance using logical ID)
    static std::map<int, int> activeZoneColorIndexByNode;
    static std::map<int, int> drawingZoneIndexByNode;
    static std::map<int, float> dragStartXByNode;
    static std::map<int, float> dragStartYByNode;
    
    int nodeId = (int)getLogicalId();
    
    // Initialize active color index if not set
    if (activeZoneColorIndexByNode.find(nodeId) == activeZoneColorIndexByNode.end())
        activeZoneColorIndexByNode[nodeId] = 0;
    
    // Initialize drawingZoneIndex to -1 (not drawing) if not set - MUST do before accessing reference!
    if (drawingZoneIndexByNode.find(nodeId) == drawingZoneIndexByNode.end())
        drawingZoneIndexByNode[nodeId] = -1;
    
    // Now safe to get references
    int& activeZoneColorIndex = activeZoneColorIndexByNode[nodeId];
    int& drawingZoneIndex = drawingZoneIndexByNode[nodeId];
    float& dragStartX = dragStartXByNode[nodeId];
    float& dragStartY = dragStartYByNode[nodeId];
    
    // Color picker boxes
    ImGui::Text("Zone Colors:");
    ImGui::SameLine();
    for (int c = 0; c < 4; ++c)
    {
        ImGui::PushID(c);
        ImVec4 color = ZONE_COLORS[c];
        color.w = 1.0f;  // Full opacity for picker button
        if (ImGui::ColorButton("##ZoneColor", color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20)))
        {
            activeZoneColorIndex = c;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click to select color %d", c + 1);
        }
        ImGui::PopID();
        if (c < 3) ImGui::SameLine();
    }
    
    ImGui::Separator();
    
    // Video preview with zone overlays
    juce::Image frame = getLatestFrame();
    if (!frame.isNull())
    {
        // Use static map for texture management (per-module-instance textures)
        static std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> localTextures;
        
        if (localTextures.find(nodeId) == localTextures.end())
            localTextures[nodeId] = std::make_unique<juce::OpenGLTexture>();
        
        auto* texture = localTextures[nodeId].get();
        texture->loadImage(frame);
        
        if (texture->getTextureID() != 0)
        {
            float ar = (float)frame.getHeight() / juce::jmax(1.0f, (float)frame.getWidth());
            ImVec2 size(itemWidth, itemWidth * ar);
            ImGui::Image((void*)(intptr_t)texture->getTextureID(), size, ImVec2(0, 1), ImVec2(1, 0));
            
            // Get image screen coordinates and size for interaction
            ImVec2 imageRectMin = ImGui::GetItemRectMin();
            ImVec2 imageRectMax = ImGui::GetItemRectMax();
            ImVec2 imageSize = ImGui::GetItemRectSize();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Use InvisibleButton to capture mouse input and prevent node movement (like CropVideoModule)
            ImGui::SetCursorScreenPos(imageRectMin);
            ImGui::InvisibleButton("##zone_interaction", imageSize);
            
            ImVec2 mousePos = ImGui::GetMousePos();
            
            // Draw zones - each color zone can have multiple rectangles
            for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
            {
                std::vector<ZoneRect> rects;
                loadZoneRects(colorIdx, rects);
                
                ImVec4 color = ZONE_COLORS[colorIdx];
                ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(color);
                ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f));
                
                for (const auto& rect : rects)
                {
                    ImVec2 zoneMin(imageRectMin.x + rect.x * imageSize.x,
                                  imageRectMin.y + rect.y * imageSize.y);
                    ImVec2 zoneMax(imageRectMin.x + (rect.x + rect.width) * imageSize.x,
                                  imageRectMin.y + (rect.y + rect.height) * imageSize.y);
                    
                    drawList->AddRectFilled(zoneMin, zoneMax, fillColor);
                    drawList->AddRect(zoneMin, zoneMax, borderColor, 0.0f, 0, 2.0f);
                }
            }
            
            // Draw object center (the point being checked for zone hits) - small red dot
            // Read latest result from FIFO for UI display
            ObjectDetectionResult uiResult;
            if (fifo.getNumReady() > 0)
            {
                auto readScope = fifo.read(1);
                if (readScope.blockSize1 > 0)
                {
                    uiResult = fifoBuffer[readScope.startIndex1];
                }
            }
            else
            {
                // Fallback to last result if FIFO is empty (might be stale but better than nothing)
                uiResult = lastResultForAudio;
            }
            
            // Draw a small red dot at the detected object's center
            if (uiResult.detected)
            {
                // Convert normalized coordinates (0-1) to screen coordinates
                float centerX = imageRectMin.x + uiResult.x * imageSize.x;
                float centerY = imageRectMin.y + uiResult.y * imageSize.y;
                ImVec2 center(centerX, centerY);
                
                // Draw small red dot (radius 3 pixels)
                ImU32 redColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                drawList->AddCircleFilled(center, 3.0f, redColor);
            }
            
            // Mouse interaction - use InvisibleButton's hover state
            if (ImGui::IsItemHovered())
            {
                // Normalize mouse position (0.0-1.0)
                float mouseX = (mousePos.x - imageRectMin.x) / imageSize.x;
                float mouseY = (mousePos.y - imageRectMin.y) / imageSize.y;
                
                // Check if Ctrl key is held
                bool ctrlHeld = ImGui::GetIO().KeyCtrl;
                
                // Zone drawing: Only with Ctrl+Left-click
                if (ctrlHeld)
                {
                    // Ctrl+Left-click: Start drawing a new rectangle for the selected color zone
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        dragStartX = mouseX;
                        dragStartY = mouseY;
                        drawingZoneIndex = activeZoneColorIndex;  // Drawing for the selected color
                    }
                    
                    // Ctrl+Left-drag: Update rectangle being drawn and show preview
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && drawingZoneIndex >= 0 && ctrlHeld)
                    {
                        float dragEndX = mouseX;
                        float dragEndY = mouseY;
                        
                        // Calculate rectangle from drag start to current position
                        float zx = juce::jmin(dragStartX, dragEndX);
                        float zy = juce::jmin(dragStartY, dragEndY);
                        float zw = std::abs(dragEndX - dragStartX);
                        float zh = std::abs(dragEndY - dragStartY);
                        
                        // Clamp to image bounds
                        zx = juce::jlimit(0.0f, 1.0f, zx);
                        zy = juce::jlimit(0.0f, 1.0f, zy);
                        zw = juce::jlimit(0.01f, 1.0f - zx, zw);
                        zh = juce::jlimit(0.01f, 1.0f - zy, zh);
                        
                        // Draw preview rectangle
                        ImVec2 previewMin(imageRectMin.x + zx * imageSize.x,
                                         imageRectMin.y + zy * imageSize.y);
                        ImVec2 previewMax(imageRectMin.x + (zx + zw) * imageSize.x,
                                         imageRectMin.y + (zy + zh) * imageSize.y);
                        
                        ImVec4 previewColor = ZONE_COLORS[drawingZoneIndex];
                        ImU32 previewFillColor = ImGui::ColorConvertFloat4ToU32(previewColor);
                        ImU32 previewBorderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(previewColor.x, previewColor.y, previewColor.z, 1.0f));
                        
                        drawList->AddRectFilled(previewMin, previewMax, previewFillColor);
                        drawList->AddRect(previewMin, previewMax, previewBorderColor, 0.0f, 0, 2.0f);
                    }
                    
                    // Ctrl+Left-release: Finish drawing - add rectangle to the color zone
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && drawingZoneIndex >= 0)
                    {
                        float dragEndX = mouseX;
                        float dragEndY = mouseY;
                        
                        // Calculate final rectangle
                        float zx = juce::jmin(dragStartX, dragEndX);
                        float zy = juce::jmin(dragStartY, dragEndY);
                        float zw = std::abs(dragEndX - dragStartX);
                        float zh = std::abs(dragEndY - dragStartY);
                        
                        // Only add if rectangle is large enough
                        if (zw > 0.01f && zh > 0.01f)
                        {
                            // Clamp to image bounds
                            zx = juce::jlimit(0.0f, 1.0f, zx);
                            zy = juce::jlimit(0.0f, 1.0f, zy);
                            zw = juce::jlimit(0.01f, 1.0f - zx, zw);
                            zh = juce::jlimit(0.01f, 1.0f - zy, zh);
                            
                            // Load existing rectangles for this color
                            std::vector<ZoneRect> rects;
                            loadZoneRects(drawingZoneIndex, rects);
                            
                            // Add new rectangle
                            ZoneRect newRect;
                            newRect.x = zx;
                            newRect.y = zy;
                            newRect.width = zw;
                            newRect.height = zh;
                            rects.push_back(newRect);
                            
                            // Save back to APVTS
                            saveZoneRects(drawingZoneIndex, rects);
                            onModificationEnded();
                        }
                        
                        drawingZoneIndex = -1;
                    }
                }
                
                // Right-drag: Eraser mode (delete rectangles from color zones) - works regardless of Ctrl
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                {
                    // Check if mouse is inside any rectangle of any color zone
                    for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                    {
                        std::vector<ZoneRect> rects;
                        loadZoneRects(colorIdx, rects);
                        
                        // Check each rectangle and remove if mouse is inside
                        bool modified = false;
                        for (auto it = rects.begin(); it != rects.end();)
                        {
                            bool inside = (mouseX >= it->x && mouseX <= it->x + it->width &&
                                          mouseY >= it->y && mouseY <= it->y + it->height);
                            
                            if (inside)
                            {
                                it = rects.erase(it);
                                modified = true;
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        
                        if (modified)
                        {
                            saveZoneRects(colorIdx, rects);
                            onModificationEnded();
                        }
                    }
                }
                
                // Show tooltip for zone drawing hints
                if (ctrlHeld)
                {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("Ctrl+Left-drag: Draw zone\nRight-drag: Erase zone");
                    ImGui::EndTooltip();
                }
            }
        }
    }
    
    ImGui::PopItemWidth();
}

void ObjectDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("X", 0);
    helpers.drawAudioOutputPin("Y", 1);
    helpers.drawAudioOutputPin("Width", 2);
    helpers.drawAudioOutputPin("Height", 3);
    helpers.drawAudioOutputPin("Gate", 4);
    helpers.drawAudioOutputPin("Video Out", 0);     // Bus 1, Channel 0
    helpers.drawAudioOutputPin("Cropped Out", 1); // Bus 2, Channel 0
}
#endif


