#include "MovementDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/theme/ThemeManager.h"
#include <juce_opengl/juce_opengl.h>
#include <map>
#include <unordered_map>
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
                     .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(8), true)  // 4 detection + 4 zone gates
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
    // Initialize all zone hits to false
    for (int z = 0; z < 4; ++z)
        result.zoneHits[z] = false;
    
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
                
                // For Optical Flow: Check if ANY successfully tracked point is inside a zone
                // This is more permissive than averaging - if movement is happening anywhere in the zone, we detect it
                if (trackedCount > 0)
                {
                    static int logCounter = 0;
                    logCounter++;
                    bool shouldLog = (logCounter % 10 == 0); // Log every 10 frames to avoid spam
                    
                    if (shouldLog)
                    {
                        juce::Logger::writeToLog("[MovementDetector] Optical Flow: trackedCount=" + juce::String(trackedCount) + 
                                                 ", motionAmount=" + juce::String(result.motionAmount, 3));
                    }
                    
                    // For each color zone, check if any tracked point is inside
                    for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                    {
                        std::vector<ZoneRect> rects;
                        loadZoneRects(colorIdx, rects);
                        
                        if (shouldLog && !rects.empty())
                        {
                            juce::String zoneName = colorIdx == 0 ? "Red" : (colorIdx == 1 ? "Green" : (colorIdx == 2 ? "Blue" : "Yellow"));
                            juce::Logger::writeToLog("[MovementDetector] Zone " + zoneName + ": " + juce::String(rects.size()) + " rectangles");
                            for (size_t r = 0; r < rects.size(); ++r)
                            {
                                juce::Logger::writeToLog("[MovementDetector]   Rect " + juce::String(r) + ": x=" + juce::String(rects[r].x, 3) + 
                                                         ", y=" + juce::String(rects[r].y, 3) + 
                                                         ", w=" + juce::String(rects[r].width, 3) + 
                                                         ", h=" + juce::String(rects[r].height, 3));
                            }
                        }
                        
                        bool hit = false;
                        int pointsChecked = 0;
                        int pointsInZone = 0;
                        
                        // Check each successfully tracked point
                        for (size_t i = 0; i < prevPoints.size(); ++i)
                        {
                            if (status[i] && i < nextPoints.size())
                            {
                                pointsChecked++;
                                
                                // Normalize point position to 0-1 range
                                float posX = nextPoints[i].x / (float)gray.cols;
                                float posY = nextPoints[i].y / (float)gray.rows;
                                
                                // Check if this point is inside any rectangle of this color zone
                                for (const auto& rect : rects)
                                {
                                    if (posX >= rect.x && posX <= rect.x + rect.width &&
                                        posY >= rect.y && posY <= rect.y + rect.height)
                                    {
                                        hit = true;
                                        pointsInZone++;
                                        if (shouldLog)
                                        {
                                            juce::String zoneName = colorIdx == 0 ? "Red" : (colorIdx == 1 ? "Green" : (colorIdx == 2 ? "Blue" : "Yellow"));
                                            juce::Logger::writeToLog("[MovementDetector] Zone " + zoneName + " HIT! Point " + juce::String(i) + 
                                                                     " at (" + juce::String(posX, 3) + ", " + juce::String(posY, 3) + ")");
                                        }
                                        break; // Found a point in this zone, no need to check more
                                    }
                                }
                                
                                if (hit) break; // Found a hit for this zone, check next zone
                            }
                        }
                        
                        if (shouldLog)
                        {
                            juce::String zoneName = colorIdx == 0 ? "Red" : (colorIdx == 1 ? "Green" : (colorIdx == 2 ? "Blue" : "Yellow"));
                            juce::Logger::writeToLog("[MovementDetector] Zone " + zoneName + " result: hit=" + (hit ? "TRUE" : "FALSE") + 
                                                     ", pointsChecked=" + juce::String(pointsChecked) + 
                                                     ", pointsInZone=" + juce::String(pointsInZone));
                        }
                        
                        result.zoneHits[colorIdx] = hit;
                    }
                }
                // If trackedCount is 0, zones remain false (already initialized)
            }
            // If trackedCount is 0, zones remain false (already initialized)
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
             // Get centroid position in pixels
             float centroidX = (float)(m.m10 / m.m00);
             float centroidY = (float)(m.m01 / m.m00);
             
             // Map to -1/+1 range using actual frame dimensions
             result.avgMotionX = juce::jmap(centroidX, 0.0f, (float)gray.cols, -1.0f, 1.0f);
             result.avgMotionY = juce::jmap(centroidY, 0.0f, (float)gray.rows, -1.0f, 1.0f);
             
             cv::Point2f centroid(centroidX, centroidY);
             cv::circle(displayFrame, centroid, 5, cv::Scalar(0, 255, 0), -1);
             cv::circle(displayFrame, centroid, 8, cv::Scalar(255, 255, 255), 2);
             
             // Check zones: normalize centroid position to 0-1 range using actual frame dimensions
             float posX = centroidX / (float)gray.cols;
             float posY = centroidY / (float)gray.rows;
             
             static int logCounterBgSub = 0;
             logCounterBgSub++;
             bool shouldLogBgSub = (logCounterBgSub % 10 == 0); // Log every 10 frames
             
             if (shouldLogBgSub)
             {
                 juce::Logger::writeToLog("[MovementDetector] Background Subtraction: motionAmount=" + juce::String(result.motionAmount, 3) + 
                                          ", centroid=(" + juce::String(centroidX, 1) + ", " + juce::String(centroidY, 1) + 
                                          "), normalized=(" + juce::String(posX, 3) + ", " + juce::String(posY, 3) + ")");
             }
             
             // Check each color zone
             for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
             {
                 std::vector<ZoneRect> rects;
                 loadZoneRects(colorIdx, rects);
                 
                 bool hit = false;
                 for (const auto& rect : rects)
                 {
                     if (posX >= rect.x && posX <= rect.x + rect.width &&
                         posY >= rect.y && posY <= rect.y + rect.height)
                     {
                         hit = true;
                         if (shouldLogBgSub)
                         {
                             juce::String zoneName = colorIdx == 0 ? "Red" : (colorIdx == 1 ? "Green" : (colorIdx == 2 ? "Blue" : "Yellow"));
                             juce::Logger::writeToLog("[MovementDetector] Zone " + zoneName + " HIT! Centroid at (" + 
                                                      juce::String(posX, 3) + ", " + juce::String(posY, 3) + ")");
                         }
                         break;
                     }
                 }
                 
                 if (shouldLogBgSub)
                 {
                     juce::String zoneName = colorIdx == 0 ? "Red" : (colorIdx == 1 ? "Green" : (colorIdx == 2 ? "Blue" : "Yellow"));
                     juce::Logger::writeToLog("[MovementDetector] Zone " + zoneName + " result: hit=" + (hit ? "TRUE" : "FALSE"));
                 }
                 
                 result.zoneHits[colorIdx] = hit;
             }
        }
        // If motionAmount <= 0.001, zones remain false (already initialized)
        
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

