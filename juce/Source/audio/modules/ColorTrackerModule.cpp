#include "ColorTrackerModule.h"
#include "../graph/ModularSynthProcessor.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ColorTrackerModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
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
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "numAutoColors", "Auto-Track Colors", 2, 24, 12));
    return { params.begin(), params.end() };
}

ColorTrackerModule::ColorTrackerModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(73), true) // 24 colors x 3 + 1 for numColors
                      .withOutput("Video Out", juce::AudioChannelSet::mono(), true)), // PASSTHROUGH
      juce::Thread("Color Tracker Thread"),
      apvts(*this, nullptr, "ColorTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    numAutoColorsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numAutoColors"));
    fifoBuffer.resize(16);
}

ColorTrackerModule::~ColorTrackerModule()
{
    stopThread(5000);
}

void ColorTrackerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void ColorTrackerModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void ColorTrackerModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (!frame.empty())
        {
            // Cache last good frame for paused/no-signal scenarios
            {
                const juce::ScopedLock lk(frameLock);
                frame.copyTo(lastFrameBgr);
            }
        }
        else
        {
            // Use cached frame when no fresh frames are available (e.g., transport paused)
            const juce::ScopedLock lk(frameLock);
            if (!lastFrameBgr.empty())
                frame = lastFrameBgr.clone();
        }

        if (!frame.empty())
        {
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0);
            #endif
            
            cv::Mat hsv;
            #if WITH_CUDA_SUPPORT
                cv::cuda::GpuMat hsvGpu; // Keep in scope for use in inRange loop
                if (useGpu)
                {
                    cv::cuda::GpuMat frameGpu;
                    frameGpu.upload(frame);
                    cv::cuda::cvtColor(frameGpu, hsvGpu, cv::COLOR_BGR2HSV);
                    // Download for CPU fallback checks (hsv.empty() check below)
                    hsvGpu.download(hsv);
                }
                else
                {
                    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
                }
            #else
                cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
            #endif

            // NOTE: No queued color-pick path here anymore; add/update is handled synchronously by addColorAt()

            ColorResult result;
            {
                const juce::ScopedLock lock(colorListLock);
                for (auto& tc : trackedColors)
                {
                    if (frame.empty() || hsv.empty())
                    {
                        result.emplace_back(0.5f, 0.5f, 0.0f);
                        continue;
                    }
                    // Compute tolerance-adjusted bounds
                    double centerH = 0.5 * ((double)tc.hsvLower[0] + (double)tc.hsvUpper[0]);
                    double centerS = 0.5 * ((double)tc.hsvLower[1] + (double)tc.hsvUpper[1]);
                    double centerV = 0.5 * ((double)tc.hsvLower[2] + (double)tc.hsvUpper[2]);
                    double deltaH  = 0.5 * ((double)tc.hsvUpper[0] - (double)tc.hsvLower[0]);
                    double deltaS  = 0.5 * ((double)tc.hsvUpper[1] - (double)tc.hsvLower[1]);
                    double deltaV  = 0.5 * ((double)tc.hsvUpper[2] - (double)tc.hsvLower[2]);
                    double scale   = juce::jlimit(0.1, 5.0, (double)tc.tolerance);
                    double lowH  = juce::jlimit(0.0, 179.0, centerH - deltaH * scale);
                    double highH = juce::jlimit(0.0, 179.0, centerH + deltaH * scale);
                    double lowS  = juce::jlimit(0.0, 255.0, centerS - deltaS * scale);
                    double highS = juce::jlimit(0.0, 255.0, centerS + deltaS * scale);
                    double lowV  = juce::jlimit(0.0, 255.0, centerV - deltaV * scale);
                    double highV = juce::jlimit(0.0, 255.0, centerV + deltaV * scale);

                    cv::Scalar lower(lowH, lowS, lowV);
                    cv::Scalar upper(highH, highS, highV);

                    cv::Mat mask;
                    // NOTE: inRange doesn't have CUDA implementation in this OpenCV build
                    // Using CPU version even when GPU is enabled (fast enough, avoids upload/download overhead)
                    cv::inRange(hsv, lower, upper, mask);
                    
                    // Morphological cleanup
                    cv::erode(mask, mask, cv::Mat(), cv::Point(-1,-1), 1);
                    cv::dilate(mask, mask, cv::Mat(), cv::Point(-1,-1), 1);
                    
                    std::vector<std::vector<cv::Point>> contours;
                    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                    
                    if (!contours.empty())
                    {
                        size_t best = 0;
                        double maxArea = 0.0;
                        for (size_t i = 0; i < contours.size(); ++i)
                        {
                            double a = cv::contourArea(contours[i]);
                            if (a > maxArea) { maxArea = a; best = i; }
                        }
                        const auto& c = contours[best];
                        cv::Moments m = cv::moments(c);
                        float cx = (m.m00 > 0.0) ? (float)(m.m10 / m.m00) / (float)frame.cols : 0.5f;
                        float cy = (m.m00 > 0.0) ? (float)(m.m01 / m.m00) / (float)frame.rows : 0.5f;
                        
                        // Normalize area to frame size (0.0 to 1.0)
                        float normalizedArea = juce::jlimit(0.0f, 1.0f, (float)(maxArea / (frame.cols * frame.rows)));
                        
                        // Apply power curve to boost smaller values, then map to [0.5, 1.0]
                        // sqrt curve: smaller areas get boosted more, large areas scale smoothly
                        float sqrtArea = std::sqrt(normalizedArea);
                        // Map sqrt [0,1] to [0.5, 1.0]: 0.5 + sqrtArea * 0.5
                        float area = 0.5f + sqrtArea * 0.5f;
                        area = juce::jlimit(0.5f, 1.0f, area);
                        
                        result.emplace_back(cx, cy, area);
                        
                        // Draw
                        cv::Rect bbox = cv::boundingRect(c);
                        cv::rectangle(frame, bbox, cv::Scalar(0, 255, 255), 2);
                        cv::putText(frame, tc.name.toStdString(), bbox.tl() + cv::Point(0, -5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
                    }
                    else
                    {
                        // No color found: strict 0 output
                        result.emplace_back(0.5f, 0.5f, 0.0f);
                    }
                }
            }

            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                    fifoBuffer[writeScope.startIndex1] = result;
            }
            
            if (!frame.empty())
            {
                // --- PASSTHROUGH LOGIC ---
                VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
                updateGuiFrame(frame);
            }
        }
        
        wait(33);
    }
}

void ColorTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgraFrame.cols || latestFrameForGui.getHeight() != bgraFrame.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image ColorTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void ColorTrackerModule::addColorAt(int x, int y)
{
    // Synchronous immediate update only (no background queuing)
    bool appliedSync = false;

    cv::Mat frameCopy;
    {
        const juce::ScopedLock lk(frameLock);
        if (!lastFrameBgr.empty())
            frameCopy = lastFrameBgr.clone();
    }
    if (!frameCopy.empty())
    {
        const int mx = juce::jlimit(0, frameCopy.cols - 1, x);
        const int my = juce::jlimit(0, frameCopy.rows - 1, y);
        cv::Rect roi(std::max(0, mx - 2), std::max(0, my - 2), 5, 5);
        roi &= cv::Rect(0, 0, frameCopy.cols, frameCopy.rows);
        if (roi.area() > 0)
        {
            cv::Scalar avgBgr = cv::mean(frameCopy(roi));
            cv::Vec3b bgr8((uchar)avgBgr[0], (uchar)avgBgr[1], (uchar)avgBgr[2]);
            cv::Mat onePix(1,1,CV_8UC3);
            onePix.at<cv::Vec3b>(0,0) = bgr8;
            cv::Mat onePixHsv;
            cv::cvtColor(onePix, onePixHsv, cv::COLOR_BGR2HSV);
            cv::Vec3b avgHsv = onePixHsv.at<cv::Vec3b>(0,0);
            int avgHue = (int)avgHsv[0];
            int avgSat = (int)avgHsv[1];
            int avgVal = (int)avgHsv[2];

            const juce::ScopedLock lock(colorListLock);
            int targetIdx = pickerTargetIndex.load();
            if (targetIdx < 0 || targetIdx >= (int)trackedColors.size())
            {
                TrackedColor tc;
                tc.name = juce::String("Color ") + juce::String((int)trackedColors.size() + 1);
                tc.hsvLower = cv::Scalar(
                    juce::jlimit(0, 179, avgHue - 10),
                    juce::jlimit(0, 255, avgSat - 40),
                    juce::jlimit(0, 255, avgVal - 40));
                tc.hsvUpper = cv::Scalar(
                    juce::jlimit(0, 179, avgHue + 10),
                    juce::jlimit(0, 255, avgSat + 40),
                    juce::jlimit(0, 255, avgVal + 40));
                tc.displayColour = juce::Colour((juce::uint8)bgr8[2], (juce::uint8)bgr8[1], (juce::uint8)bgr8[0]);
                trackedColors.push_back(tc);
            }
            else
            {
                auto& tc = trackedColors[(size_t)targetIdx];
                tc.hsvLower = cv::Scalar(
                    juce::jlimit(0, 179, avgHue - 10),
                    juce::jlimit(0, 255, avgSat - 40),
                    juce::jlimit(0, 255, avgVal - 40));
                tc.hsvUpper = cv::Scalar(
                    juce::jlimit(0, 179, avgHue + 10),
                    juce::jlimit(0, 255, avgSat + 40),
                    juce::jlimit(0, 255, avgVal + 40));
                tc.displayColour = juce::Colour((juce::uint8)bgr8[2], (juce::uint8)bgr8[1], (juce::uint8)bgr8[0]);
                appliedSync = true;
            }

            // Push the same frame to GUI immediately for instant visual feedback
            updateGuiFrame(frameCopy);
        }
    }

    // Finalize picker state; never queue async to avoid duplicates
    addColorRequested.store(false);
    pickerMouseX.store(-1);
    pickerMouseY.store(-1);
    isColorPickerActive.store(false);
}

