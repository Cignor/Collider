# Timeline Sync Feature - External Expert Consultation Request

**Date**: 2025-11-18  
**Project**: Collider PYO - Modular Synthesizer (JUCE Framework)  
**Feature**: Timeline Synchronization for TempoClock Module  
**Status**: Planning Phase - Seeking Architecture Review

---

## Executive Summary

We are implementing a timeline synchronization feature that allows our `TempoClockModuleProcessor` to sync the global transport position to linear media playback from `SampleLoaderModuleProcessor` or `VideoFileLoaderModule`. This is a complex multi-threaded feature requiring careful architecture to ensure thread safety, accurate position synchronization, and proper integration with existing BPM priority systems.

**We are seeking expert review on:**
1. Thread safety architecture (audio thread updates vs UI thread reads)
2. Position synchronization accuracy strategies
3. Module lifecycle management patterns
4. JUCE-specific best practices for this use case

---

## Project Context

### Technology Stack
- **Framework**: JUCE 7.x (C++17)
- **Architecture**: Modular audio processor graph
- **Threading Model**: 
  - Audio thread (processBlock, ~44.1kHz)
  - UI thread (ImGui rendering, ~60fps)
  - Message thread (module add/remove operations)

### Current System Architecture

**ModularSynthProcessor** (`juce/Source/audio/graph/ModularSynthProcessor.h`)
- Manages all audio modules in a graph
- Maintains global `TransportState` (BPM, position, play state)
- Broadcasts transport state to all modules via `setTimingInfo()`
- Handles module lifecycle (add/remove)

**ModuleProcessor** (`juce/Source/audio/modules/ModuleProcessor.h`)
- Base class for all modules
- Virtual `setTimingInfo()` receives transport updates
- Virtual `processBlock()` for audio processing
- APVTS for parameter management

**TempoClockModuleProcessor** (`juce/Source/audio/modules/TempoClockModuleProcessor.h`)
- Controls global BPM and transport
- Current priority: BPM CV > Host Sync > Manual BPM
- Outputs clock signals, beat triggers, phase, etc.
- Has `syncToHost` parameter for DAW sync

**SampleLoaderModuleProcessor** / **VideoFileLoaderModule**
- Play linear media (samples/video)
- Track playback position internally
- Need to report position/duration for timeline sync

---

## Feature Requirements

### Core Functionality
1. **Timeline Source Registry**: Central registry in `ModularSynthProcessor` where media modules report their position/duration
2. **Source Selection**: UI dropdown in TempoClock to select which timeline source to sync to
3. **Transport Synchronization**: Global transport position follows selected timeline source position
4. **BPM Derivation**: Optional BPM calculation from timeline duration (configurable beats-per-timeline)
5. **State Persistence**: Selected timeline source saved/loaded in presets

### Priority System (CLARIFIED)
```
1. Timeline Sync (if enabled AND source is active)  [HIGHEST]
   - Controls both transport position AND BPM (if derivation enabled)
2. BPM CV (if connected)
   - Only affects BPM, not transport position
3. Sync to Host (if enabled)
   - Only affects BPM, not transport position
4. Manual BPM/Tap/Nudge                           [LOWEST]
```

**Key Point**: Timeline sync controls POSITION (always) and BPM (if derivation enabled). BPM CV only affects BPM, allowing combined control.

---

## Proposed Architecture

### 1. Timeline Source Registry

**Location**: `ModularSynthProcessor`

**Data Structure**:
```cpp
struct TimelineSourceInfo {
    juce::uint32 logicalId { 0 };           // Module identifier
    double totalDurationSeconds { 0.0 };    // Media length
    double currentPositionSeconds { 0.0 };  // Current playback position
    bool isActive { false };                // Has media loaded and is reporting
    juce::String sourceType;                // "sample_loader" or "video_file_loader"
    juce::String displayName;               // For UI dropdown
};

std::map<juce::uint32, TimelineSourceInfo> timelineSources;
juce::CriticalSection timelineSourcesLock;  // Thread safety
```

**Thread Access Pattern**:
- **Audio thread**: `updateTimelineSource()` called every block from SampleLoader/VideoLoader
- **UI thread**: `getAvailableTimelineSources()` called for dropdown (every frame)
- **Message thread**: `unregisterTimelineSource()` called on module deletion

### 2. Module Reporting Interface

**Location**: `ModuleProcessor.h`

