# MIDI Player Output Routing - Implementation Plan

## Overview

Add MIDI output routing to MIDI Player node with a dropdown menu that integrates seamlessly with the Audio Settings dialog. The MIDI Player will generate MIDI messages from the loaded MIDI file and route them to the selected MIDI output device.

## Key Insight: Audio Settings Integration

The Audio Settings dialog already shows a MIDI Output dropdown (as seen in your screenshot). JUCE's `AudioDeviceManager` manages the default MIDI output device. We need to:

1. **Access the selected MIDI output device** from AudioDeviceManager
2. **Allow per-module override** (MIDI Player can override default or use it)
3. **Generate MIDI messages** in MIDI Player's `processBlock()`
4. **Send to selected device** via JUCE's MidiOutput API

---

## Architecture: Two-Level MIDI Output

### Level 1: Global Default (Audio Settings)
- Set in Audio Settings dialog
- Stored in AudioDeviceManager preferences
- Used as default for all modules that need MIDI output

### Level 2: Per-Module Override (MIDI Player)
- Dropdown in MIDI Player node
- Option: "Use Global Default" or specific device
- Module-specific preference stored in module's APVTS

**This design:**
- ✅ Respects Audio Settings (global default)
- ✅ Allows per-module control (MIDI Player can override)
- ✅ Follows JUCE patterns (AudioDeviceManager integration)

---

## Implementation Plan

### Phase 1: Access AudioDeviceManager's MIDI Output

**Key Discovery**: JUCE's `AudioDeviceManager` already has direct methods for MIDI output:
- `setDefaultMidiOutputDevice(identifier)` - Sets global default
- `getDefaultMidiOutputIdentifier()` - Gets current default identifier
- `getDefaultMidiOutput()` - Gets actual MidiOutput* pointer

**Simplified Approach**: Pass AudioDeviceManager reference to MIDI Player module only (not all modules).

#### Step 1.1: Pass AudioDeviceManager Reference to MIDI Player

**File**: `MIDIPlayerModuleProcessor.h`

Add AudioDeviceManager access:

```cpp
class MIDIPlayerModuleProcessor : public ModuleProcessor
{
    // ... existing code ...
    
    // NEW: Set AudioDeviceManager for MIDI output access
    void setAudioDeviceManager(juce::AudioDeviceManager* adm) { audioDeviceManager = adm; }
    
private:
    // ... existing members ...
    
    juce::AudioDeviceManager* audioDeviceManager { nullptr };
};
```

**File**: `PresetCreatorComponent.cpp`

Pass AudioDeviceManager when creating MIDI Player (or set it later):

```cpp
// After creating synth (around line 46)
// Note: We'll need to set this after modules are created
// Better: Set it in ModularSynthProcessor when module is added

// OR: Set it directly in MIDI Player after it's created
// This can be done in a post-creation callback or via ModularSynthProcessor
```

**Better Approach**: Set it via ModularSynthProcessor when MIDI Player is added:

**File**: `ModularSynthProcessor.h/.cpp`

```cpp
// Add method to set AudioDeviceManager
void setAudioDeviceManager(juce::AudioDeviceManager* adm) 
{ 
    audioDeviceManager = adm;
    // Update all MIDI Player modules
    updateMidiPlayerAudioDeviceManager();
}

private:
    juce::AudioDeviceManager* audioDeviceManager { nullptr };
    
    void updateMidiPlayerAudioDeviceManager()
    {
        if (!audioDeviceManager) return;
        
        for (const auto& [uid, node] : modules)
        {
            if (auto* module = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
            {
                if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(module))
                {
                    midiPlayer->setAudioDeviceManager(audioDeviceManager);
                }
            }
        }
    }
```

**File**: `PresetCreatorComponent.cpp`

After synth creation:

```cpp
synth->setAudioDeviceManager(&deviceManager);
```

---

### Phase 2: MIDI Output Device Selection in MIDI Player

#### Step 2.1: Add MIDI Output Parameters

**File**: `MIDIPlayerModuleProcessor.cpp`

Add to `createParameterLayout()`:

