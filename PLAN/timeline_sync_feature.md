# Timeline Sync Feature - Detailed Implementation Plan

**Date**: 2025-11-18  
**Feature**: Timeline Sync for TempoClockModuleProcessor  
**Status**: Planning Phase

---

## Executive Summary

Implement a timeline synchronization system that allows `TempoClockModuleProcessor` to sync the global transport to a linear timeline from `SampleLoaderModuleProcessor` or `VideoFileLoaderModule`. The system will support multiple timeline sources with a dropdown selection menu, ensuring precise synchronization of the entire patch to media playback.

---

## Feature Overview

### Core Functionality
1. **Timeline Source Registration**: Sample/Video loaders report their position and duration
2. **Source Selection**: Dropdown menu in TempoClock to select active timeline source
3. **Transport Synchronization**: Transport position follows selected timeline source
4. **BPM Derivation**: Optional BPM calculation from timeline duration
5. **State Persistence**: Save/load selected timeline source in presets

### Use Cases
- Sample-driven arrangements (drum breaks, loops)
- Video-to-audio synchronization
- Linear media as master clock
- Multi-module synchronization to single timeline

---

## Expert Review & Architecture Refinement

**Date**: 2025-11-18  
**Status**: Architecture updated based on expert feedback

### Key Changes from Expert Review

1. **Thread Safety**: Changed from Central Registry with CriticalSection → **Decentralized Atomic State**
2. **Update Frequency**: Update every block (no threshold) to prevent LFO jitter
3. **Module Lifecycle**: Use `std::shared_ptr` snapshots (already exists as `activeAudioProcessors`)
4. **Circular Dependency**: Explicit mode switching (Master vs Follower)

**Expert Verdict**: The original "Central Registry with CriticalSection" approach was **not recommended** due to priority inversion risk in audio thread. The new architecture eliminates all locks from the audio path.

---

## Architecture Design

### 1. Decentralized Atomic State (EXPERT RECOMMENDED)

**Location**: Individual modules (SampleLoader, VideoLoader)

**Approach**: Modules own their timeline state atomically. TempoClock pulls directly from modules using lock-free snapshots.

**No Central Registry Required**: Eliminates CriticalSection from audio thread entirely.

### 2. Module Interface for Timeline Reporting

**Location**: `ModuleProcessor.h`

**New Interface** (virtual methods returning atomic data):
```cpp
// In ModuleProcessor base class:
virtual bool canProvideTimeline() const { return false; }

// Thread-safe getters (using std::atomic internally in implementations)
virtual double getTimelinePositionSeconds() const { return 0.0; }
virtual double getTimelineDurationSeconds() const { return 0.0; }
virtual bool isTimelineActive() const { return false; }
```

**Why This Approach**:
- **Zero locks in audio path**
- **Zero memory allocation in audio path**
- **Modules own their state** (better encapsulation)
- **UI can scan graph** to build dropdown without affecting audio

### 2. Module Interface for Timeline Reporting

**Location**: `ModuleProcessor.h`

**New Interface** (optional virtual method):
```cpp
// In ModuleProcessor base class:
virtual bool canProvideTimeline() const { return false; }
virtual void reportTimelineState(double& positionSeconds, 
                                 double& durationSeconds, 
                                 bool& isActive) const
{
    juce::ignoreUnused(positionSeconds, durationSeconds, isActive);
}
```

**Implementation in SampleLoader/VideoLoader**:
```cpp
// SampleLoaderModuleProcessor.h
private:
    // Atomic state for timeline reporting (updated at end of processBlock)
    std::atomic<double> reportPosition { 0.0 };
    std::atomic<double> reportDuration { 0.0 };
    std::atomic<bool>   reportActive { false };

// SampleLoaderModuleProcessor.cpp
void SampleLoaderModuleProcessor::processBlock(...) {
    // ... existing processing logic ...
    
    // At END of block, publish state atomically
    if (currentSample && sampleProcessor)
    {
        double posSeconds = readPosition / sampleSampleRate;
        reportPosition.store(posSeconds, std::memory_order_relaxed);
        reportDuration.store(sampleDurationSeconds, std::memory_order_relaxed);
        reportActive.store(isPlaying && (readPosition < sampleDurationSeconds * sampleSampleRate), 
                          std::memory_order_relaxed);
    }
    else
    {
        reportActive.store(false, std::memory_order_relaxed);
    }
}

// Override interface methods
bool canProvideTimeline() const override { return hasSampleLoaded(); }
double getTimelinePositionSeconds() const override { 
    return reportPosition.load(std::memory_order_relaxed); 
}
double getTimelineDurationSeconds() const override { 
    return reportDuration.load(std::memory_order_relaxed); 
}
bool isTimelineActive() const override { 
    return reportActive.load(std::memory_order_relaxed); 
}
```

