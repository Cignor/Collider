#include "ColorTrackerModule.h"
#include "../graph/ModularSynthProcessor.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include <juce_opengl/juce_opengl.h>
#include <map>
#include <unordered_map>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/theme/ThemeManager.h"
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
                      .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(77), true) // 24 colors x 3 + 1 for numColors + 4 zone gates
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
                    ColorResultEntry entry;
                    // Initialize all zone hits to false
                    for (int z = 0; z < 4; ++z)
                        entry.zoneHits[z] = false;
                    
                    if (frame.empty() || hsv.empty())
                    {
                        entry.x = 0.5f;
                        entry.y = 0.5f;
                        entry.area = 0.0f;
                        result.push_back(entry);
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
                        
                        entry.x = cx;
                        entry.y = cy;
                        entry.area = area;
                        
                        // Check zones: cx and cy are already normalized to 0-1
                        // Only check zones if area > 0 (color was actually detected)
                        if (entry.area > 0.0f)
                        {
                            // Check each color zone
                            for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                            {
                                std::vector<ZoneRect> rects;
                                loadZoneRects(colorIdx, rects);
                                
                                bool hit = false;
                                for (const auto& rect : rects)
                                {
                                    // Point-in-rectangle check
                                    if (cx >= rect.x && cx <= rect.x + rect.width &&
                                        cy >= rect.y && cy <= rect.y + rect.height)
                                    {
                                        hit = true;
                                        break;
                                    }
                                }
                                entry.zoneHits[colorIdx] = hit;
                            }
                        }
                        // If area is 0, zones remain false (already initialized)
                        
                        result.push_back(entry);
                        
                        // Draw
                        cv::Rect bbox = cv::boundingRect(c);
                        cv::rectangle(frame, bbox, cv::Scalar(0, 255, 255), 2);
                        cv::putText(frame, tc.name.toStdString(), bbox.tl() + cv::Point(0, -5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
                    }
                    else
                    {
                        // No color found: strict 0 output
                        entry.x = 0.5f;
                        entry.y = 0.5f;
                        entry.area = 0.0f;
                        // Zones remain false (already initialized)
                        result.push_back(entry);
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
                // Use storedLogicalId if available, otherwise try to find it
                juce::uint32 frameId = storedLogicalId;
                if (frameId == 0 && parentSynth != nullptr)
                {
                    for (const auto& info : parentSynth->getModulesInfo())
                    {
                        if (parentSynth->getModuleForLogical(info.first) == this)
                        {
                            frameId = info.first;
                            storedLogicalId = frameId; // Cache it
                            break;
                        }
                    }
                }
                if (frameId != 0)
                    VideoFrameManager::getInstance().setFrame(frameId, frame);
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

// Serialize zone rectangles to string: "x1,y1,w1,h1;x2,y2,w2,h2;..."
juce::String ColorTrackerModule::serializeZoneRects(const std::vector<ZoneRect>& rects)
{
    juce::String result;
    for (size_t i = 0; i < rects.size(); ++i)
    {
        if (i > 0) result += ";";
        result += juce::String(rects[i].x, 4) + "," +
                  juce::String(rects[i].y, 4) + "," +
                  juce::String(rects[i].width, 4) + "," +
                  juce::String(rects[i].height, 4);
    }
    return result;
}

// Deserialize zone rectangles from string
std::vector<ColorTrackerModule::ZoneRect> ColorTrackerModule::deserializeZoneRects(const juce::String& data)
{
    std::vector<ZoneRect> rects;
    if (data.isEmpty()) return rects;
    
    juce::StringArray rectStrings;
    rectStrings.addTokens(data, ";", "");
    
    for (const auto& rectStr : rectStrings)
    {
        juce::StringArray coords;
        coords.addTokens(rectStr, ",", "");
        if (coords.size() == 4)
        {
            ZoneRect rect;
            rect.x = coords[0].getFloatValue();
            rect.y = coords[1].getFloatValue();
            rect.width = coords[2].getFloatValue();
            rect.height = coords[3].getFloatValue();
            rects.push_back(rect);
        }
    }
    return rects;
}

// Load zone rectangles for a color from APVTS state tree
void ColorTrackerModule::loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::var value = apvts.state.getProperty(key);
    if (value.isString())
    {
        rects = deserializeZoneRects(value.toString());
    }
    else
    {
        rects.clear();
    }
}

// Save zone rectangles for a color to APVTS state tree
void ColorTrackerModule::saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects)
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::String data = serializeZoneRects(rects);
    apvts.state.setProperty(key, data, nullptr);
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
            const auto& entry = lastResultForAudio[i];
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            {
                cvOutBus.setSample(chX, s, entry.x);
                cvOutBus.setSample(chY, s, entry.y);
                cvOutBus.setSample(chA, s, entry.area);
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
    
    // Output zone gates (channels 73-76)
    // A zone gate is TRUE if ANY tracked color is in that zone
    for (int z = 0; z < 4; ++z)
    {
        int ch = 73 + z;
        if (ch < cvOutBus.getNumChannels())
        {
            bool anyHit = false;
            for (const auto& entry : lastResultForAudio)
            {
                if (entry.zoneHits[z])
                {
                    anyHit = true;
                    break;
                }
            }
            
            float gateValue = anyHit ? 1.0f : 0.0f;
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            {
                cvOutBus.setSample(ch, s, gateValue);
            }
        }
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
    
    // Add zone gate outputs (channels 73-76)
    pins.emplace_back("Red Zone Gate", 73, PinDataType::Gate);
    pins.emplace_back("Green Zone Gate", 74, PinDataType::Gate);
    pins.emplace_back("Blue Zone Gate", 75, PinDataType::Gate);
    pins.emplace_back("Yellow Zone Gate", 76, PinDataType::Gate);
    
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
        ThemeText("Click on the video preview to pick a color", theme.text.warning);
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

    
    // Zone color palette (4 colors)
    static const ImVec4 ZONE_COLORS[4] = {
        ImVec4(1.0f, 0.0f, 0.0f, 0.3f),  // Red - 30% opacity
        ImVec4(0.0f, 1.0f, 0.0f, 0.3f),  // Green - 30% opacity
        ImVec4(0.0f, 0.0f, 1.0f, 0.3f),  // Blue - 30% opacity
        ImVec4(1.0f, 1.0f, 0.0f, 0.3f)   // Yellow - 30% opacity
    };
    
    // Static state for mouse interaction (per-instance using logical ID)
    static std::map<int, int> activeZoneColorIndexByNode;
    static std::map<int, int> drawingZoneIndexByNode;
    static std::map<int, float> dragStartXByNode;
    static std::map<int, float> dragStartYByNode;
    
    int nodeId = (int)getLogicalId();
    
    // Initialize active color index if not set
    if (activeZoneColorIndexByNode.find(nodeId) == activeZoneColorIndexByNode.end())
        activeZoneColorIndexByNode[nodeId] = 0;
    
    // Initialize drawingZoneIndex to -1 (not drawing) if not set - MUST do before accessing reference!
    if (drawingZoneIndexByNode.find(nodeId) == drawingZoneIndexByNode.end())
        drawingZoneIndexByNode[nodeId] = -1;
    
    // Now safe to get references
    int& activeZoneColorIndex = activeZoneColorIndexByNode[nodeId];
    int& drawingZoneIndex = drawingZoneIndexByNode[nodeId];
    float& dragStartX = dragStartXByNode[nodeId];
    float& dragStartY = dragStartYByNode[nodeId];
    
    // Color picker boxes
    ImGui::Text("Zone Colors:");
    ImGui::SameLine();
    for (int c = 0; c < 4; ++c)
    {
        ImGui::PushID(c);
        ImVec4 color = ZONE_COLORS[c];
        color.w = 1.0f;  // Full opacity for picker button
        if (ImGui::ColorButton("##ZoneColor", color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20)))
        {
            activeZoneColorIndex = c;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click to select color %d", c + 1);
        }
        ImGui::PopID();
        if (c < 3) ImGui::SameLine();
    }
    
    
    // Video preview with zone overlays
    juce::Image frame = getLatestFrame();
    if (!frame.isNull())
    {
        // Use static map for texture management (per-module-instance textures)
        static std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> localTextures;
        
        if (localTextures.find(nodeId) == localTextures.end())
            localTextures[nodeId] = std::make_unique<juce::OpenGLTexture>();
        
        auto* texture = localTextures[nodeId].get();
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
            
            // Use InvisibleButton to capture mouse input and prevent node movement (like CropVideoModule)
            ImGui::SetCursorScreenPos(imageRectMin);
            ImGui::InvisibleButton("##zone_interaction", imageSize);
            
            ImVec2 mousePos = ImGui::GetMousePos();
            
            // Draw zones - each color zone can have multiple rectangles
            for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
            {
                std::vector<ZoneRect> rects;
                loadZoneRects(colorIdx, rects);
                
                ImVec4 color = ZONE_COLORS[colorIdx];
                ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(color);
                ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f));
                
                for (const auto& rect : rects)
                {
                    ImVec2 zoneMin(imageRectMin.x + rect.x * imageSize.x,
                                  imageRectMin.y + rect.y * imageSize.y);
                    ImVec2 zoneMax(imageRectMin.x + (rect.x + rect.width) * imageSize.x,
                                  imageRectMin.y + (rect.y + rect.height) * imageSize.y);
                    
                    drawList->AddRectFilled(zoneMin, zoneMax, fillColor);
                    drawList->AddRect(zoneMin, zoneMax, borderColor, 0.0f, 0, 2.0f);
                }
            }
            
            // Draw color centroids (the points being checked for zone hits) - small red dots
            // Read latest result from FIFO for UI display
            ColorResult uiResult;
            if (fifo.getNumReady() > 0)
            {
                auto readScope = fifo.read(1);
                if (readScope.blockSize1 > 0)
                {
                    uiResult = fifoBuffer[readScope.startIndex1];
                }
            }
            else
            {
                // Fallback to last result if FIFO is empty (might be stale but better than nothing)
                uiResult = lastResultForAudio;
            }
            
            // Draw a small red dot at each detected color's centroid
            for (const auto& entry : uiResult)
            {
                // Only draw if color was actually detected (area > 0)
                if (entry.area > 0.0f)
                {
                    // Convert normalized coordinates (0-1) to screen coordinates
                    float centerX = imageRectMin.x + entry.x * imageSize.x;
                    float centerY = imageRectMin.y + entry.y * imageSize.y;
                    ImVec2 center(centerX, centerY);
                    
                    // Draw small red dot (radius 3 pixels)
                    ImU32 redColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    drawList->AddCircleFilled(center, 3.0f, redColor);
                }
            }
            
            // Mouse interaction - use InvisibleButton's hover state
            if (ImGui::IsItemHovered())
            {
                // Normalize mouse position (0.0-1.0)
                float mouseX = (mousePos.x - imageRectMin.x) / imageSize.x;
                float mouseY = (mousePos.y - imageRectMin.y) / imageSize.y;
                
                // Check if Ctrl key is held
                bool ctrlHeld = ImGui::GetIO().KeyCtrl;
                
                // Static state for hover preview (per-node)
                static std::map<int, int> hoverRadiusByNode; // logicalId -> radius (half-size), default 2 => 5x5
                int& rad = hoverRadiusByNode[nodeId];
                if (rad <= 0) rad = 2;
                
                // Handle color picker clicks: Left-click always adds color (unless Ctrl is held for zone drawing)
                if (!ctrlHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Get mouse position in frame coordinates
                    int frameX = juce::roundToInt(mouseX * frame.getWidth());
                    int frameY = juce::roundToInt(mouseY * frame.getHeight());
                    addColorAt(frameX, frameY);
                }
                
                // Zone drawing: Only with Ctrl+Left-click
                if (ctrlHeld)
                {
                    // Ctrl+Left-click: Start drawing a new rectangle for the selected color zone
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        dragStartX = mouseX;
                        dragStartY = mouseY;
                        drawingZoneIndex = activeZoneColorIndex;  // Drawing for the selected color
                    }
                    
                    // Ctrl+Left-drag: Update rectangle being drawn and show preview
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && drawingZoneIndex >= 0 && ctrlHeld)
                    {
                        float dragEndX = mouseX;
                        float dragEndY = mouseY;
                        
                        // Calculate rectangle from drag start to current position
                        float zx = juce::jmin(dragStartX, dragEndX);
                        float zy = juce::jmin(dragStartY, dragEndY);
                        float zw = std::abs(dragEndX - dragStartX);
                        float zh = std::abs(dragEndY - dragStartY);
                        
                        // Clamp to image bounds
                        zx = juce::jlimit(0.0f, 1.0f, zx);
                        zy = juce::jlimit(0.0f, 1.0f, zy);
                        zw = juce::jlimit(0.01f, 1.0f - zx, zw);
                        zh = juce::jlimit(0.01f, 1.0f - zy, zh);
                        
                        // Draw preview rectangle
                        ImVec2 previewMin(imageRectMin.x + zx * imageSize.x,
                                         imageRectMin.y + zy * imageSize.y);
                        ImVec2 previewMax(imageRectMin.x + (zx + zw) * imageSize.x,
                                         imageRectMin.y + (zy + zh) * imageSize.y);
                        
                        ImVec4 previewColor = ZONE_COLORS[drawingZoneIndex];
                        ImU32 previewFillColor = ImGui::ColorConvertFloat4ToU32(previewColor);
                        ImU32 previewBorderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(previewColor.x, previewColor.y, previewColor.z, 1.0f));
                        
                        drawList->AddRectFilled(previewMin, previewMax, previewFillColor);
                        drawList->AddRect(previewMin, previewMax, previewBorderColor, 0.0f, 0, 2.0f);
                    }
                    
                    // Ctrl+Left-release: Finish drawing - add rectangle to the color zone
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && drawingZoneIndex >= 0)
                    {
                        float dragEndX = mouseX;
                        float dragEndY = mouseY;
                        
                        // Calculate final rectangle
                        float zx = juce::jmin(dragStartX, dragEndX);
                        float zy = juce::jmin(dragStartY, dragEndY);
                        float zw = std::abs(dragEndX - dragStartX);
                        float zh = std::abs(dragEndY - dragStartY);
                        
                        // Only add if rectangle is large enough
                        if (zw > 0.01f && zh > 0.01f)
                        {
                            // Clamp to image bounds
                            zx = juce::jlimit(0.0f, 1.0f, zx);
                            zy = juce::jlimit(0.0f, 1.0f, zy);
                            zw = juce::jlimit(0.01f, 1.0f - zx, zw);
                            zh = juce::jlimit(0.01f, 1.0f - zy, zh);
                            
                            // Load existing rectangles for this color
                            std::vector<ZoneRect> rects;
                            loadZoneRects(drawingZoneIndex, rects);
                            
                            // Add new rectangle
                            ZoneRect newRect;
                            newRect.x = zx;
                            newRect.y = zy;
                            newRect.width = zw;
                            newRect.height = zh;
                            rects.push_back(newRect);
                            
                            // Save back to APVTS
                            saveZoneRects(drawingZoneIndex, rects);
                            onModificationEnded();
                        }
                        
                        drawingZoneIndex = -1;
                    }
                }
                
                // Right-drag: Eraser mode (delete rectangles from color zones) - works regardless of Ctrl
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                {
                    // Check if mouse is inside any rectangle of any color zone
                    for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                    {
                        std::vector<ZoneRect> rects;
                        loadZoneRects(colorIdx, rects);
                        
                        // Check each rectangle and remove if mouse is inside
                        bool modified = false;
                        for (auto it = rects.begin(); it != rects.end();)
                        {
                            bool inside = (mouseX >= it->x && mouseX <= it->x + it->width &&
                                          mouseY >= it->y && mouseY <= it->y + it->height);
                            
                            if (inside)
                            {
                                it = rects.erase(it);
                                modified = true;
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        
                        if (modified)
                        {
                            saveZoneRects(colorIdx, rects);
                            onModificationEnded();
                        }
                    }
                }
                
                // Original hover preview: median/average color swatch and scroll-wheel radius control
                // (Always show when hovering, unless actively drawing a zone)
                if (drawingZoneIndex < 0)
                {
                    // Update radius by mouse wheel
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        rad += (wheel > 0) ? 1 : -1;
                        rad = juce::jlimit(1, 30, rad); // (2*rad+1)^2 window, max 61x61
                    }
                    
                    // Map mouse to pixel
                    float nx = mouseX;
                    float ny = mouseY;
                    nx = juce::jlimit(0.0f, 1.0f, nx);
                    ny = juce::jlimit(0.0f, 1.0f, ny);
                    int cx = (int)juce::jlimit(0.0f, (float)frame.getWidth()  - 1.0f, nx * (float)frame.getWidth());
                    int cy = (int)juce::jlimit(0.0f, (float)frame.getHeight() - 1.0f, ny * (float)frame.getHeight());
                    
                    // Sample ROI from juce::Image
                    std::vector<int> vr, vg, vb;
                    vr.reserve((2*rad+1)*(2*rad+1));
                    vg.reserve(vr.capacity());
                    vb.reserve(vr.capacity());
                    
                    juce::Image::BitmapData bd(frame, juce::Image::BitmapData::readOnly);
                    auto clampi = [](int v, int lo, int hi){ return (v < lo) ? lo : (v > hi ? hi : v); };
                    for (int y = cy - rad; y <= cy + rad; ++y)
                    {
                        int yy = clampi(y, 0, frame.getHeight()-1);
                        const juce::PixelARGB* row = (const juce::PixelARGB*)(bd.getLinePointer(yy));
                        for (int x = cx - rad; x <= cx + rad; ++x)
                        {
                            int xx = clampi(x, 0, frame.getWidth()-1);
                            const juce::PixelARGB& p = row[xx];
                            vr.push_back(p.getRed());
                            vg.push_back(p.getGreen());
                            vb.push_back(p.getBlue());
                        }
                    }
                    auto median = [](std::vector<int>& v){ 
                        if (v.empty()) return 0;
                        std::nth_element(v.begin(), v.begin()+v.size()/2, v.end()); 
                        return v[v.size()/2]; 
                    };
                    int mr = median(vr), mg = median(vg), mb = median(vb);
                    juce::Colour mc((juce::uint8)mr, (juce::uint8)mg, (juce::uint8)mb);
                    float h = mc.getHue(), s = mc.getSaturation(), b = mc.getBrightness();
                    
                    // Tooltip near cursor with swatch and numbers - ALWAYS show when hovering
                    ImGui::BeginTooltip();
                    ImGui::Text("(%d,%d) rad=%d", cx, cy, rad);
                    ImGui::ColorButton("##hoverSwatch", ImVec4(mc.getFloatRed(), mc.getFloatGreen(), mc.getFloatBlue(), 1.0f), 0, ImVec2(22,22));
                    ImGui::SameLine();
                    ImGui::Text("RGB %d,%d,%d\nHSV %d,%d,%d", mr, mg, mb, (int)(h*180.0f), (int)(s*255.0f), (int)(b*255.0f));
                    // Add zone drawing hint if Ctrl is held
                    if (ctrlHeld)
                    {
                        ImGui::TextDisabled("Ctrl+Left-drag: Draw zone\nRight-drag: Erase zone");
                    }
                    ImGui::EndTooltip();
                    
                    // Textual summary under the image (lightweight) - always show
                    ImGui::TextDisabled("Hover RGB %d,%d,%d  HSV %d,%d,%d  rad=%d", mr, mg, mb, (int)(h*180.0f), (int)(s*255.0f), (int)(b*255.0f), rad);
                }
            }
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