```cpp
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    
    // ... existing parameters ...
    
    // === MIDI OUTPUT PARAMETERS ===
    // Enable/disable MIDI output
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        "enable_midi_output", "Enable MIDI Output", false));
    
    // MIDI output device selection
    // Value: -1 = use global default, 0+ = device index
    // We'll use a string parameter to store device identifier (more reliable than index)
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "midi_output_mode", "MIDI Output Mode",
        juce::StringArray("Use Global Default", "Custom Device"), 0));
    
    // Custom device identifier (empty = use global)
    // Stored as string parameter for reliability
    parameters.push_back(std::make_unique<juce::AudioParameterString>(
        "midi_output_device", "MIDI Output Device", ""));
    
    // MIDI channel for output (1-16, 0 = preserve track channels)
    parameters.push_back(std::make_unique<juce::AudioParameterInt>(
        "midi_output_channel", "MIDI Output Channel", 0, 16, 0));
    
    return { parameters.begin(), parameters.end() };
}
```

#### Step 2.2: Add MIDI Output Device Manager

**File**: `MIDIPlayerModuleProcessor.h`

Add private members:

```cpp
private:
    // ... existing members ...
    
    // === MIDI OUTPUT ===
    std::unique_ptr<juce::MidiOutput> midiOutputDevice;
    juce::String currentMidiOutputDeviceId;
    juce::CriticalSection midiOutputLock;
    
    // Parameter pointers for MIDI output
    juce::AudioParameterBool* enableMidiOutputParam { nullptr };
    juce::AudioParameterChoice* midiOutputModeParam { nullptr };
    juce::AudioParameterString* midiOutputDeviceParam { nullptr };
    juce::AudioParameterInt* midiOutputChannelParam { nullptr };
    
    // Methods
    void updateMidiOutputDevice();
    void sendMidiToOutput(const juce::MidiMessage& message, int sampleOffset);
```

#### Step 2.3: Initialize MIDI Output Parameters

**File**: `MIDIPlayerModuleProcessor.cpp`

In constructor:

```cpp
MIDIPlayerModuleProcessor::MIDIPlayerModuleProcessor()
    : ModuleProcessor(...)
    , apvts(*this, nullptr, "MIDIPlayerParameters", createParameterLayout())
{
    // ... existing initialization ...
    
    // Get MIDI output parameter pointers
    enableMidiOutputParam = dynamic_cast<juce::AudioParameterBool*>(
        apvts.getParameter("enable_midi_output"));
    midiOutputModeParam = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter("midi_output_mode"));
    midiOutputDeviceParam = dynamic_cast<juce::AudioParameterString*>(
        apvts.getParameter("midi_output_device"));
    midiOutputChannelParam = dynamic_cast<juce::AudioParameterInt*>(
        apvts.getParameter("midi_output_channel"));
}
```

---

### Phase 3: MIDI Output Device Management

#### Step 3.1: Device Selection Logic

**File**: `MIDIPlayerModuleProcessor.cpp`

Add implementation:

```cpp
void MIDIPlayerModuleProcessor::updateMidiOutputDevice()
{
    const juce::ScopedLock lock(midiOutputLock);
    
    if (!enableMidiOutputParam || !enableMidiOutputParam->get())
    {
        // MIDI output disabled - close device
        midiOutputDevice.reset();
        currentMidiOutputDeviceId.clear();
        return;
    }
    
    juce::String targetDeviceId;
    
    // Determine which device to use
    if (midiOutputModeParam && midiOutputModeParam->getCurrentChoiceName() == "Use Global Default")
    {
        // Use global default from AudioDeviceManager
        if (audioDeviceManager)
        {
            // JUCE AudioDeviceManager provides direct access to default MIDI output!
            targetDeviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
            
            // If AudioDeviceManager has the device open, we can use it directly
            // OR we can open our own copy using the identifier
        }
    }
    else
    {
        // Use custom device from parameter
        if (midiOutputDeviceParam)
            targetDeviceId = midiOutputDeviceParam->get();
    }
    
    // Only change device if identifier changed
    if (targetDeviceId == currentMidiOutputDeviceId && midiOutputDevice)
        return;
    
    // Close current device
    midiOutputDevice.reset();
    currentMidiOutputDeviceId.clear();
    
    // Open new device
    if (targetDeviceId.isNotEmpty())
    {
        midiOutputDevice = juce::MidiOutput::openDevice(targetDeviceId);
        if (midiOutputDevice)
        {
            currentMidiOutputDeviceId = targetDeviceId;
            juce::Logger::writeToLog("[MIDI Player] Opened MIDI output: " + targetDeviceId);
        }
        else
        {
            juce::Logger::writeToLog("[MIDI Player] Failed to open MIDI output: " + targetDeviceId);
        }
    }
}
```

