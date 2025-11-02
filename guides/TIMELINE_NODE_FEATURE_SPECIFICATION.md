# Timeline Node Feature Specification

## Executive Summary

We need to create a **Timeline Node** - a sophisticated automation recording and playback system that integrates deeply with our modular synthesizer's transport system. This node will serve as the **single source of truth** for temporal automation in our system, recording and playing back precise CV, gate, trigger, and raw audio data synchronized with the Tempo Clock.

---

## Core Requirements

### 1. Transport Synchronization

The Timeline Node **MUST** integrate seamlessly with the existing Tempo Clock system:

- **Start/Stop/Pause**: Responds automatically to Tempo Clock transport controls
- **Position Tracking**: Uses `TransportState.songPositionBeats` as the absolute time reference
- **BPM Sync**: Respects global BPM changes without losing synchronization
- **Truth Model**: Becomes the authoritative source for "where we are in time" for automation purposes

**Key Files to Study:**
- `TempoClockModuleProcessor.cpp` - See how transport integration works
- `TEMPO_CLOCK_INTEGRATION_GUIDE.md` - Complete patterns for transport-aware modules
- `ModuleProcessor.h` - Contains `TransportState` struct definition

### 2. Dynamic Inputs/Outputs

The Timeline Node needs **dynamic I/O capability** to support flexible automation routing:

- **Dynamic Input Pins**: Created automatically based on what's connected
- **Passthrough Architecture**: Inputs immediately pass through to outputs (zero-latency monitoring)
- **Simultaneous Record/Play**: Must record incoming data while playing back automation
- **Type-Aware**: Handles CV, gate, trigger, and raw audio signals correctly

**Key Files to Study:**
- `ColorTrackerModule.cpp` - Excellent example of `getDynamicOutputPins()` implementation
- `AnimationModuleProcessor.cpp` - Shows dynamic output creation based on tracked data
- `ModuleProcessor.h` - `DynamicPinInfo` struct definition

### 3. Ultra-Precise Keyframe Recording

Record automation data with **sample-accurate precision**:

- **Per-Sample Recording**: Capture every input sample along with its exact timestamp
- **Keyframe Format**: Store position (in beats) + value pairs
- **Minimal Storage**: Use interpolation/compression to avoid storing redundant data
- **Metadata**: Store BPM at recording time, signal type (CV/Gate/Raw), channel info

**Example Keyframe Structure:**
```cpp
struct AutomationKeyframe {
    double positionBeats;    // Precise position in beats (from TransportState)
    float value;             // The recorded value
    SignalType type;         // CV, Gate, Trigger, Raw
    int channel;             // Which input channel
};
```

**Key Files to Study:**
- `MIDIPlayerModuleProcessor.cpp` - See how MIDI timing is managed with beats
- `StepSequencerModuleProcessor.cpp` - Example of time-based data structure

### 4. XML Persistence

Save and load automation data as **ultra-precise XML files**:

- **Human-Readable**: XML format for debugging and manual editing
- **Lossless**: No compression that would degrade precision
- **Modular**: Each automation layer (per input channel) saved separately
- **Metadata Rich**: Include BPM, recording date, signal types, etc.

**Expected XML Structure:**
```xml
<TimelineAutomation version="1.0">
    <metadata>
        <bpm>120.0</bpm>
        <durationBeats>16.0</durationBeats>
        <recorded>2025-01-15T10:30:00Z</recorded>
    </metadata>
    <channel id="0" name="CV 1" type="CV">
        <keyframe position="0.0" value="0.0"/>
        <keyframe position="2.5" value="1.0"/>
        <keyframe position="5.0" value="0.5"/>
    </channel>
    <channel id="1" name="Gate 1" type="Gate">
        <keyframe position="0.0" value="1.0"/>
        <keyframe position="1.0" value="0.0"/>
    </channel>
</TimelineAutomation>
```

**Key Files to Study:**
- `XML_SAVING_AND_LOADING_HOW_TO.md` - **COMPLETE** guide to XML in this codebase
- `ModularSynthProcessor.cpp` - See `getStateInformation()` and `setStateInformation()`
- `PresetCreatorComponent.cpp` - Example of XML file chooser and save/load flow

### 5. Playback System

Play back recorded automation **exactly as recorded**:

- **Sample-Accurate**: Interpolate between keyframes for smooth playback
- **Loop Support**: Option to loop automation or play once
- **Transport-Aware**: Pause/resume/st Seek with transport controls
- **Crossfading**: Smooth transition when switching between record/play modes
- **Output Routing**: Send automation to corresponding output channels

**Playback Modes:**
1. **Record**: Capture input data while passing through
2. **Play**: Output recorded automation, ignore inputs
3. **Record+Play**: Layer new recording over existing automation
4. **Override**: Replace automation when new data arrives

