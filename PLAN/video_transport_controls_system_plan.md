## Video Transport Controls System-wide Plan

### Objective
- Extend the improved Play/Pause/Stop semantics implemented in `VideoFileLoaderModule` to every UI surface that exposes global or per-node transport controls (top bar, mixer strips, timeline widgets, other video-related modules), ensuring “Pause resumes from current position” while “Stop returns to in-point/zero”.

### Discovery & Analysis Tasks
1. **Inventory Control Surfaces**
   - Audit top bar transport implementation (likely in `PresetCreatorApplication`, `MainComponent`, or `TransportHeaderComponent`) to understand existing button wiring and shared state (`TempoClock`, `TransportState`, etc.).
   - Identify other modules that render their own play/stop buttons (e.g., `SampleLoaderModule`, `VideoFXModule`, `VideoDrawImpactModuleProcessor`, timeline inspector panels) to determine whether they already mirror transport state or keep private flags.
2. **Trace State Flow**
   - Map the lifecycle of `TransportState` updates (source → broadcaster → module listeners) to ensure new semantics do not fight existing sync logic.
   - Verify whether top bar “Stop” currently resets `songPositionSeconds` to zero and forces modules to load from start; record any divergence.
3. **Assess Current Video Modules**
   - Review `VideoFXModule`, `VideoDrawImpactModuleProcessor`, `CropVideoModule`, and any OpenCV consumers for assumptions about play/stop semantics.
   - Document modules that lack pause awareness or assume “Stop=Pause”.
4. **UX Requirements**
   - Confirm with user whether top bar should show discrete Play, Pause, Stop buttons (vs toggle + stop) and how keyboard shortcuts should behave.
   - Determine desired behavior when multiple modules are timeline masters.

### Implementation Strategy
1. **Abstract Transport Control Logic**
   - Introduce a shared helper/service (e.g., `TransportControlModel`) that exposes `play()`, `pause()`, `stop()`, and tracks `isStopped`.
   - Ensure the helper notifies both the transport engine and all modules about state changes (possibly via `TempoClock`, `TransportBroadcaster`, or JUCE `ChangeBroadcaster`).
2. **Update Top Bar UI**
   - Refactor the top bar to use the shared helper, rendering Play, Pause, Stop states consistent with node-level controls.
   - Add visual feedback (disabled states, tooltips) when transport is driven externally (automation, external clock).
3. **Propagate to Modules**
   - For each module with local controls, route button events through the helper so semantics stay in sync.
   - Where modules follow transport (e.g., `syncToTransport`), ensure pause vs stop is respected by honoring the `isStopped` flag instead of assuming any `playing=false` means “stopped”.
4. **Testing Matrix**
   - Unit-test helper logic if feasible.
   - Scenario tests: 
        - global pause/resume,
        - global stop while module unsynced,
        - module-specific pause while top bar continues,
        - transport master switching.
   - Regression tests around looping, scrubbing, preset load/startup behavior.

### Risk Assessment
- **Overall Risk: Medium-High**
  - **Integration Risk (High):** Multiple modules rely on legacy assumptions; changing semantics could break synchronization loops or automation.
  - **UI Consistency Risk (Medium):** Top bar and nodes might desync visually if events are dropped; need robust state broadcasting.
  - **Performance Risk (Low-Medium):** Additional state checks should be lightweight, but must ensure no extra locks on audio thread.
  - **Preset Compatibility Risk (Medium):** Existing presets may rely on “stop resumes” behavior; need migration notes or compatibility flag.

### Difficulty Options
- **Level 1 – Minimal (Medium difficulty):** Apply pause/stop semantics only to the top bar using current transport signals, without touching other modules. Lowest effort but may leave inconsistencies.
- **Level 2 – Coordinated Controls (High difficulty):** Introduce shared helper, update top bar and key video modules. Requires moderate refactors and thorough testing.
- **Level 3 – Full Ecosystem (Very High difficulty):** Standardize every audio/video module, timeline view, automation lane, and remote control API. Large-scope effort likely spanning multiple iterations.

### Confidence Rating
- **Confidence: 0.63 (Moderate)**
  - **Strengths:** Recent hands-on experience with `VideoFileLoaderModule` gives concrete guidance for pause/stop flags; codebase has clear separation between transport state and modules.
  - **Weaknesses:** Top bar implementation details and other module behaviors still unknown; potential hidden dependencies (e.g., scripting, OSC) could complicate changes. Requires further discovery before precise estimates.

### Potential Pitfalls & Mitigations
- **Race Conditions Between UI and Audio Threads:** Ensure helper updates use lock-free atomics or message queues to avoid audio thread stalls.
- **Transport Master Conflicts:** When multiple modules attempt to drive transport, define precedence and guard with assertions/logging.
- **Legacy Automation/Shortcuts:** Verify keyboard shortcuts and MIDI mappings still trigger expected behavior; update documentation.
- **User Expectation Mismatch:** Some users might prefer current “Stop” semantics; consider preference toggle or at least release notes explaining the change.
- **Testing Complexity:** Scrubbing, looping, and timeline sync features must be retested; plan time for manual QA with representative videos.

### Next Steps
1. Complete discovery tasks (inventory + tracing) and document findings.
2. Prototype shared helper & top bar integration in a feature branch.
3. Gradually migrate modules, starting with high-impact video components.
4. Author regression tests / manual test checklist.
5. Communicate behavior change in release notes.