void ColorTrackerModule::autoTrackColors()
{
    cv::Mat frameCopy;
    {
        const juce::ScopedLock lk(frameLock);
        if (!lastFrameBgr.empty())
            frameCopy = lastFrameBgr.clone();
    }

    if (frameCopy.empty())
    {
        juce::Logger::writeToLog("[ColorTracker] Auto-Track failed: No video frame available.");
        return;
    }

    juce::Logger::writeToLog("[ColorTracker] Starting Auto-Track color analysis...");

    // 1. Resize for performance. K-means is slow on large images.
    cv::Mat smallFrame;
    cv::resize(frameCopy, smallFrame, cv::Size(100, (int)(100.0f * (float)frameCopy.rows / (float)frameCopy.cols)));

    // 2. Reshape image into a list of pixels for clustering
    cv::Mat data;
    smallFrame.convertTo(data, CV_32F);
    data = data.reshape(1, data.total());

    // 3. Perform k-means clustering to find the dominant colors
    const int k = numAutoColorsParam ? numAutoColorsParam->get() : 12;
    cv::Mat labels, centers;
    cv::kmeans(data, k, labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 1.0),
               3, cv::KMEANS_PP_CENTERS, centers);

    // 4. Clear existing colors and create new ones from the cluster centers
    const juce::ScopedLock lock(colorListLock);
    trackedColors.clear();

    for (int i = 0; i < centers.rows; ++i)
    {
        cv::Vec3f center_bgr_float = centers.at<cv::Vec3f>(i);
        cv::Vec3b bgr8((uchar)center_bgr_float[0], (uchar)center_bgr_float[1], (uchar)center_bgr_float[2]);

        // Convert the BGR center to HSV to create a tracking window
        cv::Mat onePix(1,1,CV_8UC3);
        onePix.at<cv::Vec3b>(0,0) = bgr8;
        cv::Mat onePixHsv;
        cv::cvtColor(onePix, onePixHsv, cv::COLOR_BGR2HSV);
        cv::Vec3b avgHsv = onePixHsv.at<cv::Vec3b>(0,0);
        int avgHue = (int)avgHsv[0];
        int avgSat = (int)avgHsv[1];
        int avgVal = (int)avgHsv[2];

        TrackedColor tc;
        tc.name = juce::String("Color ") + juce::String(i + 1);
        tc.hsvLower = cv::Scalar(
            juce::jlimit(0, 179, avgHue - 8),
            juce::jlimit(0, 255, avgSat - 35),
            juce::jlimit(0, 255, avgVal - 35));
        tc.hsvUpper = cv::Scalar(
            juce::jlimit(0, 179, avgHue + 8),
            juce::jlimit(0, 255, avgSat + 35),
            juce::jlimit(0, 255, avgVal + 35));
        tc.displayColour = juce::Colour(bgr8[2], bgr8[1], bgr8[0]);
        trackedColors.push_back(tc);
    }
    
    juce::Logger::writeToLog("[ColorTracker] Auto-Track complete. Found " + juce::String((int)trackedColors.size()) + " colors.");
}