**Key Files to Study:**
- `MIDIPlayerModuleProcessor.cpp` - See playback position tracking
- `AnimationRenderer.cpp` - Example of time-based interpolation
- `Animator.cpp` - Animation playback patterns

---

## Technical Architecture

### Module Structure

```cpp
class TimelineModuleProcessor : public ModuleProcessor
{
public:
    // Standard module interface
    const juce::String getName() const override { return "timeline"; }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Dynamic I/O
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
    // Automation persistence
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;
    
private:
    // Transport state (inherited from ModuleProcessor)
    TransportState m_currentTransport;
    
    // Automation data
    struct ChannelData {
        std::vector<AutomationKeyframe> keyframes;
        SignalType type;
        std::string name;
    };
    std::vector<ChannelData> automationChannels;
    
    // Recording state
    enum class RecordMode { Idle, Record, Play, Overdub };
    RecordMode currentMode = RecordMode::Idle;
    
    // Transport tracking
    double lastPlaybackPosition = 0.0;
    bool isFirstBlock = true;
    
    // XML file management
    juce::File currentAutomationFile;
};
```

### ProcessBlock Logic Flow

```
1. Check transport state (m_currentTransport.isPlaying, songPositionBeats)
   
2. Determine mode:
   - Record: Capture input → store keyframes → passthrough to output
   - Play: Read keyframes → interpolate → output automation
   - Overdub: Do both simultaneously
   - Idle: Passthrough only

3. For each dynamic input channel:
   - Read input sample
   - If recording: Store (position, value) if significant change detected
   - If playing: Interpolate between keyframes based on songPositionBeats
   - Write to corresponding output channel

4. Update playback position tracking
5. Handle transport events (start/stop/seek)
```

### Dynamic Pin Management

```cpp
std::vector<DynamicPinInfo> TimelineModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Create one input per currently-tracked automation channel
    for (size_t i = 0; i < automationChannels.size(); ++i)
    {
        const auto& channel = automationChannels[i];
        pins.emplace_back(
            channel.name + " In",
            (int)i,
            GetPinDataTypeForSignalType(channel.type)
        );
    }
    
    return pins;
}

std::vector<DynamicPinInfo> TimelineModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Create one output per automation channel (for passthrough)
    for (size_t i = 0; i < automationChannels.size(); ++i)
    {
        const auto& channel = automationChannels[i];
        pins.emplace_back(
            channel.name + " Out",
            (int)i,
            GetPinDataTypeForSignalType(channel.type)
        );
    }
    
    return pins;
}
```

---

## Implementation Checklist

### Phase 1: Foundation
- [ ] Create `TimelineModuleProcessor.h` and `.cpp`
- [ ] Inherit from `ModuleProcessor`
- [ ] Implement basic APVTS with transport control parameters
- [ ] Set up dynamic I/O infrastructure
- [ ] Implement passthrough (input → output)

### Phase 2: Transport Integration
- [ ] Override `setTimingInfo()` to receive transport updates
- [ ] Implement transport state tracking in `processBlock()`
- [ ] Handle start/stop/pause events
- [ ] Track playback position from `TransportState.songPositionBeats`
- [ ] Add seek/jump-to-position capability

### Phase 3: Recording System
- [ ] Implement keyframe data structure
- [ ] Add recording logic in `processBlock()`
- [ ] Implement change detection (only store when value changes significantly)
- [ ] Create XML serialization for keyframes
- [ ] Add file chooser integration for save/load

### Phase 4: Playback System
- [ ] Implement keyframe interpolation (linear, hermitian, etc.)
- [ ] Add playback position calculation
- [ ] Implement play/stop/loop modes
- [ ] Handle crossfading between modes
- [ ] Add overdub capability

### Phase 5: UI Integration
- [ ] Add parameter UI (record/play/stop buttons, BPM display)
- [ ] Implement automation visualization (waveform view)
- [ ] Add timeline scrubber for seeking
- [ ] Show transport state indicators
- [ ] Display recording time/duration

### Phase 6: Advanced Features
- [ ] Add quantization options (snap keyframes to beats)
- [ ] Implement curve editing (per-keyframe interpolation type)
- [ ] Add gain/offset per channel
- [ ] Support multiple takes/layers per channel
- [ ] Add export to DAW-compatible formats

---

## Critical Design Patterns

### Pattern 1: Transport-Aware Processing

Always use `m_currentTransport` for timing, not local counters:

```cpp
void TimelineModuleProcessor::processBlock(...)
{
    // Get current transport state (already updated by parent)
    double currentBPM = m_currentTransport.bpm;
    double songPosition = m_currentTransport.songPositionBeats;
    bool isPlaying = m_currentTransport.isPlaying;
    
    // Use songPosition for all time-based operations
    for (int i = 0; i < numSamples; ++i)
    {
        double samplePosition = songPosition + (i / sampleRate) * (currentBPM / 60.0);
        
        // Interpolate keyframes based on samplePosition
        float automationValue = InterpolateKeyframes(samplePosition);
        
        // Apply to output...
    }
}
```