**Key Points**:
- Use `std::memory_order_relaxed` (sufficient for this use case)
- Update atomics at END of processBlock (after position calculation)
- No locks, no allocations, zero audio thread overhead

### 6. TempoClock Timeline Sync Parameters

**Location**: `TempoClockModuleProcessor`

**New Parameters**:
```cpp
static constexpr auto paramIdSyncToTimeline = "syncToTimeline";
static constexpr auto paramIdTimelineSourceId = "timelineSourceId";
static constexpr auto paramIdEnableBPMDerivation = "enableBPMDerivation";
static constexpr auto paramIdBeatsPerTimeline = "beatsPerTimeline";

// In createParameterLayout():
params.push_back(std::make_unique<juce::AudioParameterBool>(
    paramIdSyncToTimeline, "Sync to Timeline", false));
params.push_back(std::make_unique<juce::AudioParameterInt>(
    paramIdTimelineSourceId, "Timeline Source", 0, 9999, 0));
params.push_back(std::make_unique<juce::AudioParameterBool>(
    paramIdEnableBPMDerivation, "Derive BPM from Timeline", true));
params.push_back(std::make_unique<juce::AudioParameterFloat>(
    paramIdBeatsPerTimeline, "Beats per Timeline", 
    juce::NormalisableRange<float>(1.0f, 32.0f, 0.1f), 4.0f));
```

**Note**: `beatsPerTimeline` can be hidden initially, made visible later if needed.

**State Storage**:
```cpp
std::atomic<float>* syncToTimelineParam { nullptr };
std::atomic<int>* timelineSourceIdParam { nullptr };
std::atomic<float>* enableBPMDerivationParam { nullptr };
std::atomic<float>* beatsPerTimelineParam { nullptr };
```

### 5. UI Dropdown Implementation (EXPERT REFINED)

**Location**: `TempoClockModuleProcessor::drawParametersInNode()`

**Implementation Pattern** (scan graph on UI thread):
```cpp
// EXPERT: UI thread can scan graph at 60fps without affecting audio
// Build dropdown by iterating activeAudioProcessors snapshot
auto* parent = getParent();
if (!parent) return;

// Get snapshot (thread-safe, lock-free)
auto currentProcessors = parent->getActiveAudioProcessors();

// Build dropdown items
juce::StringArray items;
items.add("None");
std::vector<juce::uint32> logicalIds;
logicalIds.push_back(0); // "None" option

if (currentProcessors)
{
    for (const auto& mod : *currentProcessors)
    {
        if (mod && mod->canProvideTimeline())
        {
            double dur = mod->getTimelineDurationSeconds();
            bool active = mod->isTimelineActive();
            
            // Build display name
            juce::String displayName = mod->getName() + " #" + 
                juce::String(mod->getLogicalId());
            if (dur > 0.0)
                displayName += " (" + juce::String(dur, 1) + "s)";
            if (active)
                displayName += " [Active]";
            
            items.add(displayName);
            logicalIds.push_back(mod->getLogicalId());
        }
    }
}

// Find current selection index
int currentSelection = 0;
juce::uint32 currentId = (juce::uint32)timelineSourceIdParam->load();
for (size_t i = 0; i < logicalIds.size(); ++i)
{
    if (logicalIds[i] == currentId)
    {
        currentSelection = (int)i;
        break;
    }
}

// Display dropdown
if (ImGui::Combo("Timeline Source", &currentSelection, 
                 items.begin(), (int)items.size()))
{
    if (currentSelection > 0 && currentSelection < (int)logicalIds.size())
    {
        *timelineSourceIdParam = (int)logicalIds[currentSelection];
        onModificationEnded();
    }
}
```

**Expert Note**: UI thread scanning is safe - if it reads a "tearing" value (rare with doubles on 64-bit), it only affects one GUI frame, not audio.

### 3. Transport Position Update

**Location**: `ModularSynthProcessor`

**New Methods**:
```cpp
// Set transport position from timeline sync
// EXPERT: Update every block (no threshold) to prevent LFO jitter
void setTransportPositionSeconds(double positionSeconds)
{
    // Clamp position to valid range
    positionSeconds = juce::jmax(0.0, positionSeconds);
    
    m_transportState.songPositionSeconds = positionSeconds;
    
    // Derive beats from position if BPM is set
    if (m_transportState.bpm > 0.0)
    {
        m_transportState.songPositionBeats = 
            (positionSeconds * m_transportState.bpm) / 60.0;
    }
    
    // Update sample position for consistency
    m_samplePosition = (juce::uint64)(positionSeconds * getSampleRate());
    
    // Broadcast to all modules (for NEXT block or immediate, depending on call order)
    if (auto processors = activeAudioProcessors.load())
    {
        for (const auto& modulePtr : *processors)
            if (modulePtr)
                modulePtr->setTimingInfo(m_transportState);
    }
}

// Flag to indicate which module is the timeline master
// Prevents circular dependency (master ignores transport, drives it instead)
std::atomic<juce::uint32> timelineMasterLogicalId { 0 };  // 0 = no master

// Helper to check if a module is the timeline master
bool isModuleTimelineMaster(juce::uint32 logicalId) const {
    return timelineMasterLogicalId.load() == logicalId;
}
```

