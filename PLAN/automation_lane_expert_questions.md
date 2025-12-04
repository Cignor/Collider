# Automation Lane Node - Expert Questions

**Context:** We're designing an Automation Lane node that allows drawing automation curves on an infinitely scrolling timeline with a fixed center playhead. The plan is in `automation_lane_node_plan.md`. These questions need expert input to ensure proper implementation.

---

## Thread Safety & Data Access Patterns

### Question 1: Lock-Free vs. Locked Access
**Context:** The audio thread needs to read automation data continuously while the UI thread modifies it via drawing.

**Current Plan:** Use copy-on-write: UI creates new immutable chunks, audio reads from old chunks.

**Questions:**
1. Is the **copy-on-write pattern** the best approach here? I see `AnimationModuleProcessor` uses atomic pointer swapping with `std::memory_order_acquire`. Should we follow that pattern instead?
2. For chunk-based storage, should we:
   - Use `juce::ReadWriteLock` for chunk access?
   - Or use atomic pointer swapping (like AnimationModuleProcessor)?
   - Or use lock-free ring buffer pattern?
3. What's the performance impact of reading from a `std::deque<AutomationChunk>` during audio processing? Should we flatten to a single buffer after chunk updates?

**Reference Code:**
- `AnimationModuleProcessor.cpp` lines 119-150: Uses atomic pointer with try-lock for cleanup
- `VideoDrawImpactModuleProcessor.cpp`: Uses `ScopedLock` for frame access

---

## Memory Management & Storage Limits

### Question 2: ValueTree State Size Limits
**Context:** We need to save potentially large amounts of automation data (5-10 minutes at 256 samples/beat).

**Questions:**
1. Are there **practical size limits** for `ValueTree` state in `getExtraStateTree()`?
   - I see modules save file paths, MIDI data, curve points, etc.
   - Is there a recommended maximum size (MB) before performance degrades?
2. For large automation data, should we:
   - Save directly in ValueTree (base64 encoded)?
   - Save as external file reference (like SampleLoader does for audio files)?
   - Use compression (e.g., delta encoding, run-length encoding)?
3. How does the undo/redo system handle large state snapshots? Should we exclude automation data from undo stack?

**Reference Code:**
- `guides/UNDO_REDO_SYSTEM_GUIDE.md`: Mentions memory concerns with large snapshots
- `guides/XML_SAVING_AND_LOADING_HOW_TO.md`: Shows ValueTree serialization patterns

---

### Question 3: Memory Budget
**Context:** Infinite timeline with maximum duration cap (5-10 minutes).

**Questions:**
1. What's a **reasonable memory budget** for a single Automation Lane node?
   - Example: 5 minutes @ 120 BPM = 600 beats = 153,600 samples = ~614 KB (float)
   - Is this acceptable, or should we be more aggressive with limits?
2. Should we implement:
   - Hard memory limit (fail gracefully when reached)?
   - Soft limit (warn user, continue but trim old data)?
   - Dynamic limit based on available system memory?
3. Is chunk cleanup during `processBlock` acceptable (non-blocking try-lock pattern), or should cleanup happen in a background thread?

**Reference Code:**
- `AnimationModuleProcessor.cpp` lines 123-134: Non-blocking cleanup in audio thread

---

## Performance & Real-Time Requirements

### Question 4: Drawing Performance
**Context:** User draws on scrolling timeline in real-time, UI thread needs to update visualization.

**Questions:**
1. What's the **expected frame rate** for ImGui drawing?
   - Should we throttle drawing updates (e.g., update every N mouse events)?
   - Or is real-time drawing at 60 FPS expected?
2. For scrolling timeline drawing:
   - Is scroll-aware culling (like MIDI Player) sufficient?
   - Should we implement dirty region tracking for redraws?
   - Or rely on ImGui's automatic clipping?
3. What's the performance cost of converting mouse coordinates to time positions during drag? Should we cache conversion factors?

**Reference Code:**
- `MIDIPlayerModuleProcessor.cpp` lines 1182-1211: Scroll-aware culling
- `guides/NodeVisualizationGuide.md`: Performance best practices

---

### Question 5: Audio Thread Performance
**Context:** Audio thread samples automation data at playback position, potentially looking up across multiple chunks.

**Questions:**
1. For chunk lookup during playback:
   - Is linear search through `std::deque` acceptable (chunks are time-ordered)?
   - Or should we maintain a time-to-chunk index map?
   - What's the expected chunk count (e.g., 10-20 chunks for 5 minutes)?
2. For interpolation between samples:
   - Linear interpolation sufficient, or need cubic?
   - Should interpolation happen in audio thread or pre-computed?
3. What's the acceptable latency for automation changes? (e.g., user draws → appears in audio output)

**Reference Code:**
- `FunctionGeneratorModuleProcessor.cpp`: Linear interpolation between curve points
- `MIDIPlayerModuleProcessor.cpp` lines 181-209: Efficient note lookup with hints

---

## Coordinate System & Transformations

### Question 6: Fixed Playhead Coordinate Math
**Context:** Playhead fixed at center, timeline scrolls around it. Need to convert between screen coords, content coords, time, and samples.

**Questions:**
1. For fixed center playhead:
   - Should playhead always be at `nodeWidth / 2` in screen space?
   - Or should it be at a fixed content position (e.g., always shows time=currentPlaybackTime)?
2. During auto-scroll, should we:
   - Smooth scroll (e.g., lerp over multiple frames)?
   - Instant snap to keep playhead centered?
   - What's the UX preference?
