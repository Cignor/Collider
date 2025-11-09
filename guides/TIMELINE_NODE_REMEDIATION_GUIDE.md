# Timeline Node Remediation Guide

## Overview

The Timeline node is intended to be the transport-synced automation recorder/player for Collider. It should capture CV/gate/trigger/raw data with sample accuracy, persist it as XML, and feed it back into the graph in perfect sync with the Tempo Clock. This guide documents the intended design, the current implementation state, the critical gaps, and a remediation strategy to finish and harden the feature for production.

---

## Intended Capabilities (from spec & architecture docs)

1. **Transport Authority** – Follows Tempo Clock start/stop/seek, exposes precise beat position, and remains the truth source for automation timing.
2. **Dynamic I/O** – Creates input/output pins that mirror connected signals (type-aware, channel counts, passthrough during record/idle).
3. **Sample-Accurate Recording** – Writes keyframes every sample (or after intelligent compression) across multiple channels without dropping data.
4. **Deterministic Playback** – Interpolates between keyframes, supports loop/one-shot, keeps hinting/indexing stable across seeks, and never drifts from transport position.
5. **Persistence** – Saves/loads automation layers to XML with metadata so sessions survive reloads and can be shared.
6. **UI & Workflow** – Mutually-exclusive Record/Play controls, channel management, transport HUD, visualizer, and future editing affordances.

Reference: `guides/TIMELINE_NODE_FEATURE_SPECIFICATION.md`.

---

## Current Implementation Snapshot

### Transport Handling

```82:182:juce/Source/audio/modules/TimelineModuleProcessor.cpp
    if (!m_currentTransport.isPlaying)
    {
        outBus.clear();
        setLiveParamValue("song_position_beats_live", 0.0f);
        return;
    }
    else if (isPlayingBack)
    {
        const juce::ScopedLock lock(automationLock);
        outBus.clear();
        ...
        m_internalPositionBeats = blockStartBeats + (numSamples * beatsPerSample);
        m_lastPositionBeats = m_internalPositionBeats;
        setLiveParamValue("song_position_beats_live", (float)m_internalPositionBeats);
    }
```

- Transport position comes from `m_currentTransport`, but processing halts with silence when `isPlaying` is false (no idle preview, no static output).
- Beat counter resets to `0` whenever transport stops, causing HUD jumps and complicating seek logic.

### Recording Path

```185:242:juce/Source/audio/modules/TimelineModuleProcessor.cpp
    else if (isRecording)
    {
        const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels(), 32);
        ...
        if (m_automationChannels.empty())
        {
            m_automationChannels.resize(1);
            m_automationChannels[0].name = "Channel 1";
            ...
        }
        ...
                double samplePosition = blockStartBeats + (i * beatsPerSample);
                float currentValue = inputData[i];
                bool shouldRecord = m_automationChannels[ch].keyframes.empty();
                if (!shouldRecord)
                {
                    const float lastValue = m_automationChannels[ch].keyframes.back().value;
                    shouldRecord = (std::abs(currentValue - lastValue) > 0.001f);
                }
                if (shouldRecord)
                    m_automationChannels[ch].keyframes.push_back({ samplePosition, currentValue });
```

- Forces at least one channel but never inspects graph wiring; dynamic channels must be added manually via UI, otherwise recording ignores extra inputs.
- Uses fixed epsilon (`0.001f`) to decimate samples, so rapid modulation gets under-sampled and “sample-accurate” promise is broken.
- No metadata stored per keyframe (signal type, channel index, timestamps in seconds, BPM snapshot).

### Playback Path

```104:177:juce/Source/audio/modules/TimelineModuleProcessor.cpp
        if (m_lastKeyframeIndexHints.size() != m_automationChannels.size())
            m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
        if (m_internalPositionBeats < m_lastPositionBeats)
            for (auto& hint : m_lastKeyframeIndexHints)
                hint = 0;
        for (size_t ch = 0; ch < m_automationChannels.size(); ++ch)
        {
            const auto& keyframes = m_automationChannels[ch].keyframes;
            ...
            while (p0Index < numKeyframes - 1 && keyframes[p0Index + 1].positionBeats <= samplePosition)
                ++p0Index;
            ... // linear interpolation only
            outputData[i] = outputValue;
        }
```

