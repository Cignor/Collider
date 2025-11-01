#include "HumanDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout HumanDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Detection Mode", juce::StringArray{"Faces (Haar)", "Bodies (HOG)"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("scaleFactor", "Scale Factor", 1.05f, 2.0f, 1.1f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("minNeighbors", "Min Neighbors", 1, 10, 3));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    return { params.begin(), params.end() };
}

HumanDetectorModule::HumanDetectorModule()
    : ModuleProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(5), true)
                     .withOutput("Video Out", juce::AudioChannelSet::mono(), true)    // PASSTHROUGH
                     .withOutput("Cropped Out", juce::AudioChannelSet::mono(), true)), // CROPPED
      juce::Thread("Human Detector Analysis Thread"),
      apvts(*this, nullptr, "HumanParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    scaleFactorParam = apvts.getRawParameterValue("scaleFactor");
    minNeighborsParam = apvts.getRawParameterValue("minNeighbors");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));

    // Load Haar Cascade (CPU)
    // Try multiple possible locations for the cascade file
    juce::File exeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File appDir = exeFile.getParentDirectory();
    
    juce::File cascadeFile = appDir.getChildFile("haarcascade_frontalface_default.xml");
    if (!cascadeFile.existsAsFile())
    {
        // Try in assets directory
        cascadeFile = appDir.getChildFile("assets").getChildFile("haarcascade_frontalface_default.xml");
    }
    if (!cascadeFile.existsAsFile())
    {
        // Try next to executable (getSiblingFile)
        cascadeFile = exeFile.getSiblingFile("haarcascade_frontalface_default.xml");
    }
    
    if (cascadeFile.existsAsFile())
    {
        faceCascadeLoaded = faceCascade.load(cascadeFile.getFullPathName().toStdString());
        if (faceCascadeLoaded)
        {
            juce::Logger::writeToLog("[HumanDetector] Loaded cascade: " + cascadeFile.getFileName());
            
            #if WITH_CUDA_SUPPORT
                try
                {
                    // Load GPU cascade
                    faceCascadeGpu = cv::cuda::CascadeClassifier::create(cascadeFile.getFullPathName().toStdString());
                    if (faceCascadeGpu)
                    {
                        juce::Logger::writeToLog("[HumanDetector] GPU cascade loaded successfully");
                    }
                }
                catch (const cv::Exception& e)
                {
                    juce::Logger::writeToLog("[HumanDetector] WARNING: Failed to load GPU cascade: " + juce::String(e.what()));
                    faceCascadeGpu.release();
                }
            #endif
        }
        else
        {
            juce::Logger::writeToLog("[HumanDetector] ERROR: Failed to load cascade file: " + cascadeFile.getFullPathName());
        }
    }
    else
    {
        juce::Logger::writeToLog("[HumanDetector] WARNING: haarcascade_frontalface_default.xml not found in: " + appDir.getFullPathName());
        juce::Logger::writeToLog("[HumanDetector] Face detection will be disabled. Body detection (HOG) will still work.");
    }
    
    // Set up HOG detector (CPU)
    hog.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
    
    #if WITH_CUDA_SUPPORT
        // Set up HOG detector (GPU)
        hogGpu = cv::cuda::HOG::create();
        hogGpu->setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
    #endif
    
    fifoBuffer.resize(16);
    fifo.setTotalSize(16);
}

HumanDetectorModule::~HumanDetectorModule()
{
    stopThread(5000);
}

void HumanDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void HumanDetectorModule::releaseResources()
{
    signalThreadShouldExit();
}

void HumanDetectorModule::run()
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
            DetectionResult result = analyzeFrame(frame);
            
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