3. When user draws, the mouse position needs to be converted:
   - Screen X → Content X (add scroll offset) → Time → Sample Index
   - Is this conversion chain correct? Any gotchas?

**Reference Code:**
- `MIDIPlayerModuleProcessor.cpp` lines 1327-1363: Playhead positioning and click-to-seek
- `MidiLoggerModuleProcessor.cpp` lines 517-560: Similar playhead handling

---

## Architecture & Design Decisions

### Question 7: Chunk Size Strategy
**Context:** Need to decide chunk granularity for infinite timeline.

**Questions:**
1. What's the **optimal chunk size**?
   - Small chunks (e.g., 1 bar = 4 beats): More chunks, but easier to edit
   - Large chunks (e.g., 16 bars = 64 beats): Fewer chunks, but more memory per chunk
   - Dynamic sizing based on editing density?
2. Should chunks be:
   - Fixed-size (easier memory management)?
   - Variable-size (more efficient for sparse automation)?
3. When extending timeline, should we:
   - Pre-allocate chunks (reduce allocation during playback)?
   - Allocate on-demand (save memory when timeline is short)?

---

### Question 8: Loop Mode vs. Infinite Playback
**Context:** User can loop automation or play infinitely.

**Questions:**
1. For loop mode:
   - Should loop length be explicit (user sets bars)?
   - Or auto-detect from data (loop until first non-drawn region)?
2. When looping, should we:
   - Reset playback position to 0 (hard reset)?
   - Continue phase accumulator (smooth loop)?
3. What happens at timeline boundaries?
   - Wrap to beginning (loop)?
   - Hold last value?
   - Return to default (0.5)?

**Reference Code:**
- `FunctionGeneratorModuleProcessor.cpp` lines 244-251: Loop behavior
- `StepSequencerModuleProcessor.cpp`: Loop handling

---

## Integration & Transport Sync

### Question 9: Transport Sync Accuracy
**Context:** Need to sync automation playback to host transport.

**Questions:**
1. For transport sync:
   - Should we use `m_currentTransport.songPositionBeats` directly?
   - Or maintain our own phase accumulator synced to transport?
   - Which is more accurate over long durations?
2. How should we handle transport tempo changes mid-playback?
   - Recalculate automation position?
   - Or continue at previous tempo until next loop?
3. Should we respect `forceGlobalReset` from timeline master (like FunctionGenerator does)?

**Reference Code:**
- `FunctionGeneratorModuleProcessor.cpp` lines 157-162: Global reset handling
- `StepSequencerModuleProcessor.cpp` lines 323-328: Transport reset

---

### Question 10: State Persistence Format
**Context:** Need to save/load automation data efficiently.

**Questions:**
1. For saving automation chunks:
   - Save each chunk as separate ValueTree child?
   - Or flatten to single array with chunk metadata?
   - Which is faster to load?
2. Should we version the state format (for future compatibility)?
   - Current plan: Single version
   - Or should we use versioning like other modules?
3. For loading, should we:
   - Load all chunks immediately (simple but slower)?
   - Lazy-load chunks on-demand (complex but faster)?

**Reference Code:**
- `FunctionGeneratorModuleProcessor.cpp` lines 611-650: State save/load
- `guides/XML_SAVING_AND_LOADING_HOW_TO.md`: ValueTree patterns

---

## UI/UX Decisions

### Question 11: Drawing Interaction Model
**Context:** User draws on scrolling timeline.

**Questions:**
1. Should drawing be:
   - Always enabled (can draw at any time)?
   - Mode-based (Record mode = draw, Edit mode = edit existing)?
2. When drawing during playback:
   - Should changes appear immediately in audio output?
   - Or queue changes to apply on next loop/bar?
3. Should we support:
   - Draw tool (freehand)?
   - Line tool (click start, drag to end)?
   - Both (toggle between)?

**Reference Code:**
- `FunctionGeneratorModuleProcessor.cpp` lines 515-542: Freehand drawing
- `MidiLoggerModuleProcessor.cpp`: Recording workflow

---

### Question 12: Visual Feedback
**Context:** Need to show automation curve, playhead, and drawing state.

**Questions:**
1. Should we show:
   - Only drawn regions (sparse visualization)?
   - Complete curve (interpolated, fills gaps)?
2. For playhead indicator:
   - Solid line (like MIDI Player)?
   - Or add waveform preview at playhead?
3. Should we highlight:
   - Active drawing region?
   - Regions under playhead?
   - Both?

---

## Summary of Critical Decisions Needed

**High Priority:**
1. ✅ Thread safety pattern (atomic vs. lock-based)
2. ✅ Memory budget and limits
3. ✅ Chunk size strategy
4. ✅ Coordinate transformation approach

**Medium Priority:**
5. ValueTree state size limits
6. Drawing performance expectations
7. Loop behavior

**Low Priority (can defer):**
8. Advanced drawing tools
9. Visual feedback enhancements
10. Undo/redo integration

---

## Questions for Expert

Please review the plan document (`automation_lane_node_plan.md`) and these questions. Your input on the **High Priority** items would be most valuable, but any feedback is appreciated!

**Specific Areas Where Expert Input Would Help:**
1. **Thread safety architecture** - Best pattern for this use case?
2. **Memory management** - Acceptable limits and cleanup strategies?
3. **Performance optimization** - Where should we focus effort?
4. **State persistence** - Best format for large data?

Thank you!

