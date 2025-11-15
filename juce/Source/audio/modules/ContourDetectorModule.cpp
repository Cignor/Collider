#include "ContourDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include <juce_opengl/juce_opengl.h>
#include <unordered_map>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ContourDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold", 0.0f, 255.0f, 128.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("noiseReduction", "Noise Reduction", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    
    // Zone rectangles are stored in APVTS state tree as strings (not as parameters)
    // Format: "x1,y1,w1,h1;x2,y2,w2,h2;..." per color zone
    
    return { params.begin(), params.end() };
}

ContourDetectorModule::ContourDetectorModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(7), true)  // 3 + 4 zone gates
                      .withOutput("Video Out", juce::AudioChannelSet::mono(), true)), // PASSTHROUGH
      juce::Thread("Contour Detector Thread"),
      apvts(*this, nullptr, "ContourDetectorParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    thresholdParam = apvts.getRawParameterValue("threshold");
    noiseReductionParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("noiseReduction"));
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    
    fifoBuffer.resize(16);
    backSub = cv::createBackgroundSubtractorMOG2();
}

ContourDetectorModule::~ContourDetectorModule()
{
    stopThread(5000);
}

void ContourDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void ContourDetectorModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void ContourDetectorModule::run()
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
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (!frame.empty())
        {
            bool useGpu = false;
            #if WITH_CUDA_SUPPORT
                useGpu = useGpuParam->get() && (cv::cuda::getCudaEnabledDeviceCount() > 0);
            #endif
            
            cv::Mat fgMask;
            backSub->apply(frame, fgMask);
            
            float thresh = thresholdParam ? thresholdParam->load() : 128.0f;
            // NOTE: threshold doesn't have CUDA implementation in this OpenCV build
            // Using CPU version even when GPU is enabled (fast enough, avoids upload/download overhead)
            cv::threshold(fgMask, fgMask, thresh, 255, cv::THRESH_BINARY);
            
            if (noiseReductionParam && noiseReductionParam->get())
            {
                cv::erode(fgMask, fgMask, cv::Mat(), cv::Point(-1,-1), 2);
                cv::dilate(fgMask, fgMask, cv::Mat(), cv::Point(-1,-1), 2);
            }

            // findContours is CPU-only
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(fgMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            ContourResult result;
            if (!contours.empty())
            {
                double maxArea = 0.0;
                int maxIdx = -1;
                for (int i = 0; i < (int)contours.size(); ++i)
                {
                    double a = cv::contourArea(contours[i]);
                    if (a > maxArea) { maxArea = a; maxIdx = i; }
                }
                if (maxIdx >= 0)
                {
                    const auto& c = contours[(size_t)maxIdx];
                    result.area = juce::jlimit(0.0f, 1.0f, (float)(maxArea / (frame.cols * frame.rows)));
                    std::vector<cv::Point> approx;
                    cv::approxPolyDP(c, approx, 0.02 * cv::arcLength(c, true), true);
                    result.complexity = juce::jmap((float)approx.size(), 3.0f, 50.0f, 0.0f, 1.0f);
                    cv::Rect bbox = cv::boundingRect(c);
                    result.aspectRatio = bbox.height > 0 ? (float)bbox.width / (float)bbox.height : 0.0f;
                    
                    // Calculate contour centroid (normalized 0.0-1.0)
                    float centroidX = (bbox.x + bbox.width * 0.5f) / frame.cols;
                    float centroidY = (bbox.y + bbox.height * 0.5f) / frame.rows;
                    
                    // Check each color zone for hit detection (each color = one zone with multiple rectangles)
                    for (int colorIdx = 0; colorIdx < 4; ++colorIdx)
                    {
                        std::vector<ZoneRect> rects;
                        loadZoneRects(colorIdx, rects);
                        
                        bool hit = false;
                        for (const auto& rect : rects)
                        {
                            // Point-in-rectangle check
                            if (centroidX >= rect.x && centroidX <= rect.x + rect.width &&
                                centroidY >= rect.y && centroidY <= rect.y + rect.height)
                            {
                                hit = true;
                                break;
                            }
                        }
                        result.zoneHits[colorIdx] = hit;
                    }
                    
                    // Draw
                    cv::drawContours(frame, contours, maxIdx, cv::Scalar(0,255,0), 2);
                    cv::rectangle(frame, bbox, cv::Scalar(255,0,0), 2);
                }
                else
                {
                    // No contour detected - all zones are false
                    for (int z = 0; z < 4; ++z)
                        result.zoneHits[z] = false;
                }
            }
            else
            {
                // No contours - all zones are false
                for (int z = 0; z < 4; ++z)
                    result.zoneHits[z] = false;
            }

            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                    fifoBuffer[writeScope.startIndex1] = result;
            }

            // --- PASSTHROUGH LOGIC ---
            if (myLogicalId != 0)
                VideoFrameManager::getInstance().setFrame(myLogicalId, frame);
            updateGuiFrame(frame);
        }
        
        wait(40);
    }
}

void ContourDetectorModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra;
    cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgra.cols || latestFrameForGui.getHeight() != bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgra.data, bgra.total() * bgra.elemSize());
}

juce::Image ContourDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

// Serialize zone rectangles to string: "x1,y1,w1,h1;x2,y2,w2,h2;..."
juce::String ContourDetectorModule::serializeZoneRects(const std::vector<ZoneRect>& rects)
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
std::vector<ContourDetectorModule::ZoneRect> ContourDetectorModule::deserializeZoneRects(const juce::String& data)
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
void ContourDetectorModule::loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const
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
void ContourDetectorModule::saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects)
{
    juce::String key = "zone_color_" + juce::String(colorIndex) + "_rects";
    juce::String data = serializeZoneRects(rects);
    apvts.state.setProperty(key, data, nullptr);
}

std::vector<DynamicPinInfo> ContourDetectorModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (7 channels: 3 detection + 4 zone gates)
    // Bus 1: Video Out (1 channel)
    const int cvOutChannels = 7;
    const int videoOutStartChannel = cvOutChannels;

    std::vector<DynamicPinInfo> pins;
    pins.push_back({ "Area", 0, PinDataType::CV });
    pins.push_back({ "Complexity", 1, PinDataType::CV });
    pins.push_back({ "Aspect Ratio", 2, PinDataType::CV });
    pins.push_back({ "Red Zone Gate", 3, PinDataType::Gate });
    pins.push_back({ "Green Zone Gate", 4, PinDataType::Gate });
    pins.push_back({ "Blue Zone Gate", 5, PinDataType::Gate });
    pins.push_back({ "Yellow Zone Gate", 6, PinDataType::Gate });
    pins.push_back({ "Video Out", videoOutStartChannel, PinDataType::Video });
    return pins;
}

void ContourDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    // Output CV on bus 0
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    const float values[3] { lastResultForAudio.area, lastResultForAudio.complexity, lastResultForAudio.aspectRatio };
    for (int ch = 0; ch < juce::jmin(3, cvOutBus.getNumChannels()); ++ch)
        for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            cvOutBus.setSample(ch, s, values[ch]);
    
    // Output zone gates (channels 3-6)
    for (int z = 0; z < 4; ++z)
    {
        int ch = 3 + z;
        if (ch < cvOutBus.getNumChannels())
        {
            float gateValue = lastResultForAudio.zoneHits[z] ? 1.0f : 0.0f;
            for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
                cvOutBus.setSample(ch, s, gateValue);
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

#if defined(PRESET_CREATOR_UI)
ImVec2 ContourDetectorModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void ContourDetectorModule::drawParametersInNode(float itemWidth,
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
            ImGui::SetTooltip("Enable GPU acceleration for contour detection.\nRequires CUDA-capable NVIDIA GPU.");
        }
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
    #endif

    float th = thresholdParam ? thresholdParam->load() : 128.0f;
    if (ImGui::SliderFloat("Threshold", &th, 0.0f, 255.0f, "%.0f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("threshold")))
            *p = th;
        onModificationEnded();
    }
    bool nr = noiseReductionParam ? noiseReductionParam->get() : true;
    if (ImGui::Checkbox("Noise Reduction", &nr))
    {
        if (noiseReductionParam) *noiseReductionParam = nr;
        onModificationEnded();
    }

    // Zoom controls
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
    
    ImGui::Separator();
    
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

void ContourDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    // Outputs are dynamic - editor queries via getDynamicOutputPins()
    helpers.drawAudioOutputPin("Video Out", 0); // Bus 1
}
#endif