juce::ValueTree ColorTrackerModule::getExtraStateTree() const
{
    juce::ValueTree state("ColorTrackerState");
    const juce::ScopedLock lock(colorListLock);
    for (const auto& tc : trackedColors)
    {
        juce::ValueTree node("TrackedColor");
        node.setProperty("name", tc.name, nullptr);
        node.setProperty("displayColour", tc.displayColour.toString(), nullptr);
        // Persist HSV windows (indexed fields for stability)
        node.setProperty("hsvLower0", (int)tc.hsvLower[0], nullptr);
        node.setProperty("hsvLower1", (int)tc.hsvLower[1], nullptr);
        node.setProperty("hsvLower2", (int)tc.hsvLower[2], nullptr);
        node.setProperty("hsvUpper0", (int)tc.hsvUpper[0], nullptr);
        node.setProperty("hsvUpper1", (int)tc.hsvUpper[1], nullptr);
        node.setProperty("hsvUpper2", (int)tc.hsvUpper[2], nullptr);
        node.setProperty("tolerance", tc.tolerance, nullptr);
        state.addChild(node, -1, nullptr);
    }
    return state;
}

void ColorTrackerModule::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.hasType("ColorTrackerState")) return;
    const juce::ScopedLock lock(colorListLock);
    trackedColors.clear();
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto node = state.getChild(i);
        if (!node.hasType("TrackedColor")) continue;
        TrackedColor tc;
        tc.name = node.getProperty("name", juce::String("Color ") + juce::String(i)).toString();
        tc.displayColour = juce::Colour::fromString(node.getProperty("displayColour", "ff000000").toString());
        int hL = (int)node.getProperty("hsvLower0", 0);
        int sL = (int)node.getProperty("hsvLower1", 100);
        int vL = (int)node.getProperty("hsvLower2", 100);
        int hU = (int)node.getProperty("hsvUpper0", 10);
        int sU = (int)node.getProperty("hsvUpper1", 255);
        int vU = (int)node.getProperty("hsvUpper2", 255);
        tc.hsvLower = cv::Scalar(hL, sL, vL);
        tc.hsvUpper = cv::Scalar(hU, sU, vU);
        tc.tolerance = (float)(double)node.getProperty("tolerance", 1.0);
        trackedColors.push_back(tc);
    }
    // UI will re-query pins on next frame; no explicit rebuild signal required
}

void ColorTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    // Map each tracked color to 3 outputs: X, Y, Area Gate (bus 0 - CV Out)
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    for (size_t i = 0; i < lastResultForAudio.size(); ++i)
    {
        int chX = (int)i * 3 + 0;
        int chY = (int)i * 3 + 1;
        int chA = (int)i * 3 + 2;
        if (chA < cvOutBus.getNumChannels())
        {
            const auto& tpl = lastResultForAudio[i];
            float vx = std::get<0>(tpl);
            float vy = std::get<1>(tpl);
            float va = std::get<2>(tpl);
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            {
                cvOutBus.setSample(chX, s, vx);
                cvOutBus.setSample(chY, s, vy);
                cvOutBus.setSample(chA, s, va);
            }
        }
    }
    
    // Output the number of tracked colors on channel 72
    const int numColorsChannel = 72;
    if (cvOutBus.getNumChannels() > numColorsChannel)
    {
        const juce::ScopedLock lock(colorListLock);
        float numColorsValue = (float)trackedColors.size();
        juce::FloatVectorOperations::fill(cvOutBus.getWritePointer(numColorsChannel), numColorsValue, cvOutBus.getNumSamples());
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

std::vector<DynamicPinInfo> ColorTrackerModule::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const juce::ScopedLock lock(colorListLock);
    
    // Add "Num Colors" output first (channel 72)
    pins.emplace_back("Num Colors", 72, PinDataType::CV);
    
    // Add color outputs: X, Y, Area Gate for each tracked color
    for (size_t i = 0; i < trackedColors.size(); ++i)
    {
        pins.emplace_back(trackedColors[i].name + " X", (int)(i * 3 + 0), PinDataType::CV);
        pins.emplace_back(trackedColors[i].name + " Y", (int)(i * 3 + 1), PinDataType::CV);
        pins.emplace_back(trackedColors[i].name + " Area Gate", (int)(i * 3 + 2), PinDataType::Gate);
    }
    return pins;
}

