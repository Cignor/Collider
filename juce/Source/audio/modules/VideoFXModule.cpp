#include "VideoFXModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#if defined(WITH_CUDA_SUPPORT)
    #include <opencv2/cudaimgproc.hpp>
    #include <opencv2/cudaarithm.hpp>
    #include <opencv2/cudafilters.hpp>
#endif

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout VideoFXModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", true)); // Default ON for better performance
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // Color
    params.push_back(std::make_unique<juce::AudioParameterFloat>("brightness", "Brightness", -100.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("contrast", "Contrast", 0.0f, 3.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("saturation", "Saturation", 0.0f, 3.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("hueShift", "Hue Shift", -180.0f, 180.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainRed", "Red Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainGreen", "Green Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainBlue", "Blue Gain", 0.0f, 2.0f, 1.0f));
    
    // Filters & Effects
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sharpen", "Sharpen", 0.0f, 2.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("blur", "Blur", 0.0f, 20.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("grayscale", "Grayscale", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("invert", "Invert Colors", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("flipH", "Flip Horizontal", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("flipV", "Flip Vertical", false));

    // Threshold Effect
    params.push_back(std::make_unique<juce::AudioParameterBool>("thresholdEnable", "Enable Threshold", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("thresholdLevel", "Threshold Level", 0.0f, 255.0f, 127.0f));

    return { params.begin(), params.end() };
}

VideoFXModule::VideoFXModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      juce::Thread("VideoFX Thread"),
      apvts(*this, nullptr, "VideoFXParams", createParameterLayout())
{
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    brightnessParam = apvts.getRawParameterValue("brightness");
    contrastParam = apvts.getRawParameterValue("contrast");
    saturationParam = apvts.getRawParameterValue("saturation");
    hueShiftParam = apvts.getRawParameterValue("hueShift");
    gainRedParam = apvts.getRawParameterValue("gainRed");
    gainGreenParam = apvts.getRawParameterValue("gainGreen");
    gainBlueParam = apvts.getRawParameterValue("gainBlue");
    sharpenParam = apvts.getRawParameterValue("sharpen");
    blurParam = apvts.getRawParameterValue("blur");
    grayscaleParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("grayscale"));
    invertParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("invert"));
    flipHorizontalParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("flipH"));
    flipVerticalParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("flipV"));
    
    // Initialize threshold parameters
    thresholdEnableParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("thresholdEnable"));
    thresholdLevelParam = apvts.getRawParameterValue("thresholdLevel");
}

VideoFXModule::~VideoFXModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFXModule::prepareToPlay(double, int) { startThread(); }
void VideoFXModule::releaseResources() { signalThreadShouldExit(); stopThread(5000); }

void VideoFXModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }

        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(33); continue; }

        // Get all parameter values once at the start of the frame
        float brightness = brightnessParam ? brightnessParam->load() : 0.0f;
        float contrast = contrastParam ? contrastParam->load() : 1.0f;
        float saturation = saturationParam ? saturationParam->load() : 1.0f;
        float hueShift = hueShiftParam ? hueShiftParam->load() : 0.0f;
        float gainR = gainRedParam ? gainRedParam->load() : 1.0f;
        float gainG = gainGreenParam ? gainGreenParam->load() : 1.0f;
        float gainB = gainBlueParam ? gainBlueParam->load() : 1.0f;
        float sharpen = sharpenParam ? sharpenParam->load() : 0.0f;
        float blur = blurParam ? blurParam->load() : 0.0f;
        bool grayscale = grayscaleParam ? grayscaleParam->get() : false;
        bool invert = invertParam ? invertParam->get() : false;
        bool flipH = flipHorizontalParam ? flipHorizontalParam->get() : false;
        bool flipV = flipVerticalParam ? flipVerticalParam->get() : false;
        bool thresholdEnable = thresholdEnableParam ? thresholdEnableParam->get() : false;
        float thresholdLevel = thresholdLevelParam ? thresholdLevelParam->load() : 127.0f;
        bool useGpu = useGpuParam ? useGpuParam->get() : false;

        cv::Mat processedFrame; // Create a destination frame

