#include "MovementDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/theme/ThemeManager.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MovementDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray{"Optical Flow", "Background Subtraction"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Sensitivity", 0.01f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    // NEW: tuning parameters
    params.push_back(std::make_unique<juce::AudioParameterInt>("maxFeatures", "Max Features", 20, 500, 100));
    params.push_back(std::make_unique<juce::AudioParameterBool>("noiseReduction", "Noise Reduction", false));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    return { params.begin(), params.end() };
}

MovementDetectorModule::MovementDetectorModule()
    : ModuleProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(4), true)
                     .withOutput("Video Out", juce::AudioChannelSet::mono(), true)), // PASSTHROUGH
      juce::Thread("Movement Detector Analysis Thread"),
      apvts(*this, nullptr, "MovementParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    sensitivityParam = apvts.getRawParameterValue("sensitivity");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    // NEW: init parameter pointers
    maxFeaturesParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("maxFeatures"));
    noiseReductionParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("noiseReduction"));
    pBackSub = cv::createBackgroundSubtractorMOG2();
    
    fifoBuffer.resize(16);
    fifo.setTotalSize(16);
    
    // Initialize lastMaxFeatures to default value
    lastMaxFeatures = maxFeaturesParam ? maxFeaturesParam->get() : 100;
}

MovementDetectorModule::~MovementDetectorModule()
{
    stopThread(5000);
}

void MovementDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void MovementDetectorModule::releaseResources()
{
    signalThreadShouldExit();
}

void MovementDetectorModule::run()
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
    
    // Analysis loop runs on background thread
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        
        if (sourceId == 0)
        {
            // No source connected
            wait(100);
            continue;
        }
        
        // Get frame from VideoFrameManager
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        if (!frame.empty())
        {
            // Perform analysis
            MovementResult result = analyzeFrame(frame, myLogicalId);
            
            // Push result to FIFO for audio thread
            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                {
                    fifoBuffer[writeScope.startIndex1] = result;
                }
            }
        }
        else if (myLogicalId != 0)
        {
            // Input frame is empty, but we should still output the last good frame to prevent freezing
            // This ensures continuous video output even when source temporarily stops
            const juce::ScopedLock lock(lastOutputFrameLock);
            if (!lastOutputFrame.empty())
            {
                VideoFrameManager::getInstance().setFrame(myLogicalId, lastOutputFrame);
            }
        }
        
        wait(33); // ~30 FPS analysis rate
    }
}