// Serialize zone rectangles to string: "x1,y1,w1,h1;x2,y2,w2,h2;..."
juce::String MovementDetectorModule::serializeZoneRects(const std::vector<ZoneRect>& rects)
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
std::vector<MovementDetectorModule::ZoneRect> MovementDetectorModule::deserializeZoneRects(const juce::String& data)
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
void MovementDetectorModule::loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const
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
void MovementDetectorModule::saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects)
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::String data = serializeZoneRects(rects);
    apvts.state.setProperty(key, data, nullptr);
}

std::vector<DynamicPinInfo> MovementDetectorModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (8 channels: 4 detection + 4 zone gates)
    // Bus 1: Video Out (1 channel)
    const int cvOutChannels = 8;
    const int videoOutStartChannel = cvOutChannels;

    return {
        { "X", 0, PinDataType::CV },
        { "Y", 1, PinDataType::CV },
        { "Amount", 2, PinDataType::CV },
        { "Gate", 3, PinDataType::Gate },
        { "Red Zone Gate", 4, PinDataType::Gate },
        { "Green Zone Gate", 5, PinDataType::Gate },
        { "Blue Zone Gate", 6, PinDataType::Gate },
        { "Yellow Zone Gate", 7, PinDataType::Gate },
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
    // Read ALL available results and keep only the latest (like ContourDetector)
    while (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
        {
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
        }
    }
    
    // Write results to output channels (bus 0 - CV Out)
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    if (cvOutBus.getNumChannels() < 8) return;
    
    // Output detection channels (0-3)
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
    
    // Output zone gates (channels 4-7)
    static int logCounterGates = 0;
    logCounterGates++;
    bool shouldLogGates = (logCounterGates % 100 == 0); // Log every 100 audio blocks (~2 seconds at 48kHz)
    
    if (shouldLogGates)
    {
        juce::Logger::writeToLog("[MovementDetector] === Gate Output (before) ===");
        juce::Logger::writeToLog("[MovementDetector] FIFO ready: " + juce::String(fifo.getNumReady()));
        juce::String zoneHitsStr = "[MovementDetector] lastResult.zoneHits: R=";
        zoneHitsStr += (lastResultForAudio.zoneHits[0] ? "T" : "F");
        zoneHitsStr += ", G=";
        zoneHitsStr += (lastResultForAudio.zoneHits[1] ? "T" : "F");
        zoneHitsStr += ", B=";
        zoneHitsStr += (lastResultForAudio.zoneHits[2] ? "T" : "F");
        zoneHitsStr += ", Y=";
        zoneHitsStr += (lastResultForAudio.zoneHits[3] ? "T" : "F");
        juce::Logger::writeToLog(zoneHitsStr);
    }
    
    for (int z = 0; z < 4; ++z)
    {
        int ch = 4 + z;
        if (ch < cvOutBus.getNumChannels())
        {
            float gateValue = lastResultForAudio.zoneHits[z] ? 1.0f : 0.0f;
            
            // Fill entire buffer with same value (gates should be steady-state)
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            {
                cvOutBus.setSample(ch, s, gateValue);
            }
            
            if (shouldLogGates)
            {
                juce::String zoneName = z == 0 ? "Red" : (z == 1 ? "Green" : (z == 2 ? "Blue" : "Yellow"));
                juce::Logger::writeToLog("[MovementDetector] Gate " + zoneName + " (ch " + juce::String(ch) + "): " + 
                                         juce::String(gateValue, 1) + " (zoneHit=" + (lastResultForAudio.zoneHits[z] ? "TRUE" : "FALSE") + ")");
            }
        }
    }
    
    if (shouldLogGates)
    {
        juce::Logger::writeToLog("[MovementDetector] === Gate Output Summary ===");
        juce::Logger::writeToLog("[MovementDetector] Red=" + juce::String(lastResultForAudio.zoneHits[0] ? 1.0f : 0.0f, 1) + 
                                 ", Green=" + juce::String(lastResultForAudio.zoneHits[1] ? 1.0f : 0.0f, 1) + 
                                 ", Blue=" + juce::String(lastResultForAudio.zoneHits[2] ? 1.0f : 0.0f, 1) + 
                                 ", Yellow=" + juce::String(lastResultForAudio.zoneHits[3] ? 1.0f : 0.0f, 1));
    }
    
    // Fill rest of buffer (for channels 0-3 only, channels 4-7 are already filled above)
    for (int channel = 0; channel < juce::jmin(4, cvOutBus.getNumChannels()); ++channel)
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
    bool modeMod = isParamModulated("mode");
    if (modeMod) ImGui::BeginDisabled();
    int mode = (int)modeParam->load();
    const char* modes[] = { "Optical Flow", "Background Subtraction" };
    if (ImGui::Combo("Mode", &mode, modes, 2))
    {
        if (!modeMod) *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = mode;
        onModificationEnded();
    }
    // Scroll-edit for mode combo
    if (!modeMod && ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int newMode = juce::jlimit(0, 1, mode + (wheel > 0.0f ? -1 : 1));
            if (newMode != mode)
            {
                *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = newMode;
                onModificationEnded();
            }
        }
    }
    if (modeMod) ImGui::EndDisabled();
    
    // Sensitivity slider
    bool sensitivityMod = isParamModulated("sensitivity");
    float sensitivity = sensitivityMod ? getLiveParamValue("sensitivity", sensitivityParam->load()) : sensitivityParam->load();
    if (sensitivityMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.01f, 1.0f, "%.2f"))
    {
        if (!sensitivityMod) *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("sensitivity")) = sensitivity;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !sensitivityMod)
    {
        onModificationEnded();
    }
    if (!sensitivityMod) adjustParamOnWheel(apvts.getParameter("sensitivity"), "sensitivity", sensitivity);
    if (sensitivityMod) ImGui::EndDisabled();
    
    // Zoom controls (-/+) Small/Normal/Large
    bool zoomMod = isParamModulated("zoomLevel");
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    float buttonWidth = (itemWidth / 2.0f) - 4.0f;
    const bool atMin = (level <= 0);
    const bool atMax = (level >= 2);
    if (zoomMod) ImGui::BeginDisabled();
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
    // Scroll-edit for zoom level
    if (!zoomMod && ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int newLevel = juce::jlimit(0, 2, level + (wheel > 0.0f ? 1 : -1));
            if (newLevel != level)
            {
                if (auto* p = apvts.getParameter("zoomLevel"))
                    p->setValueNotifyingHost((float)newLevel / 2.0f);
                onModificationEnded();
            }
        }
    }
    if (zoomMod) ImGui::EndDisabled();

    // NEW: Algorithm tuning controls
    if (mode == 0) // Optical Flow
    {
        ImGui::Text("Optical Flow Settings");
        if (maxFeaturesParam)
        {
            bool maxFeaturesMod = isParamModulated("maxFeatures");
            int maxF = maxFeaturesParam->get();
            if (maxFeaturesMod) ImGui::BeginDisabled();
            if (ImGui::SliderInt("Max Features", &maxF, 20, 500)) { if (!maxFeaturesMod) *maxFeaturesParam = maxF; }
            if (ImGui::IsItemDeactivatedAfterEdit() && !maxFeaturesMod) onModificationEnded();
            if (!maxFeaturesMod) adjustParamOnWheel(apvts.getParameter("maxFeatures"), "maxFeatures", (float)maxF);
            if (maxFeaturesMod) ImGui::EndDisabled();
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
    int& activeZoneColorIndex = activeZoneColorIndexByNode[nodeId];
    int& drawingZoneIndex = drawingZoneIndexByNode[nodeId];
    float& dragStartX = dragStartXByNode[nodeId];
    float& dragStartY = dragStartYByNode[nodeId];
    
    // Initialize active color index if not set
    if (activeZoneColorIndexByNode.find(nodeId) == activeZoneColorIndexByNode.end())
        activeZoneColorIndex = 0;
    
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
            
            // Mouse interaction - use InvisibleButton's hover state
            if (ImGui::IsItemHovered())
            {
                // Normalize mouse position (0.0-1.0)
                float mouseX = (mousePos.x - imageRectMin.x) / imageSize.x;
                float mouseY = (mousePos.y - imageRectMin.y) / imageSize.y;
                
                // Check if Ctrl key is held
                bool ctrlHeld = ImGui::GetIO().KeyCtrl;
                
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
                
                // Show tooltip for zone drawing hints
                ImGui::BeginTooltip();
                ImGui::TextDisabled("Ctrl+Left-drag: Draw zone\nRight-drag: Erase zone");
                ImGui::EndTooltip();
            }
        }
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
