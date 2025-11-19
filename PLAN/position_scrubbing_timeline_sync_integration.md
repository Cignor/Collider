# Position Scrubbing + Timeline Sync Integration Plan

**Date**: 2025-01-XX  
**Feature**: Integrate position scrubbing with timeline sync system  
**Status**: Planning Phase

---

## Problem Analysis

### Current Issues

1. **Stale Timeline Reporting**
   - Timeline reporting uses `readPosition` variable (line 423)
   - When scrubbing occurs, `sampleProcessor->setCurrentPosition()` updates the playhead
   - But `readPosition` in SampleLoaderModuleProcessor is NOT updated to match
   - Result: TempoClock reads stale position data

2. **Timeline Reporting Timing**
   - Timeline reporting happens AFTER position scrubbing logic
   - But it uses `readPosition` which is not updated during scrubbing
   - Should use actual playhead position from `sampleProcessor->getCurrentPosition()`

3. **Processing Order Dependency**
   - If TempoClock runs before SampleLoader in the graph:
     - TempoClock reads old position
     - SampleLoader scrubs to new position
     - Next block: TempoClock reads new position (one block delay)
   - If SampleLoader runs before TempoClock:
     - SampleLoader scrubs to new position
     - Timeline reporting updates immediately
     - TempoClock reads new position same block (good!)

4. **Timeline Master + Scrubbing Conflict**
   - When SampleLoader is timeline master and user scrubs:
     - Playhead jumps to new position
     - Timeline reporting should reflect this immediately
     - TempoClock should read the new position and update transport
     - But there might be a feedback loop if transport affects SampleLoader

5. **Range-Aware Scrubbing**
   - Position scrubbing now respects range start/end
   - But timeline reporting should also account for range
   - Position reported to TempoClock should be relative to the full sample, not just the range

---

## Proposed Solutions

### 1. Fix Timeline Reporting to Use Actual Playhead Position

**Problem**: Timeline reporting uses stale `readPosition` variable

**Solution**: 
- Update timeline reporting to use `sampleProcessor->getCurrentPosition()` instead of `readPosition`
- This ensures scrubbing is immediately reflected in timeline reporting
- Update `readPosition` when scrubbing for consistency (or remove it if not needed elsewhere)

**Code Changes**:
```cpp
// In timeline reporting section (around line 420):
// OLD: double currentPosSeconds = readPosition / sampleSampleRate;
// NEW: Use actual playhead position
double currentSamplePos = sampleProcessor->getCurrentPosition();
double currentPosSeconds = currentSamplePos / sampleSampleRate;
```

### 2. Update readPosition During Scrubbing

**Problem**: `readPosition` is not updated when scrubbing occurs

