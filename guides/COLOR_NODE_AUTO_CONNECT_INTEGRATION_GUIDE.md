# Color Node Auto-Connect Integration Guide

## Overview

This guide documents how `MultiSequencerModuleProcessor` implements auto-connect functionality to create and wire up PolyVCO, SampleLoader, and TrackMixer modules. This pattern can be adapted for the Color Tracker node to automatically create synthesizer modules based on tracked colors.

---

## Architecture Overview

The auto-connect system has three components:

1. **UI Button** - Triggers the auto-connect operation
2. **Atomic Flag** - Thread-safe communication between UI and main loop
3. **Handler Function** - Creates modules and connections in `ImGuiNodeEditorComponent`

### Data Flow

```
[ColorTracker UI] → [Atomic Flag] → [Main Loop] → [Handler Function] → [Module Creation + Connections]
```

---

## Step-by-Step Implementation

### Step 1: Add Atomic Flags to Module Header

**File**: `juce/Source/audio/modules/ColorTrackerModule.h`

Add these members to the `ColorTrackerModule` class (inside the `#if defined(PRESET_CREATOR_UI)` block):

```cpp
#if defined(PRESET_CREATOR_UI)
    // Auto-connect trigger flags (similar to MultiSequencerModuleProcessor)
    std::atomic<bool> autoConnectPolyVCOTriggered { false };
    std::atomic<bool> autoConnectSamplersTriggered { false };
    
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    // ... rest of UI methods ...
#endif
```

**Reference**: `MultiSequencerModuleProcessor.h` lines 32-33

---

### Step 2: Add UI Buttons to drawParametersInNode

**File**: `juce/Source/audio/modules/ColorTrackerModule.cpp`

In your `drawParametersInNode()` method, add buttons at the end (before closing the function):

```cpp
void ColorTrackerModule::drawParametersInNode(float itemWidth,
                                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                               const std::function<void()>& onModificationEnded)
{
    // ... existing parameter UI code ...
    
    // Auto-Connect Buttons
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    const int numColors = (int)trackedColors.size();
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
}
```

**Reference**: `MultiSequencerModuleProcessor.cpp` lines 624-625

---

### Step 3: Add Handler Function Declarations

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.h`

Add these declarations to the `ImGuiNodeEditorComponent` class:

```cpp
// Color Tracker auto-connect handlers
void handleColorTrackerAutoConnectPolyVCO(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid);
void handleColorTrackerAutoConnectSamplers(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid);
```

**Reference**: `ImGuiNodeEditorComponent.h` lines 148-149

---

### Step 4: Add Flag Checking in Main Loop

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

Find the section where auto-connect flags are checked (around line 6660). Add ColorTracker checks:

```cpp
// --- Check MultiSequencer Flags ---
if (auto* multiSeq = dynamic_cast<MultiSequencerModuleProcessor*>(module))
{
    if (multiSeq->autoConnectSamplersTriggered.exchange(false))
    {
        handleMultiSequencerAutoConnectSamplers(multiSeq, modInfo.first);
        pushSnapshot();
        return;
    }
    if (multiSeq->autoConnectVCOTriggered.exchange(false))
    {
        handleMultiSequencerAutoConnectVCO(multiSeq, modInfo.first);
        pushSnapshot();
        return;
    }
}