```cpp
// Optional virtual methods
virtual bool canProvideTimeline() const { return false; }
virtual void reportTimelineState(double& positionSeconds, 
                                 double& durationSeconds, 
                                 bool& isActive) const
{
    juce::ignoreUnused(positionSeconds, durationSeconds, isActive);
}
```

**Implementation**: SampleLoader/VideoLoader override to report their internal position tracking.

### 3. Transport Position Update

**New Method in ModularSynthProcessor**:
```cpp
void setTransportPositionSeconds(double positionSeconds)
{
    // Clamp, update m_transportState, broadcast to all modules
}

std::atomic<bool> isTransportControlledByTimeline { false };
```

**Flag Purpose**: Prevents external position changes (e.g., from host DAW) from conflicting with timeline sync.

### 4. TempoClock Integration

**New Parameters**:
- `syncToTimeline` (bool)
- `timelineSourceId` (int, logical ID)
- `enableBPMDerivation` (bool)
- `beatsPerTimeline` (float, 1.0-32.0)

**Sync Logic in processBlock()**:
```cpp
if (syncToTimeline && source is valid && source is active)
{
    // Update transport position (ALWAYS)
    parent->setTransportPositionSeconds(source.currentPositionSeconds);
    
    // Optionally derive BPM (if enabled and BPM CV not connected)
    if (enableBPMDerivation && !bpmFromCV)
    {
        double derivedBPM = (beatsPerTimeline * 60.0) / source.totalDurationSeconds;
        parent->setBPM(derivedBPM);
    }
}
```

---

## Critical Questions for Expert Review

### 1. Thread Safety Architecture

**Question**: Is `CriticalSection` the right approach for this use case?

**Context**:
- Audio thread updates position every block (~44.1kHz, potentially 10+ sources)
- UI thread reads for dropdown (~60fps)
- Message thread removes sources (infrequent)

**Concerns**:
- Lock contention in audio thread (real-time constraint)
- Lock duration (need minimal lock time)

**Alternatives Considered**:
- Lock-free: Atomic values for position/duration, lock-free map
- Double-buffering: Audio thread writes to buffer, UI reads from snapshot
- Message passing: Audio thread sends messages, message thread updates map

**What we need**: JUCE best practices for this pattern. Is CriticalSection acceptable, or should we use lock-free from the start?

---

### 2. Position Update Frequency

**Question**: Should we update transport position every block, or use a threshold?

**Context**:
- Timeline sources update position every block
- Transport position update triggers broadcast to all modules
- May cause unnecessary work if position changes are tiny

**Proposed Solution**:
- Update only if position changed >1ms (threshold)
- Cache last position to detect significant changes

**Concerns**:
- Could cause position drift if threshold too large
- Need to balance accuracy vs performance

**What we need**: Is threshold-based updating acceptable, or should we update every block for maximum accuracy?

---

### 3. Module Lifecycle Management

**Question**: How to safely handle module deletion while timeline sync is active?

**Context**:
- TempoClock may be syncing to a source
- Source module gets deleted (message thread)
- TempoClock's `processBlock()` (audio thread) tries to access deleted source

**Proposed Solution**:
- Validate source exists before each access in `processBlock()`
- Reset to "None" if source becomes invalid
- Use `isValidTimelineSource()` helper

**Concerns**:
- Race condition: source deleted between validation and access
- Need atomic unregistration

**What we need**: Best pattern for safe module reference in audio thread when module can be deleted from message thread.

---

### 4. Position Synchronization Accuracy

**Question**: How to ensure transport position matches timeline source exactly?

**Context**:
- Block-based updates (not per-sample)
- Sample rate conversion (source may have different sample rate)
- Rounding errors in position calculations

**Proposed Solution**:
- Store positions in seconds (time, not samples)
- Use double precision
- Clamp position to [0, duration]
- Update every block (or with threshold)

**Concerns**:
- Drift over long playback sessions
- Position jumps when switching sources

**What we need**: Strategies for maintaining sub-millisecond accuracy over long sessions.

---

### 5. Priority System Integration

**Question**: Is our priority hierarchy correct and implementable?

**Context**:
- Timeline sync should control position (always) and BPM (if enabled)
- BPM CV should only affect BPM, not position
- Need to prevent conflicts

**Proposed Solution**:
- Clear separation: position control vs BPM control
- Timeline sync always controls position when enabled
- BPM can come from timeline (if derivation enabled) OR CV OR host OR manual

