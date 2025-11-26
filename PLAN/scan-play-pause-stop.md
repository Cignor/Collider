## System-wide Play/Pause/Stop Scan

### Overview
- Goal: map every code path that toggles transport state or module-local playback, with emphasis on spacebar handling, top bar controls, and module buttons (esp. `VideoFileLoaderModule`).

### Key Components

1. **Global Input (Spacebar & Shortcuts)**
   - `PresetCreatorComponent::keyPressed / keyStateChanged` (`juce/Source/preset_creator/PresetCreatorComponent.cpp` lines ~461-598):
     - Short press toggles via `setMasterPlayState(!isCurrentlyPlaying)`.
     - Long press (hold) triggers play, releasing triggers stop.
     - `setMasterPlayState` controls audio callback + `ModularSynthProcessor::setPlaying`.

2. **Top Bar Transport (ImGui Node Editor Menu)**
   - `ImGuiNodeEditorComponent::drawMenuBar()` (`juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` lines ~1752-1810):
     - Play/Pause button -> `PresetCreatorComponent::setMasterPlayState(true/false)` (Fallback: `synth->setPlaying`).
     - Stop button: `setMasterPlayState(false)` + `synth->resetTransportPosition()`.

3. **ModularSynthProcessor Transport API**
   - `setPlaying(bool)` (`juce/Source/audio/graph/ModularSynthProcessor.h` ~89): sets `m_transportState.isPlaying`, broadcasts via `ModuleProcessor::setTimingInfo`.
   - `resetTransportPosition()` resets `songPosition` and `m_samplePosition`.
   - `setTransportPositionSeconds()` used by timeline masters to drive global transport (e.g., video/audio modules).

4. **Module Processor Hooks**
   - `ModuleProcessor::setTimingInfo(const TransportState&)` is the handshake each module overrides. Key modules:
     - `VideoFileLoaderModule::setTimingInfo` (file inspected).
     - `SampleLoaderModuleProcessor::setTimingInfo` (likely similar logic).
     - Other sequencer/time-based modules (StepSequencer, MultiSequencer, StrokeSequencer, TTS, etc.) need review for pause/stop semantics.
   - `ModuleProcessor::forceStop()` invoked after patch load to ensure modules aren't playing unexpectedly.

5. **Video Module-Specific Controls**
   - Node UI buttons for Play/Pause/Stop (within `VideoFileLoaderModule::drawParametersInNode`).
   - Interaction with `syncToTransport` flag controls whether module follows global transport.
   - Additional modules (Video FX, Video Draw Impact, Crop Video) likely have similar UI controls; need to inspect their `drawParametersInNode` and playback logic.

6. **Other Global Control Surfaces**
   - Toolbar or menu commands (search for strings: `"Play"`, `"Pause"`, `"Stop"`, `setMasterPlayState`, `resetTransportPosition`).
   - Potential MIDI/OSC bindings or external triggers (TempoClock module edges, CV inputs) found via `setPlaying()` calls or `TransportState` usage.

### Pause-Specific Implementation Notes
1. **Spacebar Short vs Long Press**
   - Short press toggles `setMasterPlayState(!isPlaying)` (true pause if modules honor `state.isPlaying=false` without rewinding).
   - Long press (hold) enforces play; releasing triggers a “stop” (spacebar audition UX) → modules need to treat this as either pause or stop.
2. **Top Bar Pause Button**
   - Simple `setMasterPlayState(false)`; no transport reset → functions as a pause globally.
3. **Module-Level Pause Handling**
   - `VideoFileLoaderModule`: keeps internal `playing` + `isStopped`, now also caches `lastKnownNormalizedPosition` and resume flag to survive device restarts.
   - Need to inspect:
     - `SampleLoaderModuleProcessor` (likely similar playhead logic).
     - Sequencers (Step/Multi/Stroke): do they keep last position when transport stops? check `lastPlayheadPos`, `playheadParam`.
     - Recorders: `RecordModuleProcessor` has explicit pause/resume APIs; interacts with `ModularSynthProcessor::pauseAllRecorders`.
     - TTS module scrubbing pause logic.
4. **Transport Broadcast**
   - Pause semantics rely on modules respecting `state.isPlaying` transitions without calling `resetTransportPosition()` or rewinding unless stop is explicit.
   - Need to catalogue modules that ignore transport when desynced and ensure they still mirror pause (like we just fixed for video).
