# Position Scrubbing + Timeline Sync Integration - Expert Review Request

**Date**: 2025-01-XX  
**Context**: JUCE Modular Synthesizer - Timeline Sync Feature Integration  
**Request**: Expert review of position scrubbing integration with timeline sync system

---

## Executive Summary

We have implemented a **position scrubbing feature** for `SampleLoaderModuleProcessor` that allows users to manually move the playhead via UI slider or CV input. This feature works well in isolation, but we've identified potential conflicts with the existing **timeline sync system** where `TempoClockModuleProcessor` syncs to `SampleLoaderModuleProcessor` as a timeline source.

**Question**: How should position scrubbing interact with timeline sync when SampleLoader is the timeline master?

---

## Current Architecture

### Timeline Sync System (Already Implemented)

1. **Decentralized Atomic State** (Expert-Recommended Architecture)
   - Modules (SampleLoader, VideoLoader) maintain atomic state: `reportPosition`, `reportDuration`, `reportActive`
   - TempoClock pulls this data directly from modules using `std::shared_ptr` snapshots
   - Zero locks in audio path

2. **Timeline Master Mechanism**
   - When TempoClock syncs to a module, it calls `parent->setTimelineMaster(targetId)`
   - Timeline master modules should ignore incoming `setTimingInfo` calls (prevents circular dependency)
   - TempoClock reads position every block and calls `parent->setTransportPositionSeconds(pos)`

3. **Data Flow**:
   ```
   SampleLoader (Master)
     → Updates atomic reportPosition every block
     → TempoClock reads reportPosition
     → TempoClock calls parent->setTransportPositionSeconds()
     → Transport broadcasts to all modules (except master)
   ```

### Position Scrubbing Feature (Newly Implemented)

1. **Bi-Directional Control**:
   - **Manual Slider**: User drags slider → playhead moves → slider follows playback
   - **CV Input**: CV modulates position → playhead moves → playback continues

2. **Implementation**:
   - Detects scrubbing via parameter change detection or CV input
   - Calls `sampleProcessor->setCurrentPosition(newSamplePos)` to move playhead
   - Preserves playback state (if playing, continues playing; if stopped, stays stopped)
   - Respects range start/end boundaries

3. **Key Code** (SampleLoaderModuleProcessor.cpp):
   ```cpp
   if (isScrubbing)
   {
       double newSamplePos = targetPosNorm * totalSamples;
       bool wasPlaying = sampleProcessor->isPlaying;
       sampleProcessor->setCurrentPosition(newSamplePos);
       if (wasPlaying) sampleProcessor->isPlaying = true;
       // ... update tracking vars
   }
   ```

---

## Identified Conflicts

### Issue 1: Stale Timeline Reporting

**Problem**:
- Timeline reporting uses `readPosition` variable (line 423)
- When scrubbing occurs, `sampleProcessor->setCurrentPosition()` updates the playhead
- But `readPosition` in SampleLoaderModuleProcessor is NOT updated to match
- Result: TempoClock reads stale position data

**Current Code**:
```cpp
// Timeline reporting (line 420-423)
double currentPosSeconds = readPosition / sampleSampleRate;  // ❌ Uses stale readPosition
reportPosition.store(currentPosSeconds, std::memory_order_relaxed);
```

**Expected Behavior**:
- Timeline reporting should reflect actual playhead position immediately after scrubbing
- TempoClock should read the new position in the same block (or next block at worst)

### Issue 2: Timeline Master + Scrubbing Timing

**Problem**:
- When SampleLoader is timeline master and user scrubs:
  - Playhead jumps to new position
  - Timeline reporting updates (but currently uses stale data - Issue 1)
  - TempoClock reads position and updates transport
  - But there's a potential one-block delay depending on processing order

**Question**: Should SampleLoader update transport immediately when scrubbing as timeline master?

**Current Behavior**:
- SampleLoader scrubs → updates playhead
- Timeline reporting updates (stale data)
- TempoClock reads → updates transport (one block delay possible)

**Proposed Behavior**:
- SampleLoader scrubs → updates playhead
- If timeline master: Immediately update transport via `parent->setTransportPositionSeconds()`
- Timeline reporting updates with correct position
- TempoClock reads (already correct, or will be next block)

