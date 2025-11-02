#include "CropVideoModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <array>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

// YOLOv3 default input size
static constexpr int CROP_YOLO_INPUT_SIZE = 416;

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

juce::AudioProcessorValueTreeState::ParameterLayout CropVideoModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>("trackerMode", "Tracking Mode", 
        juce::StringArray{ "Manual", "Track Face", "Track Object" }, 0));
    
    // Initialize with the full list of classes for stable preset saving by index
    juce::StringArray cocoClasses;
    for (const auto& name : kCoco80) cocoClasses.add(name);
    params.push_back(std::make_unique<juce::AudioParameterChoice>("targetClass", "Target Class", cocoClasses, 0));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>("confidence", "Confidence", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("padding", "Padding", 0.0f, 2.0f, 0.1f)); // 10% padding default
    params.push_back(std::make_unique<juce::AudioParameterChoice>("aspectRatio", "Aspect Ratio", juce::StringArray{ "Stretch", "Preserve (Fit)" }, 1));
    
    // Manual crop controls (normalized 0-1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropX", "Center X", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropY", "Center Y", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropW", "Width", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropH", "Height", 0.0f, 1.0f, 0.5f));
    
    return { params.begin(), params.end() };
}

CropVideoModule::CropVideoModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Source In", juce::AudioChannelSet::mono(), true) // Bus 0 for Video Source ID
                      .withInput("Modulation In", juce::AudioChannelSet::discreteChannels(4), true) // Bus 1 for CV modulation
                      .withOutput("Output ID", juce::AudioChannelSet::mono(), true)),
      juce::Thread("CropVideo Thread"),
      apvts(*this, nullptr, "CropVideoParams", createParameterLayout())
{
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    trackerModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("trackerMode"));
    targetClassParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("targetClass"));
    confidenceParam = apvts.getRawParameterValue("confidence");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    paddingParam = apvts.getRawParameterValue("padding");
    aspectRatioModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("aspectRatio"));
    cropXParam = apvts.getRawParameterValue("cropX");
    cropYParam = apvts.getRawParameterValue("cropY");
    cropWParam = apvts.getRawParameterValue("cropW");
    cropHParam = apvts.getRawParameterValue("cropH");
    
    loadModels();
}

CropVideoModule::~CropVideoModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void CropVideoModule::prepareToPlay(double, int) { startThread(); }

void CropVideoModule::releaseResources() { signalThreadShouldExit(); stopThread(5000); }