- Playback operates only while transport is running and `play` parameter is true; no idle audition.
- Lacks loop/one-shot selection, crossfade, overdub, or blend behaviour; ignoring spec’s modes.
- Linear interpolation only, no curve selection, no guard against sparse data (e.g., missing keyframe before transport start).

### Dynamic Pins & Types

```265:288:juce/Source/audio/modules/TimelineModuleProcessor.cpp
std::vector<DynamicPinInfo> TimelineModuleProcessor::getDynamicInputPins() const
{
    const juce::ScopedLock lock(automationLock);
    std::vector<DynamicPinInfo> pins;
    for (size_t i = 0; i < m_automationChannels.size(); ++i)
    {
        juce::String name = juce::String(m_automationChannels[i].name) + " In";
        pins.emplace_back(name, (int)i, PinDataType::CV);
    }
    return pins;
}
```

- Pins mirror `m_automationChannels`, but every pin is advertised as `PinDataType::CV`; Gate/Trigger/Raw never surface correctly.
- Because channel list originates from UI button presses, plugging cables cannot auto-add channels, breaking the dynamic routing contract.

### Persistence & UI

```293:361:juce/Source/audio/modules/TimelineModuleProcessor.cpp
    juce::ValueTree channelNode("Channel");
    channelNode.setProperty("name", juce::String(channel.name), nullptr);
    channelNode.setProperty("type", (int)channel.type, nullptr);
    ...
    keyNode.setProperty("pos", keyframe.positionBeats, nullptr);
    keyNode.setProperty("val", keyframe.value, nullptr);
```

- Stores minimal data (name/type/pos/val) with no BPM, duration, unit, or versioning. File references (`currentAutomationFile`) are missing entirely.
- UI offers Record/Play buttons and channel list, but lacks seek, loop toggles, mode switches, file chooser, or timeline editing tools specified in design.

---

## Gap Analysis (What it should do vs what it does)

| Area | Intended Behaviour | Current Behaviour | Impact |
|------|--------------------|-------------------|--------|
| Transport Sync | Respect play/stop/seek, keep last position, allow idle audition | Stops output + resets position when transport stops | Loses context, prevents pre-roll, breaks HUD |
| Dynamic Channels | Auto-build pins per connection & signal type | Manual channel creation, always CV | Incorrect routing, users lose gate/trigger data |
| Recording Fidelity | Sample-accurate capture, change detection per signal type | Hard-coded epsilon decimation, no type awareness | Automation aliasing, unusable for fast modulation |
| Playback Modes | Record/Play/Overdub/Override with crossfades | Binary Record/Play flags only | No overdub, no layering, UX friction |
| Persistence | Rich XML with metadata + file mgmt | Bare ValueTree, no external files | Automation lost between sessions |
| Seek/Loop | Loop regions, transport seeks, hint reset | Only forward playback, hint reset on rewind but no loop handling | Cannot loop arranged sections |
| UI Tooling | Transport strip, timeline view, scrubbing | Minimal buttons + plot | Hard to inspect/modify automation |
| Thread Safety | Lock granularity tuned for audio/UI | Coarse lock across audio/UI | Potential audio glitch on large channel edits |

---

## Failure Modes Observed / Likely

- **Under-recording**: Rapid CV or gate bursts under 0.001 delta never get captured, so playback misses events.
- **Stale Channels**: Removing channels in UI while cables remain connected leaves orphaned data and mismatched pins.
- **Transport Re-entry**: When transport restarts mid-clip, hint index may point past next keyframe, causing flat output until pointer catches up.
- **Persistence Loss**: Without external files, automation disappears after host restart despite ValueTree snapshotting.
- **Type Drift**: Gate/Trigger recorded as floats but replayed as CV amplitude; downstream modules expecting binary gates misbehave.

---

## Remediation Strategy