#if defined(WITH_CUDA_SUPPORT)
        if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
        {
            try
            {
                // GPU PATH
                cv::cuda::GpuMat gpuFrame, gpuTemp;
                gpuFrame.upload(frame);

                // --- GPU PROCESSING CHAIN ---
                // The output of one operation becomes the input for the next.

                // Brightness & Contrast
                if (brightness != 0.0f || contrast != 1.0f) {
                    gpuFrame.convertTo(gpuTemp, CV_8UC3, contrast, brightness);
                    gpuTemp.copyTo(gpuFrame);
                }

                // Grayscale
                if (grayscale) {
                    cv::cuda::cvtColor(gpuFrame, gpuTemp, cv::COLOR_BGR2GRAY);
                    cv::cuda::cvtColor(gpuTemp, gpuFrame, cv::COLOR_GRAY2BGR);
                }

                // Threshold (NEW)
                if (thresholdEnable) {
                    cv::cuda::GpuMat grayGpu;
                    cv::cuda::cvtColor(gpuFrame, grayGpu, cv::COLOR_BGR2GRAY);
                    cv::cuda::threshold(grayGpu, gpuTemp, thresholdLevel, 255, cv::THRESH_BINARY);
                    cv::cuda::cvtColor(gpuTemp, gpuFrame, cv::COLOR_GRAY2BGR);
                }

                // Blur (FIXED: Ensure kernel size is always odd and positive)
                if (blur > 0.0f) {
                    int ksize = juce::jmax(1, static_cast<int>(blur) * 2 + 1);
                    auto gaussian = cv::cuda::createGaussianFilter(gpuFrame.type(), -1, cv::Size(ksize, ksize), 0);
                    gaussian->apply(gpuFrame, gpuTemp);
                    gpuTemp.copyTo(gpuFrame);
                }

                // Sharpen (FIXED: Use cv::cuda::addWeighted)
                if (sharpen > 0.0f) {
                    cv::cuda::GpuMat blurredGpu;
                    auto gaussian = cv::cuda::createGaussianFilter(gpuFrame.type(), -1, cv::Size(0, 0), 3);
                    gaussian->apply(gpuFrame, blurredGpu);
                    cv::cuda::addWeighted(gpuFrame, 1.0 + sharpen, blurredGpu, -sharpen, 0, gpuTemp);
                    gpuTemp.copyTo(gpuFrame);
                }

                // Invert
                if (invert) {
                    cv::cuda::bitwise_not(gpuFrame, gpuTemp);
                    gpuTemp.copyTo(gpuFrame);
                }

                // Flip
                if (flipH || flipV) {
                    int flipCode = flipH && flipV ? -1 : (flipH ? 1 : 0);
                    cv::cuda::flip(gpuFrame, gpuTemp, flipCode);
                    gpuTemp.copyTo(gpuFrame);
                }

                gpuFrame.download(processedFrame);

                // --- Complex color operations are still easier on CPU ---
            }
            catch (const cv::Exception& e)
            {
                juce::Logger::writeToLog("[VideoFX] GPU processing error, falling back to CPU: " + juce::String(e.what()));
                processedFrame = frame.clone(); // Start over with the original frame for CPU path
                goto cpu_path_label; // Jump to the CPU processing block
            }
        }
        else