void CropVideoModule::loadModels()
{
    juce::File appDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
    juce::File assetsDir = appDir.getChildFile("assets");

    // --- CORRECTED: YOLO Model Loading with Fallback (from ObjectDetectorModule) ---
    juce::File weights = assetsDir.getChildFile("yolov3.weights");
    juce::File cfg = assetsDir.getChildFile("yolov3.cfg");
    // Fallback to tiny if standard is missing
    if (!weights.existsAsFile() || !cfg.existsAsFile())
    {
        weights = assetsDir.getChildFile("yolov3-tiny.weights");
        cfg = assetsDir.getChildFile("yolov3-tiny.cfg");
        juce::Logger::writeToLog("[CropVideo] Standard YOLOv3 not found, falling back to yolov3-tiny.");
    }
    juce::File names = assetsDir.getChildFile("coco.names");
 
    yoloModelLoaded = false;
    if (weights.existsAsFile() && cfg.existsAsFile())
    {
        try
        {
            yoloNet = cv::dnn::readNetFromDarknet(cfg.getFullPathName().toStdString(), weights.getFullPathName().toStdString());
            
            // CRITICAL: Set backend immediately after loading model (matches ObjectDetectorModule)
            #if WITH_CUDA_SUPPORT
                bool useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
                {
                    yoloNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                    yoloNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    juce::Logger::writeToLog("[CropVideo] ✓ Model loaded with CUDA backend (GPU)");
                }
                else
                {
                    yoloNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    yoloNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[CropVideo] Model loaded with CPU backend");
                }
            #else
                yoloNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                yoloNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[CropVideo] Model loaded with CPU backend (CUDA not compiled)");
            #endif
            
            yoloModelLoaded = true;
            juce::Logger::writeToLog("[CropVideo] ✓ YOLO model loaded successfully from: " + weights.getFullPathName());
            
            if (names.existsAsFile())
            {
                yoloClassNames.clear();
                std::ifstream ifs(names.getFullPathName().toStdString().c_str());
                std::string line;
                while (std::getline(ifs, line))
                    if (!line.empty()) yoloClassNames.push_back(line);
            }
            if (yoloClassNames.empty())
            {
                yoloClassNames.assign(std::begin(kCoco80), std::end(kCoco80));
                juce::Logger::writeToLog("[CropVideo] coco.names missing; using embedded COCO-80 labels.");
            }
        }
        catch (const cv::Exception& e)
        {
            juce::Logger::writeToLog("[CropVideo] ❌ ERROR loading YOLO model: " + juce::String(e.what()));
            yoloModelLoaded = false;
            // Still populate class names for UI even if model failed to load
            yoloClassNames.assign(std::begin(kCoco80), std::end(kCoco80));
        }
    }
    else
    {
        juce::Logger::writeToLog("[CropVideo] ❌ WARNING: No YOLO model files found in " + assetsDir.getFullPathName());
        yoloModelLoaded = false;
        // Still populate class names for UI even if model failed to load
        yoloClassNames.assign(std::begin(kCoco80), std::end(kCoco80));
    }

    // --- CORRECTED: Haar Cascade Loading with Robust Path Finding ---
    juce::File cascadeFile = assetsDir.getChildFile("haarcascade_frontalface_default.xml");
    if (!cascadeFile.existsAsFile())
        cascadeFile = appDir.getChildFile("haarcascade_frontalface_default.xml");

    if (cascadeFile.existsAsFile())
    {
        if (faceCascadeCpu.load(cascadeFile.getFullPathName().toStdString()))
        {
            juce::Logger::writeToLog("[CropVideo] ✓ Haar cascade (CPU) loaded successfully: " + cascadeFile.getFileName());
            
            #if WITH_CUDA_SUPPORT
                try
                {
                    faceCascadeGpu = cv::cuda::CascadeClassifier::create(cascadeFile.getFullPathName().toStdString());
                    if (faceCascadeGpu && !faceCascadeGpu->empty())
                    {
                        juce::Logger::writeToLog("[CropVideo] ✓ GPU cascade created successfully.");
                    }
                    else
                    {
                        faceCascadeGpu.release();
                    }
                }
                catch (const cv::Exception& e)
                {
                    faceCascadeGpu.release();
                    juce::Logger::writeToLog("[CropVideo] ❌ WARNING: Failed to create GPU cascade classifier: " + juce::String(e.what()));
                }
            #endif
        }
        else
        {
            juce::Logger::writeToLog("[CropVideo] ❌ ERROR: Failed to load Haar cascade XML for CPU.");
        }
    }
    else
    {
        juce::Logger::writeToLog("[CropVideo] ❌ WARNING: haarcascade_frontalface_default.xml not found.");
    }
}

bool CropVideoModule::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 1; // Modulation inputs are on bus 1
    if (paramId == "cropX_mod") { outChannelIndexInBus = 0; return true; }
    if (paramId == "cropY_mod") { outChannelIndexInBus = 1; return true; }
    if (paramId == "cropW_mod") { outChannelIndexInBus = 2; return true; }
    if (paramId == "cropH_mod") { outChannelIndexInBus = 3; return true; }
    return false;
}

