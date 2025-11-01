#include "MovementDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
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
    params.push_back(std::make_unique<juce::AudioParameterInt>("pyramidLevels", "Pyramid Levels", 1, 5, 3));
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
    pyramidLevelsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("pyramidLevels"));
    noiseReductionParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("noiseReduction"));
    pBackSub = cv::createBackgroundSubtractorMOG2();
    
    fifoBuffer.resize(16);
    fifo.setTotalSize(16);
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
            MovementResult result = analyzeFrame(frame);
            
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
        
        wait(33); // ~30 FPS analysis rate
    }
}

MovementResult MovementDetectorModule::analyzeFrame(const cv::Mat& inputFrame)
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
        if (prevPoints.size() < 50)
        {
            int maxFeatures = maxFeaturesParam ? maxFeaturesParam->get() : 100;
            cv::goodFeaturesToTrack(gray, prevPoints, maxFeatures, 0.3, 7);
        }

        if (!prevGrayFrame.empty() && !prevPoints.empty())
        {
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0);
            #endif
            
            std::vector<cv::Point2f> nextPoints;
            std::vector<uchar> status;
            int levels = pyramidLevelsParam ? pyramidLevelsParam->get() : 3;
            
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
            for (size_t i = 0; i < prevPoints.size(); ++i)
            {
                if (status[i])
                {
                    cv::line(displayFrame, prevPoints[i], nextPoints[i], cv::Scalar(0, 255, 0), 1);
                    cv::circle(displayFrame, nextPoints[i], 2, cv::Scalar(255, 0, 0), -1);
                    
                    sumX += nextPoints[i].x - prevPoints[i].x;
                    sumY += nextPoints[i].y - prevPoints[i].y;
                    trackedCount++;
                }
            }

            if (trackedCount > 0)
            {
                result.avgMotionX = juce::jlimit(-1.0f, 1.0f, sumX / (trackedCount * 10.0f));
                result.avgMotionY = juce::jlimit(-1.0f, 1.0f, sumY / (trackedCount * 10.0f));
                result.motionAmount = juce::jlimit(0.0f, 1.0f, std::sqrt(result.avgMotionX * result.avgMotionX + result.avgMotionY * result.avgMotionY));
                
                if (result.motionAmount > sensitivityParam->load())
                {
                    result.motionTrigger = true;
                }
            }
            prevPoints = nextPoints;
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
    VideoFrameManager::getInstance().setFrame(getLogicalId(), displayFrame);
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
        float primaryId = static_cast<float>(getLogicalId());
        for (int s = 0; s < videoOutBus.getNumSamples(); ++s)
            videoOutBus.setSample(0, s, primaryId);
    }
}

#if defined(PRESET_CREATOR_UI)
void MovementDetectorModule::drawParametersInNode(float itemWidth,
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
            ImGui::SetTooltip("Enable GPU acceleration for movement detection.\nRequires CUDA-capable NVIDIA GPU.\nOnly affects Optical Flow mode.");
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
    
    ImGui::Separator();
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
    ImGui::Separator();
    if (mode == 0) // Optical Flow
    {
        ImGui::Text("Optical Flow Settings");
        if (maxFeaturesParam)
        {
            int maxF = maxFeaturesParam->get();
            if (ImGui::SliderInt("Max Features", &maxF, 20, 500)) { *maxFeaturesParam = maxF; }
            if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        }
        if (pyramidLevelsParam)
        {
            int lv = pyramidLevelsParam->get();
            if (ImGui::SliderInt("Pyramid Levels", &lv, 1, 5)) { *pyramidLevelsParam = lv; }
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
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected to Source: %d", (int)sourceId);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No source connected");
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
