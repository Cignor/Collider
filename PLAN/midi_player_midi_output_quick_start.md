# MIDI Player MIDI Output - Quick Start Implementation Guide

## Goal

Add MIDI output routing to MIDI Player node with a dropdown that integrates with Audio Settings. User can select MIDI output device directly in the MIDI Player node UI.

## Architecture

- **Global Default**: Set in Audio Settings dialog (already works!)
- **Per-Module Override**: MIDI Player dropdown can use "Global Default" or select specific device
- **MIDI Generation**: Generate MIDI messages from loaded MIDI file data
- **Device Access**: Use JUCE's `AudioDeviceManager::getDefaultMidiOutputIdentifier()` for global default

---

## Step-by-Step Implementation

### Step 1: Add MIDI Output Parameters

**File**: `MIDIPlayerModuleProcessor.cpp` (in `createParameterLayout()`)

```cpp
// Add after existing parameters (around line 63):

// MIDI Output Parameters
parameters.push_back(std::make_unique<juce::AudioParameterBool>(
    "enable_midi_output", "Enable MIDI Output", false));

parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
    "midi_output_mode", "MIDI Output Mode",
    juce::StringArray("Use Global Default", "Custom Device"), 0));

parameters.push_back(std::make_unique<juce::AudioParameterString>(
    "midi_output_device", "MIDI Output Device", ""));

parameters.push_back(std::make_unique<juce::AudioParameterInt>(
    "midi_output_channel", "MIDI Output Channel", 0, 16, 0));
```

### Step 2: Add Member Variables

**File**: `MIDIPlayerModuleProcessor.h`

```cpp
private:
    // ... existing members ...
    
    // MIDI Output
    std::unique_ptr<juce::MidiOutput> midiOutputDevice;
    juce::String currentMidiOutputDeviceId;
    juce::CriticalSection midiOutputLock;
    
    juce::AudioParameterBool* enableMidiOutputParam { nullptr };
    juce::AudioParameterChoice* midiOutputModeParam { nullptr };
    juce::AudioParameterString* midiOutputDeviceParam { nullptr };
    juce::AudioParameterInt* midiOutputChannelParam { nullptr };
    
    juce::AudioDeviceManager* audioDeviceManager { nullptr };
    
public:
    void setAudioDeviceManager(juce::AudioDeviceManager* adm) { audioDeviceManager = adm; }
    std::vector<std::pair<juce::String, juce::String>> getAvailableMidiOutputDevices() const;
    
private:
    void updateMidiOutputDevice();
    void sendMidiToOutput(const juce::MidiMessage& message);
```

### Step 3: Initialize Parameter Pointers

**File**: `MIDIPlayerModuleProcessor.cpp` (in constructor)

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

### Step 4: Implement Device Management

**File**: `MIDIPlayerModuleProcessor.cpp`

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
    
    // Determine which device to use
    if (midiOutputModeParam && midiOutputModeParam->getCurrentChoiceName() == "Use Global Default")
    {
        // Use global default from AudioDeviceManager
        if (audioDeviceManager)
            targetDeviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
    }
    else
    {
        // Use custom device
        if (midiOutputDeviceParam)
            targetDeviceId = midiOutputDeviceParam->get();
    }
    
    // Only change if device changed
    if (targetDeviceId == currentMidiOutputDeviceId && midiOutputDevice)
        return;
    
    // Close current
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
    }
}

void MIDIPlayerModuleProcessor::sendMidiToOutput(const juce::MidiMessage& message)
{
    const juce::ScopedLock lock(midiOutputLock);
    if (midiOutputDevice)
        midiOutputDevice->sendMessageNow(message);
}

std::vector<std::pair<juce::String, juce::String>> 
MIDIPlayerModuleProcessor::getAvailableMidiOutputDevices() const
{
    std::vector<std::pair<juce::String, juce::String>> devices;
    auto available = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : available)
        devices.push_back({device.name, device.identifier});
    return devices;
}
```

### Step 5: Generate MIDI Messages in processBlock()

**File**: `MIDIPlayerModuleProcessor.cpp` (in `processBlock()`, after line 312)

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
        
        const int channelOverride = midiOutputChannelParam ? 
            midiOutputChannelParam->get() : 0;  // 0 = preserve original channels
        
        // Process each track
        for (int trackIdx = 0; trackIdx < (int)notesByTrack.size(); ++trackIdx)
        {
            const auto& trackNotes = notesByTrack[trackIdx];
            
            // Determine MIDI channel
            int midiChannel = (channelOverride > 0) ? channelOverride : ((trackIdx % 16) + 1);
            
            // Check for note onsets/offsets in this buffer
            for (const auto& note : trackNotes)
            {
                const double noteStartSample = note.startTime * sampleRate;
                const double noteEndSample = note.endTime * sampleRate;
                
                // Note On
                if (currentSample <= noteStartSample && 
                    noteStartSample < currentSample + numSamples)
                {
                    int noteNumber = note.noteNumber;
                    // Apply pitch transpose
                    if (pitchParam && pitchParam->load() != 0.0f)
                    {
                        int transpose = (int)std::round(pitchParam->load());
                        noteNumber = juce::jlimit(0, 127, noteNumber + transpose);
                    }
                    
                    juce::MidiMessage noteOn = juce::MidiMessage::noteOn(
                        midiChannel, noteNumber, (juce::uint8)note.velocity);
                    sendMidiToOutput(noteOn);
                }
                
                // Note Off
                if (currentSample <= noteEndSample && 
                    noteEndSample < currentSample + numSamples)
                {
                    int noteNumber = note.noteNumber;
                    // Apply pitch transpose
                    if (pitchParam && pitchParam->load() != 0.0f)
                    {
                        int transpose = (int)std::round(pitchParam->load());
                        noteNumber = juce::jlimit(0, 127, noteNumber + transpose);
                    }
                    
                    juce::MidiMessage noteOff = juce::MidiMessage::noteOff(midiChannel, noteNumber);
                    sendMidiToOutput(noteOff);
                }
            }
        }
    }
    
    // Update device if settings changed (check periodically, not every block)
    static int updateCounter = 0;
    if ((++updateCounter % 1000) == 0) // Check every 1000 blocks (~20 seconds at 48kHz)
        updateMidiOutputDevice();
}
```

