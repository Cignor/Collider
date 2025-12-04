# Automation Lane Node - Detailed Implementation Plan

## Overview

Create a new node called **AutomationLaneModuleProcessor** that combines:
- Drawing logic from FunctionGeneratorModuleProcessor
- Scrolling timeline from MIDIPlayerModuleProcessor
- Transport sync from StepSequencerModuleProcessor
- Fixed center playhead for "tape recorder" workflow

## Risk Rating: **MEDIUM-HIGH** ⚠️

**Reasoning:**
- Complex interaction between scrolling, drawing, and playback
- Memory management for infinite timeline
- Thread safety between audio and UI threads
- Real-time performance requirements
- Complex coordinate transformations (world ↔ screen ↔ time)

## Confidence Rating: **HIGH** ✅

**Strong Points:**
- ✅ Well-established patterns from existing modules (FunctionGenerator, MIDI Player, StepSequencer)
- ✅ Clear separation of concerns (UI drawing vs. audio processing)
- ✅ JUCE framework provides robust primitives (ImGui, AudioProcessor, ValueTree)
- ✅ Transport sync infrastructure already exists
- ✅ Similar scrolling timeline code exists (MIDI Player)

**Weak Points:**
- ⚠️ Infinite timeline requires careful memory management
- ⚠️ Fixed playhead + scrolling needs precise coordinate math
- ⚠️ Real-time drawing while scrolling may cause performance issues
- ⚠️ Thread-safe access to automation data for audio thread

## Difficulty Levels

### Level 1: Basic Implementation (Week 1-2)
**Difficulty: MEDIUM**
- Basic node structure (class, parameters, I/O)
- Simple fixed-resolution automation storage (single chunk, 4 bars max)
- Basic timeline rendering (no scrolling yet)
- Fixed playhead at center
- Simple drawing interaction (no scrolling during draw)

**Deliverables:**
- Node appears in UI
- Can draw automation on static timeline
- Playback outputs CV values

### Level 2: Scrolling Timeline (Week 2-3)
**Difficulty: MEDIUM-HIGH**
- Implement scrolling timeline (left-to-right)
- Auto-scroll to keep playhead centered
- Zoom controls (pixels per beat)
- Multiple chunks for extended timeline

**Deliverables:**
- Timeline scrolls during playback/recording
- Playhead stays fixed in center
- Zoom slider works

### Level 3: Transport Sync (Week 3-4)
**Difficulty: MEDIUM**
- Free (Hz) mode vs. Sync mode
- Division selector (like StepSequencer)
- Respect global division override
- RhythmInfo integration for BPM Monitor

**Deliverables:**
- Rate control (Hz or sync)
- Sync to transport works
- BPM Monitor shows correct BPM

### Level 4: Infinite Timeline & Memory Management (Week 4-5)
**Difficulty: HIGH**
- Chunk-based storage system
- Circular buffer or chunk recycling
- Memory limits and cleanup
- Efficient data access patterns

**Deliverables:**
- Timeline can extend indefinitely
- Memory usage stays bounded
- No memory leaks

### Level 5: Advanced Features (Week 5-6, Optional)
**Difficulty: HIGH**
- Edit mode (pause to edit past automation)
- Loop mode with loop point editing
- Undo/Redo support
- Grid snap for drawing
- Multiple automation lanes

**Deliverables:**
- Full-featured automation editor
- Professional workflow

## Implementation Breakdown

### Phase 1: Core Node Structure

#### 1.1 Class Definition
**File:** `juce/Source/audio/modules/AutomationLaneModuleProcessor.h`

```cpp
class AutomationLaneModuleProcessor : public ModuleProcessor
{
    // Parameters
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdMode = "mode";  // Free/Sync
    static constexpr auto paramIdLoop = "loop";
    static constexpr auto paramIdZoom = "zoom";  // UI-only
    static constexpr auto paramIdRecordMode = "recordMode";  // Record/Edit
    
    // Output channels
    enum {
        OUTPUT_VALUE = 0,
        OUTPUT_INVERTED = 1,
        OUTPUT_BIPOLAR = 2,
        OUTPUT_PITCH = 3
    };
};
```

**Risks:**
- Risk: Parameter layout conflicts with existing nodes
- Mitigation: Use unique parameter IDs, follow existing naming conventions

**Estimated Time:** 2 hours

#### 1.2 Basic Storage
**File:** `juce/Source/audio/modules/AutomationLaneModuleProcessor.cpp`

```cpp
// Simple fixed-size buffer to start
static constexpr int AUTOMATION_RESOLUTION = 256;  // Samples per beat
static constexpr int MAX_BEATS = 32;  // 8 bars at 4/4
std::vector<float> automationData;  // AUTOMATION_RESOLUTION * MAX_BEATS

// Start with default value (0.5)
automationData.assign(AUTOMATION_RESOLUTION * MAX_BEATS, 0.5f);
```