#endif
        {
        cpu_path_label: // Label for the GPU fallback goto
            // --- CPU PATH ---
            processedFrame = frame.clone(); // Start with a fresh copy

            // Brightness & Contrast
            if (brightness != 0.0f || contrast != 1.0f) {
                processedFrame.convertTo(processedFrame, -1, contrast, brightness);
            }

            // Grayscale
            if (grayscale) {
                cv::cvtColor(processedFrame, processedFrame, cv::COLOR_BGR2GRAY);
                cv::cvtColor(processedFrame, processedFrame, cv::COLOR_GRAY2BGR);
            }

            // Threshold (NEW)
            if (thresholdEnable) {
                cv::Mat gray;
                cv::cvtColor(processedFrame, gray, cv::COLOR_BGR2GRAY);
                cv::threshold(gray, gray, thresholdLevel, 255, cv::THRESH_BINARY);
                cv::cvtColor(gray, processedFrame, cv::COLOR_GRAY2BGR);
            }

            // Blur (FIXED: Ensure kernel size is always odd and positive)
            if (blur > 0.0f) {
                int ksize = juce::jmax(1, static_cast<int>(blur) * 2 + 1);
                cv::GaussianBlur(processedFrame, processedFrame, cv::Size(ksize, ksize), 0);
            }
            
            // Sharpen (FIXED: The darkening is from clipping. This is the correct formula, but values can go out of 0-255 range.
            // Using a temporary 16-bit signed matrix prevents clipping during the operation.)
            if (sharpen > 0.0f) {
                cv::Mat temp;
                processedFrame.convertTo(temp, CV_16SC3); // Convert to signed 16-bit
                cv::Mat blurred;
                cv::GaussianBlur(temp, blurred, cv::Size(0, 0), 3);
                cv::addWeighted(temp, 1.0 + sharpen, blurred, -sharpen, 0, temp);
                temp.convertTo(processedFrame, CV_8UC3); // Convert back to 8-bit, automatically clamping values
            }
            
            // Invert
            if (invert) {
                cv::bitwise_not(processedFrame, processedFrame);
            }

            // Flip
            if (flipH && flipV) { cv::flip(processedFrame, processedFrame, -1); }
            else if (flipH) { cv::flip(processedFrame, processedFrame, 1); }
            else if (flipV) { cv::flip(processedFrame, processedFrame, 0); }
        }
        
        // --- Complex color operations (Hue/Sat/Gain) remain on CPU for both paths ---
        // (This is because they are more complex and less performance-critical than spatial filters)
        if (saturation != 1.0f || hueShift != 0.0f || gainR != 1.0f || gainG != 1.0f || gainB != 1.0f)
        {
            cv::Mat hsv;
            cv::cvtColor(processedFrame, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> hsvChannels(3);
            cv::split(hsv, hsvChannels);

            if (hueShift != 0.0f) {
                hsvChannels[0].convertTo(hsvChannels[0], CV_32F);
                hsvChannels[0] += (hueShift / 2.0f); // OpenCV Hue is 0-179
                hsvChannels[0].convertTo(hsvChannels[0], CV_8U, 1, 180); // Wrap around
                cv::add(hsvChannels[0], 180, hsvChannels[0], hsvChannels[0] < 0);
            }

            if (saturation != 1.0f) {
                hsvChannels[1].convertTo(hsvChannels[1], CV_32F);
                hsvChannels[1] *= saturation;
                cv::threshold(hsvChannels[1], hsvChannels[1], 255, 255, cv::THRESH_TRUNC);
                hsvChannels[1].convertTo(hsvChannels[1], CV_8U);
            }
            
            cv::merge(hsvChannels, hsv);
            cv::cvtColor(hsv, processedFrame, cv::COLOR_HSV2BGR);

            // Apply RGB Gains
            std::vector<cv::Mat> bgrChannels(3);
            cv::split(processedFrame, bgrChannels);
            if(gainB != 1.0f) bgrChannels[0] *= gainB;
            if(gainG != 1.0f) bgrChannels[1] *= gainG;
            if(gainR != 1.0f) bgrChannels[2] *= gainR;
            cv::merge(bgrChannels, processedFrame);
        }
        
        // Publish and update UI
        VideoFrameManager::getInstance().setFrame(getLogicalId(), processedFrame);
        updateGuiFrame(processedFrame);

        wait(33); // ~30 FPS
    }
}

void VideoFXModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Read the Source ID from our input pin
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumSamples() > 0)
    {
        currentSourceId.store((juce::uint32)inputBuffer.getSample(0, 0));
    }
    
    buffer.clear();
    
    // Output our own Logical ID on the output pin, so we can be chained
    if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
    {
        float sourceId = (float)getLogicalId();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample(0, sample, sourceId);
        }
    }
}

void VideoFXModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image VideoFXModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

juce::ValueTree VideoFXModule::getExtraStateTree() const
{
    // No special state to save for VideoFX module
    return juce::ValueTree("VideoFXState");
}

void VideoFXModule::setExtraStateTree(const juce::ValueTree& state)
{
    // No special state to restore for VideoFX module
    juce::ignoreUnused(state);
}

std::vector<DynamicPinInfo> VideoFXModule::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    pins.push_back({ "Source In", 0, PinDataType::Video });
    return pins;
}

std::vector<DynamicPinInfo> VideoFXModule::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    pins.push_back({ "Output", 0, PinDataType::Video });
    return pins;
}