**Better Approach**: Use AudioDeviceManager's built-in MIDI output:

```cpp
void MIDIPlayerModuleProcessor::updateMidiOutputDevice()
{
    const juce::ScopedLock lock(midiOutputLock);
    
    if (!enableMidiOutputParam || !enableMidiOutputParam->get())
    {
        midiOutputDevice.reset();
        currentMidiOutputDeviceId.clear();
        return;
    }
    
    juce::String targetDeviceId;
    
    if (midiOutputModeParam && midiOutputModeParam->getCurrentChoiceName() == "Use Global Default")
    {
        // Use AudioDeviceManager's default MIDI output
        if (audioDeviceManager)
        {
            // JUCE AudioDeviceManager stores MIDI output device ID internally
            // We need to access it via the device type's default device
            // This is a bit tricky - JUCE doesn't expose it directly
            
            // ALTERNATIVE: Read from Preferences directly (JUCE stores it there)
            auto* props = juce::PropertiesFile::getCommonSettings(true);
            if (props)
            {
                // Try reading the default MIDI output identifier
                // JUCE stores this in the audio device setup XML
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                audioDeviceManager->getAudioDeviceSetup(setup);
                
                // Actually, AudioDeviceManager doesn't directly expose MIDI output
                // We need to read it from the device manager's saved state XML
                // OR use a simpler approach: always use the first available device
                // OR store it separately in our own preferences
            }
        }
    }
    else
    {
        // Use custom device
        if (midiOutputDeviceParam)
            targetDeviceId = midiOutputDeviceParam->get();
    }
    
    // ... rest of device opening logic ...
}
```

**Simplest Approach**: Store MIDI output device ID in AudioDeviceManager's preferences manually, or create a simple helper to read it.

---

### Phase 4: Generate and Send MIDI Messages

#### Step 4.1: Generate MIDI in processBlock()

**File**: `MIDIPlayerModuleProcessor.cpp`

Add to `processBlock()` after CV generation (around line 312):