5. **Timeline Masters**
   - Modules acting as timeline masters (SampleLoader/Video) ignore transport position updates; pause/resume must be handled internally (mirroring transport edges even when `syncToTransport=false`).

### Targets for Deep Pause Audit
1. `PresetCreatorComponent::setMasterPlayState` (spacebar & menu).
2. `ImGuiNodeEditorComponent` transport buttons (pause path).
3. `ModularSynthProcessor::setPlaying`, `pauseAllRecorders`, `resumeAllRecorders`.
4. Modules with internal play state:
   - Video: `VideoFileLoaderModule`, `VideoFX`, `VideoDrawImpact`, `CropVideo`.
   - Audio sample players: `SampleLoaderModuleProcessor`, `SampleSfxModuleProcessor`.
   - Sequencers: Step, Multi, Stroke, TTS performer.
   - Recorders / Physics / Debug modules that expose pause toggles.
5. CV-triggered play/pause (TempoClock edges, `setPlaying(true/false)` from modules).

### Detailed Findings (Pause-Focused)

#### 1. `PresetCreatorComponent`
- `keyPressed/keyStateChanged`:
  - Short press toggles play state. No transport reset, so pause semantics depend on modules honoring `state.isPlaying=false`.
  - Long press (hold) triggers play, release calls `setMasterPlayState(false)` **and** (in timer) `setMasterPlayState(false)` again, but **no reset** here either.
- `setMasterPlayState(bool shouldBePlaying)`:
  - Controls audio device callback (start/stop).
  - Calls `synth->setPlaying(shouldBePlaying)` only; no stop-specific logic. So modules must differentiate pause vs stop internally.

#### 2. `ModularSynthProcessor`
- `setPlaying(bool)` updates `m_transportState.isPlaying` and immediately broadcasts to modules via `setTimingInfo`.
- `resetTransportPosition()` only called explicitly (e.g., Stop button, some modules).
- `pauseAllRecorders/resumeAllRecorders` exist for recording modules; check `RecordModuleProcessor` for usage.

#### 3. `ImGuiNodeEditorComponent` Transport Buttons
- Play/Pause button: toggles using `PresetCreatorComponent::setMasterPlayState`.
- Stop button: `setMasterPlayState(false)` + `synth->resetTransportPosition()`—this is the only default UI action that rewinds transport, so modules equate this with “hard stop.”

#### 4. Module-Level Pause Behavior
- **Video Modules**
  - `VideoFileLoaderModule`: now caches playhead and respects pause both synced & unsynced.
  - `VideoFXModule`, `CropVideoModule`: no `setTimingInfo` override; rely on upstream sources (if source pauses, effects pause). No desynced handling.
  - `VideoDrawImpactModuleProcessor`: uses `getSourceTimelineState()` but doesn’t override `setTimingInfo`; tracking depends on source timeline.
- **Sample Playback**
  - `SampleLoaderModuleProcessor`: only honors pause when `syncToTransport` is on; desynced mode ignores transport.
  - `SampleSfxModuleProcessor`: no transport awareness; playback purely trigger-driven.
- **Sequencers**
  - `StepSequencer`, `MultiSequencer`, `StrokeSequencer`: `setTimingInfo` resets playhead on every play edge; pause behaves like stop.
  - `TTSPerformerModuleProcessor`: good pause behavior—sets `isPlaying` from transport and only resets on play edges.
- **Record Modules**
  - `RecordModuleProcessor` exposes `pauseRecording()`/`resumeRecording()`, used by `ModularSynthProcessor::pauseAllRecorders`.
- **Physics/Input/Debug modules**
  - `PhysicsModuleProcessor::setTimingInfo` simply caches the transport state; the timer loop checks `m_currentTransport.isPlaying` before stepping, so pause should freeze simulation automatically. `forceStop()` also zeroes velocities.
  - `InputDebugModuleProcessor`, `DebugModuleProcessor`, etc. don’t hook transport at all (pause handled via their own UI toggles).
- **Noise/LFO/Random/Function Generators**
  - `NoiseModuleProcessor`, `LFOModuleProcessor`, `FunctionGeneratorModuleProcessor`, `RandomModuleProcessor` all just cache `state` in `setTimingInfo`. Their DSP loops read `m_currentTransport.isPlaying` (or ignore it), so they neither rewind nor honor pause explicitly. Pause behavior depends on how their outputs are used downstream.
