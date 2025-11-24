#include "PanVolModuleProcessor.h"
#include <cmath>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

PanVolModuleProcessor::PanVolModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Mod", juce::AudioChannelSet::discreteChannels(2), true)  // Pan Mod, Vol Mod
          .withOutput("Out", juce::AudioChannelSet::discreteChannels(2), true)),  // Pan Out, Vol Out
      apvts(*this, nullptr, "PanVolParams", createParameterLayout())
{
    panParam = apvts.getRawParameterValue("pan");
    volumeParam = apvts.getRawParameterValue("volume");
    
    // Initialize output values for cable inspector
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Pan Out
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Vol Out
}

juce::AudioProcessorValueTreeState::ParameterLayout PanVolModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pan", "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volume", "Volume",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f),
        0.0f));
    return { p.begin(), p.end() };
}

void PanVolModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void PanVolModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    const int numSamples = buffer.getNumSamples();
    
    // Check if parameters are modulated via CV input
    const bool panIsModulated = isParamInputConnected(paramIdPanMod);
    const bool volumeIsModulated = isParamInputConnected(paramIdVolumeMod);
    
    // Get base parameter values
    float basePan = panParam->load();
    float baseVolumeDb = volumeParam->load();
    
    // Read CV modulation signals if connected
    const float* panCV = nullptr;
    const float* volumeCV = nullptr;
    
    if (panIsModulated && inBus.getNumChannels() > 0)
        panCV = inBus.getReadPointer(0);
    
    if (volumeIsModulated && inBus.getNumChannels() > 1)
        volumeCV = inBus.getReadPointer(1);
    
    // Process per-sample if either parameter is modulated
    if (panIsModulated || volumeIsModulated)
    {
        float* panOut = outBus.getNumChannels() > 0 ? outBus.getWritePointer(0) : nullptr;
        float* volumeOut = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : nullptr;
        
        for (int i = 0; i < numSamples; ++i)
        {
            // Calculate current pan value
            float currentPan = basePan;
            if (panIsModulated && panCV)
            {
                // Map CV [0,1] to pan [-1, 1]
                currentPan = juce::jmap(panCV[i], 0.0f, 1.0f, -1.0f, 1.0f);
            }
            
            // Calculate current volume value
            float currentVolumeDb = baseVolumeDb;
            if (volumeIsModulated && volumeCV)
            {
                // Map CV [0,1] to volume [-60, 6] dB
                currentVolumeDb = juce::jmap(volumeCV[i], 0.0f, 1.0f, -60.0f, 6.0f);
            }
            
            // Normalize outputs to CV [0,1] range for standard CV convention
            // Pan: -1.0 to +1.0 → 0.0 to 1.0
            float normalizedPan = (currentPan + 1.0f) * 0.5f;
            // Volume: -60dB to +6dB → 0.0 to 1.0
            float normalizedVolume = (currentVolumeDb + 60.0f) / 66.0f;
            
            // Output normalized CV signals [0,1]
            if (panOut) panOut[i] = normalizedPan;
            if (volumeOut) volumeOut[i] = normalizedVolume;
            
            // Store live values for UI (update every 64 samples to reduce overhead)
            if ((i & 0x3F) == 0)
            {
                if (panIsModulated) setLiveParamValue("pan_live", currentPan);
                if (volumeIsModulated) setLiveParamValue("volume_live", currentVolumeDb);
            }
        }
        
        // Store last values for cable inspector (normalized to [0,1])
        if (numSamples > 0)
        {
            float finalPan = panIsModulated && panCV ? 
                juce::jmap(panCV[numSamples - 1], 0.0f, 1.0f, -1.0f, 1.0f) : basePan;
            float finalVolumeDb = volumeIsModulated && volumeCV ?
                juce::jmap(volumeCV[numSamples - 1], 0.0f, 1.0f, -60.0f, 6.0f) : baseVolumeDb;
            
            // Normalize to [0,1] for cable inspector
            float normalizedPan = (finalPan + 1.0f) * 0.5f;
            float normalizedVolume = (finalVolumeDb + 60.0f) / 66.0f;
            
            if (lastOutputValues.size() >= 2)
            {
                lastOutputValues[0]->store(normalizedPan);
                lastOutputValues[1]->store(normalizedVolume);
            }
        }
    }
    else
    {
        // Optimized path: no modulation, use constant values
        // Normalize outputs to CV [0,1] range
        float normalizedPan = (basePan + 1.0f) * 0.5f;
        float normalizedVolume = (baseVolumeDb + 60.0f) / 66.0f;
        
        float* panOut = outBus.getNumChannels() > 0 ? outBus.getWritePointer(0) : nullptr;
        float* volumeOut = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : nullptr;
        
        if (panOut) std::fill(panOut, panOut + numSamples, normalizedPan);
        if (volumeOut) std::fill(volumeOut, volumeOut + numSamples, normalizedVolume);
        
        // Store normalized values for cable inspector
        if (lastOutputValues.size() >= 2)
        {
            lastOutputValues[0]->store(normalizedPan);
            lastOutputValues[1]->store(normalizedVolume);
        }
    }
}