```cpp
void MIDIPlayerModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, 
    juce::MidiBuffer& midiMessages)
{
    // ... existing CV generation code ...
    
    // === MIDI OUTPUT GENERATION ===
    if (enableMidiOutputParam && enableMidiOutputParam->get() > 0.5f)
    {
        const int numSamples = buffer.getNumSamples();
        const double sampleRate = getSampleRate();
        const double currentSample = currentPlaybackTime * sampleRate;
        
        // Determine MIDI channel override
        const int channelOverride = midiOutputChannelParam ? 
            midiOutputChannelParam->get() : 0;  // 0 = preserve original channels
        
        // Process each track
        for (int trackIdx = 0; trackIdx < (int)notesByTrack.size(); ++trackIdx)
        {
            const auto& trackNotes = notesByTrack[trackIdx];
            
            // Determine MIDI channel for this track
            int midiChannel = 1; // Default channel 1
            if (channelOverride > 0)
            {
                // Use override channel (1-16)
                midiChannel = channelOverride;
            }
            else
            {
                // Auto: use track index (1-16, wrapping)
                midiChannel = (trackIdx % 16) + 1;
            }
            
            // Check for note onsets and offsets in this buffer
            for (const auto& note : trackNotes)
            {
                const double noteStartSample = note.startTime * sampleRate;
                const double noteEndSample = note.endTime * sampleRate;
                
                // Note On: check if note starts within this buffer
                if (currentSample <= noteStartSample && 
                    noteStartSample < currentSample + numSamples)
                {
                    int sampleOffset = (int)(noteStartSample - currentSample);
                    sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                    
                    juce::MidiMessage noteOn = juce::MidiMessage::noteOn(
                        midiChannel,
                        note.noteNumber,
                        (juce::uint8)note.velocity
                    );
                    
                    // Apply pitch transpose if needed
                    if (pitchParam && pitchParam->load() != 0.0f)
                    {
                        int transposeSemitones = (int)std::round(pitchParam->load());
                        int newNote = note.noteNumber + transposeSemitones;
                        newNote = juce::jlimit(0, 127, newNote);
                        noteOn = juce::MidiMessage::noteOn(midiChannel, newNote, noteOn.getVelocity());
                    }
                    
                    sendMidiToOutput(noteOn, sampleOffset);
                }
                
                // Note Off: check if note ends within this buffer
                if (currentSample <= noteEndSample && 
                    noteEndSample < currentSample + numSamples)
                {
                    int sampleOffset = (int)(noteEndSample - currentSample);
                    sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                    
                    juce::MidiMessage noteOff = juce::MidiMessage::noteOff(
                        midiChannel,
                        note.noteNumber
                    );
                    
                    // Apply pitch transpose if needed
                    if (pitchParam && pitchParam->load() != 0.0f)
                    {
                        int transposeSemitones = (int)std::round(pitchParam->load());
                        int newNote = note.noteNumber + transposeSemitones;
                        newNote = juce::jlimit(0, 127, newNote);
                        noteOff = juce::MidiMessage::noteOff(midiChannel, newNote);
                    }
                    
                    sendMidiToOutput(noteOff, sampleOffset);
                }
            }
        }
    }
    
    // Update MIDI output device (check if settings changed)
    updateMidiOutputDevice();
}
```

#### Step 4.2: Send MIDI to Output Device

**File**: `MIDIPlayerModuleProcessor.cpp`

Add implementation:

```cpp
void MIDIPlayerModuleProcessor::sendMidiToOutput(const juce::MidiMessage& message, int sampleOffset)
{
    const juce::ScopedLock lock(midiOutputLock);
    
    if (midiOutputDevice)
    {
        // JUCE's MidiOutput::sendMessageNow() sends immediately
        // For sample-accurate timing, we'd need to buffer, but for now immediate is fine
        // (MIDI timing precision is typically millisecond-level anyway)
        midiOutputDevice->sendMessageNow(message);
    }
}
```

**Note**: For better timing accuracy, we could buffer messages and send them at the correct sample offset, but JUCE's `sendMessageNow()` is adequate for most use cases (MIDI devices handle timing internally).

---

### Phase 5: UI Integration - Dropdown in MIDI Player Node

#### Step 5.1: Get Available MIDI Output Devices

**File**: `MIDIPlayerModuleProcessor.cpp`

Add helper method to get device list for dropdown:

```cpp
// Helper to get available MIDI output devices (for UI dropdown)
std::vector<std::pair<juce::String, juce::String>> MIDIPlayerModuleProcessor::getAvailableMidiOutputDevices() const
{
    std::vector<std::pair<juce::String, juce::String>> devices; // {name, identifier}
    
    auto available = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : available)
    {
        devices.push_back({device.name, device.identifier});
    }
    
    return devices;
}
```

**File**: `MIDIPlayerModuleProcessor.h`

Add public method:

```cpp
public:
    // ... existing public methods ...
    
    // Get list of available MIDI output devices for UI
    std::vector<std::pair<juce::String, juce::String>> getAvailableMidiOutputDevices() const;
```

#### Step 5.2: Add UI Dropdown

**File**: `MIDIPlayerModuleProcessor.cpp`

Add to `drawParametersInNode()` (around line 1037, after Quick Connect buttons):