**Risks:**
- Risk: Fixed size limits automation length
- Mitigation: Start simple, upgrade to chunks in Phase 4

**Estimated Time:** 3 hours

#### 1.3 Basic Audio Processing
```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
{
    // 1. Calculate current playback position
    double currentTime = ...;
    
    // 2. Look up automation value at current time
    float automationValue = sampleAutomationAt(currentTime);
    
    // 3. Output CV values
    // ...
}
```

**Risks:**
- Risk: Time-to-sample conversion accuracy
- Mitigation: Use double precision, interpolate between samples

**Estimated Time:** 4 hours

### Phase 2: Drawing & UI

#### 2.1 Drawing Canvas (Static Timeline)
**File:** `drawParametersInNode()`

Based on FunctionGenerator's curve editor (lines 515-542):
- Canvas setup with ImGui::BeginChild
- Mouse drag detection
- Convert mouse Y to automation value (0-1)
- Convert mouse X to time position
- Write to automationData array

**Risks:**
- Risk: Coordinate transformations (screen → time → samples)
- Mitigation: Create helper functions, test thoroughly with debug output

**Estimated Time:** 6 hours

#### 2.2 Timeline Ruler
Based on MIDIPlayerModuleProcessor.cpp (lines 1158-1211):
- Draw bar/beat markers
- Use zoom setting (pixels per beat)
- Reserve space with ImGui::Dummy
- Scroll-aware culling for performance

**Risks:**
- Risk: Timeline ruler misalignment with automation curve
- Mitigation: Use same coordinate system, test with known data

**Estimated Time:** 4 hours

#### 2.3 Fixed Center Playhead
Based on MIDIPlayerModuleProcessor.cpp (lines 1325-1342):
- Draw vertical line at center of visible area
- Yellow color, triangle handle
- Always visible regardless of scroll

**Risks:**
- Risk: Playhead position incorrect during scrolling
- Mitigation: Calculate playhead X relative to timeline content, not screen

**Estimated Time:** 2 hours

### Phase 3: Scrolling Timeline

#### 3.1 Scroll Management
Based on MIDIPlayerModuleProcessor.cpp scroll handling:
- Track scrollX position
- Auto-scroll during playback to keep playhead centered
- Manual scroll enabled in edit mode

```cpp
// Auto-scroll logic
if (isPlaying && autoScrollEnabled) {
    double playheadTime = currentPlaybackTime;
    float playheadX_content = (playheadTime / (60.0 / bpm)) * pixelsPerBeat;
    float desiredScrollX = playheadX_content - (nodeWidth / 2.0f);
    ImGui::SetScrollX(desiredScrollX);
}
```

**Risks:**
- Risk: Scroll jitter or stuttering
- Mitigation: Smooth scroll updates, clamp to valid range

**Estimated Time:** 6 hours

#### 3.2 Drawing on Scrolling Timeline
**Challenge:** Mouse position must be converted to absolute time, not screen position.

```cpp
// Convert mouse X to absolute time
float mouseX_screen = ImGui::GetMousePos().x;
float mouseX_content = (mouseX_screen - canvasStartPos.x) + scrollX;
double absoluteTime = (mouseX_content / pixelsPerBeat) * (60.0 / bpm);

// Convert to sample index
int sampleIndex = static_cast<int>((absoluteTime / (60.0 / bpm)) * AUTOMATION_RESOLUTION);
```

**Risks:**
- Risk: Drawing in wrong location due to scroll offset
- Mitigation: Always use content-space coordinates, test drawing at different scroll positions

**Estimated Time:** 8 hours

#### 3.3 Zoom Control
Based on MIDIPlayerModuleProcessor.cpp (lines 1110-1156):
- Zoom slider (pixels per beat)
- Scroll-to-zoom centered on playhead
- Update timeline width based on zoom

**Risks:**
- Risk: Zoom changes cause timeline jumps
- Mitigation: Recalculate scroll position when zoom changes, keep playhead centered

**Estimated Time:** 4 hours

### Phase 4: Transport Sync

#### 4.1 Mode Selection
Based on FunctionGeneratorModuleProcessor.cpp (lines 219-252):
- Parameter: "Free (Hz)" vs "Sync"
- Free mode: Rate in Hz (like FunctionGenerator)
- Sync mode: Lock to transport tempo with division

**Risks:**
- Risk: Mode switching causes playback jump
- Mitigation: Maintain continuous time position, only change rate calculation

**Estimated Time:** 3 hours