#if defined(PRESET_CREATOR_UI)
ImVec2 VideoFXModule::getCustomNodeSize() const
{
    // Return different width based on zoom level (0=240,1=480,2=960)
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void VideoFXModule::drawParametersInNode(float itemWidth,
                                         const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                         const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    
    ImGui::PushItemWidth(itemWidth);
    
    // --- FEATURE: RESET BUTTON ---
    if (ImGui::Button("Reset All Effects", ImVec2(itemWidth, 0)))
    {
        // Reset all parameters to their default values
        const char* paramIds[] = {
            "useGpu", "zoomLevel", "brightness", "contrast", "saturation", "hueShift",
            "gainRed", "gainGreen", "gainBlue", "sharpen", "blur", "grayscale",
            "invert", "flipH", "flipV", "thresholdEnable", "thresholdLevel"
        };
        
        for (const char* paramId : paramIds)
        {
            if (auto* param = apvts.getParameter(paramId))
            {
                if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    rangedParam->setValueNotifyingHost(rangedParam->getDefaultValue());
                }
            }
        }
        onModificationEnded(); // Create an undo state for the reset
    }
    
    ImGui::Separator();
    
    // GPU checkbox
    bool useGpu = useGpuParam ? useGpuParam->get() : true;
    if (ImGui::Checkbox("Use GPU", &useGpu))
    {
        if (useGpuParam) *useGpuParam = useGpu;
        onModificationEnded();
    }
    
    ImGui::Separator();
    
    // Zoom buttons
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
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID In: %d", (int)currentSourceId.load());
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output ID: %d", (int)getLogicalId());
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Color Adjustments");
    
    // Color sliders
    float brightness = brightnessParam ? brightnessParam->load() : 0.0f;
    if (ImGui::SliderFloat("Brightness", &brightness, -100.0f, 100.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("brightness"))) *p = brightness;
        onModificationEnded();
    }
    
    float contrast = contrastParam ? contrastParam->load() : 1.0f;
    if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 3.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("contrast"))) *p = contrast;
        onModificationEnded();
    }
    
    float saturation = saturationParam ? saturationParam->load() : 1.0f;
    if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 3.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("saturation"))) *p = saturation;
        onModificationEnded();
    }
    
    float hueShift = hueShiftParam ? hueShiftParam->load() : 0.0f;
    if (ImGui::SliderFloat("Hue Shift", &hueShift, -180.0f, 180.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("hueShift"))) *p = hueShift;
        onModificationEnded();
    }
    
    float gainR = gainRedParam ? gainRedParam->load() : 1.0f;
    if (ImGui::SliderFloat("Red Gain", &gainR, 0.0f, 2.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gainRed"))) *p = gainR;
        onModificationEnded();
    }
    
    float gainG = gainGreenParam ? gainGreenParam->load() : 1.0f;
    if (ImGui::SliderFloat("Green Gain", &gainG, 0.0f, 2.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gainGreen"))) *p = gainG;
        onModificationEnded();
    }
    
    float gainB = gainBlueParam ? gainBlueParam->load() : 1.0f;
    if (ImGui::SliderFloat("Blue Gain", &gainB, 0.0f, 2.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gainBlue"))) *p = gainB;
        onModificationEnded();
    }
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Filters & Effects");
    
    // Filter sliders
    float sharpen = sharpenParam ? sharpenParam->load() : 0.0f;
    if (ImGui::SliderFloat("Sharpen", &sharpen, 0.0f, 2.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("sharpen"))) *p = sharpen;
        onModificationEnded();
    }
    
    float blur = blurParam ? blurParam->load() : 0.0f;
    if (ImGui::SliderFloat("Blur", &blur, 0.0f, 20.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("blur"))) *p = blur;
        onModificationEnded();
    }
    
    // Effect checkboxes
    bool grayscale = grayscaleParam ? grayscaleParam->get() : false;
    if (ImGui::Checkbox("Grayscale", &grayscale))
    {
        if (grayscaleParam) *grayscaleParam = grayscale;
        onModificationEnded();
    }
    
    bool invert = invertParam ? invertParam->get() : false;
    if (ImGui::Checkbox("Invert", &invert))
    {
        if (invertParam) *invertParam = invert;
        onModificationEnded();
    }
    
    bool flipH = flipHorizontalParam ? flipHorizontalParam->get() : false;
    if (ImGui::Checkbox("Flip H", &flipH))
    {
        if (flipHorizontalParam) *flipHorizontalParam = flipH;
        onModificationEnded();
    }
    
    bool flipV = flipVerticalParam ? flipVerticalParam->get() : false;
    if (ImGui::Checkbox("Flip V", &flipV))
    {
        if (flipVerticalParam) *flipVerticalParam = flipV;
        onModificationEnded();
    }
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "More Filters");

    // --- FEATURE: THRESHOLD EFFECT UI ---
    bool threshEnable = thresholdEnableParam ? thresholdEnableParam->get() : false;
    if (ImGui::Checkbox("Threshold", &threshEnable))
    {
        if (thresholdEnableParam) *thresholdEnableParam = threshEnable;
        onModificationEnded();
    }

    if (threshEnable)
    {
        ImGui::SameLine();
        float threshLevel = thresholdLevelParam ? thresholdLevelParam->load() : 127.0f;
        if (ImGui::SliderFloat("##level", &threshLevel, 0.0f, 255.0f, "%.0f"))
        {
             if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("thresholdLevel"))) *p = threshLevel;
             onModificationEnded();
        }
    }
    
    ImGui::PopItemWidth();
}

void VideoFXModule::drawIoPins(const NodePinHelpers& helpers)
{
    // Pins are handled via getDynamicInputPins/getDynamicOutputPins for proper Video type coloring
    // This method is called but dynamic pins take precedence
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Output", 0);
}
#endif