```cpp
void MIDIPlayerModuleProcessor::drawParametersInNode(
    float itemWidth, 
    const std::function<bool(const juce::String&)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    // ... existing UI code (file info, tempo, pitch, zoom, quick connect) ...
    
    // === MIDI OUTPUT SECTION ===
    ImGui::Separator();
    ImGui::Text("MIDI Output:");
    ImGui::SameLine();
    HelpMarkerPlayer("Send MIDI messages to external devices or VSTi plugins");
    
    // Enable MIDI output checkbox
    bool enableMIDI = enableMidiOutputParam && enableMidiOutputParam->get() > 0.5f;
    if (ImGui::Checkbox("Enable##midi_out", &enableMIDI))
    {
        if (enableMidiOutputParam)
        {
            enableMidiOutputParam->setValueNotifyingHost(enableMIDI ? 1.0f : 0.0f);
            updateMidiOutputDevice(); // Update device connection
            onModificationEnded();
        }
    }
    
    if (enableMIDI)
    {
        ImGui::Indent(20.0f);
        
        // Output mode: Global Default vs Custom Device
        int outputMode = midiOutputModeParam ? midiOutputModeParam->getIndex() : 0;
        const char* modeItems[] = { "Use Global Default", "Custom Device" };
        
        if (ImGui::Combo("Mode##midi_out_mode", &outputMode, modeItems, 2))
        {
            if (midiOutputModeParam)
            {
                midiOutputModeParam->setValueNotifyingHost(
                    apvts.getParameterRange("midi_output_mode").convertTo0to1((float)outputMode));
                updateMidiOutputDevice();
                onModificationEnded();
            }
        }
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Global Default: Uses device from Audio Settings\n"
                            "Custom Device: Select specific device for this module");
        }
        
        // Custom device selector (only shown when mode is "Custom Device")
        if (outputMode == 1) // Custom Device
        {
            auto devices = getAvailableMidiOutputDevices();
            juce::String currentDeviceId = midiOutputDeviceParam ? midiOutputDeviceParam->get() : "";
            
            // Find current device name for display
            juce::String currentDeviceName = "<None Selected>";
            int currentIndex = -1;
            for (size_t i = 0; i < devices.size(); ++i)
            {
                if (devices[i].second == currentDeviceId)
                {
                    currentDeviceName = devices[i].first;
                    currentIndex = (int)i;
                    break;
                }
            }
            
            // Build device list for combo
            std::vector<const char*> deviceNames;
            std::vector<juce::String> deviceIds; // Keep alive
            deviceIds.reserve(devices.size());
            for (const auto& dev : devices)
            {
                deviceIds.push_back(dev.first);
                deviceNames.push_back(deviceIds.back().toRawUTF8());
            }
            
            int selectedIndex = currentIndex;
            if (ImGui::Combo("Device##midi_out_device", &selectedIndex, 
                           deviceNames.data(), (int)deviceNames.size()))
            {
                if (selectedIndex >= 0 && selectedIndex < (int)devices.size())
                {
                    juce::String selectedId = devices[selectedIndex].second;
                    if (midiOutputDeviceParam)
                    {
                        midiOutputDeviceParam->setValueNotifyingHost(0.0f); // String params use setValue()
                        midiOutputDeviceParam->setValue(selectedId);
                        updateMidiOutputDevice();
                        onModificationEnded();
                    }
                }
            }
        }
        else
        {
            // Show current global device (read-only display)
            juce::String globalDeviceName = "<None>";
            if (audioDeviceManager)
            {
                juce::String deviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
                if (deviceId.isNotEmpty())
                {
                    // Find device name
                    auto devices = juce::MidiOutput::getAvailableDevices();
                    for (const auto& dev : devices)
                    {
                        if (dev.identifier == deviceId)
                        {
                            globalDeviceName = dev.name;
                            break;
                        }
                    }
                }
            }
            
            ImGui::TextDisabled("Device: %s", globalDeviceName.toRawUTF8());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Using device from Audio Settings dialog\n"
                                "Change it in Settings → Audio Settings → MIDI Output");
            }
        }
        
        // MIDI Channel selector
        int channel = midiOutputChannelParam ? midiOutputChannelParam->get() : 0;
        if (ImGui::SliderInt("Channel##midi_out_ch", &channel, 0, 16))
        {
            if (midiOutputChannelParam)
            {
                midiOutputChannelParam->setValueNotifyingHost(
                    apvts.getParameterRange("midi_output_channel").convertTo0to1((float)channel));
                onModificationEnded();
            }
        }
        if (ImGui::IsItemHovered())
        {
            if (channel == 0)
                ImGui::SetTooltip("Channel 0: Preserve track's original MIDI channel\n"
                                "Channel 1-16: Force all tracks to this channel");
            else
                ImGui::SetTooltip("All MIDI messages will be sent on channel %d", channel);
        }
        
        ImGui::Unindent(20.0f);
    }
    
    // ... rest of UI code (piano roll, etc.) ...
}
```

