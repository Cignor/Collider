# MIDI Output Integration Plan - MIDI Player Node

## Overview

Enable the MIDI Player module to output actual MIDI messages to hardware MIDI output devices, allowing VSTi plugins and external MIDI gear to receive MIDI from the loaded MIDI file.

## Current Architecture Analysis

### Current MIDI Player Behavior

The `MIDIPlayerModuleProcessor` currently:
- ✅ Loads MIDI files and parses note data
- ✅ Converts MIDI note data to CV/Gate outputs (Pitch, Gate, Velocity, Trigger)
- ✅ Supports polyphonic multi-track playback
- ❌ **Does NOT output actual MIDI messages** to MIDI output devices

### Current System State

1. **MIDI Input**: Handled by `MidiDeviceManager` → routes to modules via `handleDeviceSpecificMidi()`
2. **MIDI Processing**: Modules receive MIDI in `processBlock()` via `juce::MidiBuffer`
3. **MIDI Output**: **NOT IMPLEMENTED** - MIDI messages go nowhere after processing

### Key Code Locations

- **MIDI Player**: `juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp`
- **Audio Settings**: `juce/Source/preset_creator/PresetCreatorComponent.cpp` (line 186)
- **Audio Device Manager**: Shared `juce::AudioDeviceManager` in `PresetCreatorComponent`

## Integration Strategy

### Approach: Generate MIDI Messages from MIDI File Data

Since the MIDI Player already has parsed MIDI file data (`notesByTrack`), we can generate `juce::MidiMessage` objects directly from the active notes during playback.

**Advantages:**
- ✅ No need to decode CV back to MIDI (lossy conversion avoided)
- ✅ Access to original velocity, note numbers, timing
- ✅ Can handle all MIDI events (notes, CC, pitch bend, etc.)
- ✅ Preserves original MIDI channel assignments

**Implementation:**
1. Track active notes per track/channel during playback
2. Generate Note On/Off messages when notes start/end
3. Add messages to `MidiBuffer` in `processBlock()`
4. Route `MidiBuffer` to selected MIDI output device

---

## Implementation Plan

### Phase 1: MIDI Message Generation in MIDI Player

#### 1.1 Add MIDI Message Tracking

**File**: `MIDIPlayerModuleProcessor.h`

Add member variables to track active MIDI notes:

```cpp
private:
    // MIDI Output State
    struct ActiveMIDINote {
        int noteNumber;
        int velocity;
        double startTime;
        bool isActive;
    };
    
    std::vector<std::vector<ActiveMIDINote>> activeMIDINotes; // per track
    std::atomic<bool> enableMIDIOutput { false }; // Parameter: enable/disable MIDI output
    std::atomic<int> midiOutputChannel { 1 }; // Which MIDI channel to output to (1-16, 0 = use track's original channel)
```

#### 1.2 Generate MIDI Messages in processBlock()

**File**: `MIDIPlayerModuleProcessor.cpp`

Modify `processBlock()` to:
1. Track note onsets/offsets (compare `currentPlaybackTime` to note `startTime`/`endTime`)
2. Generate `juce::MidiMessage::noteOn()` and `juce::MidiMessage::noteOff()`
3. Add messages to the `midiMessages` buffer

**Code Addition** (in `processBlock()`, after CV generation):

```cpp
void MIDIPlayerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // ... existing CV generation code ...
    
    // === MIDI OUTPUT GENERATION ===
    if (enableMIDIOutput.load())
    {
        const int numSamples = buffer.getNumSamples();
        const double sampleRate = getSampleRate();
        const double timePerSample = 1.0 / sampleRate;
        
        // Process each track
        for (int trackIdx = 0; trackIdx < (int)notesByTrack.size(); ++trackIdx)
        {
            const auto& trackNotes = notesByTrack[trackIdx];
            const int midiChannel = (midiOutputChannel.load() > 0) 
                ? midiOutputChannel.load() - 1  // Convert 1-16 to 0-15
                : trackIdx % 16;  // Use track index if channel is 0 (auto)
            
            // Check for note onsets and offsets
            for (const auto& note : trackNotes)
            {
                const double noteStartSample = note.startTime * sampleRate;
                const double noteEndSample = note.endTime * sampleRate;
                const double currentSample = currentPlaybackTime * sampleRate;
                
                // Note On: at the exact start time
                if (currentSample >= noteStartSample && 
                    currentSample < noteStartSample + numSamples)
                {
                    int sampleOffset = (int)(currentSample - noteStartSample);
                    sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                    
                    juce::MidiMessage noteOn = juce::MidiMessage::noteOn(
                        midiChannel + 1,  // JUCE uses 1-16
                        note.noteNumber,
                        (juce::uint8)note.velocity
                    );
                    midiMessages.addEvent(noteOn, sampleOffset);
                }
                
                // Note Off: at the exact end time
                if (currentSample >= noteEndSample && 
                    currentSample < noteEndSample + numSamples)
                {
                    int sampleOffset = (int)(currentSample - noteEndSample);
                    sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                    
                    juce::MidiMessage noteOff = juce::MidiMessage::noteOff(
                        midiChannel + 1,
                        note.noteNumber,
                        (juce::uint8)note.velocity
                    );
                    midiMessages.addEvent(noteOff, sampleOffset);
                }
            }
        }
    }
}
```