// --- Check ColorTracker Flags ---
if (auto* colorTracker = dynamic_cast<ColorTrackerModule*>(module))
{
    if (colorTracker->autoConnectPolyVCOTriggered.exchange(false))
    {
        handleColorTrackerAutoConnectPolyVCO(colorTracker, modInfo.first);
        pushSnapshot();
        return;
    }
    if (colorTracker->autoConnectSamplersTriggered.exchange(false))
    {
        handleColorTrackerAutoConnectSamplers(colorTracker, modInfo.first);
        pushSnapshot();
        return;
    }
}
```

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 6661-6675

---

### Step 5: Implement PolyVCO Handler

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

Add this function (after `handleMultiSequencerAutoConnectVCO`):

```cpp
void ImGuiNodeEditorComponent::handleColorTrackerAutoConnectPolyVCO(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid)
{
    if (!synth || !colorTracker) return;

    // 1. Get ColorTracker info and get number of tracked colors
    auto colorTrackerNodeId = synth->getNodeIdForLogical(colorTrackerLid);
    ImVec2 colorTrackerPos = ImNodes::GetNodeGridSpacePos((int)colorTrackerLid);
    
    // Get tracked colors count (thread-safe access needed)
    int numColors = 0;
    {
        const juce::ScopedLock lock(colorTracker->getColorListLock()); // You'll need to expose this
        numColors = (int)colorTracker->getTrackedColors().size(); // You'll need to expose this method
    }
    
    if (numColors == 0)
    {
        juce::Logger::writeToLog("[ColorTracker Auto-Connect] No colors tracked, aborting.");
        return;
    }
    
    // 2. Create PolyVCO with matching number of voices
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(colorTrackerPos.x + 400.0f, colorTrackerPos.y);
    
    if (auto* vco = dynamic_cast<PolyVCOModuleProcessor*>(synth->getModuleForLogical(polyVcoLid)))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(vco->getAPVTS().getParameter("numVoices")))
            *p = numColors;
    }
    
    // 3. Create Track Mixer
    auto mixerNodeId = synth->addModule("track_mixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(colorTrackerPos.x + 800.0f, colorTrackerPos.y);
    
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid)))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")))
            *p = numColors;
    }

    // 4. Connect ColorTracker outputs to PolyVCO inputs
    // ColorTracker outputs: Each color has 3 outputs (X, Y, Area)
    // PolyVCO inputs: Channel 0 = NumVoices Mod, Channel 1+ = Freq Mod per voice, etc.
    
    for (int i = 0; i < numColors; ++i)
    {
        // ColorTracker output channel mapping:
        // Channel (i * 3 + 0) = X position (0.0-1.0)
        // Channel (i * 3 + 1) = Y position (0.0-1.0)
        // Channel (i * 3 + 2) = Area (0.0-1.0)
        
        // Map X position to pitch/frequency for voice i
        // PolyVCO frequency mod is at: 1 + i (Channel 1 = Voice 1 Freq Mod, Channel 2 = Voice 2 Freq Mod, etc.)
        synth->connect(colorTrackerNodeId, i * 3 + 0, polyVcoNodeId, 1 + i); // X → Freq Mod
        
        // Map Area to gate level for voice i
        // PolyVCO gate mod is at: 1 + MAX_VOICES * 2 + i
        const int gateModChannel = 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i;
        synth->connect(colorTrackerNodeId, i * 3 + 2, polyVcoNodeId, gateModChannel); // Area → Gate Mod
    }
    
    // 5. Connect ColorTracker "Num Colors" output to PolyVCO and TrackMixer
    // ColorTracker outputs the count of tracked colors on channel 72
    // This allows dynamic voice/track count based on actual detected colors
    const int numColorsChannel = 72; // Channel 72 = "Num Colors" output
    synth->connect(colorTrackerNodeId, numColorsChannel, polyVcoNodeId, 0); // Num Colors → NumVoices Mod
    synth->connect(colorTrackerNodeId, numColorsChannel, mixerNodeId, 64); // Num Colors → Num Tracks Mod
    
    // 6. Connect PolyVCO audio outputs to Track Mixer inputs
    for (int i = 0; i < numColors; ++i)
    {
        synth->connect(polyVcoNodeId, i, mixerNodeId, i); // Voice i → Mixer Track i
    }
    
    // 7. Connect Track Mixer to Main Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
    juce::Logger::writeToLog("[ColorTracker Auto-Connect] Connected " + juce::String(numColors) + " colors to PolyVCO.");
}
```

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 6368-6416

---

### Step 6: Implement Samplers Handler

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

Add this function (after `handleColorTrackerAutoConnectPolyVCO`):

```cpp
void ImGuiNodeEditorComponent::handleColorTrackerAutoConnectSamplers(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid)
{
    if (!synth || !colorTracker) return;

    // 1. Get ColorTracker info
    auto colorTrackerNodeId = synth->getNodeIdForLogical(colorTrackerLid);
    ImVec2 colorTrackerPos = ImNodes::GetNodeGridSpacePos((int)colorTrackerLid);
    
    // Get tracked colors count
    int numColors = 0;
    {
        const juce::ScopedLock lock(colorTracker->getColorListLock());
        numColors = (int)colorTracker->getTrackedColors().size();
    }
    
    if (numColors == 0)
    {
        juce::Logger::writeToLog("[ColorTracker Auto-Connect] No colors tracked, aborting.");
        return;
    }
    
    // 2. Create Track Mixer
    auto mixerNodeId = synth->addModule("track_mixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(colorTrackerPos.x + 800.0f, colorTrackerPos.y + 100.0f);
    
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid)))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")))
            *p = numColors;
    }

    // 3. Connect ColorTracker "Num Colors" output to TrackMixer
    // ColorTracker outputs the count of tracked colors on channel 72
    // This allows dynamic track count based on actual detected colors
    const int numColorsChannel = 72; // Channel 72 = "Num Colors" output
    synth->connect(colorTrackerNodeId, numColorsChannel, mixerNodeId, 64); // Num Colors → Num Tracks Mod
    
    // 4. Create a Sample Loader for each tracked color
    for (int i = 0; i < numColors; ++i)
    {
        auto samplerNodeId = synth->addModule("sample_loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        pendingNodePositions[(int)samplerLid] = ImVec2(colorTrackerPos.x + 400.0f, colorTrackerPos.y + (i * 220.0f));

        // Connect Sample Loader audio output to mixer
        synth->connect(samplerNodeId, 0, mixerNodeId, i); // Audio → Mixer Track i
        
        // Connect ColorTracker CV outputs to Sample Loader modulation inputs
        // SampleLoader inputs (from constructor analysis):
        // Bus 0 (channels 0-1): Pitch Mod, Speed Mod
        // Bus 1 (channels 2-3): Gate Mod, Trigger Mod
        
        // Map X position to pitch
        synth->connect(colorTrackerNodeId, i * 3 + 0, samplerNodeId, 0); // X → Pitch Mod
        
        // Map Area to gate (area > threshold = gate on)
        synth->connect(colorTrackerNodeId, i * 3 + 2, samplerNodeId, 2); // Area → Gate Mod
        
        // Optionally: Map Y to speed
        // synth->connect(colorTrackerNodeId, i * 3 + 1, samplerNodeId, 1); // Y → Speed Mod
    }
    
    // 5. Connect Track Mixer to Main Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
    juce::Logger::writeToLog("[ColorTracker Auto-Connect] Connected " + juce::String(numColors) + " colors to Sample Loaders.");
}
```

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 6323-6366

---

## ColorTracker Output Channel Mapping

Understanding the ColorTracker output structure is critical:

### Output Bus Structure

ColorTracker has **dynamic outputs** based on tracked colors. The output bus includes:

1. **Per-Color Outputs**: Each color has **3 outputs** (X, Y, Area)
2. **Num Colors Output**: A single output (channel 72) that reports the current number of tracked colors

| Output Channel | Signal | Range | Description |
|----------------|--------|-------|-------------|
| `72` | **Num Colors** | 0.0-24.0 | **Number of currently tracked colors** (for dynamic voice/track count) |
| `i * 3 + 0` | X Position | 0.0-1.0 | Normalized X coordinate (left to right) for color i |
| `i * 3 + 1` | Y Position | 0.0-1.0 | Normalized Y coordinate (top to bottom) for color i |
| `i * 3 + 2` | Area | 0.0-1.0 | Normalized blob area (size) for color i |

### Example for 3 Colors

- **Num Colors**: Channel 72 (value = 3.0)
- **Color 0**: Channels 0 (X), 1 (Y), 2 (Area)
- **Color 1**: Channels 3 (X), 4 (Y), 5 (Area)
- **Color 2**: Channels 6 (X), 7 (Y), 8 (Area)

### Num Colors Output Details

The **"Num Colors"** output (channel 72) is a **raw output** that continuously reports how many colors are currently being tracked. This value:

- Updates in real-time as colors are added/removed
- Is already implemented in ColorTracker's `processBlock()` method
- Is exposed via `getDynamicOutputPins()` as the first pin
- Can be connected to:
  - **PolyVCO Channel 0** (NumVoices Mod) - Dynamically controls how many voices are active
  - **TrackMixer Channel 64** (Num Tracks Mod) - Dynamically controls how many tracks are active

**Implementation Reference**: 
- `ColorTrackerModule.cpp` line 447-452: Outputs count to channel 72
- `ColorTrackerModule.cpp` line 471: Pin exposed via getDynamicOutputPins()

### Connection Strategies

#### For PolyVCO:
- **X Position → Frequency**: Higher X = higher pitch
- **Area → Gate Level**: Larger area = stronger gate signal
- **Y Position**: Can be used for modulation (e.g., filter cutoff)

#### For Sample Loaders:
- **X Position → Pitch Mod**: Scrub through sample
- **Area → Gate Mod**: Trigger playback when color is detected
- **Y Position → Speed Mod**: Playback speed based on vertical position

---

## PolyVCO Input Channel Mapping

Understanding PolyVCO inputs is essential for correct connections:

| Input Channel | Purpose | Description |
|--------------|---------|-------------|
| `0` | **NumVoices Mod** | **CV to control number of active voices** (connect ColorTracker "Num Colors" here) |
| `1 + i` | Freq Mod (Voice i) | Frequency modulation for voice i |
| `1 + MAX_VOICES + i` | Wave Mod (Voice i) | Waveform selection (unused in our case) |
| `1 + MAX_VOICES * 2 + i` | Gate Mod (Voice i) | Gate/amplitude for voice i |

**Formula**: For voice index `i` (0-based):
- Frequency Mod = `1 + i`
- Gate Mod = `1 + MAX_VOICES * 2 + i`

### Connecting Num Colors to PolyVCO

Connect ColorTracker's "Num Colors" output (channel 72) to PolyVCO's NumVoices Mod (channel 0) to enable dynamic voice count:

```cpp
synth->connect(colorTrackerNodeId, 72, polyVcoNodeId, 0); // Num Colors → NumVoices Mod
```

This allows PolyVCO to automatically adjust its active voice count based on how many colors are currently being tracked, ensuring only voices for detected colors are active.

**Reference**: `ImGuiNodeEditorComponent.cpp` line 6397-6398, 6405

---

## SampleLoader Input Channel Mapping

From `SampleLoaderModuleProcessor.cpp` constructor:

| Input Channel | Purpose | Description |
|--------------|---------|-------------|
| `0` | Pitch Mod | Bus 0, Channel 0 |
| `1` | Speed Mod | Bus 0, Channel 1 |
| `2` | Gate Mod | Bus 1, Channel 0 |
| `3` | Trigger Mod | Bus 1, Channel 1 |
| `4` | Range Start Mod | Bus 2, Channel 0 |
| `5` | Range End Mod | Bus 2, Channel 1 |
| `6` | Randomize | Bus 3, Channel 0 |

**Reference**: `SampleLoaderModuleProcessor.cpp` lines 12-15

---

## TrackMixer Input Channel Mapping

TrackMixer has:
- **Channels 0 to (numTracks-1)**: Audio inputs for each track
- **Channel 64**: Num Tracks Mod (dynamic control of track count)

### Connecting Num Colors to TrackMixer

Connect ColorTracker's "Num Colors" output (channel 72) to TrackMixer's Num Tracks Mod (channel 64) to enable dynamic track count:

```cpp
synth->connect(colorTrackerNodeId, 72, mixerNodeId, 64); // Num Colors → Num Tracks Mod
```

This allows the mixer to automatically adjust its active track count based on how many colors are currently being tracked, making the system respond dynamically to the number of detected colors.

---

## Required Helper Methods

You may need to expose some ColorTracker internals for the handler functions:

### Option 1: Add Getter Methods to ColorTrackerModule

**File**: `juce/Source/audio/modules/ColorTrackerModule.h`

```cpp
#if defined(PRESET_CREATOR_UI)
    // Helper for auto-connect (expose tracked colors count safely)
    int getTrackedColorsCount() const
    {
        const juce::ScopedLock lock(colorListLock);
        return (int)trackedColors.size();
    }
    
    // Expose lock for handlers (alternative to getter)
    juce::CriticalSection& getColorListLock() const { return colorListLock; }