**Expert Note**: Update every block (no threshold) because transport drives phase accumulators in LFOs/Sequencers. Threshold would cause staircase/jitter.

### 4. Timeline Sync Logic in TempoClock (EXPERT REFINED)

**Location**: `TempoClockModuleProcessor::processBlock()`

**Priority System** (EXPERT CLARIFIED):
```
1. Timeline Sync (if enabled AND source is active)  [HIGHEST]
   - Sets songPositionSeconds (ALWAYS)
   - Can optionally set BPM (if derivation enabled)

2. BPM Logic (Strict Separation):
   - Start with Manual BPM
   - If Host Sync: Overwrite with Host BPM
   - If Timeline Sync AND Derivation Enabled: Overwrite with Derived BPM
   - If CV Connected: Apply CV modulation to the result

3. Sync to Host (if enabled)
   - Only affects BPM, not transport position

4. Manual BPM/Tap/Nudge                           [LOWEST]
```

**Implementation** (using lock-free snapshot):
```cpp
bool syncToTimeline = syncToTimelineParam && 
                      syncToTimelineParam->load() > 0.5f;

if (syncToTimeline)
{
    juce::uint32 targetId = (juce::uint32)timelineSourceIdParam->load();
    if (targetId > 0 && auto* parent = getParent())
    {
        // EXPERT: Use shared_ptr snapshot (already exists as activeAudioProcessors)
        // This is thread-safe - module cannot be deleted while we hold the shared_ptr
        auto currentProcessors = parent->getActiveAudioProcessors(); // Need to expose this
        bool found = false;
        
        if (currentProcessors)
        {
            // Iterate snapshot (lock-free, thread-safe)
            for (const auto& mod : *currentProcessors)
            {
                if (mod && mod->getLogicalId() == targetId)
                {
                    if (mod->canProvideTimeline() && mod->isTimelineActive())
                    {
                        // Read atomic values directly (zero overhead)
                        double pos = mod->getTimelinePositionSeconds();
                        double dur = mod->getTimelineDurationSeconds();
                        
                        // Clamp position to valid range
                        pos = juce::jlimit(0.0, dur, pos);
                        
                        // Update transport position (ALWAYS, every block)
                        parent->setTransportPositionSeconds(pos);
                        
                        // Set this module as timeline master (prevents circular dependency)
                        parent->setTimelineMaster(targetId);
                        
                        // Optional: Derive BPM (only if BPM CV not connected)
                        if (!bpmFromCV && enableBPMDerivationParam && 
                            enableBPMDerivationParam->load() > 0.5f)
                        {
                            double beatsPerTimeline = beatsPerTimelineParam ? 
                                beatsPerTimelineParam->load() : 4.0;
                            double derivedBPM = (beatsPerTimeline * 60.0) / dur;
                            derivedBPM = juce::jlimit(20.0, 300.0, derivedBPM);
                            
                            parent->setBPM(derivedBPM);
                            parent->setTempoControlledByModule(true);
                            
                            // Skip other BPM sources
                            bpm = (float)derivedBPM;
                            bpmFromCV = false;
                        }
                    }
                    found = true;
                    break;
                }
            }
        }
        
        // Module was deleted or not found - handle gracefully
        if (!found && targetId != 0)
        {
            // Reset to "None" (queue message to UI thread via AsyncUpdater)
            *timelineSourceIdParam = 0;
            parent->setTimelineMaster(0);
        }
    }
}
else
{
    // Timeline sync disabled - clear master flag
    if (auto* parent = getParent())
        parent->setTimelineMaster(0);
}

// Continue with existing BPM logic (if timeline didn't set BPM)...
```

**Key Expert Recommendations**:
- Use `activeAudioProcessors` snapshot (already exists, thread-safe)
- Read atomics directly from modules (zero locks)
- Update transport every block (no threshold)
- Set master flag to prevent circular dependency

---

## Implementation Phases

### Phase 1: Foundation (Difficulty: Easy-Medium) [UPDATED]

**Tasks**:
1. Add timeline reporting interface to `ModuleProcessor.h`:
   - `canProvideTimeline()`, `getTimelinePositionSeconds()`, `getTimelineDurationSeconds()`, `isTimelineActive()`
2. Add `setTransportPositionSeconds()` method to `ModularSynthProcessor`
3. Add `setTimelineMaster()` / `isModuleTimelineMaster()` methods:
   ```cpp
   void setTimelineMaster(juce::uint32 logicalId) { 
       timelineMasterLogicalId.store(logicalId); 
   }
   bool isModuleTimelineMaster(juce::uint32 logicalId) const { 
       return timelineMasterLogicalId.load() == logicalId; 
   }
   ```
