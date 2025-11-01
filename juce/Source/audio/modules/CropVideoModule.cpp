#include "CropVideoModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout CropVideoModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("padding", "Padding", 0.0f, 2.0f, 0.1f)); // 10% padding default
    params.push_back(std::make_unique<juce::AudioParameterChoice>("aspectRatio", "Aspect Ratio", juce::StringArray{ "Stretch", "Preserve (Fit)" }, 1));
    
    // Manual crop controls (normalized 0-1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropX", "Center X", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropY", "Center Y", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropW", "Width", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cropH", "Height", 0.0f, 1.0f, 0.5f));
    
    return { params.begin(), params.end() };
}

CropVideoModule::CropVideoModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("CV In", juce::AudioChannelSet::discreteChannels(5), true) // SourceID, X, Y, W, H
                      .withOutput("Output ID", juce::AudioChannelSet::mono(), true)),
      juce::Thread("CropVideo Thread"),
      apvts(*this, nullptr, "CropVideoParams", createParameterLayout())
{
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    paddingParam = apvts.getRawParameterValue("padding");
    aspectRatioModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("aspectRatio"));
    cropXParam = apvts.getRawParameterValue("cropX");
    cropYParam = apvts.getRawParameterValue("cropY");
    cropWParam = apvts.getRawParameterValue("cropW");
    cropHParam = apvts.getRawParameterValue("cropH");
}

CropVideoModule::~CropVideoModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void CropVideoModule::prepareToPlay(double, int) { startThread(); }

void CropVideoModule::releaseResources() { signalThreadShouldExit(); stopThread(5000); }

void CropVideoModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }

        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(33); continue; }

        // Get crop values: use CV if connected (>= 0), otherwise use parameters
        float cx = (cvCropX.load() >= 0.0f) ? cvCropX.load() : (cropXParam ? cropXParam->load() : 0.5f);
        float cy = (cvCropY.load() >= 0.0f) ? cvCropY.load() : (cropYParam ? cropYParam->load() : 0.5f);
        float w = (cvCropW.load() >= 0.0f) ? cvCropW.load() : (cropWParam ? cropWParam->load() : 0.5f);
        float h = (cvCropH.load() >= 0.0f) ? cvCropH.load() : (cropHParam ? cropHParam->load() : 0.5f);

        // Convert normalized CV to pixel coordinates
        int frameW = frame.cols;
        int frameH = frame.rows;
        int pixelW = static_cast<int>(w * frameW);
        int pixelH = static_cast<int>(h * frameH);
        int pixelX = static_cast<int>(cx * frameW - pixelW / 2.0f);
        int pixelY = static_cast<int>(cy * frameH - pixelH / 2.0f);
        
        // Apply padding
        float pad = paddingParam ? paddingParam->load() : 0.0f;
        int padX = static_cast<int>(pixelW * pad);
        int padY = static_cast<int>(pixelH * pad);
        pixelX -= padX;
        pixelY -= padY;
        pixelW += (padX * 2);
        pixelH += (padY * 2);

        // Define the Region of Interest (ROI)
        cv::Rect roi(pixelX, pixelY, pixelW, pixelH);

        // Ensure ROI is within the frame boundaries
        roi &= cv::Rect(0, 0, frame.cols, frame.rows);
        
        if (roi.area() > 0)
        {
            cv::Mat croppedFrame = frame(roi);

            // Handle aspect ratio preservation if enabled
            if (aspectRatioModeParam && aspectRatioModeParam->getIndex() == 1) // "Preserve (Fit)"
            {
                // For now, we just crop - aspect ratio preservation would require resizing
                // which could be added as a future enhancement
            }

            // Publish the new cropped frame under this module's own ID
            VideoFrameManager::getInstance().setFrame(getLogicalId(), croppedFrame);
            updateGuiFrame(croppedFrame);
        }

        wait(33); // ~30 FPS
    }
}

void CropVideoModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Read CV inputs from the audio thread
    auto inputBus = getBusBuffer(buffer, true, 0);
    if (inputBus.getNumSamples() > 0)
    {
        if (inputBus.getNumChannels() > 0) 
        {
            float sourceIdVal = inputBus.getSample(0, 0);
            currentSourceId.store(static_cast<juce::uint32>(sourceIdVal));
        }
        
        // Read CV inputs: CV values override parameters when connected
        // We detect connection by checking if channel exists AND value is in valid range
        if (inputBus.getNumChannels() > 1)
        {
            float val = inputBus.getSample(1, 0);
            // Accept any value in reasonable range (including 0.0)
            if (val >= -10.0f && val <= 10.0f) // Allow some headroom for normalized values
                cvCropX.store(juce::jlimit(0.0f, 1.0f, val));
            else
                cvCropX.store(-1.0f);
        }
        else cvCropX.store(-1.0f);
        
        if (inputBus.getNumChannels() > 2)
        {
            float val = inputBus.getSample(2, 0);
            if (val >= -10.0f && val <= 10.0f)
                cvCropY.store(juce::jlimit(0.0f, 1.0f, val));
            else
                cvCropY.store(-1.0f);
        }
        else cvCropY.store(-1.0f);
        
        if (inputBus.getNumChannels() > 3)
        {
            float val = inputBus.getSample(3, 0);
            if (val >= -10.0f && val <= 10.0f)
                cvCropW.store(juce::jlimit(0.0f, 1.0f, val));
            else
                cvCropW.store(-1.0f);
        }
        else cvCropW.store(-1.0f);
        
        if (inputBus.getNumChannels() > 4)
        {
            float val = inputBus.getSample(4, 0);
            if (val >= -10.0f && val <= 10.0f)
                cvCropH.store(juce::jlimit(0.0f, 1.0f, val));
            else
                cvCropH.store(-1.0f);
        }
        else cvCropH.store(-1.0f);
    }
    else
    {
        // No input, clear CV flags
        cvCropX.store(-1.0f);
        cvCropY.store(-1.0f);
        cvCropW.store(-1.0f);
        cvCropH.store(-1.0f);
    }
    
    buffer.clear();
    
    // Output our own Logical ID, so we can be chained
    auto outputBus = getBusBuffer(buffer, false, 0);
    if (outputBus.getNumChannels() > 0)
    {
        float logicalId = static_cast<float>(getLogicalId());
        for (int s = 0; s < outputBus.getNumSamples(); ++s)
            outputBus.setSample(0, s, logicalId);
    }
}

void CropVideoModule::updateGuiFrame(const cv::Mat& frame)
{
    if (frame.empty()) return;
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgraFrame.cols || latestFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image CropVideoModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

#if defined(PRESET_CREATOR_UI)
ImVec2 CropVideoModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int)zoomLevelParam->load() : 1;
    const float widths[] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[juce::jlimit(0, 2, level)], 0.0f);
}

void CropVideoModule::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    ImGui::Text("Crop Controls:");
    
    // Center X - Always enabled, shows current parameter value
    float cropX = cropXParam ? cropXParam->load() : 0.5f;
    if (ImGui::SliderFloat("Center X", &cropX, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropX")))
        {
            *p = cropX;
            onModificationEnded();
        }
    }
    
    // Center Y - Always enabled
    float cropY = cropYParam ? cropYParam->load() : 0.5f;
    if (ImGui::SliderFloat("Center Y", &cropY, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropY")))
        {
            *p = cropY;
            onModificationEnded();
        }
    }
    
    // Width - Always enabled
    float cropW = cropWParam ? cropWParam->load() : 0.5f;
    if (ImGui::SliderFloat("Width", &cropW, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropW")))
        {
            *p = cropW;
            onModificationEnded();
        }
    }
    
    // Height - Always enabled
    float cropH = cropHParam ? cropHParam->load() : 0.5f;
    if (ImGui::SliderFloat("Height", &cropH, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropH")))
        {
            *p = cropH;
            onModificationEnded();
        }
    }
    
    ImGui::Separator();
    
    float padding = paddingParam ? paddingParam->load() : 0.1f;
    if (ImGui::SliderFloat("Padding", &padding, 0.0f, 2.0f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("padding")))
        {
            *p = padding;
            onModificationEnded();
        }
    }
    
    if (aspectRatioModeParam)
    {
        int mode = aspectRatioModeParam->getIndex();
        const char* items[] = { "Stretch", "Preserve (Fit)" };
        if (ImGui::Combo("Aspect Ratio", &mode, items, 2))
        {
            *aspectRatioModeParam = mode;
            onModificationEnded();
        }
    }
    
    ImGui::PopItemWidth();
    
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output ID: %d", (int)getLogicalId());
}

void CropVideoModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioInputPin("Center X", 1);
    helpers.drawAudioInputPin("Center Y", 2);
    helpers.drawAudioInputPin("Width", 3);
    helpers.drawAudioInputPin("Height", 4);
    helpers.drawAudioOutputPin("Output ID", 0);
}
#endif