bool PanVolModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == paramIdPanMod)
    {
        outBusIndex = 0;  // Input bus 0
        outChannelIndexInBus = 0;  // First channel (Pan Mod)
        return true;
    }
    if (paramId == paramIdVolumeMod)
    {
        outBusIndex = 0;
        outChannelIndexInBus = 1;  // Second channel (Vol Mod)
        return true;
    }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void PanVolModuleProcessor::drawParametersInNode(float itemWidth,
                                                  const std::function<bool(const juce::String&)>& isParamModulated,
                                                  const std::function<void()>& onModificationEnded)
{
    auto HelpMarker = [](const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    
    ImGui::PushItemWidth(itemWidth);
    
    // Check modulation state
    const bool panIsMod = isParamModulated(paramIdPanMod);
    const bool volIsMod = isParamModulated(paramIdVolumeMod);
    
    // Get current values (use live values if modulated)
    float panValue = panIsMod ? 
        getLiveParamValueFor(paramIdPanMod, "pan_live", panParam->load()) : 
        panParam->load();
    float volumeValue = volIsMod ?
        getLiveParamValueFor(paramIdVolumeMod, "volume_live", volumeParam->load()) :
        volumeParam->load();
    
    // Convert volume from dB to normalized 0-1 range for display
    float volumeNormalized = (volumeValue + 60.0f) / 66.0f; // Map -60dB to +6dB -> 0 to 1
    volumeNormalized = juce::jlimit(0.0f, 1.0f, volumeNormalized);
    
    // Grid size (square, keep original size for interactivity)
    const float gridSize = juce::jmin(itemWidth - 20.0f, 120.0f);
    const float gridPadding = (itemWidth - gridSize) * 0.5f;
    
    ImVec2 gridPos = ImGui::GetCursorScreenPos();
    gridPos.x += gridPadding;
    gridPos.y += 2.0f; // Reduced top padding for compactness
    
    ImVec2 gridMin = gridPos;
    ImVec2 gridMax = ImVec2(gridPos.x + gridSize, gridPos.y + gridSize);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    // Draw grid background (using theme colors)
    drawList->AddRectFilled(gridMin, gridMax, theme.modules.panvol_grid_background);
    drawList->AddRect(gridMin, gridMax, theme.modules.panvol_grid_border, 0.0f, 0, 2.0f);
    
    // Draw grid lines (optional, for visual reference)
    const int gridDivisions = 4;
    for (int i = 1; i < gridDivisions; ++i)
    {
        float t = (float)i / (float)gridDivisions;
        // Vertical lines
        float x = gridMin.x + t * gridSize;
        drawList->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y),
                         theme.modules.panvol_grid_lines, 1.0f);
        // Horizontal lines
        float y = gridMin.y + t * gridSize;
        drawList->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y),
                         theme.modules.panvol_grid_lines, 1.0f);
    }
    
    // Draw center crosshair
    ImVec2 center(gridMin.x + gridSize * 0.5f, gridMin.y + gridSize * 0.5f);
    drawList->AddLine(ImVec2(center.x, gridMin.y), ImVec2(center.x, gridMax.y),
                     theme.modules.panvol_crosshair, 1.0f);
    drawList->AddLine(ImVec2(gridMin.x, center.y), ImVec2(gridMax.x, center.y),
                     theme.modules.panvol_crosshair, 1.0f);
    
    // Calculate circle position from parameters
    // X: pan (-1 to +1) -> (0 to gridSize)
    // Y: volume (0 to 1, where 0 = bottom, 1 = top) -> inverted for screen coords
    float circleX = center.x + (panValue * gridSize * 0.45f); // 45% of half-width for range
    float circleY = center.y - (volumeNormalized - 0.5f) * gridSize * 0.9f; // Inverted Y
    
    // Clamp to grid bounds
    circleX = juce::jlimit(gridMin.x + 8.0f, gridMax.x - 8.0f, circleX);
    circleY = juce::jlimit(gridMin.y + 8.0f, gridMax.y - 8.0f, circleY);
    
    ImVec2 circlePos(circleX, circleY);
    const float circleRadius = 6.0f;
    
    // Draw circle shadow
    drawList->AddCircleFilled(ImVec2(circlePos.x + 1, circlePos.y + 1),
                             circleRadius, IM_COL32(0, 0, 0, 100), 16);
    
    // Draw circle (color-coded by modulation state, using theme colors)
    ImU32 circleColor = (panIsMod || volIsMod) ?
        theme.modules.panvol_circle_modulated :  // Cyan when modulated
        theme.modules.panvol_circle_manual;      // Orange when manual
    drawList->AddCircleFilled(circlePos, circleRadius, circleColor, 16);
    drawList->AddCircle(circlePos, circleRadius, IM_COL32(255, 255, 255, 255), 16, 1.5f);
    
    // Draw axis labels and value readouts (inside grid bounds, using theme colors)
    ImVec2 labelPos;
    const float fontSize = ImGui::GetFontSize();
    
    // Top-left: Volume indicator
    labelPos = ImVec2(gridMin.x + 2, gridMin.y + 2);
    drawList->AddText(labelPos, theme.modules.panvol_label_text, "Vol");
    
    // Volume value readout (small, discrete, below "Vol")
    char volText[16];
    snprintf(volText, sizeof(volText), "%.1fdB", volumeValue);
    ImVec2 volTextSize = ImGui::CalcTextSize(volText);
    labelPos = ImVec2(gridMin.x + 2, gridMin.y + 2 + fontSize + 2);
    drawList->AddText(labelPos, theme.modules.panvol_value_text, volText);
    
    // Top-right: Pan indicator
    const char* panLabel = "Pan";
    float panTextWidth = ImGui::CalcTextSize(panLabel).x;
    labelPos = ImVec2(gridMax.x - panTextWidth - 2, gridMin.y + 2);
    drawList->AddText(labelPos, theme.modules.panvol_label_text, panLabel);
    
    // Pan value readout (small, discrete, below "Pan")
    char panText[16];
    snprintf(panText, sizeof(panText), "%.2f", panValue);
    ImVec2 panTextSize = ImGui::CalcTextSize(panText);
    labelPos = ImVec2(gridMax.x - panTextSize.x - 2, gridMin.y + 2 + fontSize + 2);
    drawList->AddText(labelPos, theme.modules.panvol_value_text, panText);
    
    // Reserve space for grid (reduced padding for compactness)
    ImGui::Dummy(ImVec2(itemWidth, gridSize + 4.0f));
    
    // Invisible button for interaction (covers entire grid area)
    ImGui::SetCursorScreenPos(gridMin);
    ImGui::InvisibleButton("##panvol_grid", ImVec2(gridSize, gridSize));
    
    // Handle mouse interaction
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        
        // Convert mouse position to parameter values
        float newPan = ((mousePos.x - center.x) / (gridSize * 0.45f));
        newPan = juce::jlimit(-1.0f, 1.0f, newPan);
        
        float newVolNorm = ((center.y - mousePos.y) / (gridSize * 0.9f)) + 0.5f;
        newVolNorm = juce::jlimit(0.0f, 1.0f, newVolNorm);
        float newVolDb = (newVolNorm * 66.0f) - 60.0f; // Convert back to dB
        
        // Update parameters if not modulated
        if (!panIsMod)
        {
            *panParam = newPan;
        }
        if (!volIsMod)
        {
            *volumeParam = newVolDb;
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        // Jump to clicked position
        ImVec2 mousePos = ImGui::GetMousePos();
        
        float newPan = ((mousePos.x - center.x) / (gridSize * 0.45f));
        newPan = juce::jlimit(-1.0f, 1.0f, newPan);
        
        float newVolNorm = ((center.y - mousePos.y) / (gridSize * 0.9f)) + 0.5f;
        newVolNorm = juce::jlimit(0.0f, 1.0f, newVolNorm);
        float newVolDb = (newVolNorm * 66.0f) - 60.0f;
        
        if (!panIsMod) *panParam = newPan;
        if (!volIsMod) *volumeParam = newVolDb;
        
        onModificationEnded();
    }
    
    // Compact reset button directly below grid
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f); // Small spacing after grid
    if (ImGui::Button("Reset", ImVec2(itemWidth, ImGui::GetFrameHeight() * 0.8f)))
    {
        if (!panIsMod) *panParam = 0.0f;
        if (!volIsMod) *volumeParam = 0.0f;
        onModificationEnded();
    }
    
    ImGui::PopItemWidth();
}

void PanVolModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Output pins for connecting to mixer/track mixer
    helpers.drawParallelPins("Pan Mod", 0, "Pan Out", 0);
    helpers.drawParallelPins("Vol Mod", 1, "Vol Out", 1);
}

ImVec2 PanVolModuleProcessor::getCustomNodeSize() const
{
#if defined(PRESET_CREATOR_UI)
    // Get custom width from theme, default to 180px for compactness
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    float customWidth = theme.modules.panvol_node_width > 0.0f ? 
        theme.modules.panvol_node_width : 180.0f;
    return ImVec2(customWidth, 0.0f); // 0.0f height = auto
#else
    return ImVec2(0.0f, 0.0f);
#endif
}
#endif