**Note**: This approach generates MIDI messages during playback. For more accurate timing, we could pre-generate all messages and use a sample-accurate scheduler, but the current approach is simpler and adequate for most use cases.

---

### Phase 2: MIDI Output Device Selection

#### 2.1 Create MidiOutputManager

**File**: `juce/Source/audio/MidiOutputManager.h` (NEW)

```cpp
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <memory>

/**
 * @brief Manages MIDI output device selection and message routing
 * 
 * Handles:
 * - Enumerating available MIDI output devices
 * - Opening/closing selected output device
 * - Thread-safe MIDI message sending
 */
class MidiOutputManager
{
public:
    MidiOutputManager();
    ~MidiOutputManager();
    
    // Device Management
    std::vector<juce::String> getAvailableOutputDevices() const;
    juce::String getCurrentOutputDevice() const { return currentDeviceIdentifier; }
    bool setOutputDevice(const juce::String& deviceIdentifier);
    void closeCurrentDevice();
    
    // Message Sending (thread-safe, can be called from audio thread)
    void sendMIDI(const juce::MidiMessage& message);
    void sendMIDIBlock(const juce::MidiBuffer& messages);
    
    // Persistence
    juce::String getSavedDeviceIdentifier() const;
    void saveDeviceIdentifier(const juce::String& identifier);
    
private:
    std::unique_ptr<juce::MidiOutput> currentOutput;
    juce::String currentDeviceIdentifier;
    juce::CriticalSection outputLock;  // Thread safety for device switching
    
    // Persistence key
    static constexpr const char* SETTINGS_KEY = "midiOutputDevice";
};
```

**File**: `juce/Source/audio/MidiOutputManager.cpp` (NEW)

```cpp
#include "MidiOutputManager.h"

MidiOutputManager::MidiOutputManager()
{
    // Load saved device preference
    auto* props = juce::PropertiesFile::getCommonSettings(true);
    if (props)
    {
        juce::String savedDevice = props->getValue(SETTINGS_KEY);
        if (savedDevice.isNotEmpty())
        {
            setOutputDevice(savedDevice);
        }
    }
}

MidiOutputManager::~MidiOutputManager()
{
    closeCurrentDevice();
}

std::vector<juce::String> MidiOutputManager::getAvailableOutputDevices() const
{
    std::vector<juce::String> devices;
    auto available = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : available)
    {
        devices.push_back(device.identifier);
    }
    return devices;
}

bool MidiOutputManager::setOutputDevice(const juce::String& deviceIdentifier)
{
    const juce::ScopedLock lock(outputLock);
    
    // Close current device if open
    closeCurrentDevice();
    
    // Open new device
    currentOutput = juce::MidiOutput::openDevice(deviceIdentifier);
    if (currentOutput)
    {
        currentDeviceIdentifier = deviceIdentifier;
        juce::Logger::writeToLog("[MidiOutputManager] Opened device: " + deviceIdentifier);
        return true;
    }
    
    juce::Logger::writeToLog("[MidiOutputManager] Failed to open device: " + deviceIdentifier);
    return false;
}

void MidiOutputManager::closeCurrentDevice()
{
    if (currentOutput)
    {
        currentOutput->clearAllPendingMessages();
        currentOutput.reset();
        currentDeviceIdentifier.clear();
    }
}

void MidiOutputManager::sendMIDI(const juce::MidiMessage& message)
{
    const juce::ScopedLock lock(outputLock);
    if (currentOutput)
    {
        currentOutput->sendMessageNow(message);
    }
}

void MidiOutputManager::sendMIDIBlock(const juce::MidiBuffer& messages)
{
    const juce::ScopedLock lock(outputLock);
    if (currentOutput)
    {
        for (const auto metadata : messages)
        {
            currentOutput->sendMessageNow(metadata.getMessage());
        }
    }
}
```

#### 2.2 Integrate into PresetCreatorComponent

**File**: `PresetCreatorComponent.h`

