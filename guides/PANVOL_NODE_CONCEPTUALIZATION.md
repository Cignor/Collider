# PanVol Node - Conceptualization Document

## Overview

A small, intuitive node that provides a 2D control surface for simultaneous volume and panning adjustment. Users interact with a draggable circle on a small grid, where vertical movement controls volume and horizontal movement controls panning.

**Node Name**: `panvol`  
**Display Name**: "PanVol" or "XY Pad"  
**Category**: Control / Mixing

---

## Core Concept

### Visual Design
- **Small & Cute**: Compact node size (~120-150px width)
- **Grid Background**: Small square grid (e.g., 100x100px) with subtle grid lines
- **Draggable Circle**: A small, visually distinct circle that can be moved within the grid bounds
- **Visual Feedback**: 
  - Grid lines for reference
  - Center crosshair or origin indicator
  - Volume axis labels (Up = ↑, Down = ↓)
  - Pan axis labels (Left = ←, Right = →)

### Interaction Model
- **Click & Drag**: Click on the circle and drag it around the grid
- **Direct Click**: Click anywhere on the grid to jump the circle to that position
- **Constraints**: Circle stays within grid boundaries
- **Smooth Movement**: Circle follows mouse cursor smoothly during drag

### Parameter Mapping
- **X-Axis (Horizontal)**: Panning
  - Left edge = Full Left (-1.0)
  - Center = Center (0.0)
  - Right edge = Full Right (+1.0)
  
- **Y-Axis (Vertical)**: Volume
  - Top edge = Maximum Volume (0 dB or 1.0 linear)
  - Center = Unity Gain (0 dB or 0.0 offset)
  - Bottom edge = Minimum Volume (-60 dB or 0.0 linear)

---

## Technical Implementation

### Parameter Structure

```cpp
class PanVolModuleProcessor : public ModuleProcessor
{
public:
    static constexpr auto paramIdPan = "pan";
    static constexpr auto paramIdVolume = "volume";
    
    // For CV modulation
    static constexpr auto paramIdPanMod = "pan_mod";
    static constexpr auto paramIdVolumeMod = "volume_mod";
    
private:
    std::atomic<float>* panParam { nullptr };      // -1.0 to +1.0
    std::atomic<float>* volumeParam { nullptr };   // -60.0 to +6.0 dB
};
```

