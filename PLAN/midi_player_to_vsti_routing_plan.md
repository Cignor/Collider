# MIDI Player → VSTi Routing Plan

## Overview

Enable MIDI Player module to send MIDI messages to VSTi plugins (or any module that accepts MIDI). Explore two architectural approaches for routing MIDI through the modular graph.

## Current Architecture Analysis

### ✅ What Already Works

1. **VSTi nodes already accept MIDI**:
   - `VstHostModuleProcessor::processBlock()` receives `juce::MidiBuffer& midi`
   - Forwards MIDI directly to hosted plugin: `hostedPlugin->processBlock(buffer, midi)`
   - **VSTi plugins are already MIDI-ready!**

2. **Graph processes MIDI**:
   - `ModularSynthProcessor::processBlock()` calls `internalGraph->processBlock(buffer, midiMessages)`
   - JUCE's `AudioProcessorGraph` automatically routes MIDI through connections
   - MIDI flows via special "MIDI channel index" (separate from audio channels)

3. **Graph has MIDI connections**:
   - MIDI input node connects to audio output node via MIDI channel
   - MIDI can flow between any connected nodes

### ❌ What's Missing

1. **MIDI Player doesn't output MIDI**:
   - Currently only outputs CV/Gate signals
   - No MIDI messages added to `MidiBuffer` in `processBlock()`

2. **No explicit MIDI connections in UI**:
   - Audio connections are visual (cables between nodes)
   - MIDI connections might be implicit or non-existent
   - Need to verify if MIDI connections work automatically or need explicit setup

---

## Two Approaches: Comparison

### Approach A: Automatic MIDI Routing (Recommended ✅)

**Concept**: MIDI Player adds messages to `MidiBuffer`, graph automatically routes to connected nodes.

**How it works**:
- MIDI Player adds MIDI messages to `midiMessages` in `processBlock()`
- Graph routes MIDI automatically to any connected node
- VSTi receives MIDI if it's connected to MIDI Player (via audio connection)
- **OR** explicit MIDI connections needed (if graph requires them)

**Pros**:
- ✅ Simple: Just generate MIDI messages in MIDI Player
- ✅ Leverages existing graph routing
- ✅ No new connection types needed (if audio connection works)
- ✅ Fast implementation

**Cons**:
- ❓ Unclear if MIDI flows through audio connections automatically
- ❓ No visual MIDI connection indication in UI
- ❓ MIDI might go to all connected nodes (no selective routing)

**Difficulty**: **EASY (3/10)** - Just add MIDI generation to MIDI Player

---

### Approach B: Explicit MIDI Pins & Connections

**Concept**: Add MIDI input/output pins to modules, create visual MIDI connections.

**How it works**:
- MIDI Player has "MIDI Out" pin
- VSTi node has "MIDI In" pin
- User connects MIDI pins visually (like audio cables)
- Graph routes MIDI through explicit MIDI connections

**Pros**:
- ✅ Clear visual indication of MIDI routing
- ✅ Selective routing (connect specific nodes)
- ✅ Multiple MIDI outputs per module possible
- ✅ More intuitive for users

**Cons**:
- ❌ Requires UI changes (MIDI pin rendering, MIDI connection logic)
- ❌ More complex graph connection system
- ❌ Longer implementation time

**Difficulty**: **MEDIUM-HARD (7/10)** - Requires UI and graph connection changes

---

## Recommendation: **Approach A (Automatic Routing)**

**Why**: 
1. VSTi already accepts MIDI - just needs messages!
2. JUCE's graph should handle MIDI routing automatically
3. Can add Approach B later if needed
4. Faster to implement and test

**Implementation Strategy**:
1. **Phase 1**: Add MIDI generation to MIDI Player (test if automatic routing works)
2. **Phase 2**: If automatic routing doesn't work, add explicit MIDI connections
3. **Phase 3**: (Optional) Add visual MIDI pins/connections for better UX

---

## Implementation Plan: Approach A

### Phase 1: MIDI Generation in MIDI Player

#### Step 1: Generate MIDI Messages

**File**: `MIDIPlayerModuleProcessor.cpp`

Modify `processBlock()` to generate MIDI messages from active notes:

```cpp
void MIDIPlayerModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, 
    juce::MidiBuffer& midiMessages)
{
    // ... existing CV generation code ...
    
    // === MIDI OUTPUT GENERATION ===
    if (enableMIDIOutputParam && enableMIDIOutputParam->get() > 0.5f)
    {
        const int numSamples = buffer.getNumSamples();
        const double sampleRate = getSampleRate();
        const double currentSample = currentPlaybackTime * sampleRate;
        
        // Process each track
        for (int trackIdx = 0; trackIdx < (int)notesByTrack.size(); ++trackIdx)
        {
            const auto& trackNotes = notesByTrack[trackIdx];
            
            // Determine MIDI channel for this track
            int midiChannel = 1; // Default channel 1
            if (midiOutputChannelParam && midiOutputChannelParam->get() > 0.5f)
            {
                // Use parameter value (1-16)
                midiChannel = (int)midiOutputChannelParam->get();
                midiChannel = juce::jlimit(1, 16, midiChannel);
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
                    midiMessages.addEvent(noteOn, sampleOffset);
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
                    midiMessages.addEvent(noteOff, sampleOffset);
                }
            }
        }
    }
}
```

**Note**: This is similar to the MIDI output plan, but instead of sending to external device, we're adding to the graph's `MidiBuffer` for internal routing.

#### Step 2: Add Enable Parameter

**File**: `MIDIPlayerModuleProcessor.cpp` (in `createParameterLayout()`)

```cpp
parameters.push_back(std::make_unique<juce::AudioParameterBool>(
    "enable_midi_output", "Enable MIDI Output", false));
    
parameters.push_back(std::make_unique<juce::AudioParameterInt>(
    "midi_output_channel", "MIDI Output Channel", 1, 16, 1));  // 1-16, default 1
```

#### Step 3: Add UI Controls

**File**: `MIDIPlayerModuleProcessor.cpp` (in `drawParametersInNode()`)

```cpp
// MIDI Output Section
ImGui::Separator();
ImGui::Text("MIDI Output (for VSTi):");

bool enableMIDI = enableMIDIOutputParam->get() > 0.5f;
if (ImGui::Checkbox("Send MIDI", &enableMIDI))
{
    enableMIDIOutputParam->setValueNotifyingHost(enableMIDI ? 1.0f : 0.0f);
    onModificationEnded();
}

if (enableMIDI)
{
    int channel = (int)midiOutputChannelParam->get();
    if (ImGui::SliderInt("Channel", &channel, 1, 16))
    {
        midiOutputChannelParam->setValueNotifyingHost(
            apvts.getParameterRange("midi_output_channel").convertTo0to1((float)channel));
        onModificationEnded();
    }
    ImGui::TextDisabled("Connect audio output to VSTi node to receive MIDI");
}
```

### Phase 2: Test MIDI Routing

**Hypothesis**: If MIDI Player and VSTi are connected via audio connection, MIDI should flow automatically.

**Test Steps**:
1. Create MIDI Player node
2. Create VSTi node (e.g., a synth plugin)
3. Connect MIDI Player audio output → VSTi audio input
4. Enable MIDI output on MIDI Player
5. Load MIDI file and play
6. **Check**: Does VSTi receive MIDI and play notes?

**If it works**: ✅ Done! Approach A successful.

**If it doesn't work**: Need Phase 3 - explicit MIDI connections.

### Phase 3: Explicit MIDI Connections (If Needed)

If automatic routing doesn't work, we need to add explicit MIDI connections.

#### Step 1: Add MIDI Connection Method

**File**: `ModularSynthProcessor.cpp`

```cpp
bool ModularSynthProcessor::connectMIDI(const NodeID& sourceNodeID, const NodeID& destNodeID)
{
    return internalGraph->addConnection({
        {sourceNodeID, juce::AudioProcessorGraph::midiChannelIndex},
        {destNodeID, juce::AudioProcessorGraph::midiChannelIndex}
    });
}
```

#### Step 2: UI for MIDI Connections

Two options:
- **Option A**: Automatic MIDI connection when audio connection is made
- **Option B**: Separate MIDI connection tool (right-click menu, etc.)

**Recommended**: Option A - When user connects audio, also connect MIDI automatically.

```cpp
// In connection creation code:
if (connectAudio)
{
    // Connect audio channels
    synth->connect(sourceNodeID, sourceChannel, destNodeID, destChannel);
    
    // Also connect MIDI automatically
    synth->connectMIDI(sourceNodeID, destNodeID);
}
```