**Concerns**:
- Complexity of priority logic
- User confusion if multiple sources active

**What we need**: Validation that this priority system makes sense and is implementable.

---

### 6. Circular Dependency Prevention

**Question**: How to prevent feedback loops?

**Context**:
- Timeline source reports position → Transport updates → Broadcasts to all modules
- Timeline source receives transport update → Could cause feedback

**Proposed Solution**:
- Timeline sources report position independently (from their own internal tracking)
- Transport position updates don't feed back to source modules
- Clear one-way flow: source reports → transport follows

**What we need**: Confirmation this approach prevents circular dependencies.

---

## Implementation Phases

1. **Phase 1**: Timeline Source Registry (5-7 hours)
   - Add registry structure and thread-safe access methods
   - Add `setTransportPositionSeconds()` method
   - Add validation helpers

2. **Phase 2**: Module Reporting (3-4 hours)
   - Add reporting interface to ModuleProcessor
   - Implement in SampleLoader/VideoLoader
   - Call `updateTimelineSource()` in processBlock()

3. **Phase 3**: TempoClock Parameters (1-2 hours)
   - Add sync toggle, source selection, BPM derivation parameters

4. **Phase 4**: UI Dropdown (5-7 hours)
   - Implement dropdown in TempoClock UI
   - Handle source selection changes
   - Show source status indicators

5. **Phase 5**: Sync Logic (8-10 hours)
   - Implement priority system
   - Add position synchronization
   - Handle edge cases

6. **Phase 6**: State Persistence (2-3 hours)
   - Save/load timeline source selection in presets

7. **Phase 7**: Edge Cases & Error Handling (5-7 hours)
   - Module deletion handling
   - Source becoming inactive
   - Position validation
   - Logging and metrics

**Total Estimated Time**: 35-45 hours

---

## Risk Assessment

### High-Risk Areas

1. **Thread Safety** (Risk: 8/10)
   - Multiple threads accessing registry
   - Lock contention in audio thread
   - Need careful synchronization

2. **Module Lifecycle** (Risk: 7/10)
   - Source deleted while syncing
   - Invalid references in audio thread
   - Need validation and graceful handling

3. **Position Accuracy** (Risk: 6/10)
   - Block-based updates may cause drift
   - Sample rate conversion
   - Long playback sessions

4. **Priority System** (Risk: 6/10)
   - Complex logic with multiple sources
   - Potential conflicts
   - Need clear hierarchy

---

## What We're Looking For

### Specific Feedback Needed

1. **Thread Safety**: Is CriticalSection acceptable, or should we use lock-free? What's the JUCE best practice?

2. **Performance**: Will updating transport position every block cause performance issues? Should we use threshold?

3. **Architecture**: Is the registry pattern appropriate, or should we use observer pattern / event bus?

4. **Module Lifecycle**: Best pattern for safe module references in audio thread?

5. **Position Accuracy**: Strategies for maintaining accuracy over long sessions?

6. **JUCE-Specific**: Any JUCE-specific patterns or utilities we should use?

7. **Testing**: How to test thread safety and position accuracy effectively?

### Code Review Areas

If you have access to our codebase, please review:
- `ModularSynthProcessor.h/cpp` - Registry implementation location
- `TempoClockModuleProcessor.cpp` - Priority system integration point
- `ModuleProcessor.h` - Reporting interface location
- Thread safety patterns in existing modules

---

## Additional Context

### Existing Similar Features

- **BPM CV Priority**: Already implemented (BPM CV > Host Sync > Manual)
- **Transport Broadcasting**: Already implemented (setTimingInfo to all modules)
- **Module Lifecycle**: Already handled (addModule, removeModule)

### Constraints

- Must maintain real-time audio performance (no blocking in audio thread)
- Must work with existing module architecture (can't break existing modules)
- Must be thread-safe (JUCE threading model)
- Must persist in presets (APVTS integration)

---

## Contact & Next Steps

**Questions?** Please review the implementation plan (`PLAN/timeline_sync_feature.md`) and provide feedback on:
- Architecture decisions
- Thread safety approach
- Implementation risks
- JUCE best practices
- Testing strategies

**Timeline**: We're ready to start implementation but want expert validation before proceeding.

**Thank you** for your time and expertise!

---

**End of Consultation Request**