### Pattern 2: Dynamic Pin Creation

Follow the ColorTracker pattern for dynamic I/O:

```cpp
// In constructor: Allocate sufficient channels
TimelineModuleProcessor::TimelineModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(32), true)
          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(32), true))
```

### Pattern 3: XML Save/Load

Follow the `getExtraStateTree()` pattern for file persistence:

```cpp
juce::ValueTree TimelineModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree state("TimelineState");
    
    // Save file path
    state.setProperty("automationFilePath", currentAutomationFile.getFullPathName(), nullptr);
    
    // Save per-channel keyframes
    for (size_t i = 0; i < automationChannels.size(); ++i)
    {
        juce::ValueTree channelVT("channel");
        channelVT.setProperty("index", (int)i, nullptr);
        channelVT.setProperty("type", GetStringForSignalType(automationChannels[i].type), nullptr);
        
        for (const auto& keyframe : automationChannels[i].keyframes)
        {
            juce::ValueTree kfVT("keyframe");
            kfVT.setProperty("position", keyframe.positionBeats, nullptr);
            kfVT.setProperty("value", keyframe.value, nullptr);
            channelVT.addChild(kfVT, -1, nullptr);
        }
        
        state.addChild(channelVT, -1, nullptr);
    }
    
    return state;
}
```

---

## Testing Strategy

### Unit Tests
1. Keyframe interpolation accuracy (linear, cubic, etc.)
2. Transport position calculation correctness
3. XML serialization/deserialization round-trip
4. Dynamic pin creation logic

### Integration Tests
1. Record → Stop → Play → Should output exact same values
2. Transport sync: Change BPM mid-playback → Should stay locked
3. Multi-channel: Record 8 CV channels → All play back independently
4. Overdub: Layer new recording → Combined output correct

### Performance Tests
1. Record 60s of automation at 44.1kHz → Check memory usage
2. Playback with 1000 keyframes → CPU usage acceptable
3. Save/load 16 channels → XML file size reasonable

---

## Open Questions & Decisions Needed

1. **Interpolation Method**: Linear? Cubic? Hermitian? Per-keyframe selection?
2. **Change Detection**: What threshold constitutes a "significant change" for recording?
3. **Maximum Duration**: Should there be a hard limit on automation length?
4. **Undo/Redo**: Should recording support undo/redo? Probably yes.
5. **Velocity Curves**: Should keyframes store velocity information for smoother curves?
6. **Sub-Beat Precision**: Should we record at sub-beat resolution or only on beat boundaries?
7. **Multi-Take**: Support multiple takes per channel? Overdub blending?
8. **Export Formats**: CSV? MIDI CC? Audio automation files?

---

## Success Criteria

### Must Have (MVP)
✅ Records CV/gate/trigger data with sample accuracy
✅ Syncs perfectly with Tempo Clock transport
✅ Saves/loads automation as human-readable XML
✅ Plays back automation exactly as recorded
✅ Dynamic I/O for flexible routing

### Should Have (v1.0)
✅ UI visualization of automation data
✅ Loop/one-shot playback modes
✅ Overdub capability
✅ Seek/jump to any position
✅ Undo/redo for recordings

### Nice to Have (Future)
✅ Curve editing in UI
✅ Multiple takes/layers
✅ Export to DAW formats
✅ MIDI clock sync
✅ External sync protocol (Link, Ableton Link, etc.)

---

## Related Features

This Timeline Node will integrate with:

- **Tempo Clock**: Transport synchronization
- **BPM Monitor**: Analysis of automation rhythm
- **MIDI Player**: Compare MIDI playback with automation
- **Step Sequencer**: Automation can trigger sequencer steps
- **Animation System**: Coordinate with animation timelines

---

## Questions for Expert

As the external expert implementing this feature, please consider:

1. **Data Structure**: Is a flat vector of keyframes per channel optimal? Or should we use a time-sorted tree structure?

2. **Interpolation**: What's the best interpolation method for real-time audio automation? Linear is simplest but may have artifacts...

3. **Thread Safety**: Recording happens on audio thread, but save/load XML happens on UI thread. What synchronization is needed?

4. **File Format**: Should we use JUCE's ValueTree → XML, or a custom XML format for more control?

5. **Precision**: Using `double` for beat positions - sufficient precision for long recordings (hours)?

6. **Performance**: What's the best way to interpolate thousands of keyframes per sample efficiently?

---

## Next Steps

1. Review this specification thoroughly
2. Study the referenced files in the Moofy Essentials archive
3. Ask clarifying questions about any ambiguous requirements
4. Propose any architectural improvements or alternatives
5. Begin implementation starting with Phase 1 (Foundation)

Thank you for your expertise! This Timeline Node will be a cornerstone feature of our modular synthesis system.