4. Expose `getActiveAudioProcessors()` method (if not already public):
   ```cpp
   std::shared_ptr<const std::vector<std::shared_ptr<ModuleProcessor>>> getActiveAudioProcessors() const {
       return activeAudioProcessors.load();
   }
   ```

**Risks**: 
- ~~Thread safety complexity~~ (ELIMINATED - using atomics)
- Need to verify `activeAudioProcessors` is accessible

**Mitigation**:
- ~~Use CriticalSection~~ (NOT NEEDED - using atomics)
- Use existing `activeAudioProcessors` snapshot (already thread-safe)
- Verify access pattern matches existing code

**Estimated Time**: 3-4 hours (REDUCED - no registry needed)

---

### Phase 2: Module Reporting (Difficulty: Easy) [UPDATED]

**Tasks**:
1. ~~Add interface to ModuleProcessor.h~~ (Done in Phase 1)
2. Implement atomic state in `SampleLoaderModuleProcessor`:
   - Add `std::atomic<double> reportPosition`, `reportDuration`, `std::atomic<bool> reportActive`
   - Update atomics at END of `processBlock()` using `std::memory_order_relaxed`
   - Override `canProvideTimeline()`, `getTimelinePositionSeconds()`, etc.
3. Implement atomic state in `VideoFileLoaderModule`:
   - Same pattern as SampleLoader
   - Report position from `currentAudioSamplePosition` and `sourceAudioSampleRate`
   - Report duration from `totalDurationMs`
4. Add mode switching logic (Master vs Follower):
   - Check `parent->isModuleTimelineMaster(myLogicalId)` 
   - If master: ignore transport, drive from internal counter
   - If follower: use transport (existing behavior)

**Risks**: 
- Sample position calculation accuracy
- Video audio position synchronization
- Edge cases (empty samples, invalid durations)
- ~~Thread safety~~ (ELIMINATED - using atomics)

**Mitigation**:
- Validate duration > 0 before reporting
- Clamp position to [0, duration]
- Handle zero-duration gracefully
- Use `std::memory_order_relaxed` (sufficient for this use case)

**Estimated Time**: 4-5 hours (slightly increased due to mode switching)

---

### Phase 3: TempoClock Parameters (Difficulty: Easy)

**Tasks**:
1. Add `paramIdSyncToTimeline`, `paramIdTimelineSourceId` to header
2. Add `paramIdEnableBPMDerivation`, `paramIdBeatsPerTimeline` to header
3. Add parameters to `createParameterLayout()`
4. Initialize parameter pointers in constructor
5. Add to state save/load (via APVTS)

**Risks**: 
- Parameter persistence compatibility
- Parameter ID naming conflicts

**Mitigation**:
- Use unique parameter IDs
- Test preset save/load with timeline sync enabled

**Estimated Time**: 1-2 hours

---

### Phase 4: UI Dropdown (Difficulty: Medium)

**Tasks**:
1. Implement dropdown in `drawParametersInNode()`
2. Build items list from `getAvailableTimelineSources()` (cached)
3. Handle selection change
4. Display "None" option when no sources available
5. Show timeline source status (duration, active state, visual indicators)
6. Update dropdown when modules added/removed (use ChangeBroadcaster)
7. Show warning/indicator when selected source becomes inactive
8. Add BPM derivation toggle and beats-per-timeline parameter UI

**Risks**: 
- UI thread safety (getting sources from audio thread)
- Dropdown refresh timing (sources change async)
- Invalid selection after module deletion

**Mitigation**:
- Cache source list in UI thread, refresh periodically (every N frames)
- Use `juce::ChangeBroadcaster` to notify when sources change
- Invalidate cache on module add/remove (message thread callback)
- Validate selected ID still exists before using
- Reset to "None" if selected source deleted
- Show visual indicator when selected source becomes inactive
- Option to auto-select first active source if current becomes invalid

**Estimated Time**: 5-7 hours

---

### Phase 5: Sync Logic (Difficulty: Medium) [UPDATED]

**Tasks**:
1. Implement timeline sync lookup in `TempoClock::processBlock()`:
   - Get `activeAudioProcessors` snapshot
   - Iterate to find module with `timelineSourceId`
   - Read atomics directly (zero locks)
2. Integrate with existing BPM priority system:
   - Timeline sync sets position (ALWAYS)
   - BPM logic: Manual → Host → Timeline (if enabled) → CV modulation
3. Handle module deletion gracefully:
   - If module not found in snapshot, reset to "None"
   - Queue message to UI thread (AsyncUpdater) to update dropdown
4. Handle edge cases (source becomes inactive, duration changes)
5. Test priority conflicts (timeline vs BPM CV vs host sync)
6. ~~Implement position smoothing/threshold~~ (EXPERT: Update every block, no threshold)
7. Add position clamping to [0, duration]
8. Set/clear `timelineMasterLogicalId` appropriately