### Parameter Layout

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
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
```

### Audio Processing

**Note**: This node doesn't process audio directly. It outputs CV signals that can be connected to mixer/track mixer volume and pan inputs.

```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
{
    auto outBus = getBusBuffer(buffer, false, 0);
    const int numSamples = buffer.getNumSamples();
    
    // Check if parameters are modulated via CV input
    const bool panIsModulated = isParamInputConnected(paramIdPanMod);
    const bool volumeIsModulated = isParamInputConnected(paramIdVolumeMod);
    
    // Get parameter values (use CV if connected, otherwise use manual control)
    float panValue = panIsModulated 
        ? getLiveParamValueFor(paramIdPanMod, "pan_live", panParam->load())
        : panParam->load();
    
    float volumeValue = volumeIsModulated
        ? getLiveParamValueFor(paramIdVolumeMod, "volume_live", volumeParam->load())
        : volumeParam->load();
    
    // Convert volume from dB to linear for CV output (optional: could output dB directly)
    float volumeLinear = juce::Decibels::decibelsToGain(volumeValue);
    
    // Output CV signals
    if (outBus.getNumChannels() >= 2)
    {
        float* panOut = outBus.getWritePointer(0);
        float* volumeOut = outBus.getWritePointer(1);
        
        std::fill(panOut, panOut + numSamples, panValue);
        std::fill(volumeOut, volumeOut + numSamples, volumeLinear);
    }
    
    // Store for UI display
    if (lastOutputValues.size() >= 2)
    {
        lastOutputValues[0]->store(panValue);
        lastOutputValues[1]->store(volumeLinear);
    }
}
```

### Parameter Routing (for CV modulation)

```cpp
bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override
{
    if (paramId == paramIdPanMod)
    {
        outBusIndex = 0;  // Input bus 0
        outChannelIndexInBus = 0;  // First channel of modulation input
        return true;
    }
    if (paramId == paramIdVolumeMod)
    {
        outBusIndex = 0;
        outChannelIndexInBus = 1;  // Second channel of modulation input
        return true;
    }
    return false;
}
```

---

## UI Implementation

### Node Layout

```
┌─────────────────────┐
│   PanVol            │  ← Title bar
├─────────────────────┤
│                     │
│    [Grid Area]      │  ← Interactive grid (100x100px)
│    ⚫               │  ← Draggable circle
│                     │
│  Pan: 0.00          │  ← Value display
│  Vol: 0.0 dB        │
│                     │
│  [Reset]            │  ← Optional reset button
│                     │
├─────────────────────┤
│  Pan Out ──●        │  ← Output pins
│  Vol Out ──●        │
└─────────────────────┘
```

### Custom Drawing Implementation

```cpp
#if defined(PRESET_CREATOR_UI)
void drawParametersInNode(float itemWidth,
                          const std::function<bool(const juce::String&)>& isParamModulated,
                          const std::function<void()>& onModificationEnded) override
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
    
    // Get current values
    float panValue = panParam->load();
    float volumeValue = volumeParam->load();
    
    // Convert volume from dB to normalized 0-1 range for display
    float volumeNormalized = (volumeValue + 60.0f) / 66.0f; // Map -60dB to +6dB -> 0 to 1
    volumeNormalized = juce::jlimit(0.0f, 1.0f, volumeNormalized);
    
    // Grid size (square, fits within itemWidth with padding)
    const float gridSize = juce::jmin(itemWidth - 20.0f, 120.0f);
    const float gridPadding = (itemWidth - gridSize) * 0.5f;
    
    ImVec2 gridPos = ImGui::GetCursorScreenPos();
    gridPos.x += gridPadding;
    gridPos.y += 10.0f;
    
    ImVec2 gridMin = gridPos;
    ImVec2 gridMax = ImVec2(gridPos.x + gridSize, gridPos.y + gridSize);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Draw grid background
    drawList->AddRectFilled(gridMin, gridMax, IM_COL32(20, 20, 20, 255));
    drawList->AddRect(gridMin, gridMax, IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);
    
    // Draw grid lines (optional, for visual reference)
    const int gridDivisions = 4;
    for (int i = 1; i < gridDivisions; ++i)
    {
        float t = (float)i / (float)gridDivisions;
        // Vertical lines
        float x = gridMin.x + t * gridSize;
        drawList->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y), 
                         IM_COL32(50, 50, 50, 255), 1.0f);
        // Horizontal lines
        float y = gridMin.y + t * gridSize;
        drawList->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y), 
                         IM_COL32(50, 50, 50, 255), 1.0f);
    }
    
    // Draw center crosshair
    ImVec2 center(gridMin.x + gridSize * 0.5f, gridMin.y + gridSize * 0.5f);
    drawList->AddLine(ImVec2(center.x, gridMin.y), ImVec2(center.x, gridMax.y), 
                     IM_COL32(80, 80, 80, 200), 1.0f);
    drawList->AddLine(ImVec2(gridMin.x, center.y), ImVec2(gridMax.x, center.y), 
                     IM_COL32(80, 80, 80, 200), 1.0f);
    
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
    
    // Draw circle (color-coded by modulation state)
    ImU32 circleColor = (panIsMod || volIsMod) 
        ? IM_COL32(100, 200, 255, 255)  // Cyan when modulated
        : IM_COL32(255, 200, 100, 255); // Orange when manual
    drawList->AddCircleFilled(circlePos, circleRadius, circleColor, 16);
    drawList->AddCircle(circlePos, circleRadius, IM_COL32(255, 255, 255, 255), 16, 1.5f);
    
    // Draw axis labels
    ImVec2 labelPos;
    labelPos = ImVec2(gridMin.x + 4, gridMin.y + 4);
    drawList->AddText(labelPos, IM_COL32(150, 150, 150, 255), "↑ Vol");
    labelPos = ImVec2(gridMax.x - 30, gridMin.y + 4);
    drawList->AddText(labelPos, IM_COL32(150, 150, 150, 255), "← Pan →");
    labelPos = ImVec2(gridMin.x + 4, gridMax.y - 16);
    drawList->AddText(labelPos, IM_COL32(150, 150, 150, 255), "↓");
    
    // Reserve space for grid
    ImGui::Dummy(ImVec2(itemWidth, gridSize + 20.0f));
    
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
    
    ImGui::Spacing();
    
    // Value displays
    if (panIsMod)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("Pan: %.2f (CV)", panValue);
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::Text("Pan: %.2f", panValue);
    }
    HelpMarker("Panning position. -1.0 = Full Left, 0.0 = Center, +1.0 = Full Right.");
    
    if (volIsMod)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("Volume: %.1f dB (CV)", volumeValue);
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::Text("Volume: %.1f dB", volumeValue);
    }
    HelpMarker("Volume level. Range: -60.0 dB to +6.0 dB. 0.0 dB = Unity gain.");
    
    ImGui::Spacing();
    
    // Optional: Reset button
    if (ImGui::Button("Reset to Center", ImVec2(itemWidth, 0)))
    {
        if (!panIsMod) *panParam = 0.0f;
        if (!volIsMod) *volumeParam = 0.0f;
        onModificationEnded();
    }
    HelpMarker("Reset pan to center and volume to unity gain.");
    
    ImGui::PopItemWidth();
}