MovementResult MovementDetectorModule::analyzeFrame(const cv::Mat& inputFrame, juce::uint32 logicalId)
{
    MovementResult result;
    cv::Mat gray, displayFrame;
    
    inputFrame.copyTo(displayFrame);
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(320, 240));
    cv::resize(displayFrame, displayFrame, cv::Size(320, 240));

    int mode = (int)modeParam->load();

    if (mode == 0) // Optical Flow
    {
        int maxFeatures = maxFeaturesParam ? maxFeaturesParam->get() : 100;
        
        // Re-detect features if:
        // 1. Parameter changed (any change triggers re-detection for immediate feedback)
        // 2. Current point count is significantly below desired maxFeatures (less than 70% of target)
        // 3. Point count dropped below absolute minimum threshold (50)
        bool shouldRedetect = false;
        if (lastMaxFeatures != maxFeatures)
        {
            // Parameter changed - always re-detect for immediate visual feedback
            shouldRedetect = true;
            lastMaxFeatures = maxFeatures;
        }
        
        // Also re-detect if we have significantly fewer points than desired
        int minDesiredPoints = (int)(maxFeatures * 0.7f); // Want at least 70% of target
        if (prevPoints.size() < juce::jmax(50, minDesiredPoints))
        {
            shouldRedetect = true;
        }
        
        if (shouldRedetect)
        {
            // Clear previous points before re-detection
            prevPoints.clear();
            // Detect features - note: goodFeaturesToTrack finds UP TO maxFeatures, not exactly maxFeatures
            // Adjust quality threshold: lower threshold allows more features when maxFeatures is high
            // Quality threshold ranges from 0.1 (more features) to 0.3 (fewer, higher quality features)
            double qualityLevel = juce::jmap((double)maxFeatures, 20.0, 500.0, 0.3, 0.1);
            qualityLevel = juce::jlimit(0.05, 0.3, qualityLevel); // Clamp to reasonable range
            cv::goodFeaturesToTrack(gray, prevPoints, maxFeatures, qualityLevel, 7);
            // Draw all newly detected feature points immediately (blue circles)
            for (const auto& pt : prevPoints)
            {
                cv::circle(displayFrame, pt, 3, cv::Scalar(255, 0, 0), -1); // Blue filled circle
            }
        }

        if (!prevGrayFrame.empty() && !prevPoints.empty())
        {
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0);
            #endif
            
            std::vector<cv::Point2f> nextPoints;
            std::vector<uchar> status;
            const int levels = 3; // Fixed pyramid levels (optimal default)
            
            #if WITH_CUDA_SUPPORT
                if (useGpu)
                {
                    // GPU optical flow
                    cv::cuda::GpuMat prevGrayGpu, currGrayGpu;
                    prevGrayGpu.upload(prevGrayFrame);
                    currGrayGpu.upload(gray);
                    
                    cv::cuda::GpuMat prevPointsGpu, nextPointsGpu, statusGpu, errGpu;
                    // Convert vector to Mat for upload
                    cv::Mat prevPointsMat(1, (int)prevPoints.size(), CV_32FC2);
                    for (size_t i = 0; i < prevPoints.size(); ++i)
                    {
                        prevPointsMat.at<cv::Vec2f>(0, (int)i) = cv::Vec2f(prevPoints[i].x, prevPoints[i].y);
                    }
                    prevPointsGpu.upload(prevPointsMat);
                    
                    cv::Ptr<cv::cuda::SparsePyrLKOpticalFlow> optFlow = cv::cuda::SparsePyrLKOpticalFlow::create();
                    optFlow->setMaxLevel(levels);
                    optFlow->calc(prevGrayGpu, currGrayGpu, prevPointsGpu, nextPointsGpu, statusGpu, errGpu);
                    
                    // Download results
                    cv::Mat nextPointsMat, statusMat;
                    nextPointsGpu.download(nextPointsMat);
                    statusGpu.download(statusMat);
                    
                    nextPoints.resize(prevPoints.size());
                    status.resize(prevPoints.size());
                    for (size_t i = 0; i < prevPoints.size(); ++i)
                    {
                        cv::Vec2f pt = nextPointsMat.at<cv::Vec2f>(0, (int)i);
                        nextPoints[i] = cv::Point2f(pt[0], pt[1]);
                        status[i] = statusMat.at<uchar>(0, (int)i);
                    }
                }
                else
            #endif
            {
                // CPU optical flow
                cv::calcOpticalFlowPyrLK(prevGrayFrame, gray, prevPoints, nextPoints, status, cv::noArray(),
                                         cv::Size(15, 15), levels);
            }

            float sumX = 0.0f, sumY = 0.0f;
            int trackedCount = 0;
            
            // First, draw all feature points (blue circles)
            for (const auto& pt : prevPoints)
            {
                cv::circle(displayFrame, pt, 2, cv::Scalar(255, 0, 0), -1); // Blue filled circle
            }
            
            // Then draw tracking vectors for successfully tracked points (green lines)
            for (size_t i = 0; i < prevPoints.size(); ++i)
            {
                if (status[i])
                {
                    cv::line(displayFrame, prevPoints[i], nextPoints[i], cv::Scalar(0, 255, 0), 1);
                    
                    sumX += nextPoints[i].x - prevPoints[i].x;
                    sumY += nextPoints[i].y - prevPoints[i].y;
                    trackedCount++;
                }
            }

            if (trackedCount > 0)
            {
                // Normalize based on frame dimensions (320x240) to account for aspect ratio
                // Use a normalization factor that scales motion to -1 to +1 range
                // Frame is 320 wide, 240 tall, so normalize X by width and Y by height
                const float frameWidth = 320.0f;
                const float frameHeight = 240.0f;
                const float normalizationFactor = 0.1f; // Scale factor for sensitivity
                
                result.avgMotionX = juce::jlimit(-1.0f, 1.0f, (sumX / trackedCount) / (frameWidth * normalizationFactor));
                result.avgMotionY = juce::jlimit(-1.0f, 1.0f, (sumY / trackedCount) / (frameHeight * normalizationFactor));
                result.motionAmount = juce::jlimit(0.0f, 1.0f, std::sqrt(result.avgMotionX * result.avgMotionX + result.avgMotionY * result.avgMotionY));
                
                if (result.motionAmount > sensitivityParam->load())
                {
                    result.motionTrigger = true;
                }
            }
            prevPoints = nextPoints;
        }
        else if (!prevPoints.empty())
        {
            // No previous frame yet, but we have detected points - draw them
            for (const auto& pt : prevPoints)
            {
                cv::circle(displayFrame, pt, 3, cv::Scalar(255, 0, 0), -1); // Blue filled circle
            }
        }
        gray.copyTo(prevGrayFrame);
    }
    else // Background Subtraction
    {
        cv::Mat fgMask;
        pBackSub->apply(gray, fgMask);
        if (noiseReductionParam && noiseReductionParam->get())
        {
            cv::erode(fgMask, fgMask, cv::Mat(), cv::Point(-1, -1), 1);
            cv::dilate(fgMask, fgMask, cv::Mat(), cv::Point(-1, -1), 2);
        }
        
        cv::Moments m = cv::moments(fgMask, true);
        result.motionAmount = juce::jlimit(0.0f, 1.0f, (float)(m.m00 / (320 * 240)));
        
        if (result.motionAmount > 0.001)
        {
             result.avgMotionX = juce::jmap((float)(m.m10 / m.m00), 0.0f, 320.0f, -1.0f, 1.0f);
             result.avgMotionY = juce::jmap((float)(m.m01 / m.m00), 0.0f, 240.0f, -1.0f, 1.0f);
             
             cv::Point2f centroid(m.m10 / m.m00, m.m01 / m.m00);
             cv::circle(displayFrame, centroid, 5, cv::Scalar(0, 255, 0), -1);
             cv::circle(displayFrame, centroid, 8, cv::Scalar(255, 255, 255), 2);
        }
        
        cv::Mat fgMaskColor;
        cv::cvtColor(fgMask, fgMaskColor, cv::COLOR_GRAY2BGR);
        cv::addWeighted(displayFrame, 0.7, fgMaskColor, 0.3, 0, displayFrame);
        
        if (result.motionAmount > sensitivityParam->load())
        {
            result.motionTrigger = true;
        }
    }

    // --- PASSTHROUGH LOGIC ---
    if (logicalId != 0)
    {
        VideoFrameManager::getInstance().setFrame(logicalId, displayFrame);
        // Cache the last output frame for continuous passthrough
        const juce::ScopedLock lock(lastOutputFrameLock);
        displayFrame.copyTo(lastOutputFrame);
    }
    updateGuiFrame(displayFrame);
    return result;
}

void MovementDetectorModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image MovementDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

std::vector<DynamicPinInfo> MovementDetectorModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (4 channels)
    // Bus 1: Video Out (1 channel)
    const int cvOutChannels = 4;
    const int videoOutStartChannel = cvOutChannels;

    return {
        { "X", 0, PinDataType::CV },
        { "Y", 1, PinDataType::CV },
        { "Amount", 2, PinDataType::CV },
        { "Gate", 3, PinDataType::Gate },
        { "Video Out", videoOutStartChannel, PinDataType::Video }
    };
}

void MovementDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Read Source ID from input pin
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
    {
        float sourceIdFloat = inputBuffer.getSample(0, 0);
        currentSourceId.store((juce::uint32)sourceIdFloat);
    }
    
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
    
    // Get latest result from analysis thread via FIFO
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
        {
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
        }
    }
    
    // Write results to output channels (bus 0 - CV Out)
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    if (cvOutBus.getNumChannels() < 4) return;
    
    cvOutBus.setSample(0, 0, lastResultForAudio.avgMotionX);
    cvOutBus.setSample(1, 0, lastResultForAudio.avgMotionY);
    cvOutBus.setSample(2, 0, lastResultForAudio.motionAmount);

    // Handle trigger
    if (lastResultForAudio.motionTrigger)
    {
        triggerSamplesRemaining = (int)(getSampleRate() * 0.01);
        lastResultForAudio.motionTrigger = false; // Clear to avoid repeating
    }

    if (triggerSamplesRemaining > 0)
    {
        cvOutBus.setSample(3, 0, 1.0f);
        triggerSamplesRemaining--;
    }
    else
    {
        cvOutBus.setSample(3, 0, 0.0f);
    }
    
    // Fill rest of buffer
    for (int channel = 0; channel < 4; ++channel)
    {
        cvOutBus.copyFrom(channel, 1, cvOutBus, channel, 0, cvOutBus.getNumSamples() - 1);
    }
    
    // Passthrough Video ID on bus 1
    auto videoOutBus = getBusBuffer(buffer, false, 1);
    if (videoOutBus.getNumChannels() > 0)
    {
        float primaryId = static_cast<float>(myLogicalId); // Use the resolved ID
        for (int s = 0; s < videoOutBus.getNumSamples(); ++s)
            videoOutBus.setSample(0, s, primaryId);
    }
}