**Risks**: 
- Priority system conflicts (mitigated by clear hierarchy)
- ~~Position jumps~~ (acceptable - update every block prevents drift)
- BPM derivation calculations
- ~~Transport position synchronization accuracy~~ (improved - update every block)

**Mitigation**:
- Clear priority hierarchy (timeline position > BPM sources)
- Use shared_ptr snapshot (module cannot be deleted while held)
- Update transport every block (prevents LFO jitter)
- Test with various timeline durations

**Estimated Time**: 6-8 hours (REDUCED - simpler architecture)

---

### Phase 6: State Persistence (Difficulty: Easy-Medium)

**Tasks**:
1. Save timeline source ID in `getExtraStateTree()` (if using extra state)
   OR save in APVTS parameter state (already handled if using AudioParameterInt)
2. Load timeline source ID in `setExtraStateTree()` or constructor
3. Validate loaded ID exists on preset load
4. Reset to "None" if invalid

**Risks**: 
- Invalid IDs after preset load
- Module not yet created when loading state
- Version compatibility

**Mitigation**:
- Validate ID exists in `setStateInformation()`
- Defer validation until modules loaded
- Add version check for future changes

**Estimated Time**: 2-3 hours

---

### Phase 7: Edge Cases & Error Handling (Difficulty: Medium)

**Tasks**:
1. Handle module deletion while syncing (validate in processBlock)
2. Handle source becoming inactive (stop syncing gracefully, clear flag)
3. Handle duration changes mid-playback (clamp position to new duration)
4. Handle zero-duration sources (skip reporting, show warning)
5. Handle multiple TempoClocks trying to sync (prevent conflicts - existing system)
6. Handle external transport position changes (ignore when timeline controlling)
7. Handle timeline loop detection (optional transport looping)
8. Add logging for debugging (position updates, source validation, errors)
9. Add metrics for performance monitoring (lock contention, update frequency)

**Risks**: 
- Race conditions on module deletion
- Transport position corruption
- Multiple clock conflicts

**Mitigation**:
- Validate source exists before each access
- Use atomic flags for active state
- Add "last valid source" fallback
- Only one TempoClock can control transport (existing system)

**Estimated Time**: 4-6 hours

---

## Difficulty Levels

### Easy (1-3 hours each)
- Adding parameters to TempoClock
- Implementing basic timeline reporting in modules
- State persistence (if using APVTS)

### Medium (3-6 hours each)
- Timeline source registry system
- UI dropdown implementation
- Thread safety and synchronization

### Medium-Hard (6-8 hours)
- Priority system integration
- Position synchronization accuracy
- Edge case handling

### Hard (8+ hours)
- Advanced smoothing/interpolation
- Multiple timeline source blending
- Timeline-based automation

---

## Risk Assessment

### Overall Risk Rating: **LOW-MEDIUM** (4/10) [REDUCED]

**Updated**: Significantly reduced due to expert-recommended decentralized atomic architecture eliminating locks from audio thread.

### High-Risk Areas

#### 1. Thread Safety (Risk: 3/10) [REDUCED]
**Issue**: ~~Multiple threads accessing timeline sources map~~ (ELIMINATED)

**Expert Solution**: Decentralized atomic state
- Audio thread: Writes to atomics in modules (at end of processBlock)
- Audio thread: Reads atomics from modules (via shared_ptr snapshot)
- UI thread: Reads atomics from modules (via shared_ptr snapshot)
- Message thread: Module add/remove (handled by existing activeAudioProcessors)

**Mitigation**:
- ~~Use CriticalSection~~ (NOT NEEDED - using atomics)
- Use `std::atomic` with `std::memory_order_relaxed`
- Use existing `activeAudioProcessors` snapshot (lock-free)
- Modules own their state (better encapsulation)
- Zero locks in audio path

**Impact**: ~~Race conditions, crashes, data corruption~~ (ELIMINATED)

---

#### 2. Module Lifecycle (Risk: 7/10)
**Issue**: Selected timeline source deleted while syncing
- Transport position could become invalid
- UI dropdown shows deleted module
- TempoClock references invalid logical ID

**Mitigation**:
- Validate source exists before each access (use `isValidTimelineSource()`)
- Reset to "None" on module deletion (call from message thread)
- Use weak references or ID validation
- Handle gracefully in `processBlock()` (skip if invalid, reset param)
- Ensure `unregisterTimelineSource()` is called atomically with module deletion
- Add validation check at start of sync logic in `processBlock()`

**Impact**: Crashes, invalid transport state, UI errors

---

#### 3. Position Synchronization Accuracy (Risk: 4/10) [REDUCED]
**Issue**: Transport position may not match timeline exactly
- Sample-based position vs transport beats
- Rounding errors
- Block-based updates (not per-sample)