void CropVideoModule::run()
{
    #if WITH_CUDA_SUPPORT
        bool lastGpuStateForYolo = useGpuParam->get(); // Initialize with current state
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }

        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(33); continue; }

        // Cache the full, uncropped input frame for the UI and tracker initialization
        {
            const juce::ScopedLock lock(trackerLock);
            frame.copyTo(lastFrameForTracker);
        }
        updateInputGuiFrame(frame);

        // --- TRACKING / DETECTION LOGIC ---
        int mode = trackerModeParam ? trackerModeParam->getIndex() : 0;
        
        if (mode == 0 && manualTrackingActive.load()) // Manual Tracker
        {
            const juce::ScopedLock lock(trackerLock);
            if (manualTracker)
            {
                cv::Rect newBox;
                if (manualTracker->update(frame, newBox))
                {
                    *cropXParam = ((float)newBox.x + (float)newBox.width / 2.0f) / (float)frame.cols;
                    *cropYParam = ((float)newBox.y + (float)newBox.height / 2.0f) / (float)frame.rows;
                    *cropWParam = (float)newBox.width / (float)frame.cols;
                    *cropHParam = (float)newBox.height / (float)frame.rows;
                }
                else
                {
                    manualTrackingActive.store(false);
                    manualTracker.release();
                }
            }
        }
        else // Automatic Detection Modes
        {
            cv::Rect detectedBox;
            bool found = false;
            
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
                {
                    useGpu = false;
                    if (!loggedGpuWarning)
                    {
                        juce::Logger::writeToLog("[CropVideo] WARNING: GPU requested but no CUDA device found. Using CPU.");
                        loggedGpuWarning = true;
                    }
                }
            #endif
            
            if (mode == 1) // Track Face
            {
                // RUNTIME SAFETY: Check if CPU detector is loaded before attempting to use it
                if (faceCascadeCpu.empty())
                {
                    // Model not loaded, skip detection
                    found = false;
                }
                else
                {
                    bool executedOnGpu = false;
                    if (useGpu)
                    {
                    #if WITH_CUDA_SUPPORT
                        // RUNTIME SAFETY: Verify GPU cascade exists and is not empty before using
                        if (faceCascadeGpu && !faceCascadeGpu->empty())
                        {
                            cv::cuda::GpuMat frameGpu, grayGpu, facesGpu;
                            frameGpu.upload(frame);
                            cv::cuda::cvtColor(frameGpu, grayGpu, cv::COLOR_BGR2GRAY);
                            faceCascadeGpu->detectMultiScale(grayGpu, facesGpu);
                            std::vector<cv::Rect> faces;
                            faceCascadeGpu->convert(facesGpu, faces);
                            if (!faces.empty())
                            {
                                detectedBox = *std::max_element(faces.begin(), faces.end(), 
                                    [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
                                found = true;
                            }
                            executedOnGpu = true;
                        }
                    #endif
                    }
                    if (!executedOnGpu) // CPU Path
                    {
                        cv::Mat gray;
                        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
                        std::vector<cv::Rect> faces;
                        faceCascadeCpu.detectMultiScale(gray, faces);
                        if (!faces.empty())
                        {
                            detectedBox = *std::max_element(faces.begin(), faces.end(), 
                                [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
                            found = true;
                        }
                    }
                }
            }
            else if (mode == 2 && yoloModelLoaded) // Track Object (YOLO)
            {
                #if WITH_CUDA_SUPPORT
                    if (useGpu != lastGpuStateForYolo)
                    {
                        yoloNet.setPreferableBackend(useGpu ? cv::dnn::DNN_BACKEND_CUDA : cv::dnn::DNN_BACKEND_OPENCV);
                        yoloNet.setPreferableTarget(useGpu ? cv::dnn::DNN_TARGET_CUDA : cv::dnn::DNN_TARGET_CPU);
                        lastGpuStateForYolo = useGpu;
                        juce::Logger::writeToLog("[CropVideo] YOLO backend set to " + juce::String(useGpu ? "CUDA" : "CPU"));
                    }
                #endif

                // Prepare input blob
                cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0/255.0, cv::Size(CROP_YOLO_INPUT_SIZE, CROP_YOLO_INPUT_SIZE), cv::Scalar(), true, false);
                yoloNet.setInput(blob);
                std::vector<cv::Mat> outs;
                yoloNet.forward(outs, yoloNet.getUnconnectedOutLayersNames());

                std::vector<cv::Rect> boxes;
                std::vector<float> confidences;
                int targetClassId = targetClassParam ? targetClassParam->getIndex() : 0;
                float confThreshold = confidenceParam ? confidenceParam->load() : 0.5f;

                for (const auto& out : outs)
                {
                    const float* data = (const float*)out.data;
                    for (int i = 0; i < out.rows; ++i, data += out.cols)
                    {
                        cv::Mat scores = out.row(i).colRange(5, out.cols);
                        cv::Point classIdPoint;
                        double maxScore;
                        cv::minMaxLoc(scores, 0, &maxScore, 0, &classIdPoint);
                        if (maxScore > confThreshold && classIdPoint.x == targetClassId)
                        {
                            int centerX = (int)(data[0] * frame.cols);
                            int centerY = (int)(data[1] * frame.rows);
                            int width   = (int)(data[2] * frame.cols);
                            int height  = (int)(data[3] * frame.rows);
                            boxes.emplace_back(centerX - width/2, centerY - height/2, width, height);
                            confidences.push_back((float)maxScore);
                        }
                    }
                }

                std::vector<int> indices;
                cv::dnn::NMSBoxes(boxes, confidences, confThreshold, 0.4f, indices);
                if (!indices.empty())
                {
                    detectedBox = boxes[indices[0]];
                    found = true;
                }
            }
            
            if (found)
            {
                *cropWParam = (float)detectedBox.width / (float)frame.cols;
                *cropHParam = (float)detectedBox.height / (float)frame.rows;
                *cropXParam = ((float)detectedBox.x + (float)detectedBox.width / 2.0f) / (float)frame.cols;
                *cropYParam = ((float)detectedBox.y + (float)detectedBox.height / 2.0f) / (float)frame.rows;
            }
        }

        // Get final crop values (these are already modulated by the audio thread if connected)
        float cx = cropXParam ? cropXParam->load() : 0.5f;
        float cy = cropYParam ? cropYParam->load() : 0.5f;
        float w = cropWParam ? cropWParam->load() : 0.5f;
        float h = cropHParam ? cropHParam->load() : 0.5f;
        
        int frameW = frame.cols;
        int frameH = frame.rows;
        int pixelW = static_cast<int>(w * frameW);
        int pixelH = static_cast<int>(h * frameH);
        int pixelX = static_cast<int>(cx * frameW - pixelW / 2.0f);
        int pixelY = static_cast<int>(cy * frameH - pixelH / 2.0f);
        
        float pad = paddingParam ? paddingParam->load() : 0.1f;
        int padX = static_cast<int>(pixelW * pad);
        int padY = static_cast<int>(pixelH * pad);
        pixelX -= padX;
        pixelY -= padY;
        pixelW += (padX * 2);
        pixelH += (padY * 2);

        cv::Rect roi(pixelX, pixelY, pixelW, pixelH);
        roi &= cv::Rect(0, 0, frame.cols, frame.rows);
        
        if (roi.area() > 0)
        {
            cv::Mat croppedFrame = frame(roi);
            VideoFrameManager::getInstance().setFrame(getLogicalId(), croppedFrame);
            // This now updates the *output* frame preview (which we don't display in this node)
            updateGuiFrame(croppedFrame);
        }
        else
        {
            // If crop is invalid, publish an empty frame
            VideoFrameManager::getInstance().setFrame(getLogicalId(), cv::Mat());
            updateGuiFrame(cv::Mat());
        }

        wait(33);
    }
    juce::Logger::writeToLog("[CropVideo][Thread] Video processing thread finished.");
}

void CropVideoModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    // Read the Source ID from the dedicated input bus (bus 0)
    auto sourceIdBus = getBusBuffer(buffer, true, 0);
    if (sourceIdBus.getNumChannels() > 0 && sourceIdBus.getNumSamples() > 0)
    {
        currentSourceId.store(static_cast<juce::uint32>(sourceIdBus.getSample(0, 0)));
    }
    else
    {
        currentSourceId.store(0);
    }
    // Note: CV modulation is handled automatically by ModularSynthProcessor
    // because we implemented getParamRouting(). The values in cropXParam etc.
    // are already the final, modulated values.
    buffer.clear();
    // Output our own Logical ID for chaining
    auto outputBus = getBusBuffer(buffer, false, 0);
    if (outputBus.getNumChannels() > 0)
    {
        float logicalId = static_cast<float>(getLogicalId());
        for (int s = 0; s < outputBus.getNumSamples(); ++s)
            outputBus.setSample(0, s, logicalId);
    }
}

