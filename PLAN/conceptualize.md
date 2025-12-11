# Automation Lane Node - Conceptual Design

## Core Concept

An **Automation Lane** node that allows users to draw automation curves on an infinitely scrolling timeline. The timeline scrolls from left to right, with a fixed playhead indicator in the center showing the current playback position. Users can draw curves as the timeline scrolls, creating automation data that can be played back at configurable rates.

## Inspiration & References

### Similar Concepts in Music Software

1. **DAW Automation Lanes** (Ableton Live, Logic Pro, FL Studio)
   - Horizontal scrolling timeline
   - Drawing tools for automation curves
   - Fixed playhead during playback
   - Zoom and scroll controls

2. **Envelope Followers** (Eurorack modules)
   - Real-time automation capture
   - Scroll-through visualization
   - Fixed reference point for "now"

3. **Tape Recorder Metaphor**
   - Fixed playhead (recording/playback head)
   - Moving tape (scrolling timeline)
   - Can record or play back automation

## Design Questions & Decisions

### Q1: Timeline Behavior

**Option A: Infinite Scrolling**
- Timeline extends infinitely in both directions
- New automation data gets appended as user draws
- Pros: Unlimited automation length, intuitive for live recording
- Cons: Memory management, harder to visualize total length

**Option B: Loop-Based with Expandable Length**
- Start with fixed loop length (e.g., 4 bars)
- User can extend loop length as needed
- Pros: Clear boundaries, easier to understand
- Cons: Need to manage loop points, may feel limiting

**Option C: Infinite Scrolling with Maximum Length**
- Scroll infinitely but cap total automation data (e.g., 10 minutes)
- Old data gets trimmed from the beginning
- Pros: Balance between unlimited and manageable
- Cons: Data loss edge cases

**RECOMMENDATION: Option C** - Infinite scrolling with reasonable maximum (5-10 minutes of data), trim oldest data when limit reached.

### Q2: Drawing Model

**Option A: Sample-Based Drawing** (like FunctionGenerator)
- Fixed resolution curve (e.g., 256 points per loop)
- Draw directly to curve points
- Pros: Simple, predictable memory usage
- Cons: Resolution tied to loop length

**Option B: Event-Based Drawing** (like MIDI Logger)
- Store discrete "breakpoints" at specific time points
- Interpolate between breakpoints for playback
- Pros: Efficient for sparse automation, editable points
- Cons: More complex interpolation logic

**Option C: Hybrid: Sample-Based with Breakpoint Editing**
- Store high-resolution sample buffer (like FunctionGenerator)
- Allow editing via breakpoints that modify samples
- Pros: Best of both worlds
- Cons: Most complex implementation

**RECOMMENDATION: Option A initially** - Sample-based drawing matching FunctionGenerator's approach. Can upgrade to Option C later.

### Q3: Playhead Position

**Option A: Fixed Center** (requested)
- Playhead always at center of visible area
- Timeline scrolls around it
- Pros: Easy to see "now", tape recorder feel
- Cons: Can't see future/past easily

**Option B: Fixed Left** (traditional DAW)
- Playhead at left edge, content scrolls right
- Pros: Can see future, familiar UI pattern
- Cons: Less intuitive for "live drawing"

**RECOMMENDATION: Option A** - Fixed center playhead as requested, creates unique "tape recorder" workflow.

### Q4: Recording Mode vs. Editing Mode

**Option A: Always Record**
- Timeline always scrolling, drawing always active
- Pros: Simple, always ready
- Cons: Can't pause to edit, accidental drawings

**Option B: Explicit Record/Edit Toggle**
- Record mode: timeline scrolls, drawing enabled
- Edit mode: timeline paused, can edit existing automation
- Pros: Clear workflow, prevent accidents
- Cons: Extra UI controls needed

**RECOMMENDATION: Option B** - Record/Edit toggle for clarity. Transport controls also affect recording.

### Q5: Automation Data Storage

**Storage Strategy:**
- Use circular buffer or time-based chunks
- Each "chunk" represents a fixed time window (e.g., 4 bars)
- New chunks allocated as timeline extends
- Old chunks freed when beyond maximum length

