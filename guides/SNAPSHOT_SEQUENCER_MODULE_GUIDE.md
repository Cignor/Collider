# Snapshot Sequencer Module Guide

## Overview

The Snapshot Sequencer module is a control-only JUCE processor that listens to clock/reset control-voltage (CV) inputs and, on each clock edge, requests a full patch recall for the owning voice. It is intended to let performers step through up to 16 pre-captured synthesiser states in sync with external timing sources. This guide explains how the processor is wired, how it stores and restores snapshots, and what is currently missing for production use.

## Table of Contents

1. [Responsibilities & Data Flow](#responsibilities--data-flow)
2. [Parameter & State Model](#parameter--state-model)
3. [Clock and Reset Edge Detection](#clock-and-reset-edge-detection)
4. [Snapshot Capture & Recall Pipeline](#snapshot-capture--recall-pipeline)
5. [UI Integration Hooks](#ui-integration-hooks)
6. [Persistence Format](#persistence-format)
7. [Current Behaviour vs Intended Behaviour](#current-behaviour-vs-intended-behaviour)
8. [Known Gaps & Risks](#known-gaps--risks)
9. [Fix & Hardening Strategy](#fix--hardening-strategy)
10. [Validation Checklist](#validation-checklist)

---

## Responsibilities & Data Flow

- **Module contract:** Inherits from `ModuleProcessor`, declares one stereo input bus (clock/reset CV lanes) and a dummy mono output bus that is cleared each block.
- **High-level flow:**  
  1. Audio thread receives a buffer containing the clock/reset CV waveforms.  
  2. Rising edges are detected sample-by-sample.  
  3. On `reset`, the sequencer jumps to step 0 and immediately requests the stored snapshot.  
  4. On `clock`, the sequencer advances the step counter, wraps at `numSteps`, and requests the matching snapshot.  
  5. No audio is produced; instead a `Command::LoadPatchState` is enqueued on the shared `CommandBus`.

Relevant implementation excerpt:

```44:120:juce/Source/audio/modules/SnapshotSequencerModuleProcessor.cpp
void SnapshotSequencerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // ... existing code ...
    for (int i = 0; i < numSamples; ++i)
    {
        if (resetIn != nullptr)
        {
            bool resetHigh = resetIn[i] > 0.5f;
            if (resetHigh && !lastResetHigh)
            {
                currentStep.store(0);
                if (isSnapshotStored(0) && commandBus != nullptr && parentVoiceId != 0)
                {
                    Command cmd;
                    cmd.type = Command::Type::LoadPatchState;
                    cmd.voiceId = parentVoiceId;
                    cmd.patchState = snapshots[0];
                    commandBus->enqueue(cmd);
                }
            }
            lastResetHigh = resetHigh;
        }
        if (clockIn != nullptr)
        {
            bool clockHigh = clockIn[i] > 0.5f;
            if (clockHigh && !lastClockHigh)
            {
                int oldStep = currentStep.load();
                int newStep = (oldStep + 1) % numSteps;
                currentStep.store(newStep);
                if (isSnapshotStored(newStep) && commandBus != nullptr && parentVoiceId != 0)
                {
                    Command cmd;
                    cmd.type = Command::Type::LoadPatchState;
                    cmd.voiceId = parentVoiceId;
                    cmd.patchState = snapshots[newStep];
                    commandBus->enqueue(cmd);
                }
            }
            lastClockHigh = clockHigh;
        }
    }
    buffer.clear();
}
```

---

## Parameter & State Model

- **APVTS layout:** Single integer parameter `numSteps` (1–16, default 8).
- **Runtime state:**  
  - `snapshots`: array of `juce::MemoryBlock`, one per step, stores serialized patch blobs (captured via UI).  
  - `currentStep`: atomic index the audio thread advances.  
  - `lastClockHigh` / `lastResetHigh`: edge-detection latches.  
  - `parentVoiceId`: 64-bit voice handle provided by the modular host.  
  - `commandBus`: pointer to IPC queue for deferred patch loads.

---

## Clock and Reset Edge Detection

- **Inputs:** channel 0 = clock, channel 1 = reset. No other audio inputs are consulted.
- **Threshold:** Hard-coded 0.5f gate threshold; no hysteresis or debounce.
- **Edge tracking:** Boolean latch per input. Rising edges (`low -> high`) trigger sequencer events.
- **Sample-by-sample scan:** Entire `numSamples` loop iterates even when trigger already fired, adding a small CPU cost but ensuring multi-edge detection.

### Improvement Targets

- Avoid scanning disconnected inputs by checking `isAudioInputConnected(0/1)` or matching pin metadata.
- Consider per-block reduction (detect edges once per block) if upstream clock is pulse-per-block, but current design expects audio-rate pulses.

---

## Snapshot Capture & Recall Pipeline

- **Capture:** UI calls `setSnapshotForStep(step, patchState)`. The method copies the `juce::MemoryBlock` into the slot.
- **Recall:** On trigger, audio thread enqueues `Command::LoadPatchState` with the stored block for consumption by the engine thread. No synchronous state mutation happens inside `processBlock`.
- **Clear:** `clearSnapshotForStep` resets the `MemoryBlock`.

Thread-safety note: the audio thread reads from `snapshots` while the message thread may write. There is no lock or double-buffering, so copies must be sized carefully to avoid torn reads (see [Known Gaps](#known-gaps--risks)).

---

## UI Integration Hooks

- **Preset Creator build:** When `PRESET_CREATOR_UI` is defined, ImGui tooling exposes:  
  - Slider for `numSteps`.  
  - Read-only status per step (`[STORED]` vs `[EMPTY]`).  
  - Live indicator of the current step based on `currentStep.load()`.
- **IO pins:** Clock and Reset pins rendered via `drawIoPins`.
- **Capture/Clear actions:** Not handled here; delegated to a higher-level ImGui node editor which has access to synthesiser `getStateInformation()`.

Snippet:

```208:270:juce/Source/audio/modules/SnapshotSequencerModuleProcessor.cpp
void SnapshotSequencerModuleProcessor::drawParametersInNode (...)
{
    // ... existing code ...
    int currentStepIndex = currentStep.load();
    ImGui::Text("Current Step: %d", currentStepIndex + 1);

    const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    for (int i = 0; i < numSteps; ++i)
    {
        ImGui::PushID(i);
        const juce::String label = "Step " + juce::String(i + 1) + ":";
        // Highlights current step and shows [STORED]/[EMPTY]
        ImGui::PopID();
    }
    ImGui::TextWrapped("Connect a clock to advance steps. Each step can store a complete patch state.");
}
```

---

## Persistence Format

- Implements `getExtraStateTree` / `setExtraStateTree` to serialize snapshots into a `ValueTree` named `"SnapshotSeqState"`.
- Each populated step becomes a child `"Step"` node with:  
  - `index`: integer step id.  
  - `data`: Base64-encoded `MemoryBlock`.
- Restore process clears all slots before decoding Base64 back to binary.

Excerpt:

```156:205:juce/Source/audio/modules/SnapshotSequencerModuleProcessor.cpp
juce::ValueTree SnapshotSequencerModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree tree("SnapshotSeqState");
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        if (snapshots[i].getSize() > 0)
        {
            juce::ValueTree stepTree("Step");
            stepTree.setProperty("index", i, nullptr);
            stepTree.setProperty("data", snapshots[i].toBase64Encoding(), nullptr);
            tree.appendChild(stepTree, nullptr);
        }
    }
    return tree;
}
```

---

## Current Behaviour vs Intended Behaviour

| Aspect | Intended | Current Implementation | Notes |
| --- | --- | --- | --- |
| Step advancement | On each clock rising edge, modulo `numSteps` | ✅ Working; wraps with `% numSteps`. | Latch ensures single trigger per edge. |
| Reset handling | Immediate jump to step 0 and recall snapshot | ✅ Working when reset CV > 0.5f. | No guard against noisy reset signals. |
| Snapshot recall | Dispatch `LoadPatchState` command to owning voice | ✅ Works when `commandBus` and `parentVoiceId` set. | Silent failure otherwise; only logging. |
| Input gating | React only when pins connected | ❌ Always assumes channels valid. | Should query connection metadata to avoid ghost triggers. |
| Thread safety | Safe concurrent access to `snapshots` | ⚠️ `MemoryBlock` copies are unsynchronised. | Race risk if UI writes while audio reads. |
| Telemetry | Publish live data for inspector | ❌ Only clears `lastOutputValues`, no telemetry hooks. | Inspector shows no diagnostics. |
| CPU usage | Lightweight | ⚠️ Sample-level loop with Base64 logs on audio thread. | Logging inside audio path is risky. |

---

## Known Gaps & Risks

- **Unsynchronised snapshot edits:** `setSnapshotForStep` writes directly to `snapshots[N]` with no lock; simultaneous reads on the audio thread can observe torn data or read partially-copied buffers.
- **Logging on the audio thread:** `juce::Logger::writeToLog` inside `processBlock` is real-time unsafe and should be removed or guarded.
- **Input connection awareness:** Module does not leverage pin connection APIs, so unconnected inputs filled with noise/denormals could cause spurious triggers.
- **CommandBus lifetime assumptions:** No null-check beyond `commandBus != nullptr`; but `parentVoiceId` defaults to 0, so module silently does nothing until both are configured. Needs explicit validation path.
- **Lack of debounce/hysteresis:** 0.5f threshold might mis-detect jittery signals; consider Schmitt trigger style thresholds or minimum pulse width.
- **No manual step advance fallback:** Without clock CV the module cannot be stepped from the UI, limiting offline testing.

---

## Fix & Hardening Strategy

**1. Make snapshot access lock-free but safe**
- Introduce `juce::SpinLock snapshotLock;` (or use `std::atomic<std::shared_ptr<juce::MemoryBlock>>`) guarding writes in UI thread; audio thread grabs a `juce::MemoryBlock` copy under `ScopedTryLock`.
- Alternatively, double-buffer: keep `std::array<std::atomic<bool>, MAX_STEPS>` dirty flags and replace the slot with an immutable `std::shared_ptr<const juce::MemoryBlock>` to avoid copying on audio thread.

**2. Remove real-time logging**
- Replace `writeToLog` calls inside `processBlock` and reset handlers with optional diagnostic counters published through telemetry (`setLiveParamValue`) or guarded `if constexpr (kDebug)` macros.

**3. Honour pin connectivity**
- Query `isAudioInputConnected(0)` / `isAudioInputConnected(1)` (provided by `ModuleProcessor`) once per block. If disconnected, skip edge detection and reset the `last*High` latches.
- Gate `buffer.getReadPointer` accordingly to avoid out-of-bounds when host downmixes to mono.

**4. Harden command routing**
- During `prepareToPlay`, validate `commandBus` and `parentVoiceId`; if missing, flag via telemetry so UI can display "No voice attached".
- Consider failing fast by publishing a warning event to the diagnostics panel.

**5. Add manual stepping and status telemetry**
- Expose an optional UI button (message thread) that enqueues a manual advance command or writes to a thread-safe queue consumed inside `processBlock`.
- Publish `currentStep` and snapshot presence booleans via `setLiveParamValue` to feed inspector overlays.

**6. Improve signal robustness**
- Introduce Schmitt trigger thresholds (e.g. `0.4f rising` / `0.2f falling`) or a minimum inter-pulse sample count to avoid double triggers on bouncy clocks.
- Optionally allow per-step dwell time derived from global transport BPM, making the module more deterministic when clock pulses are noisy.

---

## Validation Checklist

- [ ] Verify step advancement with ideal square-wave clock (unit tests or offline render).  
- [ ] Confirm reset CV forces snapshot 0 without delay.  
- [ ] Ensure manual snapshot capture/clear operations do not glitch the audio thread (profiling for lock contention).  
- [ ] Simulate rapid clock bursts to ensure no missed steps or double fires.  
- [ ] Load/save a project containing populated snapshots; confirm Base64 persistence round-trips.  
- [ ] Test behaviour when `commandBus` or `parentVoiceId` is unset; UI should surface the issue instead of failing silently.  
- [ ] Validate that UI reflects current step and stored state after implementing telemetry.  
- [ ] Measure CPU load before/after removing Logger calls to ensure real-time compliance.

---

### Key Contacts / Ownership

- **Module owner:** Modular synth team (JUCE audio modules).  
- **Integration points:** Command bus (IPC layer), preset creator UI, modular patch persistence subsystem.  
- **Related guides:** `TEMPO_CLOCK_INTEGRATION_GUIDE.md` for clocking patterns, `UNDO_REDO_SYSTEM_GUIDE.md` for state capture best practices.