#### 4.2 Division Selector
Based on StepSequencerModuleProcessor.cpp (lines 338-353):
- Combo box with divisions: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8
- Check for global division override from Tempo Clock
- Grey out when overridden

**Risks:**
- Risk: Division changes cause timing glitches
- Mitigation: Use beat-phase accumulator, smooth transitions

**Estimated Time:** 3 hours

#### 4.3 RhythmInfo Integration
Based on FunctionGeneratorModuleProcessor.cpp (lines 652-700):
- Implement getRhythmInfo()
- Return effective BPM based on mode
- Mark as synced when sync mode enabled

**Risks:**
- Risk: BPM calculation incorrect
- Mitigation: Use same formula as FunctionGenerator, test with BPM Monitor

**Estimated Time:** 2 hours

### Phase 5: Infinite Timeline & Memory

#### 5.1 Chunk-Based Storage
```cpp
struct AutomationChunk {
    std::vector<float> samples;  // AUTOMATION_RESOLUTION samples per beat
    double startBeat;            // Beat position where chunk starts
    int numBeats;                // Chunk length in beats
};

std::deque<AutomationChunk> chunks;  // Time-ordered chunks
const double MAX_DURATION_BEATS = 256.0;  // ~1 minute at 120 BPM
```

**Risks:**
- Risk: Memory fragmentation
- Mitigation: Use deque for efficient insert/delete, pre-allocate chunks

**Estimated Time:** 8 hours

#### 5.2 Sample Lookup Across Chunks
```cpp
float sampleAutomationAt(double timeInBeats) {
    // Find chunk containing this time
    for (const auto& chunk : chunks) {
        if (timeInBeats >= chunk.startBeat && 
            timeInBeats < chunk.startBeat + chunk.numBeats) {
            // Interpolate within chunk
            return interpolateInChunk(chunk, timeInBeats);
        }
    }
    return defaultValue;  // Not found, return default
}
```

**Risks:**
- Risk: Lookup performance (linear search)
- Mitigation: Use binary search or time-to-chunk mapping, profile and optimize

**Estimated Time:** 6 hours

#### 5.3 Chunk Cleanup
```cpp
void cleanupOldChunks() {
    double oldestAllowedBeat = currentPlaybackBeat - MAX_DURATION_BEATS;
    while (!chunks.empty() && chunks.front().startBeat < oldestAllowedBeat) {
        chunks.pop_front();
    }
}
```

**Risks:**
- Risk: Cleanup deletes chunks user is viewing
- Mitigation: Keep viewing window chunks alive, only delete far past chunks

**Estimated Time:** 4 hours

### Phase 6: Record/Edit Modes

#### 6.1 Mode Toggle
```cpp
enum class AutomationMode {
    Recording,  // Timeline scrolls, drawing enabled
    Editing     // Timeline paused, can edit past
};

bool recordMode = recordModeParam->load() > 0.5f;
```

**Risks:**
- Risk: Mode switching loses drawing state
- Mitigation: Preserve drawing state, clear only on explicit reset

**Estimated Time:** 2 hours

#### 6.2 Edit Mode Behavior
- Disable auto-scroll
- Allow manual scrolling to any position
- Enable drawing at any time position (edit past)
- Show edit cursor vs. playhead

**Risks:**
- Risk: Confusion between edit cursor and playhead
- Mitigation: Different colors, clear labels, disable editing during playback

**Estimated Time:** 6 hours

## Potential Problems & Mitigations

### Problem 1: Thread Safety
**Issue:** UI thread modifies automation data while audio thread reads it.

**Mitigation:**
- Use `juce::ReadWriteLock` or atomic operations
- Copy-on-write pattern: UI thread creates new chunks, audio thread reads immutable chunks
- Lock-free access patterns where possible

**Risk Level:** HIGH
**Estimated Fix Time:** 8 hours

### Problem 2: Drawing Performance
**Issue:** Drawing on scrolling timeline causes frame drops.

**Mitigation:**
- Limit drawing updates to visible area only
- Use dirty region tracking (only redraw changed areas)
- Throttle mouse input sampling
- Use ImGui clipping for automatic culling

**Risk Level:** MEDIUM
**Estimated Fix Time:** 6 hours

### Problem 3: Memory Growth
**Issue:** Infinite timeline causes memory to grow unbounded.

**Mitigation:**
- Implement maximum duration limit (e.g., 5 minutes)
- Clean up old chunks automatically
- Monitor memory usage, warn user if approaching limits
- Provide "clear" function to reset timeline

**Risk Level:** MEDIUM
**Estimated Fix Time:** 4 hours

### Problem 4: Coordinate System Complexity
**Issue:** Multiple coordinate systems (screen, content, time, samples) cause bugs.

