#include "ObjectDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
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
    
    return { params.begin(), params.end() };
}

ObjectDetectorModule::ObjectDetectorModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)),
      juce::Thread("Object Detector Thread"),
      apvts(*this, nullptr, "ObjectDetectorParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    targetClassParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("targetClass"));
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    
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
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            
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
            // Prepare input blob
            cv::Mat blob = cv::dnn::blobFromImage(
                frame,
                1.0 / 255.0,
                cv::Size(YOLO_INPUT_SIZE, YOLO_INPUT_SIZE),
                cv::Scalar(),
                true,
                false);
            net.setInput(blob);
            
            // Forward through YOLO
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
            if (!indices.empty())
            {
                int bestIdx = indices[0];
                const cv::Rect& box = boxes[bestIdx];
                result.detected = true;
                result.x = juce::jlimit(0.0f, 1.0f, (float)(box.x + box.width * 0.5f) / (float)frame.cols);
                result.y = juce::jlimit(0.0f, 1.0f, (float)(box.y + box.height * 0.5f) / (float)frame.rows);
                result.width  = juce::jlimit(0.0f, 1.0f, (float)box.width / (float)frame.cols);
                result.height = juce::jlimit(0.0f, 1.0f, (float)box.height / (float)frame.rows);
                
                // Draw bbox and label
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
    
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
    }
    
    // Output channels: 0:X, 1:Y, 2:W, 3:H, 4:Gate
    const float values[5]
    {
        lastResultForAudio.x,
        lastResultForAudio.y,
        lastResultForAudio.width,
        lastResultForAudio.height,
        lastResultForAudio.detected ? 1.0f : 0.0f
    };
    
    for (int ch = 0; ch < juce::jmin(5, buffer.getNumChannels()); ++ch)
    {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            buffer.setSample(ch, s, values[ch]);
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
    ImGui::PushItemWidth(itemWidth);
    
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
    
    ImGui::Separator();
    if (modelLoaded)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Model: Loaded");
        ImGui::Text("Classes: %d", (int)classNames.size());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Model: NOT LOADED");
        ImGui::TextWrapped("Place files in assets/: yolov3.cfg, yolov3.weights, coco.names");
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
}
#endif