### Issue 3: Processing Order Dependency

**Problem**:
- JUCE's AudioProcessorGraph determines processing order (topological sort)
- If TempoClock runs before SampleLoader:
  - TempoClock reads old position
  - SampleLoader scrubs to new position
  - Next block: TempoClock reads new position (one block delay)
- If SampleLoader runs before TempoClock:
  - SampleLoader scrubs to new position
  - Timeline reporting updates
  - TempoClock reads new position same block (good!)

**Question**: Should we ensure SampleLoader runs before TempoClock when timeline synced? Or handle it differently?

### Issue 4: Range-Aware Scrubbing vs Timeline Reporting

**Problem**:
- Position scrubbing now respects range start/end (clamps to range boundaries)
- But timeline reporting should report absolute position (full sample)
- Range is a playback constraint, not a timeline position constraint

**Current Behavior**:
- Scrubbing: Position clamped to `[rangeStart, rangeEnd]`
- Timeline reporting: Uses `readPosition` (which might not account for range correctly)

**Question**: Should timeline reporting use:
- **Option A**: Absolute position in full sample (0.0 = start of sample, 1.0 = end of sample)
- **Option B**: Position relative to range (0.0 = rangeStart, 1.0 = rangeEnd)

**Our Assumption**: Option A - absolute position. Range is just a playback constraint.

---

## Proposed Solutions

### Solution 1: Fix Timeline Reporting (CRITICAL)

**Change**: Use actual playhead position instead of stale `readPosition`

```cpp
// OLD (line 423):
double currentPosSeconds = readPosition / sampleSampleRate;

// NEW:
double currentSamplePos = sampleProcessor->getCurrentPosition();
double currentPosSeconds = currentSamplePos / sampleSampleRate;
```

**Also**: Update `readPosition` during scrubbing to keep it in sync:
```cpp
// In scrubbing section:
readPosition = newSamplePos; // Keep in sync
```

### Solution 2: Immediate Transport Update (IMPORTANT)

**Change**: When scrubbing occurs AND SampleLoader is timeline master, update transport immediately

```cpp
// In scrubbing section, after setCurrentPosition:
if (wasPlaying)
{
    sampleProcessor->isPlaying = true;
    
    // If we're the timeline master, update transport immediately
    auto* parentSynth = getParent();
    if (parentSynth && parentSynth->isModuleTimelineMaster(getLogicalId()))
    {
        double newPosSeconds = newSamplePos / sampleSampleRate;
        parentSynth->setTransportPositionSeconds(newPosSeconds);
    }
}
```

**Rationale**:
- Ensures transport updates immediately, not waiting for TempoClock
- Eliminates processing order dependency
- Keeps timeline sync responsive

### Solution 3: Verify No Feedback Loops (SAFETY)

**Check**: Ensure SampleLoader ignores transport when timeline master

**Current Architecture** (per expert RFC):
- SampleLoader should check `parent->isModuleTimelineMaster(myLogicalId)`
- If master, ignore `setTimingInfo` calls
- This should prevent feedback loops

**Action**: Verify this is working correctly, add logging if needed

---

## Questions for Expert

### Q1: Timeline Reporting Position Source
**Question**: Should timeline reporting use `sampleProcessor->getCurrentPosition()` (actual playhead) or maintain a separate `readPosition` variable?

**Our Assumption**: Use actual playhead position for accuracy.

### Q2: Immediate Transport Update
**Question**: When SampleLoader scrubs and is timeline master, should it:
- **Option A**: Immediately update transport via `parent->setTransportPositionSeconds()` (our proposal)
- **Option B**: Let TempoClock read the position and update transport (current behavior, but has delay)

**Our Preference**: Option A - immediate update for better responsiveness.

### Q3: Processing Order
**Question**: Should we ensure SampleLoader runs before TempoClock when timeline synced? Or is the immediate transport update sufficient?

**Our Assumption**: Immediate transport update makes processing order less critical.

### Q4: Range Handling in Timeline Reporting
**Question**: Should timeline reporting account for range start/end, or always report absolute position in full sample?