- **Oscillators (PolyVCO/VCO etc.)**
  - `PolyVCOModuleProcessor`, `VCOModuleProcessor` simply store the transport state and continue generating even when paused; any gating must be handled downstream.
- **Timeline / Tempo Utilities**
  - `TimelineModuleProcessor`: caches transport state; when `m_currentTransport.isPlaying` is false it clears outputs and returns, so pause stops automation playback but doesn’t rewind until transport resumes (unless global reset flag set).
  - `TempoClockModuleProcessor`: CV Play/Stop edges call `parent->setPlaying(true/false)` and `resetTransportPosition()`; always a hard stop.

#### 5. Tempo Clock & CV
- `TempoClockModuleProcessor` watches CV edges for Play/Stop/Reset; it calls `parent->setPlaying(true/false)` and `resetTransportPosition()`.
- Need to list all modules that call `parent->setPlaying(...)` to see if they force stop behavior.

### Next Inspection Steps
1. **Video Modules (Family Pass)**
   - `VideoFileLoaderModule` ✅ (pause/resume logic implemented).
   - `VideoFXModule` ☐ inspect `setTimingInfo`/frame fetch.
   - `VideoDrawImpactModuleProcessor` ☐ inspect timeline integration.
   - `CropVideoModule` ☐ inspect transport hooks.
   - Populate TODO item #1 accordingly.
2. **Sample Playback Modules**
   - `SampleLoaderModuleProcessor` ⚠ already reviewed; needs desync pause fix.
   - `SampleSfxModuleProcessor` ☐ inspect for transport awareness.
   - Add findings to TODO item #2.
3. **Sequencer Family**
   - `StepSequencer`, `MultiSequencer`, `StrokeSequencer` reset on play edges.
   - Check any other sequencer-like modules (e.g., Timeline/FunctionGenerator if they behave like sequencers).
   - Track under TODO item #3.
4. **Recorder Modules**
   - Confirm `RecordModuleProcessor` pause/resume and how spacebar uses `pauseAllRecorders`.
   - TODO item #4.
5. **TempoClock / CV Control**
   - Inspect `TempoClockModuleProcessor` and any other CV-driven transport controllers.
   - Determine whether they can do soft pauses or only hard stops.
   - TODO item #5.
6. **Other setTimingInfo Overrides**
   - Physics, Noise, FunctionGenerator, LFOModule, Random, PolyVCO/VCO, TimelineModule, etc.
   - Determine if they need pause handling.
   - TODO item #6.

### Open Questions Focused on Pause
1. Which modules **rewind on `playing=false`** even when pause was intended?
2. Do any modules treat `setPlaying(false)` as “stop” because they also call `resetTransportPosition()` internally?
3. Can we centralize pause semantics (e.g., differentiate “pause” vs “stop” in `setMasterPlayState`) to avoid per-module hacks?
4. How do recording modules coordinate pause (spacebar) vs stop (menu/timeline reset)?
5. Are there timeline views or automation lanes that assume transport pauses always mean “rewind to start”?

### Next Steps / Questions
1. **Catalog all `setTimingInfo` overrides** (done; see sections above) and classify which need pause fixes.
2. **Trace every pause entry point**:
   - Spacebar short press → `PresetCreatorComponent::setMasterPlayState(!isPlaying)`.
   - Spacebar long press release → `setMasterPlayState(false)` (acts like stop but without transport reset).
   - Top bar Pause/Play/Stop buttons → `ImGuiNodeEditorComponent` calling `setMasterPlayState` (Stop also calls `resetTransportPosition`).
   - Node-level Play/Pause/Stop (e.g., VideoFileLoaderModule) directly toggling `playing`/`isStopped`; must remain consistent with global transport.
   - Sync checkbox in modules toggles `syncParam` and `syncToTransport`; unsynced modules must still mirror global pause edges if desired.
3. **Audit `parentSynth->setPlaying(...)` usages** (TempoClock, etc.) to see when external control forces stop/pause.
4. **Identify modules that need improvements** (e.g., SampleLoader unsynced pause, SampleSfx, sequencers resetting, VideoFX/CropVideo following unsynced pause, etc.).
5. **Plan implementation steps** (per module or central policy) to make pause behavior robust, especially for VideoFileLoaderModule when `sync` is off.

### Deliverables
- Full map of play/pause/stop call graph.
- List of modules that need pause-friendly fixes (esp. when desynced).
- Recommendation whether to centralize “pause vs stop” semantics in `setMasterPlayState` or `ModularSynthProcessor`.

