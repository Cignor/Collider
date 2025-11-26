## Sample & Sequencer Pause-Robustness Rollout

### Goals
- Mirror the new transport intents (`TransportCommand`) in every playback-heavy module.
- Ensure SampleLoader/SampleSfx handle pause vs stop exactly like VideoFileLoader (resume from cached position on pause, hard-reset on stop).
- Stop sequencers from rewinding on every pause edge; only hard stops or explicit resets should rewind the playhead.

### 1. SampleLoaderModuleProcessor
- File: `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`
- Current behavior: obeys transport only when `syncToTransport` is true; desynced mode ignores play/pause entirely.
- Plan:
  1. Add the same `TransportCommand` awareness used in `VideoFileLoaderModule::setTimingInfo`.
  2. Introduce `handlePauseRequest` / `handleStopRequest` helpers that snapshot `currentSamplePosition`, update `lastKnownNormalized`, and gate `resumeAfterPrepare`.
  3. When unsynced, always mirror global play/pause edges (spacebar and top bar) so a pause never rewinds.
  4. Remove any forced file reopens when toggling sync; rely on cached state + `pendingSeekNormalized`.
  5. Update node UI buttons to delegate to shared helpers so local controls stay in sync with global ones.

### 2. SampleSfxModuleProcessor
- File: `juce/Source/audio/modules/SampleSfxModuleProcessor.cpp`
- Current behavior: purely trigger-driven; transport pause has no effect which makes global controls confusing.
- Plan:
  1. Cache `TransportState` in `setTimingInfo` and respect `state.lastCommand`.
  2. When paused, freeze any looping playback heads and remember per-voice offsets so a resume continues mid-sample.
  3. A stop command should flush currently playing voices and reset offsets.
  4. Provide UI feedback (disabled trigger button or status text) when transport is paused but node is waiting to resume.

### 3. Sequencer Family (Step/Multi/Stroke)
- Files:
  - `juce/Source/audio/modules/StepSequencerModuleProcessor.cpp`
  - `juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp`
  - `juce/Source/audio/modules/StrokeSequencerModuleProcessor.cpp`
- Current behavior: they call `reset()` whenever `state.isPlaying` flips from false→true, so every pause behaves like a stop.
- Plan:
  1. Track `state.lastCommand` in each `setTimingInfo`.
  2. On `TransportCommand::Pause`, capture the current step index and do **not** reset internal counters.
  3. On `TransportCommand::Stop`, fall back to the existing reset logic (rewind to step 0, queue IN-point if applicable).
  4. Ensure manual Play/Pause buttons inside each sequencer reuse the same helpers so user interactions remain deterministic.

### 4. Transport Broadcast & Testing
- Confirm the new `TransportCommand` field propagates through `TempoClock` (already updated) and any other nodes that call `applyTransportCommand`.
- Manual test grid:
  - Spacebar tap (pause) vs Stop button across Video, SampleLoader, SampleSfx, StepSequencer.
  - Toggle `Sync to Transport` mid-playback (should not reopen files anymore).
  - Device restart (simulate by toggling audio device) while modules are paused and while they are playing.

### Risks & Mitigations
- **Risk:** Per-module helpers diverge.  
  *Mitigation:* Keep helper signatures identical (`handlePauseRequest`, `handleStopRequest`) so code reviews can diff easily.
- **Risk:** Sequencer timing drift after pause.  
  *Mitigation:* Store both beat-phase and sample offsets so resume uses the same phase accumulator.
- **Risk:** SampleSfx voices stuck when transport stops.  
  *Mitigation:* Force-stop voices on `TransportCommand::Stop` and clear envelopes before returning to IDLE.