---

### Phase 6: Integration with Audio Settings

#### Step 6.1: Sync with Audio Settings Changes

**File**: `MIDIPlayerModuleProcessor.cpp`

Add listener for AudioDeviceManager changes:

```cpp
// In MIDIPlayerModuleProcessor.h, add ChangeListener inheritance
class MIDIPlayerModuleProcessor : public ModuleProcessor,
                                  public juce::ChangeListener  // ADD THIS
{
    // ... existing code ...
    
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
};
```

```cpp
// In MIDIPlayerModuleProcessor.cpp

void MIDIPlayerModuleProcessor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == audioDeviceManager)
    {
        // Audio settings changed - update MIDI output device if using global default
        if (midiOutputModeParam && 
            midiOutputModeParam->getCurrentChoiceName() == "Use Global Default")
        {
            updateMidiOutputDevice();
        }
    }
}
```

In constructor, add listener:

```cpp
MIDIPlayerModuleProcessor::MIDIPlayerModuleProcessor()
{
    // ... existing initialization ...
    
    // Listen for AudioDeviceManager changes (when Audio Settings changes)
    if (audioDeviceManager)
        audioDeviceManager->addChangeListener(this);
}
```

In destructor (or when audioDeviceManager changes):

```cpp
~MIDIPlayerModuleProcessor()
{
    if (audioDeviceManager)
        audioDeviceManager->removeChangeListener(this);
}
```

---

## Simplified Approach: Direct MidiOutput Access

Actually, there's a simpler approach - just access MidiOutput directly without going through AudioDeviceManager:

**Alternative Simple Approach:**

```cpp
// In MIDIPlayerModuleProcessor, just use MidiOutput::openDevice() directly
// The dropdown shows all available devices
// User selects device, we store identifier in parameter
// Open device directly - no need for AudioDeviceManager integration
```

This is simpler and more direct. Audio Settings can still manage the "default" but each module can override independently.

---

## Implementation Summary

### Files to Modify

1. **`MIDIPlayerModuleProcessor.h`**
   - Add MIDI output parameter pointers
   - Add `midiOutputDevice` member
   - Add `getAvailableMidiOutputDevices()` method
   - Add `updateMidiOutputDevice()` and `sendMidiToOutput()` methods

2. **`MIDIPlayerModuleProcessor.cpp`**
   - Add MIDI output parameters to `createParameterLayout()`
   - Initialize parameter pointers in constructor
   - Implement MIDI generation in `processBlock()`
   - Implement device management methods
   - Add UI dropdown in `drawParametersInNode()`

3. **`ModuleProcessor.h`** (optional - for AudioDeviceManager access)
   - Add `setAudioDeviceManager()` method if needed

### Code Changes Estimate

- **MIDI generation logic**: ~100 lines
- **Device management**: ~80 lines
- **UI dropdown**: ~150 lines
- **Parameter additions**: ~20 lines
- **Total**: ~350 lines

### Time Estimate

- **Implementation**: 4-6 hours
- **Testing**: 2-3 hours
- **Total**: 6-9 hours (~1 day)

---

## Success Criteria

- ✅ MIDI Player can generate MIDI messages from loaded MIDI file
- ✅ Dropdown shows available MIDI output devices
- ✅ Can select "Use Global Default" or specific device
- ✅ MIDI messages sent to selected device correctly
- ✅ Pitch transpose applies to MIDI output
- ✅ MIDI channel selection works (0 = preserve, 1-16 = force)
- ✅ Settings persist (device selection saved in preset)

---

## Next Steps

1. Implement MIDI generation in `processBlock()`
2. Add device management methods
3. Add UI dropdown
4. Test with external MIDI device
5. Test with VSTi plugin receiving MIDI

This approach integrates cleanly with Audio Settings while allowing per-module control!