**Note**: We check device update every 1000 blocks to avoid overhead. Could also use parameter change callbacks for immediate updates.

### Step 6: Add UI Dropdown

**File**: `MIDIPlayerModuleProcessor.cpp` (in `drawParametersInNode()`, after Quick Connect section, around line 1037)

```cpp
    // === MIDI OUTPUT SECTION ===
    ImGui::Separator();
    ImGui::Text("MIDI Output:");
    ImGui::SameLine();
    HelpMarkerPlayer("Send MIDI messages to external devices or VSTi plugins");
    
    bool enableMIDI = enableMidiOutputParam && enableMidiOutputParam->get() > 0.5f;
    if (ImGui::Checkbox("Enable##midi_out", &enableMIDI))
    {
        if (enableMidiOutputParam)
        {
            enableMidiOutputParam->setValueNotifyingHost(enableMIDI ? 1.0f : 0.0f);
            updateMidiOutputDevice();
            onModificationEnded();
        }
    }
    
    if (enableMIDI)
    {
        ImGui::Indent(20.0f);
        
        // Output mode dropdown
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
        
        // Custom device selector (only when mode is "Custom Device")
        if (outputMode == 1)
        {
            auto devices = getAvailableMidiOutputDevices();
            juce::String currentDeviceId = midiOutputDeviceParam ? midiOutputDeviceParam->get() : "";
            
            // Find current device name
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
            
            // Build combo list
            std::vector<const char*> deviceNames;
            std::vector<juce::String> deviceNamesStr; // Keep alive
            deviceNamesStr.reserve(devices.size());
            for (const auto& dev : devices)
            {
                deviceNamesStr.push_back(dev.first);
                deviceNames.push_back(deviceNamesStr.back().toRawUTF8());
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
                        midiOutputDeviceParam->setValue(selectedId);
                        updateMidiOutputDevice();
                        onModificationEnded();
                    }
                }
            }
        }
        else
        {
            // Show global device name
            juce::String globalDeviceName = "<None>";
            if (audioDeviceManager)
            {
                juce::String deviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
                if (deviceId.isNotEmpty())
                {
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
                ImGui::SetTooltip("Using device from Audio Settings\n"
                                "Change in Settings → Audio Settings");
        }
        
        // Channel selector
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
            ImGui::SetTooltip(channel == 0 ? 
                "Channel 0: Preserve track's original MIDI channel" :
                juce::String("All tracks output on channel " + juce::String(channel)).toRawUTF8());
        
        ImGui::Unindent(20.0f);
    }
```

### Step 7: Connect to AudioDeviceManager

**File**: `ModularSynthProcessor.h`

```cpp
class ModularSynthProcessor : public juce::AudioProcessor
{
    // ... existing code ...
    
    void setAudioDeviceManager(juce::AudioDeviceManager* adm);
    
private:
    juce::AudioDeviceManager* audioDeviceManager { nullptr };
    void updateMidiPlayerAudioDeviceManager();
};
```

**File**: `ModularSynthProcessor.cpp`

```cpp
void ModularSynthProcessor::setAudioDeviceManager(juce::AudioDeviceManager* adm)
{
    audioDeviceManager = adm;
    updateMidiPlayerAudioDeviceManager();
}

void ModularSynthProcessor::updateMidiPlayerAudioDeviceManager()
{
    if (!audioDeviceManager) return;
    
    for (const auto& [uid, node] : modules)
    {
        if (auto* module = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
        {
            if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(module))
                midiPlayer->setAudioDeviceManager(audioDeviceManager);
        }
    }
}
```

Also update `addModule()` to set AudioDeviceManager when MIDI Player is created:

```cpp
ModularSynthProcessor::NodeID ModularSynthProcessor::addModule(...)
{
    // ... existing module creation code ...
    
    if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
    {
        mp->setParent(this);
        
        // Set AudioDeviceManager if available
        if (audioDeviceManager)
        {
            if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(mp))
                midiPlayer->setAudioDeviceManager(audioDeviceManager);
        }
    }
    
    // ... rest of code ...
}
```

**File**: `PresetCreatorComponent.cpp`

After synth creation (around line 46):

```cpp
synth->setAudioDeviceManager(&deviceManager);
```

---

## Testing Checklist

- [ ] Load MIDI file in MIDI Player
- [ ] Enable MIDI output checkbox
- [ ] Select "Use Global Default" → verify uses Audio Settings device
- [ ] Select "Custom Device" → choose specific device → verify works
- [ ] Change MIDI channel → verify messages on correct channel
- [ ] Apply pitch transpose → verify MIDI notes transposed
- [ ] Test with external MIDI device
- [ ] Test with VSTi plugin receiving MIDI
- [ ] Change Audio Settings MIDI output → verify "Global Default" updates
- [ ] Save/load preset → verify MIDI output settings persist

---

## Estimated Time

- **Implementation**: 4-6 hours
- **Testing**: 2-3 hours
- **Total**: 6-9 hours (~1 day)

This integrates cleanly with Audio Settings while giving per-module control!