#if defined(PRESET_CREATOR_UI)
ImVec2 ColorTrackerModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void ColorTrackerModule::drawParametersInNode(float itemWidth,
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
            ImGui::SetTooltip("Enable GPU acceleration for color tracking.\nRequires CUDA-capable NVIDIA GPU.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
    #endif

    if (ImGui::Button("Add Color...", ImVec2(itemWidth, 0)))
    {
        pickerTargetIndex.store(-1);
        isColorPickerActive.store(true);
    }

    int k = numAutoColorsParam ? numAutoColorsParam->get() : 12;
    ImGui::PushItemWidth(itemWidth - 60.0f); // Make space for button
    if (ImGui::SliderInt("##numautocolors", &k, 2, 24, "Auto-Track %d Colors"))
    {
        if (numAutoColorsParam) *numAutoColorsParam = k;
        onModificationEnded();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Go", ImVec2(50.0f, 0)))
    {
        autoTrackColors();
        onModificationEnded(); // Create an undo state
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Automatically finds the N most dominant colors in the current frame.\nThis will replace all existing tracked colors.");
    }

    if (isColorPickerActive.load())
    {
        ImGui::TextColored(ImVec4(1.f,1.f,0.f,1.f), "Click on the video preview to pick a color");
    }

    // Auto-Connect Buttons
    ImGui::Spacing();
    
    const int numColors = getTrackedColorsCount();
    if (numColors > 0)
    {
        if (ImGui::Button("Connect to PolyVCO", ImVec2(itemWidth, 0)))
        {
            autoConnectPolyVCOTriggered = true;
        }
        if (ImGui::Button("Connect to Samplers", ImVec2(itemWidth, 0)))
        {
            autoConnectSamplersTriggered = true;
        }
        ImGui::TextDisabled("Creates %d voices based on tracked colors", numColors);
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Connect to PolyVCO", ImVec2(itemWidth, 0));
        ImGui::Button("Connect to Samplers", ImVec2(itemWidth, 0));
        ImGui::EndDisabled();
        ImGui::TextDisabled("No colors tracked. Add colors first.");
    }

    // Zoom controls (-/+) like PoseEstimator
    {
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
        }
        if (atMin) ImGui::EndDisabled();
        ImGui::SameLine();
        if (atMax) ImGui::BeginDisabled();
        if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
        {
            int newLevel = juce::jmin(2, level + 1);
            if (auto* p = apvts.getParameter("zoomLevel"))
                p->setValueNotifyingHost((float)newLevel / 2.0f);
        }
        if (atMax) ImGui::EndDisabled();
    }

    // Render tracked color list with swatch, tolerance, and remove
    {
        const juce::ScopedLock lock(colorListLock);
        for (size_t i = 0; i < trackedColors.size(); )
        {
            const auto& tc = trackedColors[i];
            ImVec4 imc(tc.displayColour.getFloatRed(), tc.displayColour.getFloatGreen(), tc.displayColour.getFloatBlue(), 1.0f);
            if (ImGui::ColorButton((tc.name + "##swatch" + juce::String((int)i)).toRawUTF8(), imc, ImGuiColorEditFlags_NoTooltip, ImVec2(20,20)))
            {
                pickerTargetIndex.store((int)i);
                isColorPickerActive.store(true);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted((tc.name + "##label" + juce::String((int)i)).toRawUTF8());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            float tol = (float)tc.tolerance;
            if (ImGui::SliderFloat((juce::String("Tol##") + juce::String((int)i)).toRawUTF8(), &tol, 0.1f, 5.0f, "%.2fx"))
            {
                const_cast<TrackedColor&>(tc).tolerance = tol;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((juce::String("Remove##") + juce::String((int)i)).toRawUTF8()))
            {
                trackedColors.erase(trackedColors.begin() + (long long)i);
                continue; // don't increment i when erased
            }
            ++i;
        }
    }

    ImGui::PopItemWidth();
}

void ColorTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    // Outputs are dynamic; editor queries via getDynamicOutputPins
    helpers.drawAudioOutputPin("Video Out", 0); // Bus 1
}
#endif