void CropVideoModule::updateGuiFrame(const cv::Mat& frame)
{
    const juce::ScopedLock lock(imageLock);
    if (frame.empty())
    {
        latestOutputFrameForGui = juce::Image();
        return;
    }
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    if (latestOutputFrameForGui.isNull() || latestOutputFrameForGui.getWidth() != bgraFrame.cols || latestOutputFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestOutputFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData dest(latestOutputFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

// NEW function to handle the input frame preview
void CropVideoModule::updateInputGuiFrame(const cv::Mat& frame)
{
    const juce::ScopedLock lock(imageLock);
    if (frame.empty())
    {
        latestInputFrameForGui = juce::Image();
        return;
    }
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    if (latestInputFrameForGui.isNull() || latestInputFrameForGui.getWidth() != bgraFrame.cols || latestInputFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestInputFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData dest(latestInputFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image CropVideoModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    // This returns the cropped output, useful if another node wants a preview
    return latestOutputFrameForGui.createCopy();
}

#if defined(PRESET_CREATOR_UI)
ImVec2 CropVideoModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int)zoomLevelParam->load() : 1;
    const float widths[] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[juce::jlimit(0, 2, level)], 0.0f);
}

void CropVideoModule::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // State for tracking drag operations (per-instance using logical ID)
    static std::map<int, bool> isResizingCropBoxByNode; // Left-click drag for resizing
    static std::map<int, bool> isMovingCropBoxByNode;   // Right-click drag for moving
    static std::map<int, ImVec2> dragStartPosByNode;
    static std::map<int, float> initialCropXByNode; // Store crop center at the start of a move operation
    static std::map<int, float> initialCropYByNode;
    static std::map<int, std::array<char, 64>> filterBufByNode;
    
    int mode = trackerModeParam ? trackerModeParam->getIndex() : 0;
    int nodeId = (int)getLogicalId();
    bool& isResizingCropBox = isResizingCropBoxByNode[nodeId];
    bool& isMovingCropBox = isMovingCropBoxByNode[nodeId];
    ImVec2& dragStartPos = dragStartPosByNode[nodeId];
    float& initialCropX = initialCropXByNode[nodeId];
    float& initialCropY = initialCropYByNode[nodeId];
    
    juce::Image frame;
    {
        const juce::ScopedLock lock(imageLock);
        frame = latestInputFrameForGui.createCopy(); // Always draw the full, uncropped input frame
    }
    
    if (!frame.isNull())
    {
        // Use a local static map for texture management (per-module-instance textures)
        static std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> localVisionTextures;
        
        if (localVisionTextures.find((int)getLogicalId()) == localVisionTextures.end())
            localVisionTextures[(int)getLogicalId()] = std::make_unique<juce::OpenGLTexture>();
        
        auto* texture = localVisionTextures[(int)getLogicalId()].get();
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
            
            ImGui::SetCursorScreenPos(imageRectMin);
            ImGui::InvisibleButton("##crop_interaction", imageSize);
            
            ImVec2 mousePos = ImGui::GetMousePos();
            
            // Get current crop rect from parameters to display it and check for hover
            float currX = cropXParam ? cropXParam->load() : 0.5f;
            float currY = cropYParam ? cropYParam->load() : 0.5f;
            float currW = cropWParam ? cropWParam->load() : 0.5f;
            float currH = cropHParam ? cropHParam->load() : 0.5f;
            
            ImVec2 currentCropMin = ImVec2(imageRectMin.x + (currX - currW/2.0f) * imageSize.x, imageRectMin.y + (currY - currH/2.0f) * imageSize.y);
            ImVec2 currentCropMax = ImVec2(imageRectMin.x + (currX + currW/2.0f) * imageSize.x, imageRectMin.y + (currY + currH/2.0f) * imageSize.y);
            bool isMouseInCropBox = (mousePos.x >= currentCropMin.x && mousePos.x <= currentCropMax.x && mousePos.y >= currentCropMin.y && mousePos.y <= currentCropMax.y);
            
            // Set tooltip and cursor based on mouse position
            if (ImGui::IsItemHovered())
            {
                if (isMouseInCropBox && !isResizingCropBox && !isMovingCropBox)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Right-drag to move Crop Box\nLeft-drag outside to resize");
                }
                else
                {
                    ImGui::SetTooltip("Left-drag to define new Crop Box");
                }
            }
            
            // --- Handle Mouse Clicks ---
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !isMouseInCropBox)
            {
                isResizingCropBox = true;
                dragStartPos = mousePos;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && isMouseInCropBox)
            {
                isMovingCropBox = true;
                dragStartPos = mousePos;
                initialCropX = currX;
                initialCropY = currY;
            }
            
            // Draw crop rect - green for tracking, yellow for manual
            if (mode == 0 && manualTrackingActive.load())
            {
                // Draw a thick green rectangle for manual tracking
                drawList->AddRect(currentCropMin, currentCropMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 3.0f);
                const char* text = "TRACKING";
                ImVec2 textSize = ImGui::CalcTextSize(text);
                ImVec2 textPos = ImVec2(currentCropMin.x + 5, currentCropMin.y + 5);
                if (textPos.x + textSize.x < imageRectMax.x && textPos.y + textSize.y < imageRectMax.y)
                {
                    drawList->AddText(textPos, IM_COL32(0, 255, 0, 255), text);
                }
            }
            else if (mode != 0)
            {
                // Green for auto-detection modes
                drawList->AddRect(currentCropMin, currentCropMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 3.0f);
            }
            else
            {
                // Draw the yellow rectangle for manual selection
                drawList->AddRect(currentCropMin, currentCropMax, IM_COL32(255, 255, 0, 150), 0.0f, 0, 2.0f);
            }
            
            // Handle Resize Drag (Left Click)
            if (isResizingCropBox)
            {
                ImVec2 dragCurrentPos = mousePos;
                ImVec2 rectMin = ImVec2(std::min(dragStartPos.x, dragCurrentPos.x), std::min(dragStartPos.y, dragCurrentPos.y));
                ImVec2 rectMax = ImVec2(std::max(dragStartPos.x, dragCurrentPos.x), std::max(dragStartPos.y, dragCurrentPos.y));
                
                rectMin.x = std::max(rectMin.x, imageRectMin.x);
                rectMin.y = std::max(rectMin.y, imageRectMin.y);
                rectMax.x = std::min(rectMax.x, imageRectMax.x);
                rectMax.y = std::min(rectMax.y, imageRectMax.y);
                
                ImU32 overlayColor = IM_COL32(0, 0, 0, 120);
                drawList->AddRectFilled(imageRectMin, ImVec2(imageRectMax.x, rectMin.y), overlayColor);
                drawList->AddRectFilled(ImVec2(imageRectMin.x, rectMax.y), imageRectMax, overlayColor);
                drawList->AddRectFilled(ImVec2(imageRectMin.x, rectMin.y), ImVec2(rectMin.x, rectMax.y), overlayColor);
                drawList->AddRectFilled(ImVec2(rectMax.x, rectMin.y), ImVec2(imageRectMax.x, rectMax.y), overlayColor);
            }
            
            // Handle Move Drag (Right Click)
            if (isMovingCropBox && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                ImVec2 dragDelta = ImVec2(mousePos.x - dragStartPos.x, mousePos.y - dragStartPos.y);
                if (imageSize.x > 0 && imageSize.y > 0)
                {
                    float normDeltaX = dragDelta.x / imageSize.x;
                    float normDeltaY = dragDelta.y / imageSize.y;
                    
                    // Calculate new position and clamp it so the box stays within the image bounds
                    float newX = juce::jlimit(currW / 2.0f, 1.0f - currW / 2.0f, initialCropX + normDeltaX);
                    float newY = juce::jlimit(currH / 2.0f, 1.0f - currH / 2.0f, initialCropY + normDeltaY);
                    
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropX"))) *p = newX;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropY"))) *p = newY;
                }
            }
            
            // --- Handle Mouse Release ---
            if (isResizingCropBox && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                isResizingCropBox = false;
                ImVec2 dragEndPos = mousePos;
                ImVec2 rectMin = ImVec2(std::min(dragStartPos.x, dragEndPos.x), std::min(dragStartPos.y, dragEndPos.y));
                ImVec2 rectMax = ImVec2(std::max(dragStartPos.x, dragEndPos.x), std::max(dragStartPos.y, dragEndPos.y));
                
                rectMin.x = std::max(rectMin.x, imageRectMin.x);
                rectMin.y = std::max(rectMin.y, imageRectMin.y);
                rectMax.x = std::min(rectMax.x, imageRectMax.x);
                rectMax.y = std::min(rectMax.y, imageRectMax.y);
                
                if (imageSize.x > 0 && imageSize.y > 0)
                {
                    float newW = (rectMax.x - rectMin.x) / imageSize.x;
                    float newH = (rectMax.y - rectMin.y) / imageSize.y;
                    float newX = ((rectMin.x - imageRectMin.x) / imageSize.x) + newW / 2.0f;
                    float newY = ((rectMin.y - imageRectMin.y) / imageSize.y) + newH / 2.0f;
                    
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropX"))) *p = newX;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropY"))) *p = newY;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropW"))) *p = newW;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropH"))) *p = newH;
                    onModificationEnded();
                }
            }
            
            if (isMovingCropBox && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                isMovingCropBox = false;
                onModificationEnded();
            }
        }
    }
    
    // --- GPU Checkbox ---
    #if WITH_CUDA_SUPPORT
        bool cudaAvailable = (cv::cuda::getCudaEnabledDeviceCount() > 0);
        if (!cudaAvailable) ImGui::BeginDisabled();
        
        bool useGpu = useGpuParam ? useGpuParam->get() : false;
        if (ImGui::Checkbox("⚡ Use GPU (CUDA)", &useGpu))
        {
            *useGpuParam = useGpu;
            onModificationEnded();
        }
        
        if (!cudaAvailable)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("No CUDA-enabled GPU detected.\nCheck that your GPU supports CUDA and drivers are installed.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
    #endif
    
    // --- CORRECTED MODE SELECTION WITH DISABLED STATES ---
    ImGui::SetNextItemWidth(itemWidth);
    if (trackerModeParam && ImGui::BeginCombo("Tracking Mode", trackerModeParam->getCurrentChoiceName().toRawUTF8()))
    {
        if (ImGui::Selectable("Manual", mode == 0))
        {
            *trackerModeParam = 0;
            onModificationEnded();
        }
        
        // Disable "Track Face" if CPU cascade is not loaded
        if (faceCascadeCpu.empty()) ImGui::BeginDisabled();
        if (ImGui::Selectable("Track Face", mode == 1))
        {
            *trackerModeParam = 1;
            if (mode != 0) // If switching to an auto mode, stop manual tracking
            {
                const juce::ScopedLock lock(trackerLock);
                manualTrackingActive.store(false);
                manualTracker.release();
            }
            onModificationEnded();
        }
        if (ImGui::IsItemHovered() && faceCascadeCpu.empty())
            ImGui::SetTooltip("Face model not loaded!");
        if (faceCascadeCpu.empty()) ImGui::EndDisabled();
        
        // Disable "Track Object" if YOLO model is not loaded
        if (!yoloModelLoaded) ImGui::BeginDisabled();
        if (ImGui::Selectable("Track Object", mode == 2))
        {
            *trackerModeParam = 2;
            if (mode != 0) // If switching to an auto mode, stop manual tracking
            {
                const juce::ScopedLock lock(trackerLock);
                manualTrackingActive.store(false);
                manualTracker.release();
            }
            onModificationEnded();
        }
        if (ImGui::IsItemHovered() && !yoloModelLoaded)
            ImGui::SetTooltip("Object model not loaded!");
        if (!yoloModelLoaded) ImGui::EndDisabled();
        
        ImGui::EndCombo();
    }
    
    if (mode == 2) // Object Tracking
    {
        if (targetClassParam && !yoloClassNames.empty())
        {
            int idx = targetClassParam->getIndex();
            idx = juce::jlimit(0, (int)yoloClassNames.size() - 1, idx);
            const char* currentLabel = yoloClassNames[idx].c_str();
            
            auto& filterBuf = filterBufByNode[nodeId];
            
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::InputTextWithHint("##class_filter", "Filter...", filterBuf.data(), 64);
            
            ImGui::SetNextItemWidth(itemWidth * 0.55f);
            if (ImGui::BeginCombo("Target Class", currentLabel))
            {
                juce::String filter = juce::String(filterBuf.data()).toLowerCase();
                for (int i = 0; i < (int)yoloClassNames.size(); ++i)
                {
                    juce::String name(yoloClassNames[i]);
                    if (filter.isNotEmpty() && !name.toLowerCase().contains(filter))
                        continue;
                    if (ImGui::Selectable(name.toRawUTF8(), idx == i))
                    {
                        *targetClassParam = i;
                        onModificationEnded();
                    }
                    if (idx == i) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth * 0.45f - ImGui::GetStyle().ItemSpacing.x);
        float conf = confidenceParam ? confidenceParam->load() : 0.5f;
        if (ImGui::SliderFloat("##conf", &conf, 0.0f, 1.0f, "Conf %.2f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence")))
            {
                *p = conf;
                onModificationEnded();
            }
        }
    }
    
    if (mode == 0) // Manual Mode
    {
        if (manualTrackingActive.load())
        {
            if (ImGui::Button("Stop Tracking", ImVec2(itemWidth, 0)))
            {
                const juce::ScopedLock lock(trackerLock);
                manualTrackingActive.store(false);
                manualTracker.release();
            }
        }
        else
        {
            if (ImGui::Button("Start Tracking", ImVec2(itemWidth, 0)))
            {
                const juce::ScopedLock lock(trackerLock);
                if (!lastFrameForTracker.empty())
                {
                    cv::Rect initBox(
                        (int)((cropXParam->load() - cropWParam->load()/2.f) * lastFrameForTracker.cols),
                        (int)((cropYParam->load() - cropHParam->load()/2.f) * lastFrameForTracker.rows),
                        (int)(cropWParam->load() * lastFrameForTracker.cols),
                        (int)(cropHParam->load() * lastFrameForTracker.rows)
                    );
                    
                    if (initBox.area() > 0)
                    {
                        manualTracker = cv::TrackerMIL::create();
                        manualTracker->init(lastFrameForTracker, initBox);
                        manualTrackingActive.store(true);
                    }
                }
            }
        }
    }
    
    // --- Parameter Sliders ---
    ImGui::PushItemWidth(itemWidth);
    ImGui::Text("Manual Crop Controls:");
    
    // Disable sliders if any automatic tracking is active
    bool slidersDisabled = (mode != 0) || manualTrackingActive.load();
    if (slidersDisabled) ImGui::BeginDisabled();
    
    auto drawModSlider = [&](const char* label, const char* paramId, std::atomic<float>* paramPtr)
    {
        bool modulated = isParamModulated(juce::String(paramId) + "_mod");
        if (modulated) ImGui::BeginDisabled();
        
        float value = paramPtr ? paramPtr->load() : 0.5f;
        if (ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.3f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
            {
                *p = value;
                onModificationEnded();
            }
        }
        
        if (modulated)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("CV input connected");
        }
    };
    
    drawModSlider("Center X", "cropX", cropXParam);
    drawModSlider("Center Y", "cropY", cropYParam);
    drawModSlider("Width", "cropW", cropWParam);
    drawModSlider("Height", "cropH", cropHParam);
    
    if (slidersDisabled) ImGui::EndDisabled();
    
    float padding = paddingParam ? paddingParam->load() : 0.1f;
    if (ImGui::SliderFloat("Padding", &padding, 0.0f, 2.0f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("padding")))
        {
            *p = padding;
            onModificationEnded();
        }
    }
    
    if (aspectRatioModeParam)
    {
        int mode = aspectRatioModeParam->getIndex();
        const char* items[] = { "Stretch", "Preserve (Fit)" };
        if (ImGui::Combo("Aspect Ratio", &mode, items, 2))
        {
            *aspectRatioModeParam = mode;
            onModificationEnded();
        }
    }
    
    ImGui::PopItemWidth();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output ID: %d", (int)getLogicalId());
}

void CropVideoModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    // Modulation pins are now drawn automatically by the editor
    helpers.drawAudioOutputPin("Output ID", 0);
}
#endif