```cpp
#include "../audio/MidiOutputManager.h"

class PresetCreatorComponent : public juce::Component, ...
{
    // ... existing code ...
    
    std::unique_ptr<MidiOutputManager> midiOutputManager;  // ADD THIS
};
```

**File**: `PresetCreatorComponent.cpp`

```cpp
PresetCreatorComponent::PresetCreatorComponent(...)
{
    // ... existing initialization ...
    
    // Initialize MIDI output manager
    midiOutputManager = std::make_unique<MidiOutputManager>();
}

PresetCreatorComponent::~PresetCreatorComponent()
{
    midiOutputManager.reset();  // Clean shutdown
}
```

---

### Phase 3: Route MIDI from Player to Output

#### 3.1 Collect MIDI from Modules

**File**: `ModularSynthProcessor.cpp`

Modify `processBlock()` to collect MIDI messages from all modules:

```cpp
void ModularSynthProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    // ... existing MIDI input processing ...
    
    // === COLLECT MIDI OUTPUT FROM MODULES ===
    juce::MidiBuffer moduleMIDIOutput;
    
    // Process graph (modules may add MIDI to midiMessages)
    internalGraph->processBlock(buffer, midiMessages);
    
    // After graph processing, midiMessages contains:
    // 1. Input MIDI (already processed)
    // 2. Module-generated MIDI (from MIDI Player, etc.)
    
    // Extract module-generated MIDI for output routing
    // (Input MIDI is already processed, so everything remaining is from modules)
    moduleMIDIOutput.addEvents(midiMessages, 0, buffer.getNumSamples(), 0);
    
    // Store for output routing (handled by PresetCreatorComponent)
    lastModuleMIDIOutput.swapWith(moduleMIDIOutput);
}
```

**File**: `ModularSynthProcessor.h`

```cpp
private:
    juce::MidiBuffer lastModuleMIDIOutput;  // MIDI generated by modules
    juce::CriticalSection midiOutputLock;
    
public:
    // Thread-safe access to module-generated MIDI
    void getModuleMIDIOutput(juce::MidiBuffer& targetBuffer)
    {
        const juce::ScopedLock lock(midiOutputLock);
        targetBuffer.swapWith(lastModuleMIDIOutput);
        lastModuleMIDIOutput.clear();
    }
```

#### 3.2 Send MIDI to Output Device

**File**: `PresetCreatorComponent.cpp`

Add to `timerCallback()` (runs at 30Hz):

```cpp
void PresetCreatorComponent::timerCallback()
{
    // ... existing MIDI input handling ...
    
    // === MIDI OUTPUT ROUTING ===
    if (midiOutputManager && synth)
    {
        juce::MidiBuffer moduleMIDI;
        synth->getModuleMIDIOutput(moduleMIDI);
        
        if (!moduleMIDI.isEmpty())
        {
            midiOutputManager->sendMIDIBlock(moduleMIDI);
        }
    }
}
```

**Alternative Approach (Better Timing)**: Send MIDI directly from audio thread:

In `ModularSynthProcessor::processBlock()`, after graph processing:
```cpp
// Give MIDI output manager direct access
if (auto* owner = getOwnerProcessor())
{
    if (auto* manager = owner->getMidiOutputManager())
    {
        manager->sendMIDIBlock(midiMessages);
    }
}
```

But this requires passing manager reference through processor chain - simpler to use timer callback approach initially.

---

### Phase 4: UI Integration - Audio Settings Dropdown

#### 4.1 Enable MIDI Output in Audio Settings Dialog

**File**: `PresetCreatorComponent.cpp`

Modify `showAudioSettingsDialog()` to enable MIDI output selection:

```cpp
void PresetCreatorComponent::showAudioSettingsDialog()
{
    // ... existing dialog setup ...
    
    // CHANGE THIS LINE:
    // OLD: auto* component = new juce::AudioDeviceSelectorComponent(
    //     deviceManager, 0, 256, 0, 256, true, true, false, false);
    
    // NEW: Enable MIDI output selection (last parameter = true)
    auto* component = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, 256, 0, 256, true, true, false, true);  // ← Enable MIDI output
    
    // ... rest of dialog code ...
}
```

**Note**: `AudioDeviceSelectorComponent` constructor signature:
```cpp
AudioDeviceSelectorComponent(
    AudioDeviceManager& deviceManager,
    int minAudioInputChannels, int maxAudioInputChannels,
    int minAudioOutputChannels, int maxAudioOutputChannels,
    bool showMidiInputDevices, bool showMidiOutputDevices,  // ← These control MIDI UI
    bool showChannelsAsStereoPairs, bool hideAdvancedOptions
);
```

