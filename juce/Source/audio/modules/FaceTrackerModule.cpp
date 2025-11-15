#include "FaceTrackerModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout FaceTrackerModule::createParameterLayout()
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

FaceTrackerModule::FaceTrackerModule()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(36), true)  // Simplified: 2 face center + 2 nose + 8 R eye + 8 L eye + 8 mouth + 8 eyebrows = 36
                        .withOutput("Video Out", juce::AudioChannelSet::mono(), true)    // PASSTHROUGH
                        .withOutput("Cropped Out", juce::AudioChannelSet::mono(), true)), // CROPPED
      juce::Thread("Face Tracker Thread"),
      apvts(*this, nullptr, "FaceTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    fifoBuffer.resize(16);
}

FaceTrackerModule::~FaceTrackerModule()
{
    stopThread(5000);
}

void FaceTrackerModule::prepareToPlay(double, int)
{
    startThread(juce::Thread::Priority::normal);
}

void FaceTrackerModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void FaceTrackerModule::loadModel()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
    auto assetsDir = appDir.getChildFile("assets");
    auto faceDir = assetsDir.getChildFile("openpose_models").getChildFile("face");
    auto haarPath = faceDir.getChildFile("haarcascade_frontalface_alt.xml").getFullPathName();
    auto protoPath = faceDir.getChildFile("pose_deploy.prototxt").getFullPathName();
    auto modelPath = faceDir.getChildFile("pose_iter_116000.caffemodel").getFullPathName();
    bool ok = faceCascade.load(haarPath.toStdString());
    if (ok && juce::File(protoPath).existsAsFile() && juce::File(modelPath).existsAsFile())
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
                    juce::Logger::writeToLog("[FaceTracker] ✓ Model loaded with CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[FaceTracker] Model loaded with CPU backend");
                }
            #else
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[FaceTracker] Model loaded with CPU backend (CUDA not compiled)");
            #endif
            
            modelLoaded = true; 
        }
        catch (...) { modelLoaded = false; }
    }
}

void FaceTrackerModule::run()
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
                    juce::Logger::writeToLog("[FaceTracker] WARNING: GPU requested but no CUDA device found. Using CPU.");
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
                    juce::Logger::writeToLog("[FaceTracker] ✓ Switched to CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[FaceTracker] Switched to CPU backend");
                }
                lastGpuState = useGpu;
            }
        #endif

        // Save a clean copy for cropping (before drawing annotations)
        cv::Mat originalFrame;
        frame.copyTo(originalFrame);
        
        cv::Mat gray; cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> faces; faceCascade.detectMultiScale(gray, faces);
        FaceResult result{};
        if (!faces.empty())
        {
            cv::Rect box = faces[0];
            
            // --- CROPPED OUTPUT LOGIC ---
            cv::Rect validBox = box & cv::Rect(0, 0, originalFrame.cols, originalFrame.rows);
            if (validBox.area() > 0)
            {
                cv::Mat cropped = originalFrame(validBox);
                VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), cropped);
            }
            
            cv::Mat roi = frame(box);
            // NOTE: For DNN models, blobFromImage works on CPU
            // The GPU acceleration happens in net.forward() when backend is set to CUDA
            cv::Mat blob = cv::dnn::blobFromImage(roi, 1.0/255.0, cv::Size(368,368), cv::Scalar(), false, false);
            net.setInput(blob);
            // Forward pass (GPU-accelerated if backend is CUDA)
            cv::Mat out = net.forward();
            parseFaceOutput(out, box, result);
            // Draw box
            cv::rectangle(frame, box, {0,255,0}, 2);
            for (int i=0;i<FACE_NUM_KEYPOINTS;++i)
                if (result.keypoints[i][0] >= 0)
                    cv::circle(frame, { (int)result.keypoints[i][0], (int)result.keypoints[i][1] }, 2, {0,0,255}, -1);
        }
        else
        {
            // Clear cropped output when no face detected
            cv::Mat emptyFrame;
            VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), emptyFrame);
            // Mark face center as invalid when no face detected
            result.faceCenterX = -1.0f;
            result.faceCenterY = -1.0f;
        }
        
        if (fifo.getFreeSpace()>=1){ auto w=fifo.write(1); if (w.blockSize1>0) fifoBuffer[w.startIndex1]=result; }
        
        // --- PASSTHROUGH LOGIC ---
        if (myLogicalId != 0)
            VideoFrameManager::getInstance().setFrame(myLogicalId, frame);
        updateGuiFrame(frame);
        wait(66);
    }
}

