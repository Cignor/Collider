#include "ContourDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ContourDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold", 0.0f, 255.0f, 128.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("noiseReduction", "Noise Reduction", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", false)); // Default OFF for compatibility
    return { params.begin(), params.end() };
}

ContourDetectorModule::ContourDetectorModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::discreteChannels(3), true)),
      juce::Thread("Contour Detector Thread"),
      apvts(*this, nullptr, "ContourDetectorParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    thresholdParam = apvts.getRawParameterValue("threshold");
    noiseReductionParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("noiseReduction"));
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    fifoBuffer.resize(16);
    backSub = cv::createBackgroundSubtractorMOG2();
}

ContourDetectorModule::~ContourDetectorModule()
{
    stopThread(5000);
}

void ContourDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void ContourDetectorModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void ContourDetectorModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (!frame.empty())
        {
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0);
            #endif
            
            cv::Mat fgMask;
            backSub->apply(frame, fgMask);
            
            float thresh = thresholdParam ? thresholdParam->load() : 128.0f;
            // NOTE: threshold doesn't have CUDA implementation in this OpenCV build
            // Using CPU version even when GPU is enabled (fast enough, avoids upload/download overhead)
            cv::threshold(fgMask, fgMask, thresh, 255, cv::THRESH_BINARY);
            
            if (noiseReductionParam && noiseReductionParam->get())
            {
                cv::erode(fgMask, fgMask, cv::Mat(), cv::Point(-1,-1), 2);
                cv::dilate(fgMask, fgMask, cv::Mat(), cv::Point(-1,-1), 2);
            }

            // findContours is CPU-only
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(fgMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            ContourResult result;
            if (!contours.empty())
            {
                double maxArea = 0.0;
                int maxIdx = -1;
                for (int i = 0; i < (int)contours.size(); ++i)
                {
                    double a = cv::contourArea(contours[i]);
                    if (a > maxArea) { maxArea = a; maxIdx = i; }
                }
                if (maxIdx >= 0)
                {
                    const auto& c = contours[(size_t)maxIdx];
                    result.area = juce::jlimit(0.0f, 1.0f, (float)(maxArea / (frame.cols * frame.rows)));
                    std::vector<cv::Point> approx;
                    cv::approxPolyDP(c, approx, 0.02 * cv::arcLength(c, true), true);
                    result.complexity = juce::jmap((float)approx.size(), 3.0f, 50.0f, 0.0f, 1.0f);
                    cv::Rect bbox = cv::boundingRect(c);
                    result.aspectRatio = bbox.height > 0 ? (float)bbox.width / (float)bbox.height : 0.0f;
                    // Draw
                    cv::drawContours(frame, contours, maxIdx, cv::Scalar(0,255,0), 2);
                    cv::rectangle(frame, bbox, cv::Scalar(255,0,0), 2);
                }
            }

            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                    fifoBuffer[writeScope.startIndex1] = result;
            }

            updateGuiFrame(frame);
        }
        
        wait(40);
    }
}

void ContourDetectorModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra;
    cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgra.cols || latestFrameForGui.getHeight() != bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgra.data, bgra.total() * bgra.elemSize());
}

juce::Image ContourDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void ContourDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
        currentSourceId.store((juce::uint32)inputBuffer.getSample(0, 0));

    buffer.clear();
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
    }

    const float values[3] { lastResultForAudio.area, lastResultForAudio.complexity, lastResultForAudio.aspectRatio };
    for (int ch = 0; ch < juce::jmin(3, buffer.getNumChannels()); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            buffer.setSample(ch, s, values[ch]);
}

#if defined(PRESET_CREATOR_UI)
ImVec2 ContourDetectorModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void ContourDetectorModule::drawParametersInNode(float itemWidth,
                                                 const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                 const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
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
            ImGui::SetTooltip("Enable GPU acceleration for contour detection.\nRequires CUDA-capable NVIDIA GPU.");
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

    float th = thresholdParam ? thresholdParam->load() : 128.0f;
    if (ImGui::SliderFloat("Threshold", &th, 0.0f, 255.0f, "%.0f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("threshold")))
            *p = th;
        onModificationEnded();
    }
    bool nr = noiseReductionParam ? noiseReductionParam->get() : true;
    if (ImGui::Checkbox("Noise Reduction", &nr))
    {
        if (noiseReductionParam) *noiseReductionParam = nr;
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

    ImGui::PopItemWidth();
}

void ContourDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Area", 0);
    helpers.drawAudioOutputPin("Complexity", 1);
    helpers.drawAudioOutputPin("Aspect Ratio", 2);
}
#endif