---

## Alternative: "MIDI Bus" Approach

**Concept**: Create a global MIDI bus that all nodes can read from/write to.

**How it works**:
- MIDI Player writes to global MIDI bus
- VSTi nodes read from global MIDI bus
- No explicit connections needed

**Pros**: Very simple, no connections needed
**Cons**: All VSTi nodes receive all MIDI (no selective routing)

**Difficulty**: **EASY (2/10)**

**Implementation**:
```cpp
// In ModularSynthProcessor
juce::MidiBuffer globalMIDIBus;

// In processBlock, before graph processing:
globalMIDIBus.clear();
// Let modules add MIDI to globalMIDIBus
internalGraph->processBlock(buffer, globalMIDIBus);
```

But this might not work with JUCE's graph architecture - need to investigate.

---

## Testing Strategy

### Test Case 1: Basic MIDI Flow
1. MIDI Player → VSTi (simple connection)
2. Load MIDI file with one track
3. Verify VSTi receives and plays notes

### Test Case 2: Multi-Track MIDI
1. MIDI Player → Multiple VSTi nodes
2. Each track routes to different VSTi
3. Verify correct MIDI channel routing

### Test Case 3: MIDI + Audio Simultaneously
1. MIDI Player → VSTi (MIDI for notes)
2. MIDI Player → Mixer (audio for CV monitoring)
3. Verify both work simultaneously

### Test Case 4: Timing Accuracy
1. Verify MIDI note timing matches CV output timing
2. Check for sample-accurate MIDI messages

---

## Comparison with External MIDI Output

| Feature | Internal MIDI (This Plan) | External MIDI (Previous Plan) |
|---------|---------------------------|-------------------------------|
| **Destination** | VSTi nodes in graph | External MIDI devices |
| **Routing** | Via graph connections | Via MidiOutputManager |
| **Use Case** | Internal synthesis | External hardware/VSTi |
| **UI** | Enable checkbox in MIDI Player | Dropdown in Audio Settings |
| **Implementation** | Generate MIDI in processBlock | Generate MIDI + send to device |
| **Complexity** | Simpler (no device management) | More complex (device selection) |

**Both can coexist**: MIDI Player can output MIDI internally AND externally simultaneously!

---

## Questions to Resolve

1. **Does MIDI flow through audio connections automatically?**
   - Need to test: Connect audio cable, check if MIDI also flows
   - **Action**: Implement Phase 1, test immediately

2. **Should MIDI connections be visual in UI?**
   - Option A: Implicit (hidden, automatic)
   - Option B: Visual (separate MIDI cables)
   - **Recommendation**: Start with implicit, add visual later if needed

3. **Multi-track MIDI routing?**
   - One MIDI Player → Multiple VSTi nodes (different channels)
   - Need channel filtering on VSTi side? Or automatic?
   - **Recommendation**: Start simple (all channels to all connected VSTi)

4. **MIDI vs CV output priority?**
   - Should MIDI Player output both MIDI and CV simultaneously?
   - **Answer**: Yes! They serve different purposes (MIDI for VSTi, CV for modular)

---

## Recommended Implementation Order

1. ✅ **Phase 1**: Add MIDI generation to MIDI Player (2-3 hours)
2. ✅ **Test**: Verify MIDI flows to connected VSTi
3. ⏭️ **If needed**: Add explicit MIDI connections (4-6 hours)
4. ⏭️ **Polish**: Add UI controls, multi-track support (2-3 hours)

**Total Estimate**: 8-12 hours (~1-2 days)

---

## Success Criteria

- ✅ MIDI Player can send MIDI to VSTi nodes
- ✅ VSTi receives and plays MIDI notes correctly
- ✅ MIDI timing is accurate (matches CV output)
- ✅ Multiple VSTi nodes can receive MIDI simultaneously
- ✅ MIDI and CV outputs work simultaneously
- ✅ Settings persist (enable MIDI output, channel selection)

---

## Conclusion

**Recommended Approach**: Start with **Approach A (Automatic Routing)** - it's the simplest and likely to work with JUCE's existing graph architecture. If automatic routing doesn't work, fall back to explicit MIDI connections.

The key insight: **VSTi nodes already accept MIDI** - we just need to get MIDI messages from MIDI Player to them via the graph routing system!

