# Tempo Clock Module Integration Guide

## Overview

This guide explains how the **TempoClockModuleProcessor** integrates with the global transport system and provides patterns for implementing similar transport-aware behavior in other modules.

## Table of Contents

1. [Transport System Architecture](#transport-system-architecture)
2. [Reading Transport State](#reading-transport-state)
3. [Controlling Transport](#controlling-transport)
4. [Tempo Synchronization Modes](#tempo-synchronization-modes)
5. [Division Override System](#division-override-system)
6. [CV Input Edge Detection](#cv-input-edge-detection)
7. [Live Parameter Telemetry](#live-parameter-telemetry)
8. [UI Integration Patterns](#ui-integration-patterns)
9. [Implementation Checklist](#implementation-checklist)

---

## Transport System Architecture

### Key Concepts

The transport system provides:
- **Global BPM** - Shared tempo across all modules
- **Song Position** - Current playback position in beats
- **Play/Stop State** - Global transport control
- **Division Override** - One clock can control global subdivision
- **Tempo Control Flag** - Indicates if a module is controlling the global tempo

### Parent Processor Interface

All modules inherit from `ModuleProcessor` which provides access to the parent `ModularSynthProcessor`:

```cpp
auto* parent = getParent();  // Returns ModularSynthProcessor*
```

### Transport State Structure

The `m_currentTransport` member (inherited from `ModuleProcessor`) contains:

```cpp
struct {
    double bpm;                    // Current tempo
    double songPositionBeats;      // Current position in beats
    bool isPlaying;                // Transport running state
    // ... other fields
} m_currentTransport;
```

This structure is automatically updated **before** each `processBlock()` call via `updateTransportState()`.

---

## Reading Transport State

### Basic Pattern

```cpp
void YourModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // m_currentTransport is already updated by base class
    double currentBPM = m_currentTransport.bpm;
    double songPosition = m_currentTransport.songPositionBeats;
    bool isPlaying = m_currentTransport.isPlaying;
    
    // Use transport state in your processing...
}
```

### Computing Phase from Transport

```cpp
// Use transport position for stable, drift-free timing
double phaseBeats = m_currentTransport.songPositionBeats;

for (int i = 0; i < numSamples; ++i)
{
    // Advance phase using current BPM
    phaseBeats += (1.0 / sampleRate) * (currentBPM / 60.0);
    
    // Compute subdivision phase
    double scaled = phaseBeats * division;
    double frac = scaled - std::floor(scaled);
    
    // Generate clock output from phase...
}
```

**Why this matters:** Using the transport position ensures all modules stay in sync, even when BPM changes or transport is reset.

---

## Controlling Transport

### Setting Global BPM

```cpp
if (auto* parent = getParent())
{
    parent->setBPM(newBPM);
}
```

### Play/Stop/Reset

```cpp
if (auto* parent = getParent())
{
    parent->setPlaying(true);   // Start transport
    parent->setPlaying(false);  // Stop transport
    parent->resetTransportPosition();  // Reset to beat 0
}
```

### Example: Responding to CV Triggers

```cpp
// Edge detection helper
auto edge = [&](const float* cv, bool& last) {
    bool now = (cv && cv[0] > 0.5f);
    bool rising = now && !last;
    last = now;
    return rising;
};

// Process transport control inputs
if (edge(playCV, lastPlayHigh))   if (auto* p = getParent()) p->setPlaying(true);
if (edge(stopCV, lastStopHigh))   if (auto* p = getParent()) p->setPlaying(false);
if (edge(resetCV, lastResetHigh)) if (auto* p = getParent()) p->resetTransportPosition();
```

---

## Tempo Synchronization Modes

The Tempo Clock module supports two modes:

### Mode 1: Controlling Transport (Default)

```cpp
// This module CONTROLS the global BPM
if (auto* parent = getParent())
{
    parent->setBPM(localBPM);
    parent->setTempoControlledByModule(true);  // Mark as controlling
}
```

**Effect:**
- Module pushes its BPM to global transport
- Other modules see this BPM in `m_currentTransport.bpm`
- Top bar BPM control is **greyed out** (controlled by module)

### Mode 2: Syncing to Transport

```cpp
// This module FOLLOWS the global BPM
if (auto* parent = getParent())
{
    localBPM = (float)m_currentTransport.bpm;  // Read from transport
    parent->setTempoControlledByModule(false);  // Not controlling
}
```

**Effect:**
- Module reads BPM from transport
- Top bar BPM control is **enabled** (user can adjust)
- Module responds to external tempo changes

### Implementing Switchable Modes

```cpp
bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;

if (auto* parent = getParent())
{
    if (syncToHost)
    {
        // Mode 2: FOLLOW transport
        bpm = (float)m_currentTransport.bpm;
        parent->setTempoControlledByModule(false);
    }
    else
    {
        // Mode 1: CONTROL transport
        parent->setBPM(bpm);
        parent->setTempoControlledByModule(true);
    }
}
```

---

## Division Override System

### Purpose

Allows one Tempo Clock to broadcast its subdivision (1/4, 1/16, etc.) globally, forcing all synced modules to use the same division.

### Implementation

#### Broadcasting Division (Master Clock)

```cpp
bool divisionOverride = divisionOverrideParam && divisionOverrideParam->load() > 0.5f;
int localDivisionIdx = divisionParam ? (int)divisionParam->load() : 3;  // 0=1/32, 3=1/4, etc.

if (auto* parent = getParent())
{
    if (divisionOverride)
    {
        // Broadcast this module's division globally
        parent->setGlobalDivisionIndex(localDivisionIdx);
    }
    else
    {
        // Not overriding - clear the broadcast
        parent->setGlobalDivisionIndex(-1);
    }
}
```

#### Reading Global Division (Follower Module)

```cpp
if (auto* parent = getParent())
{
    int globalDiv = parent->getGlobalDivisionIndex();
    
    if (globalDiv >= 0)
    {
        // Use global division (master clock is broadcasting)
        divisionIdx = globalDiv;
    }
    else
    {
        // Use local division parameter
        divisionIdx = (int)divisionParam->load();
    }
}
```

### Division Index to Value Mapping

```cpp
static const double divisions[] = {
    1.0/32.0,  // idx 0: 1/32 notes
    1.0/16.0,  // idx 1: 1/16 notes
    1.0/8.0,   // idx 2: 1/8 notes
    1.0/4.0,   // idx 3: 1/4 notes (quarter notes)
    1.0/2.0,   // idx 4: 1/2 notes (half notes)
    1.0,       // idx 5: whole notes
    2.0,       // idx 6: 2 bars
    4.0        // idx 7: 4 bars
};

double div = divisions[juce::jlimit(0, 7, divisionIdx)];
double scaled = phaseBeats * div;  // Scale transport position by division
```

---

## CV Input Edge Detection

### Pattern: Reading CV Inputs Only When Connected

```cpp
// Check connection state ONCE per buffer (fast)
const bool playMod = isParamInputConnected("play_mod");
const bool stopMod = isParamInputConnected("stop_mod");
const bool resetMod = isParamInputConnected("reset_mod");

// Get read pointers ONLY if connected
const float* playCV  = (playMod  && in.getNumChannels() > 4) ? in.getReadPointer(4) : nullptr;
const float* stopCV  = (stopMod  && in.getNumChannels() > 5) ? in.getReadPointer(5) : nullptr;
const float* resetCV = (resetMod && in.getNumChannels() > 6) ? in.getReadPointer(6) : nullptr;
```

**Benefits:**
- Zero overhead when inputs are disconnected
- Follows best practices from TTS/BestPractice modules

### Edge Detection Helper

```cpp
// Lambda for detecting rising edges (0->1 transitions)
auto edge = [&](const float* cv, bool& last) {
    bool now = (cv && cv[0] > 0.5f);  // High when CV > 0.5
    bool rising = now && !last;        // Rising edge
    last = now;                        // Store state for next frame
    return rising;
};

// Usage:
if (edge(playCV, lastPlayHigh))   { /* Trigger action */ }
if (edge(stopCV, lastStopHigh))   { /* Trigger action */ }
if (edge(resetCV, lastResetHigh)) { /* Trigger action */ }
```

### Member Variables for Edge Detection

```cpp
class YourModuleProcessor : public ModuleProcessor
{
private:
    // Edge detection state
    bool lastPlayHigh = false;
    bool lastStopHigh = false;
    bool lastResetHigh = false;
    bool lastTapHigh = false;
    // ... etc
};
```

---

## Live Parameter Telemetry

### Purpose

Publish real-time values to the UI so parameters can display actual processed values (e.g., after CV modulation).

### Publishing Values

```cpp
void YourModuleProcessor::processBlock(...)
{
    // Compute final values (after CV modulation)
    float finalBPM = bpmParam->load();
    if (bpmCV)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, bpmCV[0]);
        finalBPM = juce::jmap(std::pow(cv, 0.3f), 0.0f, 1.0f, 20.0f, 300.0f);
    }
    
    // Publish live telemetry (used by UI)
    setLiveParamValue("bpm_live", finalBPM);
    setLiveParamValue("swing_live", finalSwing);
    setLiveParamValue("phase_live", currentPhase);
}
```

### Reading Values in UI

```cpp
void YourModuleProcessor::drawParametersInNode(...)
{
    // Check if parameter is modulated
    bool bpmMod = isParamModulated("bpm_mod");
    
    // Read live value if modulated, otherwise read parameter
    float displayBPM = bpmMod 
        ? getLiveParamValueFor("bpm_mod", "bpm_live", bpmParam->load())
        : bpmParam->load();
    
    // Display with modulation indicator
    if (bpmMod) ImGui::BeginDisabled();
    ImGui::SliderFloat("BPM", &displayBPM, 20.0f, 300.0f, "%.1f");
    if (bpmMod) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted("(mod)");
    }
}
```

---

## UI Integration Patterns

### Pattern 1: Disabling Controls When Synced

```cpp
// Grey out BPM control when synced to host
bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;

if (syncToHost) ImGui::BeginDisabled();

if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f"))
{
    if (!syncToHost)  // Only allow changes when NOT synced
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("bpm")))
            *p = bpm;
    }
}

if (syncToHost) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.8f, 1.0f), "(synced)");
}
```

### Pattern 2: Status Indicators

```cpp
// Show sync status prominently
if (syncToHost)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.8f, 1.0f));
    ImGui::Text("⚡ SYNCED TO HOST TRANSPORT");
    ImGui::PopStyleColor();
}

if (divisionOverride)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
    ImGui::Text("⚡ MASTER DIVISION SOURCE");
    ImGui::PopStyleColor();
}
```

### Pattern 3: Live Clock Display

```cpp
// Animated beat indicator (4 boxes for 4/4 time)
float phase = getLiveParamValue("phase_live", 0.0f);
int currentBeat = (int)(phase * 4.0f) % 4;

for (int i = 0; i < 4; ++i)
{
    if (i > 0) ImGui::SameLine();
    
    bool isCurrentBeat = (currentBeat == i);
    ImVec4 color = isCurrentBeat 
        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)  // Red when active
        : ImVec4(0.3f, 0.3f, 0.3f, 1.0f); // Grey when inactive
    
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::Button(juce::String(i + 1).toRawUTF8(), ImVec2(itemWidth * 0.23f, 30));
    ImGui::PopStyleColor();
}

// Current BPM display
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.7f, 1.0f));
ImGui::Text("♩ = %.1f BPM", getLiveParamValue("bpm_live", bpm));
ImGui::PopStyleColor();
```

### Pattern 4: Help Tooltips

```cpp
auto HelpMarker = [](const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
};

// Usage:
ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f);
ImGui::SameLine();
HelpMarker("Beats per minute (20-300 BPM)\nDisabled when synced to host");
```

---

## Implementation Checklist

### For Modules That Need Transport Awareness

- [ ] **Read transport state** from `m_currentTransport` (automatically updated)
- [ ] **Use transport position** for timing instead of local counters
- [ ] **Respect global BPM** unless your module is controlling it
- [ ] **Add sync parameter** if module can follow OR control transport
- [ ] **Call `setTempoControlledByModule()`** to indicate control state
- [ ] **Check global division** if your module uses subdivisions
- [ ] **Publish live telemetry** for UI display

### For Modules That Control Transport

- [ ] **Call `setBPM()`** when controlling global tempo
- [ ] **Call `setTempoControlledByModule(true)`** when active
- [ ] **Call `setTempoControlledByModule(false)`** when syncing
- [ ] **Handle play/stop/reset** from CV inputs
- [ ] **Disable BPM control** in UI when syncing to host
- [ ] **Show sync status** prominently in UI

### For Modules That Use Division Override

- [ ] **Check `getGlobalDivisionIndex()`** before using local division
- [ ] **Use global division** when >= 0, local division when -1
- [ ] **Optionally broadcast** division using `setGlobalDivisionIndex()`
- [ ] **Add divisionOverride parameter** if module can be master
- [ ] **Show override status** in UI

### Parameter Routing

- [ ] **Map CV inputs** to virtual parameter IDs
- [ ] **Implement `getParamRouting()`** for input routing
- [ ] **Check connections** before reading CV buffers
- [ ] **Use edge detection** for trigger inputs
- [ ] **Store edge state** in member variables

### UI Best Practices

- [ ] **Disable controls** when externally controlled
- [ ] **Show modulation indicators** "(mod)" next to live values
- [ ] **Show sync status** with colored text/icons
- [ ] **Display live values** using telemetry
- [ ] **Add help tooltips** for complex parameters
- [ ] **Group related controls** with section headers
- [ ] **Use consistent colors** (green=synced, yellow=master, red=active)

---

## Common Patterns Summary

### Reading BPM

```cpp
// Simple: Just read current BPM
double bpm = m_currentTransport.bpm;
```

### Controlling BPM

```cpp
// Set global BPM and mark as controlling
if (auto* p = getParent()) {
    p->setBPM(myBPM);
    p->setTempoControlledByModule(true);
}
```

### Reading Division

```cpp
// Check for global division override
int div = 3;  // Default to 1/4
if (auto* p = getParent()) {
    int globalDiv = p->getGlobalDivisionIndex();
    if (globalDiv >= 0) div = globalDiv;
    else div = (int)divisionParam->load();
}
```

### Syncing to Transport

```cpp
// Use transport position for stable timing
double phase = m_currentTransport.songPositionBeats;
for (int i = 0; i < numSamples; ++i) {
    phase += (1.0 / sampleRate) * (bpm / 60.0);
    // ... use phase for clock generation
}
```

---

## Example: Minimal Transport-Aware Module

```cpp
class MyTransportModule : public ModuleProcessor
{
public:
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        // 1. Read transport state (already updated by base class)
        double bpm = m_currentTransport.bpm;
        double songPos = m_currentTransport.songPositionBeats;
        bool playing = m_currentTransport.isPlaying;
        
        // 2. Check for global division override
        int divIdx = 3;  // Default 1/4
        if (auto* p = getParent()) {
            int globalDiv = p->getGlobalDivisionIndex();
            if (globalDiv >= 0) divIdx = globalDiv;
        }
        
        // 3. Use transport position for timing
        double phase = songPos;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            phase += (1.0 / sampleRate) * (bpm / 60.0);
            // ... process using phase
        }
        
        // 4. Publish telemetry for UI
        setLiveParamValue("phase_live", (float)phase);
    }
};
```

---

## Questions to Ask When Implementing

1. **Does my module need to know the current BPM?**
   - YES: Read `m_currentTransport.bpm`
   - Add sync parameter if module should control OR follow

2. **Does my module need timing/phase information?**
   - YES: Use `m_currentTransport.songPositionBeats`
   - Advance phase using BPM/sampleRate formula

3. **Should my module respect global division?**
   - YES: Check `getGlobalDivisionIndex()` before using local division
   - Fallback to local division when global is -1

4. **Does my module generate tempo?**
   - YES: Call `setBPM()` and `setTempoControlledByModule(true)`
   - Disable in UI when syncing to host

5. **Does my module respond to transport controls?**
   - YES: Handle play/stop/reset from CV inputs
   - Use edge detection pattern

6. **Do I need to show live values in UI?**
   - YES: Publish telemetry with `setLiveParamValue()`
   - Read with `getLiveParamValue()` in UI code

---

## Related Files

- `TempoClockModuleProcessor.cpp` - Reference implementation
- `ModularSynthProcessor.h` - Parent processor interface
- `ModuleProcessor.h` - Base class with transport support
- `StepSequencerModuleProcessor.cpp` - Example of division override usage

---

## Conclusion

The transport system provides a unified way for modules to:
- Share timing information
- Control or follow global tempo
- Synchronize subdivisions
- Respond to transport controls

By following these patterns, your modules will integrate seamlessly with the rest of the synthesis environment and provide a consistent user experience.

For most modules, you only need to:
1. Read `m_currentTransport` for timing
2. Check global division if using subdivisions
3. Show live values in UI with telemetry

Advanced modules can control the transport or broadcast divisions, but this is optional.