#### 4.2 Connect Audio Settings to MidiOutputManager

**Problem**: `AudioDeviceSelectorComponent` manages MIDI output via `AudioDeviceManager`, but we need to also update `MidiOutputManager`.

**Solution**: Listen for device changes and sync:

```cpp
void PresetCreatorComponent::showAudioSettingsDialog()
{
    // ... create dialog component ...
    
    // Create a listener to detect MIDI output device changes
    struct MIDIOutputListener : public juce::ChangeListener
    {
        MIDIOutputListener(PresetCreatorComponent* owner, MidiOutputManager* mgr)
            : owner(owner), manager(mgr) {}
        
        void changeListenerCallback(juce::ChangeBroadcaster* source) override
        {
            if (auto* selector = dynamic_cast<juce::AudioDeviceSelectorComponent*>(source))
            {
                // Get current MIDI output device from AudioDeviceManager
                juce::StringArray midiOutputs;
                owner->deviceManager.getAvailableDeviceTypes()[0]->getDeviceNames(midiOutputs, true);  // true = output devices
                
                // Find which device is selected (via AudioDeviceManager's MIDI output setup)
                // NOTE: AudioDeviceManager doesn't directly expose MIDI output selection
                // We need to read from the selector component or use a different approach
            }
        }
        
        PresetCreatorComponent* owner;
        MidiOutputManager* manager;
    };
    
    // ... show dialog ...
}
```

**Better Approach**: Add custom MIDI output dropdown in a custom settings panel, or extend the Audio Settings dialog with a custom section.

#### 4.3 Custom MIDI Output Selection UI

**Option A: Add to existing Audio Settings dialog** (Complex - requires custom component)
**Option B: Separate "MIDI Settings" dialog** (Simpler)
**Option C: Add to MIDI Device Manager window** (Best - already exists!)

**RECOMMENDED: Option C** - Add MIDI output dropdown to existing MIDI Device Manager UI.

**File**: `ImGuiNodeEditorComponent.cpp` (around line 7963)

Add MIDI output selection to the MIDI Device Manager window:

```cpp
if (showMidiDeviceManager)
{
    if (ImGui::Begin("MIDI Device Manager", &showMidiDeviceManager, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // ... existing MIDI input device list ...
        
        // === MIDI OUTPUT SELECTION ===
        ImGui::Separator();
        ImGui::Text("MIDI Output Device:");
        
        if (presetCreator && presetCreator->midiOutputManager)
        {
            auto& outputMgr = *presetCreator->midiOutputManager;
            auto devices = outputMgr.getAvailableOutputDevices();
            
            juce::String currentDevice = outputMgr.getCurrentOutputDevice();
            const char* previewText = currentDevice.isNotEmpty() 
                ? currentDevice.toRawUTF8() 
                : "<None Selected>";
            
            if (ImGui::BeginCombo("##midi_output", previewText))
            {
                // "None" option
                bool noneSelected = currentDevice.isEmpty();
                if (ImGui::Selectable("<None>", noneSelected))
                {
                    outputMgr.closeCurrentDevice();
                }
                if (noneSelected) ImGui::SetItemDefaultFocus();
                
                // Device list
                for (const auto& deviceId : devices)
                {
                    bool isSelected = (deviceId == currentDevice);
                    if (ImGui::Selectable(deviceId.toRawUTF8(), isSelected))
                    {
                        outputMgr.setOutputDevice(deviceId);
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                
                ImGui::EndCombo();
            }
        }
    }
    ImGui::End();
}
```

---

### Phase 5: MIDI Player UI Controls

#### 5.1 Add Enable MIDI Output Parameter

**File**: `MIDIPlayerModuleProcessor.cpp`

Add to `createParameterLayout()`:

```cpp
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    // ... existing parameters ...
    
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        "enable_midi_output", "Enable MIDI Output", false));
    
    parameters.push_back(std::make_unique<juce::AudioParameterInt>(
        "midi_output_channel", "MIDI Output Channel", 0, 16, 0));  // 0 = auto (use track channel)
    
    return { parameters.begin(), parameters.end() };
}
```

#### 5.2 Add UI Controls

**File**: `MIDIPlayerModuleProcessor.cpp` (in `drawParametersInNode()`)