#if defined(PRESET_CREATOR_UI)
void MovementDetectorModule::drawParametersInNode(float itemWidth,
                                                  const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                  const std::function<void()>& onModificationEnded)
{
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
            ImGui::SetTooltip("Enable GPU acceleration for movement detection.\nRequires CUDA-capable NVIDIA GPU.\nOnly affects Optical Flow mode.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
    #endif
    
    // Mode selection
    int mode = (int)modeParam->load();
    const char* modes[] = { "Optical Flow", "Background Subtraction" };
    if (ImGui::Combo("Mode", &mode, modes, 2))
    {
        *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = mode;
        onModificationEnded();
    }
    
    // Sensitivity slider
    float sensitivity = sensitivityParam->load();
    if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.01f, 1.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("sensitivity")) = sensitivity;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        onModificationEnded();
    }
    
    // Zoom controls (-/+) Small/Normal/Large
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

    // NEW: Algorithm tuning controls
    if (mode == 0) // Optical Flow
    {
        ImGui::Text("Optical Flow Settings");
        if (maxFeaturesParam)
        {
            int maxF = maxFeaturesParam->get();
            if (ImGui::SliderInt("Max Features", &maxF, 20, 500)) { *maxFeaturesParam = maxF; }
            if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        }
    }
    else
    {
        ImGui::Text("Background Subtraction Settings");
        if (noiseReductionParam)
        {
            bool nr = noiseReductionParam->get();
            if (ImGui::Checkbox("Noise Reduction", &nr)) { *noiseReductionParam = nr; onModificationEnded(); }
        }
    }

    // Show current source ID
    juce::uint32 sourceId = currentSourceId.load();
    if (sourceId > 0)
    {
        const juce::String text = juce::String::formatted("Connected to Source: %d", (int)sourceId);
        ThemeText(text.toRawUTF8(), theme.text.active);
    }
    else
    {
        ThemeText("No source connected", theme.text.error);
    }
    
    ImGui::PopItemWidth();
}

void MovementDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Motion X", 0);
    helpers.drawAudioOutputPin("Motion Y", 1);
    helpers.drawAudioOutputPin("Amount", 2);
    helpers.drawAudioOutputPin("Trigger", 3);
    helpers.drawAudioOutputPin("Video Out", 0); // Bus 1
}
#endif
