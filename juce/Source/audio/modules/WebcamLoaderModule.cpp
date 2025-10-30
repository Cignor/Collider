#include "WebcamLoaderModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout WebcamLoaderModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterInt>("cameraIndex", "Camera Index", 0, 3, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>("isZoomed", "Zoom", false));
    
    return { params.begin(), params.end() };
}

WebcamLoaderModule::WebcamLoaderModule()
    : ModuleProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      juce::Thread("Webcam Loader Thread"),
      apvts(*this, nullptr, "WebcamLoaderParams", createParameterLayout())
{
    cameraIndexParam = apvts.getRawParameterValue("cameraIndex");
    isZoomedParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("isZoomed"));
    
    // Enumerate available cameras with resolution info
    for (int i = 0; i < 10; ++i) // Check first 10 camera indices
    {
        cv::VideoCapture tempCap(i);
        if (tempCap.isOpened())
        {
            int width = (int)tempCap.get(cv::CAP_PROP_FRAME_WIDTH);
            int height = (int)tempCap.get(cv::CAP_PROP_FRAME_HEIGHT);
            
            juce::String cameraName = "Camera " + juce::String(i);
            if (width > 0 && height > 0)
            {
                cameraName += " (" + juce::String(width) + "x" + juce::String(height) + ")";
            }
            
            availableCameraNames.add(cameraName);
            tempCap.release();
        }
    }
    
    if (availableCameraNames.isEmpty())
    {
        availableCameraNames.add("No cameras found");
    }
    
    juce::Logger::writeToLog("[WebcamLoader] Found " + juce::String(availableCameraNames.size()) + " camera(s)");
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
    // Return different width based on zoom state
    if (isZoomedParam && isZoomedParam->get())
    {
        return ImVec2(960.0f, 0.0f); // Doubled width when zoomed
    }
    return ImVec2(480.0f, 0.0f); // Normal width
}

void WebcamLoaderModule::drawParametersInNode(float itemWidth,
                                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                              const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // Camera selection with real device names
    int currentIndex = (int)cameraIndexParam->load();
    currentIndex = juce::jlimit(0, juce::jmax(0, availableCameraNames.size() - 1), currentIndex);
    
    const char* currentCameraName = availableCameraNames[currentIndex].toRawUTF8();
    if (ImGui::BeginCombo("Camera", currentCameraName))
    {
        for (int i = 0; i < availableCameraNames.size(); ++i)
        {
            const bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(availableCameraNames[i].toRawUTF8(), isSelected))
            {
                *dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("cameraIndex")) = i;
                onModificationEnded();
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    
    ImGui::Separator();
    
    // Zoom buttons (+ to zoom in, - to zoom out)
    bool isZoomed = isZoomedParam->get();
    float buttonWidth = (itemWidth / 2.0f) - 4.0f;
    
    // '-' button (zoom out) - disabled when already normal size
    if (!isZoomed) ImGui::BeginDisabled();
    if (ImGui::Button("-", ImVec2(buttonWidth, 0)))
    {
        *isZoomedParam = false;
        onModificationEnded();
    }
    if (!isZoomed) ImGui::EndDisabled();
    
    ImGui::SameLine();
    
    // '+' button (zoom in) - disabled when already zoomed
    if (isZoomed) ImGui::BeginDisabled();
    if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
    {
        *isZoomedParam = true;
        onModificationEnded();
    }
    if (isZoomed) ImGui::EndDisabled();
    
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID: %d", (int)getLogicalId());
    
    ImGui::PopItemWidth();
}

void WebcamLoaderModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Source ID", 0);
}
#endif

