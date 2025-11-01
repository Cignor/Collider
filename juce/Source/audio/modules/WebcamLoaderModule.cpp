#include "WebcamLoaderModule.h"
#include "../../video/VideoFrameManager.h"
#include "../../video/CameraEnumerator.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout WebcamLoaderModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterInt>("cameraIndex", "Camera Index", 0, 3, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    return { params.begin(), params.end() };
}

WebcamLoaderModule::WebcamLoaderModule()
    : ModuleProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      juce::Thread("Webcam Loader Thread"),
      apvts(*this, nullptr, "WebcamLoaderParams", createParameterLayout())
{
    cameraIndexParam = apvts.getRawParameterValue("cameraIndex");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    
    // Camera enumeration is now done by CameraEnumerator singleton on a background thread
    // This constructor is now instant!
}

WebcamLoaderModule::~WebcamLoaderModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void WebcamLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void WebcamLoaderModule::releaseResources()
{
    signalThreadShouldExit();
}

void WebcamLoaderModule::run()
{
    int currentCameraIndex = -1;
    
    while (!threadShouldExit())
    {
        int requestedIndex = (int)cameraIndexParam->load();
        
        // If camera changed or is not open, try to open it
        if (requestedIndex != currentCameraIndex || !videoCapture.isOpened())
        {
            if (videoCapture.isOpened())
            {
                videoCapture.release();
            }
            
            if (videoCapture.open(requestedIndex))
            {
                currentCameraIndex = requestedIndex;
                juce::Logger::writeToLog("[WebcamLoader] Opened camera " + juce::String(requestedIndex));
            }
            else
            {
                wait(500);
                continue;
            }
        }
        
        cv::Mat frame;
        if (videoCapture.read(frame))
        {
            // Publish frame to central manager using this module's logical ID
            VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
            
            // Update local preview for UI
            updateGuiFrame(frame);
        }
        else
        {
            // Lost camera connection
            videoCapture.release();
            currentCameraIndex = -1;
        }
        
        wait(33); // ~30 FPS
    }
    
    videoCapture.release();
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void WebcamLoaderModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image WebcamLoaderModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void WebcamLoaderModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    
    // Output this module's logical ID on the "Source ID" pin
    if (buffer.getNumChannels() > 0)
    {
        float sourceId = (float)getLogicalId();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample(0, sample, sourceId);
        }
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 WebcamLoaderModule::getCustomNodeSize() const
{
    // Return different width based on zoom level (0=240,1=480,2=960)
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void WebcamLoaderModule::drawParametersInNode(float itemWidth,
                                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                              const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // Get the latest list from the fast, cached singleton
    auto availableCameraNames = CameraEnumerator::getInstance().getAvailableCameraNames();
    
    // Add a refresh button to re-scan for cameras if needed
    if (ImGui::Button("Refresh List"))
    {
        CameraEnumerator::getInstance().rescan();
    }
    ImGui::SameLine();
    
    int currentIndex = (int)cameraIndexParam->load();
    currentIndex = juce::jlimit(0, juce::jmax(0, availableCameraNames.size() - 1), currentIndex);
    
    const char* currentCameraName = availableCameraNames[currentIndex].toRawUTF8();
    
    // Check if we're in a scanning state or no cameras found
    bool isScanning = (availableCameraNames.size() == 1 && availableCameraNames[0].startsWith("Scanning"));
    bool noCameras = (availableCameraNames.size() == 1 && availableCameraNames[0].startsWith("No cameras"));
    
    if (isScanning || noCameras)
    {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::BeginCombo("Camera", currentCameraName))
    {
        for (int i = 0; i < availableCameraNames.size(); ++i)
        {
            const bool isSelected = (currentIndex == i);
            const juce::String& cameraName = availableCameraNames[i];
            
            // Don't allow selecting "Scanning..." or "No cameras found"
            bool isSelectable = !cameraName.startsWith("Scanning") && !cameraName.startsWith("No cameras");
            
            if (!isSelectable)
            {
                ImGui::BeginDisabled();
            }
            
            if (ImGui::Selectable(cameraName.toRawUTF8(), isSelected))
            {
                *dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("cameraIndex")) = i;
                onModificationEnded();
            }
            
            if (!isSelectable)
            {
                ImGui::EndDisabled();
            }
            
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    
    if (isScanning || noCameras)
    {
        ImGui::EndDisabled();
    }
    
    ImGui::Separator();
    
    // Zoom buttons (+ to increase, - to decrease) across 3 levels
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
    
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID: %d", (int)getLogicalId());
    
    ImGui::PopItemWidth();
}

void WebcamLoaderModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Source ID", 0);
}
#endif