#endif
```

### Option 2: Use Direct Access (Less Safe)

The handler can directly access `trackedColors` if you make it accessible, but this requires careful locking.

---

## Positioning Strategy

Nodes are positioned relative to the source node:

```cpp
ImVec2 sourcePos = ImNodes::GetNodeGridSpacePos((int)sourceLid);

// PolyVCO: To the right
pendingNodePositions[(int)polyVcoLid] = ImVec2(sourcePos.x + 400.0f, sourcePos.y);

// Mixer: Further right
pendingNodePositions[(int)mixerLid] = ImVec2(sourcePos.x + 800.0f, sourcePos.y);

// Samplers: Stacked vertically
pendingNodePositions[(int)samplerLid] = ImVec2(sourcePos.x + 400.0f, sourcePos.y + (i * 220.0f));
```

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 6336, 6346, 6381, 6388

---

## Testing Checklist

After implementation, verify:

- [ ] Buttons appear in ColorTracker UI when colors are tracked
- [ ] Buttons are disabled when no colors are tracked
- [ ] Clicking "Connect to PolyVCO" creates PolyVCO with correct number of voices
- [ ] Clicking "Connect to Samplers" creates correct number of SampleLoaders
- [ ] TrackMixer is created and configured with correct track count
- [ ] ColorTracker "Num Colors" (channel 72) connects to PolyVCO NumVoices Mod (channel 0)
- [ ] ColorTracker "Num Colors" (channel 72) connects to TrackMixer Num Tracks Mod (channel 64)
- [ ] ColorTracker X position maps to PolyVCO frequency
- [ ] ColorTracker Area maps to PolyVCO gate
- [ ] PolyVCO/Samplers connect to TrackMixer
- [ ] TrackMixer connects to main output
- [ ] Adding/removing colors dynamically updates PolyVCO voices and TrackMixer tracks
- [ ] Nodes are positioned correctly (no overlap)
- [ ] Connections are visible in the node editor
- [ ] Audio plays when colors are detected
- [ ] No crashes when clicking buttons rapidly
- [ ] Undo/redo works correctly

---

## Debugging Tips

### Check Console Logs

The handlers log important information:

```cpp
juce::Logger::writeToLog("[ColorTracker Auto-Connect] Connected " + juce::String(numColors) + " colors...");
```

### Verify Channel Mappings

Add temporary logging in handlers:

```cpp
juce::Logger::writeToLog("Connecting ColorTracker channel " + juce::String(i * 3 + 0) + 
                        " to PolyVCO channel " + juce::String(1 + i));