**Our Assumption**: Absolute position (full sample). Range is just a playback constraint.

### Q5: Feedback Loop Prevention
**Question**: Is the current "timeline master ignores transport" mechanism sufficient to prevent feedback loops when scrubbing?

**Our Assumption**: Yes, but we should verify.

### Q6: Scrubbing During Timeline Sync
**Question**: Should scrubbing temporarily disable timeline sync, or should they work together seamlessly?

**Our Preference**: Work together seamlessly - scrubbing should update timeline immediately.

---

## Code Context

### Current Timeline Reporting (SampleLoaderModuleProcessor.cpp, ~line 420)
```cpp
// === TIMELINE REPORTING (at START of block, before processing) ===
if (currentSample && sampleProcessor && sampleSampleRate > 0)
{
    // Calculate position at START of block (in seconds)
    double currentPosSeconds = readPosition / sampleSampleRate;  // ❌ Uses stale readPosition
    double durationSeconds = sampleDurationSeconds;
    
    // ... validation ...
    
    reportPosition.store(currentPosSeconds, std::memory_order_relaxed);
    reportDuration.store(durationSeconds, std::memory_order_relaxed);
    reportActive.store(isActive, std::memory_order_relaxed);
}
```

### Current Scrubbing Logic (SampleLoaderModuleProcessor.cpp, ~line 340)
```cpp
if (isScrubbing)
{
    double newSamplePos = targetPosNorm * totalSamples;
    bool wasPlaying = sampleProcessor->isPlaying;
    
    sampleProcessor->setCurrentPosition(newSamplePos);  // ✅ Updates playhead
    
    if (wasPlaying)
    {
        sampleProcessor->isPlaying = true;
        // ❌ Missing: Update readPosition
        // ❌ Missing: Update transport if timeline master
    }
    
    lastReadPosition = newSamplePos;  // ✅ Updates tracking
    lastUiPosition = targetPosNorm;
    positionParam->store(targetPosNorm);
}
```

### TempoClock Timeline Sync (TempoClockModuleProcessor.cpp, ~line 210)
```cpp
if (syncToTimeline && wasActive)
{
    double pos = mod->getTimelinePositionSeconds();  // Reads from SampleLoader
    double dur = mod->getTimelineDurationSeconds();
    
    // Update transport position (ALWAYS, every block)
    parent->setTransportPositionSeconds(pos);  // ✅ Updates transport
    
    // Set this module as timeline master
    parent->setTimelineMaster(targetId);
}
```

---

## Expected Behavior After Fix

1. **User scrubs position slider**:
   - Playhead moves immediately
   - Timeline reporting updates immediately (uses actual playhead)
   - If timeline master: Transport updates immediately
   - TempoClock reads correct position (same or next block)
   - All modules sync to new position

2. **CV input scrubs position**:
   - Same as manual scrubbing
   - Smooth movement without freezing
   - Timeline sync remains accurate

3. **Normal playback**:
   - Playhead advances naturally
   - Timeline reporting reflects actual position
   - TempoClock reads and updates transport
   - Everything stays in sync

---

## Risk Assessment

**Low Risk**:
- Fixing timeline reporting to use actual playhead position
- Updating `readPosition` during scrubbing

**Medium Risk**:
- Immediate transport update when scrubbing timeline master
- Need to verify no feedback loops
- Need to verify processing order doesn't cause issues

**High Risk**:
- None identified

---

## Implementation Priority

1. **Phase 1 (Critical)**: Fix timeline reporting to use actual playhead position
2. **Phase 2 (Important)**: Immediate transport update when scrubbing timeline master
3. **Phase 3 (Safety)**: Verify no feedback loops, add logging if needed

---

## Request for Expert Input

We would appreciate expert guidance on:

1. **Architecture**: Is our proposed solution (immediate transport update) the right approach?
2. **Timing**: Should we worry about processing order, or is immediate update sufficient?
3. **Feedback Prevention**: Is the current "master ignores transport" mechanism sufficient?
4. **Best Practices**: Are there any JUCE-specific considerations we should be aware of?

Thank you for your review!