### Phase 1 – Stabilize Core Engine
- Refactor transport handling to keep last known beat position when stopped and allow optional idle playback preview.
- Replace epsilon-based change detection with per-type policies (e.g., every sample for gate/trigger edges, tolerance-configurable for CV).
- Introduce per-channel metadata (signal type, units, scaling) and expand `AutomationKeyframe` to carry timestamps/BPM snapshot.
- Harden playback search: binary search + cached iterators, handle seeks/loops, support both forward and reverse transport moves.

### Phase 2 – Dynamic I/O & Routing
- Drive channel creation from graph connections (observe `DynamicPinRequest` events) instead of UI-only management.
- Map signal types to `PinDataType` correctly and maintain passthrough routing for idle/record modes.
- Implement channel lifecycle (auto-add on new connection, mark-orphan + cleanup on disconnect) to keep UI + processing in sync.

### Phase 3 – Persistence + File Workflow
- Introduce automation library files (`.timeline.xml`) with metadata block (BPM, sample rate, duration, version, author).
- Persist file paths via `getExtraStateTree()` and add load/save commands to UI (with async file chooser on message thread).
- Provide import/export helpers to convert between ValueTree (for state) and disk XML (for portability).

### Phase 4 – Playback Modes & UX
- Expand mode enum to include Play, Record, Overdub, Override, Loop-only.
- Add transport HUD controls (loop toggle, locate to start/end, scrub wheel) and ensure mutual exclusivity between Record and Play states.
- Implement crossfade-on-mode-switch to avoid clicks, and optional smoothing for CV outputs.

### Phase 5 – Visualization & Editing
- Replace simple `PlotLines` with zoomable timeline view showing per-channel lanes, keyframe handles, and drag-edit support.
- Hook into undo/redo stack when editing automation or changing channel sets.
- Surface live transport info (bars/beats/ticks, BPM, duration) and allow manual position entry.

---

## Implementation Notes

- **Threading**: Audio thread currently takes `automationLock` for the entire block. Split into per-channel lock-free buffers or double-buffered copies to avoid UI-induced audio stalls.
- **Keyframe Storage**: Consider SoA layout (positions + values arrays) or ring buffer while recording. Compress contiguous identical samples using run-length encoding post-record to balance fidelity vs memory.
- **Transport Hooks**: `setTimingInfo()` already signals play-state transitions—expand to detect tempo jumps and trigger re-evaluation of `samplesPerBeat` dynamically mid-block.
- **Signal Types**: Introduce helper `PinDataType GetPinDataTypeForSignal(SignalType)` to keep routing consistent with other modules (see `ColorTrackerModule` for precedent).
- **Testing Harness**: Build offline tests that feed synthetic envelopes into `processBlock()` and assert round-trip equality after record→playback.

---

## Testing Checklist (after remediation)

- [ ] Record sine LFO at 10 Hz, verify playback error < 1e-5 peak.
- [ ] Capture gate bursts, ensure rising edges align within 1 sample after round-trip.
- [ ] Save automation, reload session, confirm channels/pins restored and data intact.
- [ ] Seek transport backwards mid-playback, verify interpolation resumes correctly.
- [ ] Loop 4-bar region while overdubbing; confirm new data merges without discontinuities.
- [ ] Stress-test 16 channels @ 256-sample buffer, ensure no XRuns and UI responsive.

---

## Open Questions

1. What interpolation curve set do we expose first (Linear, Hold, Catmull-Rom)?
2. Should idle playback honor transport stop (preview mode) or require explicit toggle?
3. How do we synchronize automation with external clock sources (Ableton Link, MIDI clock)?
4. Do we need versioned migration for legacy timeline files once structure evolves?
5. How will UI expose per-channel quantization or smoothing options?

---

## Next Steps

1. Align on interpolation + recording fidelity requirements.
2. Design dynamic pin lifecycle that cooperates with existing graph infrastructure.
3. Prototype revised recording buffer with per-type policies and benchmark.
4. Extend persistence layer with XML schema + load/save UI.
5. Iterate with UX to deliver full transport/timeline controls.

The Timeline node is close to a working prototype but still far from the production-grade automation backbone described in the specification. Addressing the gaps above will unlock reliable song-level automation across Collider’s modular ecosystem.
