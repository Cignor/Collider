#include "VideoFXModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
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

    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // Color
    params.push_back(std::make_unique<juce::AudioParameterFloat>("brightness", "Brightness", -100.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("contrast", "Contrast", 0.0f, 3.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("saturation", "Saturation", 0.0f, 3.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("hueShift", "Hue Shift", -180.0f, 180.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainRed", "Red Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainGreen", "Green Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gainBlue", "Blue Gain", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("sepia", "Sepia", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("temperature", "Temperature", -1.0f, 1.0f, 0.0f));
    
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
    
    // New Effects
    params.push_back(std::make_unique<juce::AudioParameterInt>("posterizeLevels", "Posterize Levels", 2, 16, 16));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("vignetteAmount", "Vignette Amount", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("vignetteSize", "Vignette Size", 0.1f, 2.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("pixelateSize", "Pixelate Block Size", 1, 128, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>("cannyEnable", "Edge Detect", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cannyThresh1", "Canny Thresh 1", 0.0f, 255.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cannyThresh2", "Canny Thresh 2", 0.0f, 255.0f, 150.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("kaleidoscope", "Kaleidoscope", juce::StringArray{ "None", "4-Way", "8-Way" }, 0));

    return { params.begin(), params.end() };
}

VideoFXModule::VideoFXModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      juce::Thread("VideoFX Thread"),
      apvts(*this, nullptr, "VideoFXParams", createParameterLayout())
{
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
    
    // Initialize new effect parameters
    sepiaParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("sepia"));
    temperatureParam = apvts.getRawParameterValue("temperature");
    posterizeLevelsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("posterizeLevels"));
    vignetteAmountParam = apvts.getRawParameterValue("vignetteAmount");
    vignetteSizeParam = apvts.getRawParameterValue("vignetteSize");
    pixelateBlockSizeParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("pixelateSize"));
    cannyEnableParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("cannyEnable"));
    cannyThresh1Param = apvts.getRawParameterValue("cannyThresh1");
    cannyThresh2Param = apvts.getRawParameterValue("cannyThresh2");
    kaleidoscopeModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("kaleidoscope"));
}

VideoFXModule::~VideoFXModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFXModule::prepareToPlay(double, int) { startThread(); }
void VideoFXModule::releaseResources() { signalThreadShouldExit(); stopThread(5000); }

// ==============================================================================
// === PRIVATE EFFECT HELPER FUNCTIONS ==========================================
// ==============================================================================

void VideoFXModule::applyBrightnessContrast(cv::Mat& ioFrame, float brightness, float contrast)
{
    if (brightness != 0.0f || contrast != 1.0f) {
        ioFrame.convertTo(ioFrame, -1, contrast, brightness);
    }
}

void VideoFXModule::applyTemperature(cv::Mat& ioFrame, float temperature)
{
    if (temperature == 0.0f) return;

    std::vector<cv::Mat> bgr;
    cv::split(ioFrame, bgr);
    float factor = temperature;
    
    // Original logic was wrong (applied same effect for warm/cool)
    // This is corrected:
    if (factor < 0.0f) { // Cool (add blue, remove red)
        bgr[0] = bgr[0] * (1.0f - factor); // Add Blue
        bgr[2] = bgr[2] * (1.0f + factor); // Remove Red
    } else { // Warm (add red, remove blue)
        bgr[0] = bgr[0] * (1.0f - factor); // Remove Blue
        bgr[2] = bgr[2] * (1.0f + factor); // Add Red
    }
    
    cv::merge(bgr, ioFrame);
}

void VideoFXModule::applySepia(cv::Mat& ioFrame, bool sepia)
{
    if (!sepia) return;
    cv::Mat sepiaKernel = (cv::Mat_<float>(3,3) << 0.272, 0.534, 0.131, 0.349, 0.686, 0.168, 0.393, 0.769, 0.189);
    cv::transform(ioFrame, ioFrame, sepiaKernel);
}

void VideoFXModule::applySaturationHue(cv::Mat& ioFrame, float saturation, float hueShift)
{
    if (saturation == 1.0f && hueShift == 0.0f) return;

    cv::Mat hsv;
    cv::cvtColor(ioFrame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> hsvChannels;
    cv::split(hsv, hsvChannels);
    
    if (hueShift != 0.0f) {
         hsvChannels[0].convertTo(hsvChannels[0], CV_32F);
         hsvChannels[0] += (hueShift / 2.0f);
         cv::Mat mask = hsvChannels[0] < 0;
         cv::add(hsvChannels[0], 180, hsvChannels[0], mask);
         mask = hsvChannels[0] >= 180;
         cv::subtract(hsvChannels[0], 180, hsvChannels[0], mask);
         hsvChannels[0].convertTo(hsvChannels[0], CV_8U);
    }
    if (saturation != 1.0f) {
        hsvChannels[1].convertTo(hsvChannels[1], CV_32F);
        hsvChannels[1] *= saturation;
        hsvChannels[1].convertTo(hsvChannels[1], CV_8U);
    }
    cv::merge(hsvChannels, hsv);
    cv::cvtColor(hsv, ioFrame, cv::COLOR_HSV2BGR);
}

void VideoFXModule::applyRgbGain(cv::Mat& ioFrame, float gainR, float gainG, float gainB)
{
    if (gainR == 1.0f && gainG == 1.0f && gainB == 1.0f) return;
    
    std::vector<cv::Mat> bgr;
    cv::split(ioFrame, bgr);
    if (gainB != 1.0f) bgr[0] *= gainB;
    if (gainG != 1.0f) bgr[1] *= gainG;
    if (gainR != 1.0f) bgr[2] *= gainR;
    cv::merge(bgr, ioFrame);
}

void VideoFXModule::applyPosterize(cv::Mat& ioFrame, int levels)
{
    // levels is 2-16. 16 is "off".
    if (levels >= 16) return;
    if (levels < 2) levels = 2;

    // --- NEW, ROBUST LOGIC ---
    // This math correctly maps the 0-255 range to 'levels' number of steps.
    // e.g., if levels = 2, it maps to 0 and 255.
    // e.g., if levels = 3, it maps to 0, 127, 255.

    // 1. Calculate the divider
    const int divider = 255 / (levels - 1); // e.g., 255 / (2-1) = 255

    // 2. Convert to 16-bit to prevent overflow during math
    ioFrame.convertTo(ioFrame, CV_16U);

    // 3. Scale down, round, and scale back up
    // This is an integer-math version of:
    // ioFrame = round(ioFrame / divider) * divider;
    ioFrame = (ioFrame + (divider / 2)) / divider;
    ioFrame = ioFrame * divider;

    // 4. Convert back to 8-bit
    ioFrame.convertTo(ioFrame, CV_8U);
}

void VideoFXModule::applyGrayscale(cv::Mat& ioFrame, bool grayscale)
{
    if (!grayscale) return;
    cv::cvtColor(ioFrame, ioFrame, cv::COLOR_BGR2GRAY);
    cv::cvtColor(ioFrame, ioFrame, cv::COLOR_GRAY2BGR);
}

void VideoFXModule::applyCanny(cv::Mat& ioFrame, float thresh1, float thresh2)
{
    // Canny needs a grayscale image
    cv::Mat gray;
    cv::cvtColor(ioFrame, gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, gray, thresh1, thresh2);
    cv::cvtColor(gray, ioFrame, cv::COLOR_GRAY2BGR);
}

void VideoFXModule::applyThreshold(cv::Mat& ioFrame, float level)
{
    cv::Mat gray;
    cv::cvtColor(ioFrame, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, gray, level, 255, cv::THRESH_BINARY);
    cv::cvtColor(gray, ioFrame, cv::COLOR_GRAY2BGR);
}

void VideoFXModule::applyInvert(cv::Mat& ioFrame, bool invert)
{
    if (!invert) return;
    cv::bitwise_not(ioFrame, ioFrame);
}

void VideoFXModule::applyFlip(cv::Mat& ioFrame, bool flipH, bool flipV)
{
    if (!flipH && !flipV) return;
    int flipCode = flipH && flipV ? -1 : (flipH ? 1 : 0);
    cv::flip(ioFrame, ioFrame, flipCode);
}

void VideoFXModule::applyVignette(cv::Mat& ioFrame, float amount, float size)
{
    if (amount <= 0.0f) return;

     cv::Mat vignette = cv::Mat::zeros(ioFrame.size(), CV_32F);
     int centerX = ioFrame.cols / 2;
     int centerY = ioFrame.rows / 2;
     float maxDist = std::sqrt((float)centerX * centerX + (float)centerY * centerY) * size;
     if (maxDist <= 0.0f) maxDist = 1.0f; // Avoid divide by zero

     for (int y = 0; y < ioFrame.rows; y++) {
         for (int x = 0; x < ioFrame.cols; x++) {
             float dist = std::sqrt(std::pow(x - centerX, 2) + std::pow(y - centerY, 2));
             float v = 1.0f - (dist / maxDist) * amount;
             vignette.at<float>(y, x) = juce::jlimit(0.0f, 1.0f, v);
         }
     }
     
     std::vector<cv::Mat> bgr;
     cv::split(ioFrame, bgr);
     for (size_t i = 0; i < bgr.size(); i++) {
         bgr[i].convertTo(bgr[i], CV_32F);
         cv::multiply(bgr[i], vignette, bgr[i]);
         bgr[i].convertTo(bgr[i], CV_8U);
     }
     cv::merge(bgr, ioFrame);
}

void VideoFXModule::applyPixelate(cv::Mat& ioFrame, int pixelSize)
{
    if (pixelSize <= 1) return;

    int w = ioFrame.cols;
    int h = ioFrame.rows;
    cv::Mat temp;
    cv::resize(ioFrame, temp, cv::Size(w / pixelSize, h / pixelSize), 0, 0, cv::INTER_NEAREST);
    cv::resize(temp, ioFrame, cv::Size(w, h), 0, 0, cv::INTER_NEAREST);
}

void VideoFXModule::applyBlur(cv::Mat& ioFrame, float blur)
{
    // *** THIS IS THE BUG FIX ***
    // We use a small threshold and ensure the kernel size is an odd number > 1.
    
    if (blur <= 0.1f) return; // No blur

    // Round to nearest integer, not truncate
    int ksize = static_cast<int>(std::round(blur));
    
    // Ensure kernel size is ODD
    if (ksize % 2 == 0) ksize++; 
    
    // Ensure kernel size is at least 3
    if (ksize < 3) ksize = 3; 

    cv::GaussianBlur(ioFrame, ioFrame, cv::Size(ksize, ksize), 0);
}

void VideoFXModule::applySharpen(cv::Mat& ioFrame, float sharpen)
{
    if (sharpen <= 0.0f) return;
    
    cv::Mat temp;
    ioFrame.convertTo(temp, CV_16SC3);
    cv::Mat blurred;
    cv::GaussianBlur(temp, blurred, cv::Size(0, 0), 3);
    cv::addWeighted(temp, 1.0 + sharpen, blurred, -sharpen, 0, temp);
    temp.convertTo(ioFrame, CV_8UC3);
}

void VideoFXModule::applyKaleidoscope(cv::Mat& ioFrame, int mode)
{
    if (mode == 0) return; // "None"

    int w = ioFrame.cols;
    int h = ioFrame.rows;
    int halfW = w / 2;
    int halfH = h / 2;
    
    // Ensure we have at least 2x2 pixels
    if (halfW < 1 || halfH < 1) return; 

    cv::Mat quadrant = ioFrame(cv::Rect(0, 0, halfW, halfH)).clone();

    if (mode == 1) { // 4-Way
        cv::Mat flippedH, flippedV, flippedBoth;
        cv::flip(quadrant, flippedH, 1);
        cv::flip(quadrant, flippedV, 0);
        cv::flip(quadrant, flippedBoth, -1);
        quadrant.copyTo(ioFrame(cv::Rect(0, 0, halfW, halfH)));
        flippedH.copyTo(ioFrame(cv::Rect(halfW, 0, halfW, halfH)));
        flippedV.copyTo(ioFrame(cv::Rect(0, halfH, halfW, halfH)));
        flippedBoth.copyTo(ioFrame(cv::Rect(halfW, halfH, halfW, halfH)));
    } else if (mode == 2) { // 8-Way
        cv::Mat symmQuadrant = quadrant.clone();
        cv::Mat mask = cv::Mat::zeros(quadrant.size(), CV_8U);
        std::vector<cv::Point> triangle_pts = {cv::Point(0,0), cv::Point(halfW, 0), cv::Point(0, halfH)};
        cv::fillConvexPoly(mask, triangle_pts, cv::Scalar(255));
        
        cv::Mat tri;
        quadrant.copyTo(tri, mask);
        cv::Mat tri_flipped_h;
        cv::flip(tri, tri_flipped_h, 1);
        tri_flipped_h.copyTo(symmQuadrant, ~mask);

        cv::Mat flippedH, flippedV, flippedBoth;
        cv::flip(symmQuadrant, flippedH, 1);
        cv::flip(symmQuadrant, flippedV, 0);
        cv::flip(symmQuadrant, flippedBoth, -1);
        symmQuadrant.copyTo(ioFrame(cv::Rect(0, 0, halfW, halfH)));
        flippedH.copyTo(ioFrame(cv::Rect(halfW, 0, halfW, halfH)));
        flippedV.copyTo(ioFrame(cv::Rect(0, halfH, halfW, halfH)));
        flippedBoth.copyTo(ioFrame(cv::Rect(halfW, halfH, halfW, halfH)));
    }
}

void VideoFXModule::run()
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
    
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }

        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(33); continue; }

        // --- 1. Get all parameter values once ---
        float brightness = brightnessParam ? brightnessParam->load() : 0.0f;
        float contrast = contrastParam ? contrastParam->load() : 1.0f;
        float saturation = saturationParam ? saturationParam->load() : 1.0f;
        float hueShift = hueShiftParam ? hueShiftParam->load() : 0.0f;
        float gainR = gainRedParam ? gainRedParam->load() : 1.0f;
        float gainG = gainGreenParam ? gainGreenParam->load() : 1.0f;
        float gainB = gainBlueParam ? gainBlueParam->load() : 1.0f;
        bool sepia = sepiaParam ? sepiaParam->get() : false;
        float temperature = temperatureParam ? temperatureParam->load() : 0.0f;
        float sharpen = sharpenParam ? sharpenParam->load() : 0.0f;
        float blur = blurParam ? blurParam->load() : 0.0f;
        bool grayscale = grayscaleParam ? grayscaleParam->get() : false;
        bool invert = invertParam ? invertParam->get() : false;
        bool flipH = flipHorizontalParam ? flipHorizontalParam->get() : false;
        bool flipV = flipVerticalParam ? flipVerticalParam->get() : false;
        bool thresholdEnable = thresholdEnableParam ? thresholdEnableParam->get() : false;
        float thresholdLevel = thresholdLevelParam ? thresholdLevelParam->load() : 127.0f;
        int posterizeLevels = posterizeLevelsParam ? posterizeLevelsParam->get() : 16;
        float vignetteAmount = vignetteAmountParam ? vignetteAmountParam->load() : 0.0f;
        float vignetteSize = vignetteSizeParam ? vignetteSizeParam->load() : 0.5f;
        int pixelateSize = pixelateBlockSizeParam ? pixelateBlockSizeParam->get() : 1;
        bool cannyEnable = cannyEnableParam ? cannyEnableParam->get() : false;
        float cannyThresh1 = cannyThresh1Param ? cannyThresh1Param->load() : 50.0f;
        float cannyThresh2 = cannyThresh2Param ? cannyThresh2Param->load() : 150.0f;
        int kaleidoscopeMode = kaleidoscopeModeParam ? kaleidoscopeModeParam->getIndex() : 0;
        
        // --- 2. Create a working copy of the frame ---
        cv::Mat processedFrame = frame.clone();

        // --- 3. Apply effects in sequence ---
        // This is now clean and manageable!
        // You can re-order these lines to change the effect chain.
        
        // Color Adjustments
        applyBrightnessContrast(processedFrame, brightness, contrast);
        applyTemperature(processedFrame, temperature);
        applySepia(processedFrame, sepia);
        applySaturationHue(processedFrame, saturation, hueShift);
        applyRgbGain(processedFrame, gainR, gainG, gainB);
        applyPosterize(processedFrame, posterizeLevels);
        
        // Monochrome & Edge Effects
        applyGrayscale(processedFrame, grayscale);
        if (cannyEnable) {
            applyCanny(processedFrame, cannyThresh1, cannyThresh2);
        } else if (thresholdEnable) {
            applyThreshold(processedFrame, thresholdLevel);
        }
        applyInvert(processedFrame, invert);

        // Geometric & Spatial Filters
        applyFlip(processedFrame, flipH, flipV);
        applyVignette(processedFrame, vignetteAmount, vignetteSize);
        applyPixelate(processedFrame, pixelateSize);
        applyBlur(processedFrame, blur);       // <-- Bug is fixed inside this function
        applySharpen(processedFrame, sharpen);
        
        // Final kaleidoscope
        applyKaleidoscope(processedFrame, kaleidoscopeMode);

        // --- 4. Publish and update UI ---
        if (myLogicalId != 0)
            VideoFrameManager::getInstance().setFrame(myLogicalId, processedFrame);
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
    
    // --- BEGIN FIX ---
    // Find our own ID if it's not set
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
    
    // Output our own Logical ID on the output pin, so we can be chained
    if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
    {
        float sourceId = (float)myLogicalId; // Use the correct ID
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
            "zoomLevel", "brightness", "contrast", "saturation", "hueShift",
            "gainRed", "gainGreen", "gainBlue", "sepia", "temperature", "sharpen", "blur", 
            "grayscale", "invert", "flipH", "flipV", "thresholdEnable", "thresholdLevel",
            "posterizeLevels", "vignetteAmount", "vignetteSize", "pixelateSize", 
            "cannyEnable", "cannyThresh1", "cannyThresh2", "kaleidoscope"
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
    
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID In: %d", (int)currentSourceId.load());
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output ID: %d", (int)getLogicalId());
    
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
    
    bool sepia = sepiaParam ? sepiaParam->get() : false;
    if (ImGui::Checkbox("Sepia", &sepia))
    {
        if (sepiaParam) *sepiaParam = sepia;
        onModificationEnded();
    }
    
    float temperature = temperatureParam ? temperatureParam->load() : 0.0f;
    if (ImGui::SliderFloat("Temperature", &temperature, -1.0f, 1.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("temperature"))) *p = temperature;
        onModificationEnded();
    }
    
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
    
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "More Filters");

    // Threshold Effect
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
    
    // Posterize
    int posterizeLevels = posterizeLevelsParam ? posterizeLevelsParam->get() : 16;
    if (ImGui::SliderInt("Posterize", &posterizeLevels, 2, 16))
    {
        if (posterizeLevelsParam) *posterizeLevelsParam = posterizeLevels;
        onModificationEnded();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reduces the number of colors.\nLower values = stronger effect.");
    
    // Pixelate
    int pixelateSize = pixelateBlockSizeParam ? pixelateBlockSizeParam->get() : 1;
    if (ImGui::SliderInt("Pixelate", &pixelateSize, 1, 128))
    {
        if (pixelateBlockSizeParam) *pixelateBlockSizeParam = pixelateSize;
        onModificationEnded();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Creates a mosaic effect.\nHigher values = larger blocks.");
    
    // Edge Detection (Canny)
    bool cannyEnable = cannyEnableParam ? cannyEnableParam->get() : false;
    if (ImGui::Checkbox("Edge Detect", &cannyEnable))
    {
        if (cannyEnableParam) *cannyEnableParam = cannyEnable;
        onModificationEnded();
    }
    
    if (cannyEnable)
    {
        float cannyTh1 = cannyThresh1Param ? cannyThresh1Param->load() : 50.0f;
        if (ImGui::SliderFloat("Canny Thresh 1", &cannyTh1, 0.0f, 255.0f))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cannyThresh1"))) *p = cannyTh1;
            onModificationEnded();
        }
        
        float cannyTh2 = cannyThresh2Param ? cannyThresh2Param->load() : 150.0f;
        if (ImGui::SliderFloat("Canny Thresh 2", &cannyTh2, 0.0f, 255.0f))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cannyThresh2"))) *p = cannyTh2;
            onModificationEnded();
        }
    }
    
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Advanced Effects");
    
    // Vignette
    float vignetteAmount = vignetteAmountParam ? vignetteAmountParam->load() : 0.0f;
    if (ImGui::SliderFloat("Vignette Amount", &vignetteAmount, 0.0f, 1.0f))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("vignetteAmount"))) *p = vignetteAmount;
        onModificationEnded();
    }
    
    if (vignetteAmount > 0.0f)
    {
        float vignetteSize = vignetteSizeParam ? vignetteSizeParam->load() : 0.5f;
        if (ImGui::SliderFloat("Vignette Size", &vignetteSize, 0.1f, 2.0f))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("vignetteSize"))) *p = vignetteSize;
            onModificationEnded();
        }
    }
    
    // Kaleidoscope
    int kaleidoscopeMode = kaleidoscopeModeParam ? kaleidoscopeModeParam->getIndex() : 0;
    const char* kaleidoscopeModes[] = { "None", "4-Way", "8-Way" };
    if (ImGui::Combo("Kaleidoscope", &kaleidoscopeMode, kaleidoscopeModes, 3))
    {
        if (kaleidoscopeModeParam) kaleidoscopeModeParam->setValueNotifyingHost((float)kaleidoscopeMode / 2.0f);
        onModificationEnded();
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