**Solution**:
- When scrubbing, update `readPosition` to match the new playhead position
- This keeps the variable in sync (if it's used elsewhere)

**Code Changes**:
```cpp
// In scrubbing section (around line 373):
// After setCurrentPosition:
readPosition = newSamplePos; // Keep in sync
```

### 3. Handle Timeline Master + Scrubbing

**Problem**: When SampleLoader is timeline master and scrubbing occurs, transport should update immediately

**Solution Options**:

**Option A: Immediate Transport Update (Recommended)**
- When scrubbing occurs AND SampleLoader is timeline master:
  - Immediately update transport position via `parent->setTransportPositionSeconds()`
  - This ensures transport is in sync immediately, not waiting for TempoClock to read
  - TempoClock will still read the position, but it will already be correct

**Option B: Let TempoClock Handle It**
- Don't update transport in SampleLoader
- Let TempoClock read the new position and update transport
- Risk: One block delay if TempoClock runs first

**Recommendation**: Option A - immediate update when timeline master scrubs

**Code Changes**:
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

### 4. Range-Aware Timeline Reporting

**Problem**: Timeline reporting should report position relative to full sample, not just range

**Current Behavior**: 
- Position scrubbing is clamped to range start/end
- But timeline reporting uses `readPosition` which might not account for range

**Solution**:
- Timeline reporting should report the actual playhead position in the full sample
- The range is just a playback constraint, not a position constraint for timeline sync
- OR: Report position relative to range (0.0 = rangeStart, 1.0 = rangeEnd) and let TempoClock handle it

**Recommendation**: Report absolute position in full sample (current behavior is correct, just needs to use actual playhead)

### 5. Prevent Feedback Loops

**Problem**: If transport affects SampleLoader, scrubbing could create feedback

**Current Architecture**:
- SampleLoader in "Master" mode ignores transport (per expert RFC)
- This should prevent feedback loops
- But we should verify this is working correctly

**Solution**:
- Ensure SampleLoader ignores `setTimingInfo` when it's the timeline master
- Verify that scrubbing doesn't trigger transport updates that feed back to SampleLoader

---

## Implementation Plan

### Phase 1: Fix Timeline Reporting (Critical)
1. Update timeline reporting to use `sampleProcessor->getCurrentPosition()` instead of `readPosition`
2. Update `readPosition` during scrubbing to keep it in sync
3. Test: Scrubbing should immediately reflect in timeline reporting

### Phase 2: Immediate Transport Update (Important)
1. When scrubbing occurs and SampleLoader is timeline master:
   - Calculate position in seconds
   - Call `parent->setTransportPositionSeconds()` immediately
2. Test: Transport should update immediately when scrubbing timeline master

### Phase 3: Verify No Feedback Loops (Safety)
1. Verify SampleLoader ignores transport when timeline master
2. Test: Scrubbing should not cause transport to feed back to SampleLoader
3. Add logging if needed to debug

### Phase 4: Range Handling (Enhancement)
1. Verify timeline reporting accounts for range correctly
2. Document expected behavior (absolute vs relative position)

---

## Code Changes Summary

### SampleLoaderModuleProcessor.cpp

1. **Timeline Reporting Section** (around line 420):
   - Change from `readPosition` to `sampleProcessor->getCurrentPosition()`
   - Ensure position is in seconds, not samples

2. **Scrubbing Section** (around line 340):
   - Update `readPosition` when scrubbing occurs
   - If timeline master and was playing, update transport immediately

3. **Normal Playback Section** (around line 381):
   - Update `readPosition` to match actual playhead position
   - This keeps the variable in sync for any other uses

---

## Testing Checklist

- [ ] Scrubbing updates timeline reporting immediately
- [ ] TempoClock reads correct position after scrubbing
- [ ] Transport updates immediately when scrubbing timeline master
- [ ] No feedback loops when scrubbing
- [ ] Range constraints work correctly with timeline sync
- [ ] CV input scrubbing works with timeline sync
- [ ] Manual slider scrubbing works with timeline sync
- [ ] Playback continues correctly after scrubbing
- [ ] Global reset still works when looping

---

## Questions to Resolve

1. **Position Units**: Should timeline reporting use absolute sample position or position relative to range?
   - **Answer**: Absolute position (full sample) - range is just a playback constraint

2. **Transport Update Timing**: Should SampleLoader update transport immediately when scrubbing, or let TempoClock handle it?
   - **Answer**: Immediate update when timeline master (Option A)

3. **Processing Order**: Should we ensure SampleLoader runs before TempoClock when timeline synced?
   - **Answer**: Not necessary if we update transport immediately in SampleLoader

4. **Range in Timeline Reporting**: Should reported position account for range start/end?
   - **Answer**: No - report absolute position, range is for playback only

---

## Risk Assessment

**Low Risk**:
- Fixing timeline reporting to use actual playhead position
- Updating `readPosition` during scrubbing

**Medium Risk**:
- Immediate transport update when scrubbing timeline master
- Need to verify no feedback loops

**High Risk**:
- None identified

---

## Next Steps

1. Review this plan
2. Implement Phase 1 (Critical fixes)
3. Test timeline sync with scrubbing
4. Implement Phase 2 if needed
5. Verify no regressions