**Expert Solution**: Update every block (no threshold)
- **Reasoning**: Transport drives phase accumulators in LFOs/Sequencers
- Threshold would cause staircase/jitter in synced modules
- By forcing transport to match every block, we eliminate drift accumulation

**Mitigation**:
- Update transport position every block (EXPERT: no threshold)
- Use double precision for positions
- Validate position bounds (clamp to [0, duration])
- ~~Add position smoothing~~ (not needed - update every block prevents drift)
- ~~Consider threshold~~ (EXPERT: don't use threshold)
- Test with various sample rates
- Handle sample rate conversion correctly (store in seconds, not samples)
- Position represents start of block (acceptable for block-accurate sync)

**Impact**: Minimal drift (block-accurate, not sample-accurate - acceptable)

---

#### 4. Priority System Conflicts (Risk: 6/10)
**Issue**: Multiple BPM sources active simultaneously
- Timeline sync vs BPM CV vs Host sync
- Conflicting BPM values
- Transport position conflicts

**Mitigation**:
- Clear priority hierarchy (timeline > CV > host > manual)
- Timeline sync controls POSITION (always) and BPM (if derivation enabled)
- BPM CV only affects BPM, not position (allows combined control)
- Only one source controls BPM at a time
- UI feedback for active source (show which is controlling)
- Add flag to prevent external transport position changes when timeline sync active

**Impact**: Unpredictable behavior, user confusion

---

#### 5. BPM Derivation (Risk: 5/10)
**Issue**: Calculating BPM from timeline duration
- Assumes fixed beats-per-timeline
- May not match user intent
- Could produce extreme BPM values

**Mitigation**:
- Make beats-per-timeline configurable (add parameter, even if hidden initially)
- Clamp derived BPM to valid range (20-300)
- Allow disabling BPM derivation entirely (separate parameter)
- User can override if needed (manual BPM still available)
- Consider deriving from timeline source metadata if available
- Add option to use existing BPM parameter as fallback

**Impact**: Unexpected tempo changes, user confusion

---

### Medium-Risk Areas

#### 6. UI Refresh Timing (Risk: 4/10)
**Issue**: Dropdown doesn't update when modules added/removed
- Sources list cached
- Stale display

**Mitigation**:
- Refresh on preset load
- Periodic refresh in UI thread
- Invalidate cache on module add/remove

**Impact**: UI confusion, usability issues

---

#### 7. State Persistence (Risk: 4/10)
**Issue**: Invalid timeline source ID after preset load
- Module not yet created
- Different logical IDs in new session

**Mitigation**:
- Validate on preset load completion (after all modules created)
- Reset to "None" if invalid
- Use module type + index instead of logical ID (future enhancement)
- Consider more stable identifier (module name + type)
- Defer validation until modules fully loaded
- Add version check for future changes

**Impact**: Sync not working after preset load

---

### Low-Risk Areas

#### 8. Zero-Duration Sources (Risk: 2/10)
**Issue**: Empty samples or invalid durations

**Mitigation**: Validate duration > 0 before reporting

---

#### 9. Multiple TempoClocks (Risk: 3/10)
**Issue**: Multiple clocks trying to control transport

**Mitigation**: Existing system handles this (only one controls)

---

## Confidence Rating

### Overall Confidence: **HIGH** (8/10)

**Note**: Confidence remains high due to clear architecture and phased approach, 
but implementation complexity is higher than initially estimated.

### Strong Points

1. **Clear Architecture**: Separation of concerns (registry, reporting, sync)
2. **Existing Patterns**: Follows current priority system (BPM CV > Host sync)
3. **Thread Safety Tools**: JUCE provides `CriticalSection`, atomic types
4. **Incremental Implementation**: Phases can be tested independently
5. **Well-Defined Interface**: Module reporting interface is simple
6. **State Management**: APVTS handles parameter persistence

### Weak Points

1. **Complexity Growth**: Adds another priority layer to BPM system
2. **Thread Safety**: Multiple thread access requires careful design
3. **Position Accuracy**: Block-based updates may cause minor drift
4. **BPM Derivation**: Assumes fixed relationship (may need user control)
5. **Testing**: Requires testing with multiple modules, rapid add/remove
6. **Performance**: Map lookups in audio thread (mitigated with caching)

### Areas Requiring Extra Attention

1. **Thread Safety Testing**: Stress test with many modules, rapid changes
2. **Position Accuracy Testing**: Long playback sessions, various durations
3. **Edge Case Testing**: Module deletion, zero durations, invalid states
4. **Priority Testing**: All combinations of sync modes
5. **State Persistence Testing**: Preset save/load with timeline sync enabled

---

## Potential Problems & Solutions

### Problem 1: Transport Position Jumps
**Scenario**: Switching timeline sources causes position jump

**Solution**: 
- Smooth transition (interpolate over N blocks)
- OR reset to 0 when switching sources
- User preference option

---

### Problem 2: Timeline Source Stops Playing
**Scenario**: Sample finishes, timeline sync still active

**Solution**: 
- Check `isActive` flag before using position
- Reset to "None" if source becomes inactive
- OR loop timeline (if source loops)

---

### Problem 3: Dropdown Performance
**Scenario**: Many timeline sources, dropdown refresh slow

**Solution**: 
- Cache source list, refresh every N frames
- Limit dropdown to first N sources (pagination)
- Filter by active state only

---

### Problem 4: BPM Derivation Wrong
**Scenario**: Calculated BPM doesn't match timeline

**Solution**: 
- Make beats-per-timeline user-configurable
- Allow disabling BPM derivation
- Use existing BPM parameter as fallback

---

### Problem 5: Module Type Mismatch
**Scenario**: Preset loads, module types don't match

**Solution**: 
- Validate module type matches expected type
- Use type + index instead of just logical ID (future)
- Reset to "None" if mismatch

---

### Problem 6: Audio Thread Blocking
**Scenario**: CriticalSection lock too long in audio thread

**Solution**: 
- Minimize lock duration (copy data, unlock, use copy)
- Use atomic values where possible
- Cache frequently accessed data

---

### Problem 7: Position Out of Bounds
**Scenario**: Timeline position exceeds duration

**Solution**: 
- Clamp position to [0, duration]
- Handle loop boundaries
- Validate in both reporting and consuming modules

---

### Problem 8: Multiple Timeline Sources Active
**Scenario**: Multiple SampleLoaders playing simultaneously

**Solution**: 
- Only selected source controls transport
- User must choose one
- UI shows which is active

---

### Problem 9: Sample Rate Mismatch
**Scenario**: Timeline source sample rate differs from transport

**Solution**: 
- Convert positions using sample rates
- Store positions in seconds (time, not samples)
- Handle sample rate changes gracefully

---

### Problem 10: UI Thread Blocking Audio
**Scenario**: Getting sources list blocks audio thread

**Solution**: 
- UI queries never block audio thread
- Use separate cached data for UI
- Refresh cache on message thread
- Use `juce::ChangeBroadcaster` for efficient notifications

---

### Problem 11: Transport Position Conflicts
**Scenario**: External transport position changes conflict with timeline sync

**Solution**: 
- Add `isTransportControlledByTimeline` flag
- Ignore external position changes when flag is set
- Clear flag when timeline sync disabled
- Allow manual override with user confirmation

---

### Problem 12: Position Update Frequency
**Scenario**: Updating transport every block may cause performance issues or position jumps

**Solution**: 
- Add threshold: only update if position changed >1ms
- Implement position smoothing/interpolation
- Cache last position to detect significant changes
- Profile update frequency impact

---

### Problem 13: Circular Dependency Risk
**Scenario**: Timeline source receives transport update, causing feedback loop

**Solution**: 
- Timeline sources report position independently (not from transport)
- Transport position updates don't feed back to source modules
- Source modules use their own internal position tracking
- Clear separation: source reports → transport follows (one-way)

---

### Problem 14: Timeline Loop Detection
**Scenario**: Source loops, but transport doesn't know when to loop

**Solution**: 
- Detect when source position resets to 0 (loop detected)
- Option to loop transport position when source loops
- Or maintain absolute position (don't loop transport)
- User preference for loop behavior

---

### Problem 15: BPM Derivation Parameter Missing
**Scenario**: Hardcoded beats-per-timeline doesn't match user's media

**Solution**: 
- Add hidden parameter for beats-per-timeline (default 4.0)
- Or add visible parameter in UI
- Allow disabling BPM derivation entirely
- Store in preset state

---

## Testing Strategy

### Unit Tests
- Timeline source registry add/remove
- Position calculation accuracy
- BPM derivation formulas
- Priority system logic

### Integration Tests
- SampleLoader reporting → TempoClock sync
- VideoLoader reporting → TempoClock sync
- Multiple sources, selection switching
- Module deletion during sync

### Stress Tests
- Rapid module add/remove
- Many timeline sources (10+)
- Long playback sessions (hours)
- High sample rates (96kHz+)

### Edge Case Tests
- Zero-duration sources
- Invalid logical IDs
- Concurrent access from multiple threads
- Preset save/load with sync enabled
- Sample rate changes

---

## Future Enhancements

### Phase 8 (Optional, Advanced)
1. **Timeline-based Automation**: Record parameter changes tied to timeline
2. **Timeline Markers**: Trigger events at specific positions
3. **Multiple Source Blending**: Smoothly blend between sources
4. **Timeline Scrubbing**: Seek transport by scrubbing timeline
5. **BPM Auto-detection**: Analyze timeline to detect BPM automatically
6. **Timeline Playlists**: Sequence multiple timelines
7. **Speed Variations**: Vary playback speed while maintaining sync

---

## Estimated Total Time

**Minimum (Core Features)**: 20-25 hours [REDUCED]
- Phases 1-5, basic edge case handling

**Realistic (Complete Feature)**: 28-35 hours [REDUCED]
- All phases, comprehensive testing, edge cases
- Simpler architecture reduces implementation time

**Maximum (With Polish)**: 40-50 hours [REDUCED]
- All phases, advanced features, extensive testing, documentation, optimization

---

## Dependencies

- Existing `TransportState` structure
- Existing priority system (BPM CV > Host sync)
- `ModularSynthProcessor` module management
- APVTS parameter system
- UI rendering system (ImGui)

**No External Dependencies**: All required systems already exist

---

## Success Criteria

### Must Have
✅ Timeline sources report position/duration correctly  
✅ TempoClock can select timeline source via dropdown  
✅ Transport position follows selected timeline  
✅ Priority system works (timeline > CV > host > manual)  
✅ State persists in presets  
✅ Edge cases handled gracefully  

### Nice to Have
✅ BPM derivation from timeline  
✅ Position smoothing on source switch  
✅ UI refresh on module add/remove  
✅ Performance optimization (caching)  

### Future Enhancements
⏳ Timeline markers/events  
⏳ Multiple source blending  
⏳ Timeline scrubbing  
⏳ BPM auto-detection  

---

## Critical Design Decisions (EXPERT REVIEWED)

### 1. Thread Safety Strategy [UPDATED]
**Decision**: **Decentralized Atomic State** (expert recommended)
**Rationale**: Eliminates all locks from audio thread, zero overhead, better encapsulation.
**Previous Approach**: Central registry with CriticalSection (rejected - priority inversion risk).

### 2. Position Update Frequency [UPDATED]
**Decision**: **Update every block (no threshold)** (expert recommended)
**Rationale**: Transport drives phase accumulators. Threshold causes staircase/jitter in LFOs.
**Previous Approach**: Threshold-based updates (rejected - causes jitter).

### 3. BPM Derivation
**Decision**: Make configurable (beats-per-timeline parameter), allow disabling.
**Rationale**: Different media has different beat structures, user should control.
**Alternative**: Auto-detect beats (future enhancement).

### 4. Priority System
**Decision**: Timeline sync controls position (always) and BPM (if enabled).
**Rationale**: Clear separation - position follows timeline, BPM can be derived or from CV.
**Alternative**: Timeline sync overrides all (simpler but less flexible).

### 5. Circular Dependency Prevention [UPDATED]
**Decision**: **Explicit mode switching** (Master vs Follower) (expert recommended)
**Rationale**: Timeline master ignores transport, drives it instead. Prevents feedback loops.
**Implementation**: `timelineMasterLogicalId` flag, modules check `isModuleTimelineMaster()`.
**Previous Approach**: Transport control flag (replaced with explicit mode).

## Expert Review Summary

**Date**: 2025-11-18  
**Status**: Architecture refined based on expert feedback

### Key Expert Recommendations Implemented

1. ✅ **Decentralized Atomic State**: Eliminated central registry, modules own state atomically
2. ✅ **Zero Locks in Audio Thread**: Using `std::atomic` with `memory_order_relaxed`
3. ✅ **Update Every Block**: No threshold to prevent LFO jitter
4. ✅ **Shared Pointer Snapshots**: Use existing `activeAudioProcessors` for safe module access
5. ✅ **Explicit Mode Switching**: Master vs Follower prevents circular dependencies
6. ✅ **Simplified Architecture**: Reduced complexity, lower risk, faster implementation

### Risk Reduction

- **Thread Safety Risk**: 8/10 → 3/10 (eliminated locks)
- **Overall Risk**: 6/10 → 4/10 (simpler architecture)
- **Implementation Time**: 35-45h → 28-35h (reduced complexity)

## Conclusion

This feature is **feasible** with **medium complexity** and **low-medium risks**. The expert-recommended architecture eliminates the primary concerns (thread safety, priority inversion) and significantly simplifies implementation. The decentralized atomic approach follows JUCE best practices and ensures zero audio thread overhead.

**Recommendation**: **Proceed with implementation** using the expert-recommended architecture. Start with Phase 1 and test each phase before moving to the next. The simplified architecture reduces risk and implementation time.

---

## Implementation Checklist

- [ ] Phase 1: Timeline Source Registry
- [ ] Phase 2: Module Reporting Interface
- [ ] Phase 3: TempoClock Parameters
- [ ] Phase 4: UI Dropdown
- [ ] Phase 5: Sync Logic
- [ ] Phase 6: State Persistence
- [ ] Phase 7: Edge Cases & Error Handling
- [ ] Testing: Unit tests
- [ ] Testing: Integration tests
- [ ] Testing: Stress tests
- [ ] Testing: Edge cases
- [ ] Documentation: Code comments
- [ ] Documentation: User guide
- [ ] Performance: Profiling
- [ ] Performance: Optimization

---

**End of Plan**

