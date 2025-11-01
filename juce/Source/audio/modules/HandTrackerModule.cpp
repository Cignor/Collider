#include "HandTrackerModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

// Thumb/Index/Middle/Ring/Pinky chains plus wrist
static const std::vector<std::pair<int, int>> HAND_SKELETON_PAIRS = {
    {0,1},{1,2},{2,3},{3,4},      // Thumb
    {0,5},{5,6},{6,7},{7,8},      // Index
    {0,9},{9,10},{10,11},{11,12}, // Middle
    {0,13},{13,14},{14,15},{15,16}, // Ring
    {0,17},{17,18},{18,19},{19,20}  // Pinky
};

juce::AudioProcessorValueTreeState::ParameterLayout HandTrackerModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("confidence", "Confidence", 0.0f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{"Small","Normal","Large"}, 1));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    return { params.begin(), params.end() };
}

HandTrackerModule::HandTrackerModule()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(HAND_NUM_KEYPOINTS * 2), true)
                        .withOutput("Video Out", juce::AudioChannelSet::mono(), true)    // PASSTHROUGH
                        .withOutput("Cropped Out", juce::AudioChannelSet::mono(), true)), // CROPPED
      juce::Thread("Hand Tracker Thread"),
      apvts(*this, nullptr, "HandTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    fifoBuffer.resize(16);
}

HandTrackerModule::~HandTrackerModule()
{
    stopThread(5000);
}

void HandTrackerModule::prepareToPlay(double, int)
{
    startThread(juce::Thread::Priority::normal);
}

void HandTrackerModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void HandTrackerModule::loadModel()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
    auto assetsDir = appDir.getChildFile("assets");
    auto handDir = assetsDir.getChildFile("openpose_models").getChildFile("hand");
    auto protoPath = handDir.getChildFile("pose_deploy.prototxt").getFullPathName();
    auto modelPath = handDir.getChildFile("pose_iter_102000.caffemodel").getFullPathName();
    if (juce::File(protoPath).existsAsFile() && juce::File(modelPath).existsAsFile())
    {
        try 
        { 
            net = cv::dnn::readNetFromCaffe(protoPath.toStdString(), modelPath.toStdString());
            
            // CRITICAL: Set backend immediately after loading model
            #if WITH_CUDA_SUPPORT
                bool useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    juce::Logger::writeToLog("[HandTracker] ✓ Model loaded with CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[HandTracker] Model loaded with CPU backend");
                }
            #else
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[HandTracker] Model loaded with CPU backend (CUDA not compiled)");
            #endif
            
            modelLoaded = true; 
        }
        catch (...) { modelLoaded = false; }
    }
}