void drawIoPins(const NodePinHelpers& helpers) override
{
    // Output pins for connecting to mixer/track mixer
    helpers.drawAudioOutputPin("Pan Out", 0);
    helpers.drawAudioOutputPin("Vol Out", 1);
    
    // Optional: CV modulation inputs
    helpers.drawAudioInputPin("Pan Mod", 0);
    helpers.drawAudioInputPin("Vol Mod", 1);
}
#endif
```

---

## Pin Configuration

### Input Pins (Optional - for CV modulation)
- **Pan Mod** (Channel 0): CV input to modulate panning
- **Vol Mod** (Channel 1): CV input to modulate volume

### Output Pins
- **Pan Out** (Channel 0): Panning CV output (-1.0 to +1.0)
- **Vol Out** (Channel 1): Volume CV output (linear gain, 0.0 to ~4.0)

---

## Connection Pattern

### To Mixer Module
1. Connect **PanVol Pan Out** → **Mixer Pan Mod** input
2. Connect **PanVol Vol Out** → **Mixer Gain Mod** input

### To Track Mixer Module
1. Connect **PanVol Pan Out** → **Track Mixer Track X Pan Mod** input
2. Connect **PanVol Vol Out** → **Track Mixer Track X Gain Mod** input

---

## Design Considerations

### Size & Proportions
- **Node Width**: 120-150px (compact, "cute" size)
- **Grid Size**: 100x100px (fits comfortably with padding)
- **Circle Size**: 6-8px radius (visible but not obtrusive)
- **Overall Height**: ~200-220px (including labels and buttons)

### Visual Style
- **Grid**: Subtle gray background with darker grid lines
- **Circle**: Bright, contrasting color (orange for manual, cyan when modulated)
- **Labels**: Small, unobtrusive text
- **Center Indicator**: Subtle crosshair or dot

### Interaction Feedback
- **Hover**: Circle could slightly enlarge or change color
- **Dragging**: Smooth, responsive movement
- **Click**: Immediate jump to clicked position
- **Modulation**: Visual indication when CV is connected (color change)

### Accessibility
- Clear axis labels (↑↓ for volume, ←→ for pan)
- Value displays showing exact numbers
- Tooltips explaining parameter ranges
- Reset button for quick return to center

---

## Use Cases

1. **Live Performance**: Quick volume and pan adjustments during performance
2. **Spatial Mixing**: Precise positioning of sounds in stereo field
3. **Automation**: Connect to sequencers or LFOs for automated movement
4. **Gesture Control**: Connect to motion sensors or touch controllers
5. **Dual Control**: Single node controls both parameters simultaneously

---

## Future Enhancements (Optional)

1. **Snap to Grid**: Option to snap circle to grid intersections
2. **Preset Positions**: Quick buttons for common positions (center, corners, etc.)
3. **Trajectory Recording**: Record and playback movement paths
4. **Multi-Touch Support**: For touchscreen devices
5. **Visual Trails**: Show path of movement over time
6. **Lock Axes**: Option to lock X or Y axis during drag
7. **Invert Axes**: Option to invert volume or pan direction

---

## Implementation Checklist

- [ ] Create `PanVolModuleProcessor.h` and `.cpp`
- [ ] Implement parameter layout (pan, volume)
- [ ] Implement `processBlock()` for CV output
- [ ] Implement `getParamRouting()` for CV modulation
- [ ] Implement `drawParametersInNode()` with custom grid drawing
- [ ] Implement mouse interaction (drag, click)
- [ ] Implement `drawIoPins()` with output pins
- [ ] Register in factory (`ModularSynthProcessor.cpp`)
- [ ] Add to PinDatabase with pin definitions
- [ ] Add to left panel menu (Control/Mixing category)
- [ ] Add to context menus
- [ ] Add to search system
- [ ] Test connections to Mixer and TrackMixer
- [ ] Verify CV modulation works correctly
- [ ] Test undo/redo functionality
- [ ] Add to documentation

---

## References

- **Design Patterns**: `guides/IMGUI_NODE_DESIGN_GUIDE.md` (Section 7.1: Custom Drawing)
- **Mixer Integration**: `juce/Source/audio/modules/MixerModuleProcessor.h`
- **Track Mixer Integration**: `juce/Source/audio/modules/TrackMixerModuleProcessor.h`
- **Custom Drawing Example**: `juce/Source/audio/modules/MIDIJogWheelModuleProcessor.cpp`
- **Node Registration**: `guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md`

---

**End of Conceptualization Document**

