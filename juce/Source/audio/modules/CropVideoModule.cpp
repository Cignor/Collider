#include "CropVideoModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <map>
#include <unordered_map>
#include <algorithm>

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
    
    // Tracker box controls (relative to crop box, normalized 0-1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trackerX", "Tracker X", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trackerY", "Tracker Y", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trackerW", "Tracker Width", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trackerH", "Tracker Height", 0.0f, 1.0f, 0.5f));
    
    return { params.begin(), params.end() };
}

CropVideoModule::CropVideoModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Source In", juce::AudioChannelSet::mono(), true) // Bus 0 for Video Source ID
                      .withInput("Modulation In", juce::AudioChannelSet::discreteChannels(4), true) // Bus 1 for CV modulation
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

bool CropVideoModule::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 1; // Modulation inputs are on bus 1
    if (paramId == "cropX_mod") { outChannelIndexInBus = 0; return true; }
    if (paramId == "cropY_mod") { outChannelIndexInBus = 1; return true; }
    if (paramId == "cropW_mod") { outChannelIndexInBus = 2; return true; }
    if (paramId == "cropH_mod") { outChannelIndexInBus = 3; return true; }
    return false;
}

void CropVideoModule::run()
{
    juce::Logger::writeToLog("[CropVideo][Thread] Video processing thread started.");
    
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }

        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(33); continue; }

        // Cache the full, uncropped input frame for the UI and tracker initialization
        {
            const juce::ScopedLock lock(trackerLock);
            frame.copyTo(lastFrameForTracker);
        }
        updateInputGuiFrame(frame);

        // Tracking Logic
        if (trackingActive.load())
        {
            const juce::ScopedLock lock(trackerLock);
            if (tracker)
            {
                juce::Logger::writeToLog("[CropVideo][Thread] Updating tracker...");
                cv::Rect trackerBox;
                bool ok = tracker->update(frame, trackerBox);

                if (ok)
                {
                    juce::Logger::writeToLog("[CropVideo][Thread] Tracker update SUCCESS.");
                    // Tracker follows the small tracker box, but we need to move the entire crop area
                    // Calculate where the tracker box center is in absolute coordinates
                    float trackerBoxCenterX = ((float)trackerBox.x + (float)trackerBox.width / 2.0f) / (float)frame.cols;
                    float trackerBoxCenterY = ((float)trackerBox.y + (float)trackerBox.height / 2.0f) / (float)frame.rows;
                    
                    // Get current tracker box position relative to crop box
                    float trackXRel = apvts.getRawParameterValue("trackerX")->load();
                    float trackYRel = apvts.getRawParameterValue("trackerY")->load();
                    
                    // Calculate the offset between tracker box and crop box center
                    float cropW = cropWParam->load();
                    float cropH = cropHParam->load();
                    float trackBoxRelCenterX = trackXRel + 0.5f * apvts.getRawParameterValue("trackerW")->load();
                    float trackBoxRelCenterY = trackYRel + 0.5f * apvts.getRawParameterValue("trackerH")->load();
                    
                    float offsetX = (trackBoxRelCenterX - 0.5f) * cropW;
                    float offsetY = (trackBoxRelCenterY - 0.5f) * cropH;
                    
                    // Adjust crop center to keep tracker box in same relative position
                    *cropXParam = trackerBoxCenterX - offsetX;
                    *cropYParam = trackerBoxCenterY - offsetY;
                }
                else
                {
                    juce::Logger::writeToLog("[CropVideo][Thread] Tracker update FAILED. Disengaging tracker.");
                    // Tracking failed, disengage
                    trackingActive.store(false);
                    tracker.release();
                }
            }
            else
            {
                juce::Logger::writeToLog("[CropVideo][Thread] WARNING: trackingActive is true, but tracker object is null!");
                trackingActive.store(false);
            }
        }

        // Get final crop values (these are already modulated by the audio thread if connected)
        float cx = cropXParam ? cropXParam->load() : 0.5f;
        float cy = cropYParam ? cropYParam->load() : 0.5f;
        float w = cropWParam ? cropWParam->load() : 0.5f;
        float h = cropHParam ? cropHParam->load() : 0.5f;
        
        int frameW = frame.cols;
        int frameH = frame.rows;
        int pixelW = static_cast<int>(w * frameW);
        int pixelH = static_cast<int>(h * frameH);
        int pixelX = static_cast<int>(cx * frameW - pixelW / 2.0f);
        int pixelY = static_cast<int>(cy * frameH - pixelH / 2.0f);
        
        float pad = paddingParam ? paddingParam->load() : 0.1f;
        int padX = static_cast<int>(pixelW * pad);
        int padY = static_cast<int>(pixelH * pad);
        pixelX -= padX;
        pixelY -= padY;
        pixelW += (padX * 2);
        pixelH += (padY * 2);

        cv::Rect roi(pixelX, pixelY, pixelW, pixelH);
        roi &= cv::Rect(0, 0, frame.cols, frame.rows);
        
        if (roi.area() > 0)
        {
            cv::Mat croppedFrame = frame(roi);
            VideoFrameManager::getInstance().setFrame(getLogicalId(), croppedFrame);
            // This now updates the *output* frame preview (which we don't display in this node)
            updateGuiFrame(croppedFrame);
        }
        else
        {
            // If crop is invalid, publish an empty frame
            VideoFrameManager::getInstance().setFrame(getLogicalId(), cv::Mat());
            updateGuiFrame(cv::Mat());
        }

        wait(33);
    }
    juce::Logger::writeToLog("[CropVideo][Thread] Video processing thread finished.");
}

void CropVideoModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    // Read the Source ID from the dedicated input bus (bus 0)
    auto sourceIdBus = getBusBuffer(buffer, true, 0);
    if (sourceIdBus.getNumChannels() > 0 && sourceIdBus.getNumSamples() > 0)
    {
        currentSourceId.store(static_cast<juce::uint32>(sourceIdBus.getSample(0, 0)));
    }
    else
    {
        currentSourceId.store(0);
    }
    // Note: CV modulation is handled automatically by ModularSynthProcessor
    // because we implemented getParamRouting(). The values in cropXParam etc.
    // are already the final, modulated values.
    buffer.clear();
    // Output our own Logical ID for chaining
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
    const juce::ScopedLock lock(imageLock);
    if (frame.empty())
    {
        latestOutputFrameForGui = juce::Image();
        return;
    }
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    if (latestOutputFrameForGui.isNull() || latestOutputFrameForGui.getWidth() != bgraFrame.cols || latestOutputFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestOutputFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData dest(latestOutputFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

// NEW function to handle the input frame preview
void CropVideoModule::updateInputGuiFrame(const cv::Mat& frame)
{
    const juce::ScopedLock lock(imageLock);
    if (frame.empty())
    {
        latestInputFrameForGui = juce::Image();
        return;
    }
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    if (latestInputFrameForGui.isNull() || latestInputFrameForGui.getWidth() != bgraFrame.cols || latestInputFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestInputFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    juce::Image::BitmapData dest(latestInputFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image CropVideoModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    // This returns the cropped output, useful if another node wants a preview
    return latestOutputFrameForGui.createCopy();
}

#if defined(PRESET_CREATOR_UI)
ImVec2 CropVideoModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int)zoomLevelParam->load() : 1;
    const float widths[] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[juce::jlimit(0, 2, level)], 0.0f);
}

void CropVideoModule::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // State for tracking drag operations (per-instance using logical ID)
    static std::map<int, bool> isResizingCropBoxByNode; // Left-click drag for resizing
    static std::map<int, bool> isMovingCropBoxByNode;   // Right-click drag for moving
    static std::map<int, bool> isEditingTrackerBoxByNode; // Ctrl + Left-click drag for tracker box
    static std::map<int, ImVec2> dragStartPosByNode;
    static std::map<int, float> initialCropXByNode; // Store crop center at the start of a move operation
    static std::map<int, float> initialCropYByNode;
    
    int nodeId = (int)getLogicalId();
    bool& isResizingCropBox = isResizingCropBoxByNode[nodeId];
    bool& isMovingCropBox = isMovingCropBoxByNode[nodeId];
    bool& isEditingTrackerBox = isEditingTrackerBoxByNode[nodeId];
    ImVec2& dragStartPos = dragStartPosByNode[nodeId];
    float& initialCropX = initialCropXByNode[nodeId];
    float& initialCropY = initialCropYByNode[nodeId];
    
    juce::Image frame;
    {
        const juce::ScopedLock lock(imageLock);
        frame = latestInputFrameForGui.createCopy(); // Always draw the full, uncropped input frame
    }
    
    if (!frame.isNull())
    {
        // Use a local static map for texture management (per-module-instance textures)
        static std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> localVisionTextures;
        
        if (localVisionTextures.find((int)getLogicalId()) == localVisionTextures.end())
            localVisionTextures[(int)getLogicalId()] = std::make_unique<juce::OpenGLTexture>();
        
        auto* texture = localVisionTextures[(int)getLogicalId()].get();
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
            
            ImGui::SetCursorScreenPos(imageRectMin);
            ImGui::InvisibleButton("##crop_interaction", imageSize);
            
            ImVec2 mousePos = ImGui::GetMousePos();
            
            // Get current crop rect from parameters to display it and check for hover
            float currX = cropXParam ? cropXParam->load() : 0.5f;
            float currY = cropYParam ? cropYParam->load() : 0.5f;
            float currW = cropWParam ? cropWParam->load() : 0.5f;
            float currH = cropHParam ? cropHParam->load() : 0.5f;
            
            ImVec2 currentCropMin = ImVec2(imageRectMin.x + (currX - currW/2.0f) * imageSize.x, imageRectMin.y + (currY - currH/2.0f) * imageSize.y);
            ImVec2 currentCropMax = ImVec2(imageRectMin.x + (currX + currW/2.0f) * imageSize.x, imageRectMin.y + (currY + currH/2.0f) * imageSize.y);
            bool isMouseInCropBox = (mousePos.x >= currentCropMin.x && mousePos.x <= currentCropMax.x && mousePos.y >= currentCropMin.y && mousePos.y <= currentCropMax.y);
            
            // Get tracker box rect (relative to the main crop box)
            float trackX = apvts.getRawParameterValue("trackerX")->load();
            float trackY = apvts.getRawParameterValue("trackerY")->load();
            float trackW = apvts.getRawParameterValue("trackerW")->load();
            float trackH = apvts.getRawParameterValue("trackerH")->load();
            
            ImVec2 trackerMin = ImVec2(currentCropMin.x + trackX * (currentCropMax.x - currentCropMin.x), currentCropMin.y + trackY * (currentCropMax.y - currentCropMin.y));
            ImVec2 trackerMax = ImVec2(trackerMin.x + trackW * (currentCropMax.x - currentCropMin.x), trackerMin.y + trackH * (currentCropMax.y - currentCropMin.y));
            bool isMouseInTrackerBox = (mousePos.x >= trackerMin.x && mousePos.x <= trackerMax.x && mousePos.y >= trackerMin.y && mousePos.y <= trackerMax.y);
            
            bool ctrlHeld = ImGui::GetIO().KeyCtrl;
            
            // Set tooltip and cursor based on mouse position
            if (ImGui::IsItemHovered())
            {
                if (ctrlHeld)
                {
                    ImGui::SetTooltip("Define Tracker Box (Red)");
                }
                else if (isMouseInCropBox && !isResizingCropBox && !isMovingCropBox)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Right-drag to move Crop Box (Yellow)\nCtrl + Left-drag to edit Tracker Box (Red)");
                }
                else
                {
                    ImGui::SetTooltip("Left-drag to define new Crop Box (Yellow)");
                }
            }
            
            // --- Handle Mouse Clicks ---
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                if (ctrlHeld)
                {
                    isEditingTrackerBox = true;
                }
                else if (!isMouseInCropBox)
                {
                    isResizingCropBox = true;
                }
                dragStartPos = mousePos;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && isMouseInCropBox)
            {
                isMovingCropBox = true;
                dragStartPos = mousePos;
                initialCropX = currX;
                initialCropY = currY;
            }
            
            // Draw crop rect - green for tracking, yellow for manual
            if (trackingActive.load())
            {
                // Draw a thick green rectangle for active tracking
                drawList->AddRect(currentCropMin, currentCropMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 3.0f);
                const char* text = "TRACKING";
                ImVec2 textSize = ImGui::CalcTextSize(text);
                ImVec2 textPos = ImVec2(currentCropMin.x + 5, currentCropMin.y + 5);
                if (textPos.x + textSize.x < imageRectMax.x && textPos.y + textSize.y < imageRectMax.y)
                {
                    drawList->AddText(textPos, IM_COL32(0, 255, 0, 255), text);
                }
            }
            else
            {
                // Draw the yellow rectangle for manual selection
                drawList->AddRect(currentCropMin, currentCropMax, IM_COL32(255, 255, 0, 150), 0.0f, 0, 2.0f);
            }
            
            // Draw tracker box (red) inside crop box
            drawList->AddRect(trackerMin, trackerMax, IM_COL32(255, 0, 0, 200), 0.0f, 0, 1.5f);
            
            // Handle Resize Drag (Left Click)
            if (isResizingCropBox)
            {
                ImVec2 dragCurrentPos = mousePos;
                ImVec2 rectMin = ImVec2(std::min(dragStartPos.x, dragCurrentPos.x), std::min(dragStartPos.y, dragCurrentPos.y));
                ImVec2 rectMax = ImVec2(std::max(dragStartPos.x, dragCurrentPos.x), std::max(dragStartPos.y, dragCurrentPos.y));
                
                rectMin.x = std::max(rectMin.x, imageRectMin.x);
                rectMin.y = std::max(rectMin.y, imageRectMin.y);
                rectMax.x = std::min(rectMax.x, imageRectMax.x);
                rectMax.y = std::min(rectMax.y, imageRectMax.y);
                
                ImU32 overlayColor = IM_COL32(0, 0, 0, 120);
                drawList->AddRectFilled(imageRectMin, ImVec2(imageRectMax.x, rectMin.y), overlayColor);
                drawList->AddRectFilled(ImVec2(imageRectMin.x, rectMax.y), imageRectMax, overlayColor);
                drawList->AddRectFilled(ImVec2(imageRectMin.x, rectMin.y), ImVec2(rectMin.x, rectMax.y), overlayColor);
                drawList->AddRectFilled(ImVec2(rectMax.x, rectMin.y), ImVec2(imageRectMax.x, rectMax.y), overlayColor);
            }
            else if (isEditingTrackerBox)
            {
                ImVec2 dragCurrentPos = mousePos;
                ImVec2 tRectMin = ImVec2(std::min(dragStartPos.x, dragCurrentPos.x), std::min(dragStartPos.y, dragCurrentPos.y));
                ImVec2 tRectMax = ImVec2(std::max(dragStartPos.x, dragCurrentPos.x), std::max(dragStartPos.y, dragCurrentPos.y));
                
                // Clamp tracker box to be inside the main crop box
                tRectMin.x = std::max(tRectMin.x, currentCropMin.x);
                tRectMin.y = std::max(tRectMin.y, currentCropMin.y);
                tRectMax.x = std::min(tRectMax.x, currentCropMax.x);
                tRectMax.y = std::min(tRectMax.y, currentCropMax.y);
                
                ImU32 overlayColor = IM_COL32(0, 0, 0, 120);
                drawList->AddRectFilled(currentCropMin, ImVec2(currentCropMax.x, tRectMin.y), overlayColor);
                drawList->AddRectFilled(ImVec2(currentCropMin.x, tRectMax.y), currentCropMax, overlayColor);
                drawList->AddRectFilled(ImVec2(currentCropMin.x, tRectMin.y), ImVec2(tRectMin.x, tRectMax.y), overlayColor);
                drawList->AddRectFilled(ImVec2(tRectMax.x, tRectMin.y), ImVec2(currentCropMax.x, tRectMax.y), overlayColor);
            }
            
            // Handle Move Drag (Right Click)
            if (isMovingCropBox && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                ImVec2 dragDelta = ImVec2(mousePos.x - dragStartPos.x, mousePos.y - dragStartPos.y);
                if (imageSize.x > 0 && imageSize.y > 0)
                {
                    float normDeltaX = dragDelta.x / imageSize.x;
                    float normDeltaY = dragDelta.y / imageSize.y;
                    
                    // Calculate new position and clamp it so the box stays within the image bounds
                    float newX = juce::jlimit(currW / 2.0f, 1.0f - currW / 2.0f, initialCropX + normDeltaX);
                    float newY = juce::jlimit(currH / 2.0f, 1.0f - currH / 2.0f, initialCropY + normDeltaY);
                    
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropX"))) *p = newX;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropY"))) *p = newY;
                }
            }
            
            // --- Handle Mouse Release ---
            if (isResizingCropBox && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                isResizingCropBox = false;
                ImVec2 dragEndPos = mousePos;
                ImVec2 rectMin = ImVec2(std::min(dragStartPos.x, dragEndPos.x), std::min(dragStartPos.y, dragEndPos.y));
                ImVec2 rectMax = ImVec2(std::max(dragStartPos.x, dragEndPos.x), std::max(dragStartPos.y, dragEndPos.y));
                
                rectMin.x = std::max(rectMin.x, imageRectMin.x);
                rectMin.y = std::max(rectMin.y, imageRectMin.y);
                rectMax.x = std::min(rectMax.x, imageRectMax.x);
                rectMax.y = std::min(rectMax.y, imageRectMax.y);
                
                if (imageSize.x > 0 && imageSize.y > 0)
                {
                    float newW = (rectMax.x - rectMin.x) / imageSize.x;
                    float newH = (rectMax.y - rectMin.y) / imageSize.y;
                    float newX = ((rectMin.x - imageRectMin.x) / imageSize.x) + newW / 2.0f;
                    float newY = ((rectMin.y - imageRectMin.y) / imageSize.y) + newH / 2.0f;
                    
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropX"))) *p = newX;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropY"))) *p = newY;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropW"))) *p = newW;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("cropH"))) *p = newH;
                    onModificationEnded();
                }
            }
            
            if (isMovingCropBox && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                isMovingCropBox = false;
                onModificationEnded();
            }
            else if (isEditingTrackerBox && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                isEditingTrackerBox = false;
                ImVec2 dragEndPos = mousePos;
                ImVec2 tRectMin = ImVec2(std::min(dragStartPos.x, dragEndPos.x), std::min(dragStartPos.y, dragEndPos.y));
                ImVec2 tRectMax = ImVec2(std::max(dragStartPos.x, dragEndPos.x), std::max(dragStartPos.y, dragEndPos.y));
                
                tRectMin.x = std::max(tRectMin.x, currentCropMin.x);
                tRectMin.y = std::max(tRectMin.y, currentCropMin.y);
                tRectMax.x = std::min(tRectMax.x, currentCropMax.x);
                tRectMax.y = std::min(tRectMax.y, currentCropMax.y);
                
                float cropBoxW_px = currentCropMax.x - currentCropMin.x;
                float cropBoxH_px = currentCropMax.y - currentCropMin.y;
                if (cropBoxW_px > 0 && cropBoxH_px > 0)
                {
                    float newTX = (tRectMin.x - currentCropMin.x) / cropBoxW_px;
                    float newTY = (tRectMin.y - currentCropMin.y) / cropBoxH_px;
                    float newTW = (tRectMax.x - tRectMin.x) / cropBoxW_px;
                    float newTH = (tRectMax.y - tRectMin.y) / cropBoxH_px;
                    
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("trackerX"))) *p = newTX;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("trackerY"))) *p = newTY;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("trackerW"))) *p = newTW;
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("trackerH"))) *p = newTH;
                    onModificationEnded();
                }
            }
        }
    }
    
    // --- Tracking Buttons ---
    ImGui::Separator();
    if (trackingActive.load())
    {
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.0f, 0.8f, 0.8f));
        if (ImGui::Button("Stop Tracking", ImVec2(itemWidth, 0)))
        {
            juce::Logger::writeToLog("[CropVideo][UI] 'Stop Tracking' clicked.");
            const juce::ScopedLock lock(trackerLock);
            trackingActive.store(false);
            tracker.release();
        }
        ImGui::PopStyleColor(3);
    }
    else
    {
        if (ImGui::Button("Start Tracking", ImVec2(itemWidth, 0)))
        {
            juce::Logger::writeToLog("[CropVideo][UI] 'Start Tracking' clicked.");
            const juce::ScopedLock lock(trackerLock);
            if (!lastFrameForTracker.empty())
            {
                juce::Logger::writeToLog("[CropVideo][UI] lastFrameForTracker is valid (" + juce::String(lastFrameForTracker.cols) + "x" + juce::String(lastFrameForTracker.rows) + "). Initializing tracker.");
                // Get crop box and tracker box from params to initialize tracker
                float cx = cropXParam ? cropXParam->load() : 0.5f;
                float cy = cropYParam ? cropYParam->load() : 0.5f;
                float cw = cropWParam ? cropWParam->load() : 0.5f;
                float ch = cropHParam ? cropHParam->load() : 0.5f;
                float tx = apvts.getRawParameterValue("trackerX")->load();
                float ty = apvts.getRawParameterValue("trackerY")->load();
                float tw = apvts.getRawParameterValue("trackerW")->load();
                float th = apvts.getRawParameterValue("trackerH")->load();
                
                int frameW = lastFrameForTracker.cols;
                int frameH = lastFrameForTracker.rows;
                
                // Calculate crop box in pixels
                float cropW_px = cw * frameW;
                float cropH_px = ch * frameH;
                float cropX_px = (cx - cw / 2.0f) * frameW;
                float cropY_px = (cy - ch / 2.0f) * frameH;
                
                // Calculate tracker box in pixels (relative to crop box)
                cv::Rect initBox(
                    (int)(cropX_px + tx * cropW_px),
                    (int)(cropY_px + ty * cropH_px),
                    (int)(tw * cropW_px),
                    (int)(th * cropH_px)
                );

                if (initBox.area() > 0)
                {
                    juce::Logger::writeToLog("[CropVideo][UI] Initializing with tracker box: x=" + juce::String(initBox.x) + " y=" + juce::String(initBox.y) + " w=" + juce::String(initBox.width) + " h=" + juce::String(initBox.height));
                    try {
                        tracker = cv::TrackerMIL::create();
                        tracker->init(lastFrameForTracker, initBox);
                        trackingActive.store(true);
                        juce::Logger::writeToLog("[CropVideo][UI] Tracker initialized SUCCESSFULLY.");
                    } catch (const cv::Exception& e) {
                        juce::Logger::writeToLog("[CropVideo][UI] CRITICAL: OpenCV exception during tracker->init(): " + juce::String(e.what()));
                        trackingActive.store(false);
                        tracker.release();
                    }
                }
                else
                {
                    juce::Logger::writeToLog("[CropVideo][UI] ERROR: Cannot start tracking with a zero-area box.");
                }
            }
            else
            {
                juce::Logger::writeToLog("[CropVideo][UI] ERROR: Cannot start tracking because lastFrameForTracker is empty.");
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock onto the current crop box and follow it automatically.");
    }
    
    // --- Parameter Sliders ---
    ImGui::PushItemWidth(itemWidth);
    ImGui::Text("Manual Crop Controls:");
    
    auto drawModSlider = [&](const char* label, const char* paramId, std::atomic<float>* paramPtr)
    {
        bool modulated = isParamModulated(juce::String(paramId) + "_mod");
        bool isControlled = modulated || trackingActive.load();
        if (isControlled) ImGui::BeginDisabled();
        
        float value = paramPtr ? paramPtr->load() : 0.5f;
        if (ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.3f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
            {
                *p = value;
                onModificationEnded();
            }
        }
        
        if (isControlled)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                if (trackingActive.load())
                    ImGui::SetTooltip("Tracking active");
                else if (modulated)
                    ImGui::SetTooltip("CV input connected");
            }
        }
    };
    
    drawModSlider("Center X", "cropX", cropXParam);
    drawModSlider("Center Y", "cropY", cropYParam);
    drawModSlider("Width", "cropW", cropWParam);
    drawModSlider("Height", "cropH", cropHParam);
    
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
    // Modulation pins are now drawn automatically by the editor
    helpers.drawAudioOutputPin("Output ID", 0);
}
#endif