**Mitigation:**
- Create helper functions for all coordinate conversions
- Use consistent naming: `screenX`, `contentX`, `timeBeats`, `sampleIndex`
- Add debug visualization showing coordinate values
- Unit tests for coordinate conversions

**Risk Level:** HIGH
**Estimated Fix Time:** 10 hours

### Problem 5: Playback Glitches
**Issue:** Automation value jumps or glitches during playback.

**Mitigation:**
- Interpolate between samples (linear or cubic)
- Smooth rate changes with parameter smoothing
- Use double precision for time calculations
- Test with extreme values (very fast/slow rates)

**Risk Level:** MEDIUM
**Estimated Fix Time:** 6 hours

### Problem 6: State Persistence
**Issue:** Automation data not saved/loaded correctly.

**Mitigation:**
- Use ValueTree for state serialization
- Version the state format
- Test save/load with various automation patterns
- Handle missing/corrupt state gracefully

**Risk Level:** MEDIUM
**Estimated Fix Time:** 8 hours

### Problem 7: Transport Sync Drift
**Issue:** Automation drifts out of sync with transport over time.

**Mitigation:**
- Use transport beat position directly (don't accumulate)
- Reset on transport stop
- Respect `forceGlobalReset` from timeline master
- Test with tempo changes and transport pause/resume

**Risk Level:** MEDIUM
**Estimated Fix Time:** 6 hours

## Testing Strategy

### Unit Tests
1. Coordinate conversion functions
2. Chunk lookup and interpolation
3. Time-to-sample calculations
4. State serialization/deserialization

### Integration Tests
1. Drawing during playback
2. Scrolling during drawing
3. Mode switching mid-playback
4. Transport sync accuracy
5. Memory cleanup behavior

### Manual Testing Checklist
- [ ] Draw automation on static timeline
- [ ] Playback outputs correct values
- [ ] Timeline scrolls during playback
- [ ] Playhead stays centered
- [ ] Zoom controls work
- [ ] Scroll-to-zoom works
- [ ] Transport sync works
- [ ] Free (Hz) mode works
- [ ] Mode switching doesn't glitch
- [ ] Drawing while scrolling works
- [ ] Edit mode allows editing past
- [ ] Memory doesn't grow unbounded
- [ ] State saves/loads correctly
- [ ] BPM Monitor shows correct BPM

## Estimated Timeline

| Phase | Duration | Difficulty | Risk |
|-------|----------|------------|------|
| Phase 1: Core Structure | 1-2 weeks | MEDIUM | LOW |
| Phase 2: Drawing & UI | 2 weeks | MEDIUM | MEDIUM |
| Phase 3: Scrolling | 2 weeks | MEDIUM-HIGH | MEDIUM |
| Phase 4: Transport Sync | 1 week | MEDIUM | LOW |
| Phase 5: Infinite Timeline | 2 weeks | HIGH | HIGH |
| Phase 6: Record/Edit Modes | 1 week | MEDIUM | MEDIUM |
| **Total** | **9-12 weeks** | **MEDIUM-HIGH** | **MEDIUM-HIGH** |

## Dependencies

### Existing Code to Study
1. `FunctionGeneratorModuleProcessor.cpp` - Curve drawing
2. `MIDIPlayerModuleProcessor.cpp` - Timeline scrolling, zoom
3. `MidiLoggerModuleProcessor.cpp` - Recording workflow
4. `StepSequencerModuleProcessor.cpp` - Transport sync

### New Dependencies
- None (all functionality exists in codebase)

## Success Criteria

### Minimum Viable Product (MVP)
- ✅ Can draw automation curves
- ✅ Timeline scrolls during playback
- ✅ Fixed center playhead
- ✅ Basic playback outputs CV
- ✅ Transport sync works

### Full Feature Set
- ✅ All MVP features
- ✅ Infinite timeline with memory management
- ✅ Zoom controls
- ✅ Record/Edit modes
- ✅ State persistence
- ✅ Performance optimized

## Next Steps

1. **Review & Approval** - Get feedback on this plan
2. **Prototype** - Build minimal version (Phase 1 + basic drawing)
3. **Iterate** - Add features incrementally
4. **Test** - Comprehensive testing at each phase
5. **Polish** - UI/UX refinement, performance optimization

## Notes

- Start simple (fixed-size buffer), upgrade to chunks later
- Focus on getting basic workflow working first
- Performance optimization can happen after core functionality
- User feedback will inform advanced features (undo, grid snap, etc.)

---

**Plan Version:** 1.0  
**Created:** 2025-01-XX  
**Author:** AI Assistant  
**Status:** Ready for Review