void HandTrackerModule::run()
{
    if (!modelLoaded) loadModel();
    
    #if WITH_CUDA_SUPPORT
        bool lastGpuState = false; // Track GPU state to minimize backend switches
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        auto srcId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(srcId);
        if (frame.empty()) { wait(50); continue; }

        bool useGpu = false;
        
        #if WITH_CUDA_SUPPORT
            // Check if user wants GPU and if CUDA device is available
            useGpu = useGpuParam ? useGpuParam->get() : false;
            if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
            {
                useGpu = false; // Fallback to CPU
                if (!loggedGpuWarning)
                {
                    juce::Logger::writeToLog("[HandTracker] WARNING: GPU requested but no CUDA device found. Using CPU.");
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
                    juce::Logger::writeToLog("[HandTracker] ✓ Switched to CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[HandTracker] Switched to CPU backend");
                }
                lastGpuState = useGpu;
            }
        #endif

        // Save a clean copy for cropping (before drawing annotations)
        cv::Mat originalFrame;
        frame.copyTo(originalFrame);
        
        // NOTE: For DNN models, blobFromImage works on CPU
        // The GPU acceleration happens in net.forward() when backend is set to CUDA
        cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0/255.0, cv::Size(368,368), cv::Scalar(), false, false);
        net.setInput(blob);
        // Forward pass (GPU-accelerated if backend is CUDA)
        cv::Mat out = net.forward();
        HandResult result{};
        parseHandOutput(out, frame.cols, frame.rows, result);

        if (fifo.getFreeSpace() >= 1) { auto w = fifo.write(1); if (w.blockSize1 > 0) fifoBuffer[w.startIndex1] = result; }

        // --- CROPPED OUTPUT LOGIC ---
        // Calculate bounding box from detected keypoints
        int minX = INT_MAX, minY = INT_MAX, maxX = 0, maxY = 0;
        int validPoints = 0;
        for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
        {
            if (result.keypoints[i][0] >= 0 && result.keypoints[i][1] >= 0)
            {
                int x = (int)result.keypoints[i][0];
                int y = (int)result.keypoints[i][1];
                minX = juce::jmin(minX, x);
                minY = juce::jmin(minY, y);
                maxX = juce::jmax(maxX, x);
                maxY = juce::jmax(maxY, y);
                validPoints++;
            }
        }
        
        if (validPoints > 0)
        {
            // Add padding around the bounding box
            int padding = 20;
            int boxX = juce::jmax(0, minX - padding);
            int boxY = juce::jmax(0, minY - padding);
            int boxW = juce::jmin(originalFrame.cols - boxX, maxX - minX + padding * 2);
            int boxH = juce::jmin(originalFrame.rows - boxY, maxY - minY + padding * 2);
            cv::Rect box(boxX, boxY, boxW, boxH);
            box &= cv::Rect(0, 0, originalFrame.cols, originalFrame.rows);
            
            if (box.area() > 0)
            {
                cv::Mat cropped = originalFrame(box);
                VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), cropped);
            }
        }
        else
        {
            // Clear cropped output when no hand detected
            cv::Mat emptyFrame;
            VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), emptyFrame);
        }

        // Draw minimal skeleton
        for (const auto& p : HAND_SKELETON_PAIRS)
        {
            int a=p.first,b=p.second; if (result.keypoints[a][0] >= 0 && result.keypoints[b][0] >= 0)
                cv::line(frame, { (int)result.keypoints[a][0], (int)result.keypoints[a][1] },
                              { (int)result.keypoints[b][0], (int)result.keypoints[b][1] }, {0,255,0}, 2);
        }
        for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
            if (result.keypoints[i][0] >= 0)
                cv::circle(frame, { (int)result.keypoints[i][0], (int)result.keypoints[i][1] }, 3, {0,0,255}, -1);
        
        // --- PASSTHROUGH LOGIC ---
        VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
        updateGuiFrame(frame);
        wait(66);
    }
}

void HandTrackerModule::parseHandOutput(const cv::Mat& netOutput, int frameWidth, int frameHeight, HandResult& result)
{
    int H = netOutput.size[2];
    int W = netOutput.size[3];
    float thresh = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    result.detectedPoints = 0;
    int count = juce::jmin(HAND_NUM_KEYPOINTS, netOutput.size[1]);
    for (int i=0;i<count;++i)
    {
        cv::Mat heat(H, W, CV_32F, (void*)netOutput.ptr<float>(0,i));
        double maxVal; cv::Point maxLoc; cv::minMaxLoc(heat, nullptr, &maxVal, nullptr, &maxLoc);
        if (maxVal > thresh) { result.keypoints[i][0] = (float)maxLoc.x * frameWidth / W; result.keypoints[i][1] = (float)maxLoc.y * frameHeight / H; result.detectedPoints++; }
        else { result.keypoints[i][0] = -1.0f; result.keypoints[i][1] = -1.0f; }
    }
}

void HandTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    // Read Source ID from input pin
    auto in = getBusBuffer(buffer, true, 0);
    if (in.getNumChannels()>0 && in.getNumSamples()>0) currentSourceId.store((juce::uint32)in.getSample(0,0));
    
    if (fifo.getNumReady()>0) { auto r=fifo.read(1); if (r.blockSize1>0) lastResultForAudio=fifoBuffer[r.startIndex1]; }
    
    // Output CV on bus 0
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
    {
        int chX=i*2, chY=i*2+1; if (chY>=cvOutBus.getNumChannels()) break;
        float xn = (lastResultForAudio.keypoints[i][0] >= 0) ? juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][0]/640.0f) : 0.0f;
        float yn = (lastResultForAudio.keypoints[i][1] >= 0) ? juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][1]/480.0f) : 0.0f;
        for (int s=0;s<cvOutBus.getNumSamples();++s)
        {
            cvOutBus.setSample(chX,s,xn); 
            cvOutBus.setSample(chY,s,yn);
        }
    }
    
    // Passthrough Video ID on bus 1
    auto videoOutBus = getBusBuffer(buffer, false, 1);
    if (videoOutBus.getNumChannels() > 0)
    {
        float primaryId = static_cast<float>(getLogicalId());
        for (int s = 0; s < videoOutBus.getNumSamples(); ++s)
            videoOutBus.setSample(0, s, primaryId);
    }
    
    // Cropped Video ID on bus 2
    auto croppedOutBus = getBusBuffer(buffer, false, 2);
    if (croppedOutBus.getNumChannels() > 0)
    {
        float secondaryId = static_cast<float>(getSecondaryLogicalId());
        for (int s = 0; s < croppedOutBus.getNumSamples(); ++s)
            croppedOutBus.setSample(0, s, secondaryId);
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 HandTrackerModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1; level = juce::jlimit(0,2,level);
    const float widths[3]{240.0f,480.0f,960.0f}; return ImVec2(widths[level],0.0f);
}