**Resolution:**
- Samples per beat (e.g., 256 samples per beat)
- Higher resolution = smoother curves but more memory
- Default: 256 samples/beat (matches FunctionGenerator's CURVE_RESOLUTION for 1-beat loop)

### Q6: Playback Rate Control

**Sync Options:**
1. **Free (Hz)** - Manual rate like FunctionGenerator
2. **Sync to Transport** - Locked to host tempo with division selector

**Rate Display:**
- Show effective BPM (for RhythmInfo introspection)
- Display as Hz in free mode, beats/bar in sync mode

### Q7: Zoom & Scrolling

**Zoom:**
- Similar to MIDI Player: pixels per beat slider
- Affects how much timeline is visible
- Doesn't affect data resolution

**Auto-Scrolling:**
- During playback/recording: auto-scroll to keep playhead centered
- During editing: manual scroll enabled
- User can override auto-scroll manually

## UI Layout Concept

```
┌─────────────────────────────────────────────────┐
│ [▶ Record] [⏸ Pause] [⏹ Stop] [Loop: ✓]       │
│ Rate: [1.0 Hz] or [Sync: 1/4]                   │
│ Zoom: [100 px/beat] ━━━━━━━━━━━━━━              │
├─────────────────────────────────────────────────┤
│ Timeline Ruler (bars/beats)                     │
│ | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |              │
├─────────────────────────────────────────────────┤
│                                                 │
│         ┃ ← FIXED PLAYHEAD (center)             │
│         ┃                                        │
│    ╱───╲│───╱                                   │
│   ╱     │   ╲                                   │
│  ╱      │    ╲                                  │
│ ╱       │     ╲───────                          │
│         │                                        │
│    [Drawing Canvas - scrolls left to right]     │
│                                                 │
├─────────────────────────────────────────────────┤
│ Output: [Value] [Inverted] [Bipolar] [Pitch]   │
└─────────────────────────────────────────────────┘
```

## Key Features

1. **Infinite Drawing Surface**
   - Start with straight line at 0.5 (center value)
   - Draw curves as timeline scrolls
   - Can draw in past (edit mode) or future (record mode)

2. **Fixed Center Playhead**
   - Always visible in center
   - Indicates current playback/recording position
   - Timeline scrolls around it

3. **Transport Sync**
   - Option to sync to host tempo
   - Division selector (1/32, 1/16, 1/8, 1/4, etc.)
   - Global division override support (like StepSequencer)

4. **Zoom Control**
   - Adjust pixels per beat
   - Scroll-zoom support (like MIDI Player)
   - Doesn't affect data, only visualization

5. **Loop Mode**
   - Can loop playback from start
   - Loop length can be set or auto-detect from data

## Data Model

```cpp
struct AutomationChunk {
    std::vector<float> samples;  // High-res automation data
    double startTime;            // Absolute start time (seconds)
    double duration;             // Chunk duration (seconds)
    int samplesPerBeat;          // Resolution
};

class AutomationLane {
    std::deque<AutomationChunk> chunks;  // Time-ordered chunks
    double totalDuration;                 // Total automation length
    double currentPosition;               // Playback position
    float defaultValue;                   // Straight line value (0.5)
};
```

## Integration Points

1. **Transport State** (like StepSequencer)
   - Respect `TransportCommand::Stop` to reset
   - Respect `TransportCommand::Pause` to pause recording
   - Check `forceGlobalReset` for timeline master loops

2. **RhythmInfo** (for BPM Monitor)
   - Report effective BPM based on rate/sync mode
   - Mark as synced when transport sync enabled

3. **State Saving**
   - Save all automation chunks
   - Save current position
   - Save zoom/rate settings

## Open Questions

1. **What happens at timeline start?**
   - Return to default value (0.5)?
   - Hold last drawn value?
   - **Answer**: Return to default value for clean start

2. **Can user edit past automation while recording?**
   - **Answer**: Yes, in Edit mode. Pause recording to enter Edit mode.

3. **Multiple automation lanes?**
   - Single lane initially
   - Can add multi-lane support later (like MIDI Logger tracks)

4. **Undo/Redo support?**
   - Store history of drawing operations
   - **Answer**: Phase 2 feature, not initial implementation

5. **Snap to grid while drawing?**
   - Optional grid snap for beat/bar alignment
   - **Answer**: Optional feature for later

## Comparison Matrix

| Feature | FunctionGenerator | MIDI Player | Automation Lane |
|---------|------------------|-------------|-----------------|
| Drawing | ✅ Fixed curve | ❌ | ✅ Infinite timeline |
| Timeline | ❌ | ✅ Scrolling | ✅ Infinite scrolling |
| Playhead | ❌ | ✅ Moving | ✅ Fixed center |
| Transport Sync | ✅ | ✅ | ✅ |
| Zoom | ❌ | ✅ | ✅ |
| Data Model | Fixed array | MIDI events | Sample chunks |

## References to Study

1. **FunctionGeneratorModuleProcessor.cpp** - Curve drawing logic
2. **MIDIPlayerModuleProcessor.cpp** - Timeline scrolling, zoom, playhead
3. **MidiLoggerModuleProcessor.cpp** - Recording workflow, timeline display
4. **StepSequencerModuleProcessor.cpp** - Transport sync, division control

## Next Steps

1. ✅ Conceptual design (this document)
2. ⏭️ Detailed implementation plan with risk assessment
3. ⏭️ Prototype core drawing + scrolling logic
4. ⏭️ Integrate transport sync
5. ⏭️ Add playback/recording modes
6. ⏭️ Polish UI/UX