```

### Test with Known Values

Start with a single tracked color to verify basic connectivity before testing multiple colors.

---

## Common Pitfalls

1. **Channel Index Offsets**: Remember PolyVCO starts at channel 1 for voice mods (channel 0 is NumVoices)
2. **MAX_VOICES Constant**: Use `PolyVCOModuleProcessor::MAX_VOICES` for gate channel calculation
3. **Thread Safety**: Always use locks when accessing `trackedColors` from handler
4. **Position Mapping**: `pendingNodePositions` uses grid space, not screen space
5. **Graph Rebuild**: Always set `graphNeedsRebuild = true` after creating connections
6. **Snapshot**: Always call `pushSnapshot()` after auto-connect to enable undo

---

## Advanced: Custom CV Mapping

You can implement more sophisticated mappings:

```cpp
// Map X position to note scale (chromatic)
// X: 0.0-1.0 → Notes: C0 to C8 (96 semitones)
float noteValue = colorTrackerOutput * 96.0f;
// Then convert to frequency or send to a quantizer module

// Map Area to velocity/amplitude
float velocity = area * 127.0f; // MIDI velocity range

// Map Y position to filter cutoff
// Y: 0.0-1.0 → Cutoff: 20Hz to 20000Hz (logarithmic)
float cutoffHz = 20.0f * std::pow(1000.0f, y);
```

---

## Reference Files

### Implementation Files
- **`juce/Source/audio/modules/MultiSequencerModuleProcessor.h`** (lines 32-33): Atomic flags
- **`juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp`** (lines 624-625): UI buttons
- **`juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`** (lines 6323-6416): Handler implementations
- **`juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`** (lines 6661-6675): Flag checking

### Related Modules
- **`juce/Source/audio/modules/PolyVCOModuleProcessor.cpp`**: Input channel structure
- **`juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`**: Input channel structure
- **`juce/Source/audio/modules/TrackMixerModuleProcessor.cpp`**: Input channel structure

---

## Summary

The auto-connect pattern follows these steps:

1. ✅ Add atomic flags to module header
2. ✅ Add UI buttons that set flags
3. ✅ Check flags in main editor loop
4. ✅ Implement handler functions that:
   - Get source node info
   - Calculate number of targets (colors/steps)
   - Create destination modules (PolyVCO, Samplers, Mixer)
   - Configure module parameters (numVoices, numTracks)
   - Connect source outputs to destination inputs
   - Connect destination outputs to mixer
   - Connect mixer to main output
   - Position nodes appropriately
   - Set graphNeedsRebuild and pushSnapshot

This pattern is reusable for any module that wants to auto-connect based on its internal state.