static const char* HAND_NAMES[HAND_NUM_KEYPOINTS] = {
    "Wrist",
    "Thumb1","Thumb2","Thumb3","Thumb4",
    "Index1","Index2","Index3","Index4",
    "Middle1","Middle2","Middle3","Middle4",
    "Ring1","Ring2","Ring3","Ring4",
    "Pinky1","Pinky2","Pinky3","Pinky4"
};

void HandTrackerModule::drawParametersInNode(float itemWidth,
                                             const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                             const std::function<void()>& onModificationEnded)
{
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
            ImGui::SetTooltip("Enable GPU acceleration for hand tracking.\nRequires CUDA-capable NVIDIA GPU.");
        }
        
        ImGui::Separator();
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
        ImGui::Separator();
    #endif
    
    // Confidence
    float conf = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    if (ImGui::SliderFloat("Confidence", &conf, 0.0f, 1.0f, "%.2f")) {
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence")) = conf; onModificationEnded();
    }
    // Zoom -/+
    int level = zoomLevelParam ? (int)zoomLevelParam->load() : 1; level = juce::jlimit(0,2,level);
    float bw = (itemWidth/2.0f)-4.0f; bool atMin=(level<=0), atMax=(level>=2);
    if(atMin) ImGui::BeginDisabled(); if(ImGui::Button("-", ImVec2(bw,0))){ int nl=juce::jmax(0,level-1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMin) ImGui::EndDisabled(); ImGui::SameLine(); if(atMax) ImGui::BeginDisabled(); if(ImGui::Button("+", ImVec2(bw,0))){ int nl=juce::jmin(2,level+1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMax) ImGui::EndDisabled();
    ImGui::PopItemWidth();
}

void HandTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
    {
        juce::String x = juce::String(HAND_NAMES[i]) + " X";
        juce::String y = juce::String(HAND_NAMES[i]) + " Y";
        helpers.drawAudioOutputPin(x.toRawUTF8(), i*2);
        helpers.drawAudioOutputPin(y.toRawUTF8(), i*2+1);
    }
    helpers.drawAudioOutputPin("Video Out", 0);     // Bus 1
    helpers.drawAudioOutputPin("Cropped Out", 1); // Bus 2
}

#endif

void HandTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra; cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth()!=bgra.cols || latestFrameForGui.getHeight()!=bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgra.data, bgra.total()*bgra.elemSize());
}

juce::Image HandTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

std::vector<DynamicPinInfo> HandTrackerModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (42 channels - 21 keypoints * 2 coordinates)
    // Bus 1: Video Out (1 channel)
    // Bus 2: Cropped Out (1 channel)
    std::vector<DynamicPinInfo> pins;
    
    // Hand keypoint names matching PinDatabase
    const char* handNames[21] = {
        "Wrist",
        "Thumb 1","Thumb 2","Thumb 3","Thumb 4",
        "Index 1","Index 2","Index 3","Index 4",
        "Middle 1","Middle 2","Middle 3","Middle 4",
        "Ring 1","Ring 2","Ring 3","Ring 4",
        "Pinky 1","Pinky 2","Pinky 3","Pinky 4"
    };
    
    // Add all 21 keypoint pins
    for (int i = 0; i < 21; ++i)
    {
        pins.emplace_back(std::string(handNames[i]) + " X", i * 2, PinDataType::CV);
        pins.emplace_back(std::string(handNames[i]) + " Y", i * 2 + 1, PinDataType::CV);
    }
    
    // Add Video Out and Cropped Out pins
    const int videoOutStartChannel = 42;
    const int croppedOutStartChannel = videoOutStartChannel + 1;
    pins.emplace_back("Video Out", videoOutStartChannel, PinDataType::Video);
    pins.emplace_back("Cropped Out", croppedOutStartChannel, PinDataType::Video);
    
    return pins;
}