```cpp
// MIDI Output Section
ImGui::Separator();
ImGui::Text("MIDI Output:");

bool enableMIDI = enableMIDIOutputParam->get();
if (ImGui::Checkbox("Send MIDI", &enableMIDI))
{
    enableMIDIOutputParam->setValueNotifyingHost(enableMIDI ? 1.0f : 0.0f);
    onModificationEnded();
}

if (enableMIDI)
{
    int channel = midiOutputChannelParam->get();
    if (ImGui::SliderInt("Channel", &channel, 0, 16))
    {
        // 0 = auto (use track's original channel)
        midiOutputChannelParam->setValueNotifyingHost(
            apvts.getParameterRange("midi_output_channel").convertTo0to1((float)channel));
        onModificationEnded();
    }
    if (channel == 0)
        ImGui::TextDisabled("(Auto: uses track's original channel)");
}
```

---

## Implementation Difficulty Assessment

### Overall Difficulty: **MEDIUM (5/10)**

| Task | Difficulty | Time | Notes |
|------|-----------|------|-------|
| MIDI message generation | 4/10 | 4-6 hours | Straightforward note On/Off generation |
| MidiOutputManager class | 3/10 | 2-3 hours | Simple wrapper around JUCE MidiOutput |
| Routing to output device | 5/10 | 3-4 hours | Need to collect MIDI from modules |
| UI integration | 6/10 | 4-6 hours | Add to MIDI Device Manager window |
| MIDI Player UI controls | 3/10 | 2-3 hours | Simple checkbox and slider |
| **TOTAL** | **5/10** | **15-22 hours** | ~2-3 days of work |

---

## Testing Strategy

1. **Unit Tests**:
   - MIDI message generation (verify Note On/Off timing)
   - Device opening/closing
   - Thread safety of output manager

2. **Integration Tests**:
   - Load MIDI file → verify MIDI output received
   - Change output device → verify switching works
   - Multiple MIDI players → verify messages don't conflict

3. **Manual Testing**:
   - Use MIDI monitor (e.g., MIDI-OX) to verify messages
   - Test with VSTi plugin receiving MIDI
   - Test with external hardware synth
   - Verify timing accuracy

---

## Potential Issues & Solutions

### Issue 1: Timing Accuracy
**Problem**: Timer callback (30Hz) may cause timing jitter
**Solution**: Initially accept small jitter. Later: use audio thread direct sending or higher-rate timer (100Hz+)

### Issue 2: Multiple MIDI Players
**Problem**: Multiple players might conflict on same channel
**Solution**: Each player can specify different MIDI channel, or use auto (track index % 16)

### Issue 3: MIDI Channel Mapping
**Problem**: MIDI file tracks may use different channels
**Solution**: Option to preserve original channel (channel = 0) or force to single channel

### Issue 4: Performance
**Problem**: Generating MIDI messages in audio thread adds overhead
**Solution**: Only generate when `enableMIDIOutput` is true, optimize note lookup (already efficient)

---

## Alternative Approaches Considered

### Option A: CV-to-MIDI Converter Module
- Separate module that converts CV/Gate → MIDI
- Pros: Reusable for other CV sources
- Cons: Lossy conversion, more complex

### Option B: Direct Audio Thread Sending
- Send MIDI directly from `processBlock()` to output
- Pros: Lowest latency
- Cons: Requires passing manager reference through processor chain

### Option C: Pre-render All MIDI Events
- Generate complete MIDI event list on load
- Pros: Most accurate timing
- Cons: More memory, less flexible for real-time modulation

**CHOSEN: Current approach** - Generate MIDI messages during playback, send via timer callback. Good balance of accuracy, simplicity, and performance.

---

## Next Steps

1. ✅ Create plan document (this)
2. ⏭️ Implement MidiOutputManager class
3. ⏭️ Add MIDI message generation to MIDI Player
4. ⏭️ Integrate routing in ModularSynthProcessor
5. ⏭️ Add UI controls to MIDI Device Manager
6. ⏭️ Add MIDI Player enable/channel parameters
7. ⏭️ Testing and refinement

---

## Code Structure Summary

```
New Files:
- juce/Source/audio/MidiOutputManager.h
- juce/Source/audio/MidiOutputManager.cpp

Modified Files:
- juce/Source/audio/modules/MIDIPlayerModuleProcessor.h
- juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp
- juce/Source/audio/graph/ModularSynthProcessor.h
- juce/Source/audio/graph/ModularSynthProcessor.cpp
- juce/Source/preset_creator/PresetCreatorComponent.h
- juce/Source/preset_creator/PresetCreatorComponent.cpp
- juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
```

---

## Success Criteria

- ✅ MIDI Player can output MIDI messages to selected device
- ✅ MIDI output device selectable via UI dropdown
- ✅ Messages sent with correct timing (within 10ms tolerance)
- ✅ Multiple MIDI players work independently
- ✅ No audio dropouts or performance degradation
- ✅ Settings persist across application restarts