DetectionResult HumanDetectorModule::analyzeFrame(const cv::Mat& inputFrame)
{
    DetectionResult result;
    cv::Mat gray, displayFrame;
    
    // Make a copy for cropping before any resizing
    cv::Mat originalFrameForCrop;
    inputFrame.copyTo(originalFrameForCrop);
    
    inputFrame.copyTo(displayFrame);
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);

    std::vector<cv::Rect> detections;
    
    // Safety checks for null parameters
    if (!modeParam || !scaleFactorParam || !minNeighborsParam)
    {
        cv::resize(displayFrame, displayFrame, cv::Size(320, 240));
        updateGuiFrame(displayFrame);
        return result; // Return empty result if parameters not initialized
    }
    
    int mode = (int)modeParam->load();
    bool useGpu = false;
    
    #if WITH_CUDA_SUPPORT
        useGpu = useGpuParam ? (useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0)) : false;
    #endif

    if (mode == 0) // Face Detection
    {
        // Face detection works better on smaller images - resize for speed
        cv::Mat graySmall, displaySmall;
        cv::resize(gray, graySmall, cv::Size(320, 240));
        cv::resize(displayFrame, displaySmall, cv::Size(320, 240));
        
        // Check if face cascade is loaded before using it
        if (faceCascadeLoaded || faceCascadeGpu)
        {
            #if WITH_CUDA_SUPPORT
                if (useGpu && faceCascadeGpu)
                {
                    cv::cuda::GpuMat grayGpu;
                    grayGpu.upload(graySmall);
                    
                    cv::cuda::GpuMat detectionsGpu;
                    faceCascadeGpu->detectMultiScale(grayGpu, detectionsGpu);
                    
                    // Convert detections to std::vector<cv::Rect>
                    faceCascadeGpu->convert(detectionsGpu, detections);
                }
                else
            #endif
            {
                // Only use CPU cascade if it's actually loaded
                if (faceCascadeLoaded)
                {
                    faceCascade.detectMultiScale(graySmall, detections, scaleFactorParam->load(), (int)minNeighborsParam->load());
                }
            }
        }
        
        displayFrame = displaySmall;
    }
    else // Body Detection (HOG)
    {
        // HOG needs larger images to work properly - use at least 640x480
        // Resize input to a reasonable size for HOG (larger = more accurate but slower)
        cv::Size hogSize = cv::Size(640, 480);
        cv::Mat grayHog;
        cv::resize(gray, grayHog, hogSize);
        
        // Scale factor to map detections back to display size
        float scaleX = (float)displayFrame.cols / 640.0f;
        float scaleY = (float)displayFrame.rows / 480.0f;
        
        #if WITH_CUDA_SUPPORT
            if (useGpu && hogGpu)
            {
                try
                {
                    cv::cuda::GpuMat grayGpu;
                    grayGpu.upload(grayHog);
                    
                    std::vector<cv::Rect> foundLocations;
                    std::vector<double> confidences;
                    
                    // CUDA HOG detectMultiScale has different signature - use minimal parameters
                    // Basic signature: detectMultiScale(img, foundLocations, confidences)
                    hogGpu->detectMultiScale(grayGpu, foundLocations, &confidences);
                    
                    detections = foundLocations;
                    
                    // Scale detections back to display frame size
                    for (auto& det : detections)
                    {
                        det.x = (int)(det.x * scaleX);
                        det.y = (int)(det.y * scaleY);
                        det.width = (int)(det.width * scaleX);
                        det.height = (int)(det.height * scaleY);
                    }
                }
                catch (const cv::Exception& e)
                {
                    juce::Logger::writeToLog("[HumanDetector] HOG GPU detection error: " + juce::String(e.what()));
                    detections.clear();
                }
            }
            else
        #endif
        {
            try
            {
                // CPU HOG with optimized parameters to prevent freezing
                std::vector<double> weights;
                hog.detectMultiScale(grayHog, detections, weights,
                                    0.0,                    // hitThreshold (0 = default)
                                    cv::Size(8, 8),         // winStride (larger stride = faster, less accurate)
                                    cv::Size(32, 32),       // padding
                                    1.05,                   // scale (smaller = faster, checks fewer scales)
                                    2.0,                    // finalThreshold (group similar detections)
                                    false);                 // useMeanshiftGrouping (false = faster)
                
                // Scale detections back to display frame size
                for (auto& det : detections)
                {
                    det.x = (int)(det.x * scaleX);
                    det.y = (int)(det.y * scaleY);
                    det.width = (int)(det.width * scaleX);
                    det.height = (int)(det.height * scaleY);
                }
            }
            catch (const cv::Exception& e)
            {
                juce::Logger::writeToLog("[HumanDetector] HOG CPU detection error: " + juce::String(e.what()));
                detections.clear();
            }
        }
        
        // Resize display frame for UI (smaller for preview)
        cv::resize(displayFrame, displayFrame, cv::Size(320, 240));
    }

    // Draw all detections (smaller ones in gray)
    for (const auto& detection : detections)
    {
        cv::rectangle(displayFrame, detection, cv::Scalar(128, 128, 128), 1);
    }

    if (!detections.empty())
    {
        // Find the largest detection
        auto largest = std::max_element(detections.begin(), detections.end(), 
            [](const cv::Rect& a, const cv::Rect& b) {
                return a.area() < b.area();
            });

        // Draw the largest detection
        cv::rectangle(displayFrame, *largest, cv::Scalar(0, 255, 0), 2);
        
        std::string label = (mode == 0) ? "Face" : "Person";
        cv::putText(displayFrame, label, cv::Point(largest->x, largest->y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);

        result.numDetections = 1;
        // Normalize coordinates to [0,1] range based on actual display frame size
        result.x[0] = juce::jmap((float)largest->x, 0.0f, (float)displayFrame.cols, 0.0f, 1.0f);
        result.y[0] = juce::jmap((float)largest->y, 0.0f, (float)displayFrame.rows, 0.0f, 1.0f);
        result.width[0] = juce::jmap((float)largest->width, 0.0f, (float)displayFrame.cols, 0.0f, 1.0f);
        result.height[0] = juce::jmap((float)largest->height, 0.0f, (float)displayFrame.rows, 0.0f, 1.0f);
        
        // --- CROPPED OUTPUT LOGIC ---
        // Use original frame and scale the ROI if necessary
        cv::Rect originalRoi = *largest;
        if (displayFrame.cols != originalFrameForCrop.cols || displayFrame.rows != originalFrameForCrop.rows)
        {
            float scaleX = (float)originalFrameForCrop.cols / (float)displayFrame.cols;
            float scaleY = (float)originalFrameForCrop.rows / (float)displayFrame.rows;
            originalRoi.x = (int)(originalRoi.x * scaleX);
            originalRoi.y = (int)(originalRoi.y * scaleY);
            originalRoi.width = (int)(originalRoi.width * scaleX);
            originalRoi.height = (int)(originalRoi.height * scaleY);
        }
        
        originalRoi &= cv::Rect(0, 0, originalFrameForCrop.cols, originalFrameForCrop.rows);
        if (originalRoi.area() > 0)
        {
            cv::Mat cropped = originalFrameForCrop(originalRoi);
            VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), cropped);
        }
    }
    else
    {
        // Clear cropped output when no detection
        cv::Mat emptyFrame;
        VideoFrameManager::getInstance().setFrame(getSecondaryLogicalId(), emptyFrame);
    }

    // --- PASSTHROUGH LOGIC ---
    VideoFrameManager::getInstance().setFrame(getLogicalId(), displayFrame);
    updateGuiFrame(displayFrame);
    return result;
}

void HumanDetectorModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image HumanDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void HumanDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    if (cvOutBus.getNumChannels() < 5) return;
    
    if (lastResultForAudio.numDetections > 0)
    {
        cvOutBus.setSample(0, 0, lastResultForAudio.x[0]);
        cvOutBus.setSample(1, 0, lastResultForAudio.y[0]);
        cvOutBus.setSample(2, 0, lastResultForAudio.width[0]);
        cvOutBus.setSample(3, 0, lastResultForAudio.height[0]);
        gateSamplesRemaining = 2; // Keep gate high
    }

    if (gateSamplesRemaining > 0)
    {
        cvOutBus.setSample(4, 0, 1.0f);
        gateSamplesRemaining--;
    }
    else
    {
        cvOutBus.setSample(4, 0, 0.0f);
    }
    
    // Fill rest of buffer
    for (int channel = 0; channel < 5; ++channel)
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
void HumanDetectorModule::drawParametersInNode(float itemWidth,
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
        
        bool useGpu = useGpuParam ? useGpuParam->get() : false;
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
            ImGui::SetTooltip("Enable GPU acceleration for human detection.\nRequires CUDA-capable NVIDIA GPU.");
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
    int mode = modeParam ? (int)modeParam->load() : 0;
    const char* modes[] = { "Faces (Haar)", "Bodies (HOG)" };
    if (ImGui::Combo("Mode", &mode, modes, 2))
    {
        *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = mode;
        onModificationEnded();
    }
    
    // Conditional parameters for Haar mode
    if (mode == 0)
    {
        if (scaleFactorParam)
        {
            float scaleFactor = scaleFactorParam->load();
            if (ImGui::SliderFloat("Scale Factor", &scaleFactor, 1.05f, 2.0f, "%.2f"))
            {
                *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("scaleFactor")) = scaleFactor;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                onModificationEnded();
            }
        }
        
        if (minNeighborsParam)
        {
            int minNeighbors = (int)minNeighborsParam->load();
            if (ImGui::SliderInt("Min Neighbors", &minNeighbors, 1, 10))
            {
                *dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("minNeighbors")) = minNeighbors;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                onModificationEnded();
            }
        }
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

void HumanDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("X", 0);
    helpers.drawAudioOutputPin("Y", 1);
    helpers.drawAudioOutputPin("Width", 2);
    helpers.drawAudioOutputPin("Height", 3);
    helpers.drawAudioOutputPin("Gate", 4);
    helpers.drawAudioOutputPin("Video Out", 0);     // Bus 1
    helpers.drawAudioOutputPin("Cropped Out", 1); // Bus 2
}
#endif