void FaceTrackerModule::parseFaceOutput(const cv::Mat& netOutput, const cv::Rect& faceBox, FaceResult& result)
{
    int H=netOutput.size[2], W=netOutput.size[3];
    float thresh = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    result.detectedPoints=0; int count=juce::jmin(FACE_NUM_KEYPOINTS, netOutput.size[1]);
    
    // Calculate face center from bounding box (this is our reference point)
    result.faceCenterX = faceBox.x + faceBox.width * 0.5f;
    result.faceCenterY = faceBox.y + faceBox.height * 0.5f;
    
    for (int i=0;i<count;++i)
    {
        cv::Mat heat(H,W,CV_32F,(void*)netOutput.ptr<float>(0,i));
        double mv; cv::Point ml; cv::minMaxLoc(heat,nullptr,&mv,nullptr,&ml);
        if (mv>thresh){ result.keypoints[i][0]=(float)faceBox.x + (float)ml.x*faceBox.width/W; result.keypoints[i][1]=(float)faceBox.y + (float)ml.y*faceBox.height/H; result.detectedPoints++; }
        else { result.keypoints[i][0]=-1.0f; result.keypoints[i][1]=-1.0f; }
    }
}

void FaceTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto in=getBusBuffer(buffer,true,0); if(in.getNumChannels()>0 && in.getNumSamples()>0) currentSourceId.store((juce::uint32)in.getSample(0,0));
    
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
    
    if(fifo.getNumReady()>0){auto r=fifo.read(1); if(r.blockSize1>0) lastResultForAudio=fifoBuffer[r.startIndex1];}
    
    // Output CV on bus 0
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    
    // Get face center position (our reference point)
    float faceCenterX = lastResultForAudio.faceCenterX;
    float faceCenterY = lastResultForAudio.faceCenterY;
    
    // Normalization factors (using typical frame size - actual frame size could vary)
    // These are used to normalize relative offsets to a reasonable 0.0-1.0 range
    const float normScaleX = 1.0f / 640.0f;  // Normalize by typical frame width
    const float normScaleY = 1.0f / 480.0f;  // Normalize by typical frame height
    
    // Helper function to output a keypoint relative to face center
    auto outputKeypoint = [&](int keypointIndex, int channelStart, int numSamples) {
        int chX = channelStart;
        int chY = channelStart + 1;
        
        if (faceCenterX >= 0 && faceCenterY >= 0 && 
            keypointIndex >= 0 && keypointIndex < FACE_NUM_KEYPOINTS &&
            lastResultForAudio.keypoints[keypointIndex][0] >= 0 && 
            lastResultForAudio.keypoints[keypointIndex][1] >= 0)
        {
            // Calculate relative offset from face center
            float relX = (lastResultForAudio.keypoints[keypointIndex][0] - faceCenterX) * normScaleX;
            float relY = (lastResultForAudio.keypoints[keypointIndex][1] - faceCenterY) * normScaleY;
            
            // Map relative offset to 0.0-1.0 range where 0.5 = no offset (centered at face center)
            const float relScale = 2.5f;  // Scale factor to make relative positions more usable
            float relX_norm = juce::jlimit(0.0f, 1.0f, 0.5f + relX * relScale);
            float relY_norm = juce::jlimit(0.0f, 1.0f, 0.5f + relY * relScale);
            
            for(int s=0; s<numSamples; ++s)
            {
                if (chX < cvOutBus.getNumChannels())
                    cvOutBus.setSample(chX, s, relX_norm);
                if (chY < cvOutBus.getNumChannels())
                    cvOutBus.setSample(chY, s, relY_norm);
            }
        }
        else
        {
            // No face center or keypoint not detected - output center value (0.5 = no offset)
            for(int s=0; s<numSamples; ++s)
            {
                if (chX < cvOutBus.getNumChannels())
                    cvOutBus.setSample(chX, s, 0.5f);
                if (chY < cvOutBus.getNumChannels())
                    cvOutBus.setSample(chY, s, 0.5f);
            }
        }
    };
    
    int numSamples = cvOutBus.getNumSamples();
    
    // Output face center absolute position (channels 0, 1)
    float faceCenterX_norm = (faceCenterX >= 0) ? juce::jlimit(0.0f, 1.0f, faceCenterX * normScaleX) : 0.5f;
    float faceCenterY_norm = (faceCenterY >= 0) ? juce::jlimit(0.0f, 1.0f, faceCenterY * normScaleY) : 0.5f;
    for (int s=0; s<numSamples; ++s)
    {
        cvOutBus.setSample(0, s, faceCenterX_norm);  // Face Center X (absolute)
        cvOutBus.setSample(1, s, faceCenterY_norm);  // Face Center Y (absolute)
    }
    
    // Output Nose Base (index 32) - channels 2, 3
    outputKeypoint(32, 2, numSamples);
    
    // Output Right Eye: Outer(36), Top(37), Inner(38), Bottom(39) - channels 4-11
    outputKeypoint(36, 4, numSamples);   // R Eye Outer
    outputKeypoint(37, 6, numSamples);   // R Eye Top
    outputKeypoint(38, 8, numSamples);   // R Eye Inner
    outputKeypoint(39, 10, numSamples);  // R Eye Bottom
    
    // Output Left Eye: Inner(42), Top(43), Outer(44), Bottom(45) - channels 12-19
    outputKeypoint(42, 12, numSamples);  // L Eye Inner
    outputKeypoint(43, 14, numSamples);  // L Eye Top
    outputKeypoint(44, 16, numSamples);  // L Eye Outer
    outputKeypoint(45, 18, numSamples);  // L Eye Bottom
    
    // Output Mouth: Corner R(48), Top Center(51), Corner L(54), Bottom Center(57) - channels 20-27
    outputKeypoint(48, 20, numSamples);  // Mouth Corner R
    outputKeypoint(51, 22, numSamples);  // Mouth Top Center
    outputKeypoint(54, 24, numSamples);  // Mouth Corner L
    outputKeypoint(57, 26, numSamples);  // Mouth Bottom Center
    
    // Output Eyebrows: R Outer(17), R Inner(21), L Inner(22), L Outer(26) - channels 28-35
    outputKeypoint(17, 28, numSamples);  // R Eyebrow Outer
    outputKeypoint(21, 30, numSamples);  // R Eyebrow Inner
    outputKeypoint(22, 32, numSamples);  // L Eyebrow Inner
    outputKeypoint(26, 34, numSamples);  // L Eyebrow Outer
    
    // Passthrough Video ID on bus 1
    auto videoOutBus = getBusBuffer(buffer, false, 1);
    if (videoOutBus.getNumChannels() > 0)
    {
        float primaryId = static_cast<float>(myLogicalId); // Use the resolved ID
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
ImVec2 FaceTrackerModule::getCustomNodeSize() const
{ int level=zoomLevelParam?(int)zoomLevelParam->load():1; level=juce::jlimit(0,2,level); const float widths[3]{240.0f,480.0f,960.0f}; return ImVec2(widths[level],0.0f);} 

void FaceTrackerModule::drawParametersInNode(float itemWidth,
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
            ImGui::SetTooltip("Enable GPU acceleration for face tracking.\nRequires CUDA-capable NVIDIA GPU.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
    #endif
    
    float conf=confidenceThresholdParam?confidenceThresholdParam->load():0.1f;
    if (ImGui::SliderFloat("Confidence", &conf, 0.0f, 1.0f, "%.2f")) { *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence"))=conf; onModificationEnded(); }
    int level=zoomLevelParam?(int)zoomLevelParam->load():1; level=juce::jlimit(0,2,level); float bw=(itemWidth/2.0f)-4.0f; bool atMin=(level<=0), atMax=(level>=2);
    if(atMin) ImGui::BeginDisabled(); if(ImGui::Button("-", ImVec2(bw,0))){ int nl=juce::jmax(0,level-1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMin) ImGui::EndDisabled(); ImGui::SameLine(); if(atMax) ImGui::BeginDisabled(); if(ImGui::Button("+", ImVec2(bw,0))){ int nl=juce::jmin(2,level+1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMax) ImGui::EndDisabled();
    ImGui::PopItemWidth();
}

void FaceTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    // Outputs are dynamic; editor queries via getDynamicOutputPins
    helpers.drawAudioOutputPin("Video Out", 0);     // Bus 1
    helpers.drawAudioOutputPin("Cropped Out", 1); // Bus 2
}

#endif

void FaceTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra; cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA); const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth()!=bgra.cols || latestFrameForGui.getHeight()!=bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly); memcpy(dest.data, bgra.data, bgra.total()*bgra.elemSize());
}

juce::Image FaceTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

std::vector<DynamicPinInfo> FaceTrackerModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (36 channels - simplified set of key expressive points)
    //   Channels 0-1: Face Center X/Y (absolute screen position)
    //   Channels 2-3: Nose Base (relative to face center)
    //   Channels 4-11: Right Eye (4 points: Outer, Top, Inner, Bottom)
    //   Channels 12-19: Left Eye (4 points: Inner, Top, Outer, Bottom)
    //   Channels 20-27: Mouth (4 points: Corner R, Top Center, Corner L, Bottom Center)
    //   Channels 28-35: Eyebrows (4 points: R Outer, R Inner, L Inner, L Outer)
    // Bus 1: Video Out (1 channel) - PASSTHROUGH
    // Bus 2: Cropped Out (1 channel) - CROPPED
    std::vector<DynamicPinInfo> pins;
    
    // Add face center pins (absolute position)
    pins.emplace_back("Face Center X (Abs)", 0, PinDataType::CV);
    pins.emplace_back("Face Center Y (Abs)", 1, PinDataType::CV);
    
    // Nose Base (channels 2, 3)
    pins.emplace_back("Nose Base X (Rel)", 2, PinDataType::CV);
    pins.emplace_back("Nose Base Y (Rel)", 3, PinDataType::CV);
    
    // Right Eye (channels 4-11)
    pins.emplace_back("R Eye Outer X (Rel)", 4, PinDataType::CV);
    pins.emplace_back("R Eye Outer Y (Rel)", 5, PinDataType::CV);
    pins.emplace_back("R Eye Top X (Rel)", 6, PinDataType::CV);
    pins.emplace_back("R Eye Top Y (Rel)", 7, PinDataType::CV);
    pins.emplace_back("R Eye Inner X (Rel)", 8, PinDataType::CV);
    pins.emplace_back("R Eye Inner Y (Rel)", 9, PinDataType::CV);
    pins.emplace_back("R Eye Bottom X (Rel)", 10, PinDataType::CV);
    pins.emplace_back("R Eye Bottom Y (Rel)", 11, PinDataType::CV);
    
    // Left Eye (channels 12-19)
    pins.emplace_back("L Eye Inner X (Rel)", 12, PinDataType::CV);
    pins.emplace_back("L Eye Inner Y (Rel)", 13, PinDataType::CV);
    pins.emplace_back("L Eye Top X (Rel)", 14, PinDataType::CV);
    pins.emplace_back("L Eye Top Y (Rel)", 15, PinDataType::CV);
    pins.emplace_back("L Eye Outer X (Rel)", 16, PinDataType::CV);
    pins.emplace_back("L Eye Outer Y (Rel)", 17, PinDataType::CV);
    pins.emplace_back("L Eye Bottom X (Rel)", 18, PinDataType::CV);
    pins.emplace_back("L Eye Bottom Y (Rel)", 19, PinDataType::CV);
    
    // Mouth (channels 20-27)
    pins.emplace_back("Mouth Corner R X (Rel)", 20, PinDataType::CV);
    pins.emplace_back("Mouth Corner R Y (Rel)", 21, PinDataType::CV);
    pins.emplace_back("Mouth Top Center X (Rel)", 22, PinDataType::CV);
    pins.emplace_back("Mouth Top Center Y (Rel)", 23, PinDataType::CV);
    pins.emplace_back("Mouth Corner L X (Rel)", 24, PinDataType::CV);
    pins.emplace_back("Mouth Corner L Y (Rel)", 25, PinDataType::CV);
    pins.emplace_back("Mouth Bottom Center X (Rel)", 26, PinDataType::CV);
    pins.emplace_back("Mouth Bottom Center Y (Rel)", 27, PinDataType::CV);
    
    // Eyebrows (channels 28-35)
    pins.emplace_back("R Eyebrow Outer X (Rel)", 28, PinDataType::CV);
    pins.emplace_back("R Eyebrow Outer Y (Rel)", 29, PinDataType::CV);
    pins.emplace_back("R Eyebrow Inner X (Rel)", 30, PinDataType::CV);
    pins.emplace_back("R Eyebrow Inner Y (Rel)", 31, PinDataType::CV);
    pins.emplace_back("L Eyebrow Inner X (Rel)", 32, PinDataType::CV);
    pins.emplace_back("L Eyebrow Inner Y (Rel)", 33, PinDataType::CV);
    pins.emplace_back("L Eyebrow Outer X (Rel)", 34, PinDataType::CV);
    pins.emplace_back("L Eyebrow Outer Y (Rel)", 35, PinDataType::CV);
    
    // Add Video Out and Cropped Out pins (after CV outputs, like HandTrackerModule)
    const int videoOutStartChannel = 36;
    const int croppedOutStartChannel = videoOutStartChannel + 1;
    pins.emplace_back("Video Out", videoOutStartChannel, PinDataType::Video);
    pins.emplace_back("Cropped Out", croppedOutStartChannel, PinDataType::Video);
    
    return pins;
}


