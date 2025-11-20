# Timeline Sync Scrubbing Fix Attempts - Full Report

## Problem Statement
When TempoClock has timeline sync enabled but no source selected, SampleLoaderModuleProcessor (with "Sync to Transport" enabled) constantly scrubs/seeks, causing audio glitches.

## Attempted Fixes

### Attempt 1: Prevent TempoClock from Updating Transport Position
**Location**: `TempoClockModuleProcessor.cpp` (lines 344-349)

**What was done**:
- Removed the code that updates transport position when timeline sync is enabled but no source is selected
- Added comment explaining that transport position should not be updated in this case
- Set `timelineMasterLogicalId` to `UINT32_MAX` to signal that TempoClock is controlling transport

**Why it failed**:
- Transport position might still be at its last known value (not 0)
- If transport was playing before enabling timeline sync, it might have a non-zero value
- SampleLoader still tries to sync to this frozen position, causing mismatch with actual playback

### Attempt 2: Early Return in SampleLoader's setTimingInfo
**Location**: `SampleLoaderModuleProcessor.cpp` (lines 759-771)

**What was done**:
- Added check for `timelineMasterLogicalId == UINT32_MAX`
- If true, return early from `setTimingInfo` before processing transport sync
- This should prevent any transport syncing when timeline sync is enabled but no source

**Why it might have failed**:
- The check happens AFTER `ModuleProcessor::setTimingInfo(state)` is called (line 753)
- If parent class updates something that affects playback, the issue persists
- Timing issue: The check might not catch all cases if transport state changes between calls

### Attempt 3: Transport Stuck Detection
**Location**: `SampleLoaderModuleProcessor.cpp` (lines 798-814)

**What was done**:
- Added logic to detect if transport position appears "stuck" (< 0.1 seconds)
- Checks if playhead is past beginning of sample while transport is stuck
- Prevents syncing if transport appears frozen

**Why it might have failed**:
- The threshold (0.1 seconds) might be too restrictive
- If transport was at 5 seconds before enabling timeline sync, it's not detected as "stuck"
- The check uses `lastSyncedTransportPosition` which might not be initialized correctly
- Race condition: Transport position might change slightly between checks

### Attempt 4: Range Clamping in Transport Sync
**Location**: `SampleLoaderModuleProcessor.cpp` (lines 818-843)

**What was done**:
- Added range start/end clamping when syncing to transport
- Ensures synced position respects the range bounds

**Status**: This fix worked for the range end issue, but doesn't solve the scrubbing problem

### Attempt 5: Prevent False Scrubbing Trigger
**Location**: `SampleLoaderModuleProcessor.cpp` (lines 344-369)

**What was done**:
- Modified "Manual Slider Movement" detection to ignore changes when transport sync is enabled
- Updates `lastUiPosition` when transport sync changes position parameter, preventing false triggers

**Why it might have failed**:
- This only prevents scrubbing from the `processBlock` path
- If `setTimingInfo` is still calling `setCurrentPosition`, scrubbing still occurs
- The check might not work correctly if timeline sync state changes between blocks

## Root Cause Analysis

### The Real Problem
The fundamental issue is a **feedback loop** between transport position and SampleLoader position:

1. SampleLoader is playing and advancing its playhead naturally
2. Transport position is frozen (when timeline sync enabled but no source)
3. SampleLoader's `setTimingInfo` is called with frozen transport position
4. There's a mismatch between SampleLoader's playhead position and transport position
5. SampleLoader tries to sync to transport, causing scrubbing
6. This creates constant position jumps

### Why All Fixes Failed
1. **Timing/Threading Issues**: `setTimingInfo` might be called from a different thread/context than `processBlock`, causing race conditions
2. **State Initialization**: `lastSyncedTransportPosition` might not be initialized correctly when timeline sync is enabled
3. **Transport Position Persistence**: Transport position doesn't reset to 0 when timeline sync is enabled - it stays at whatever it was
4. **Parameter Update Race**: When `setTimingInfo` updates `positionParam`, it triggers checks in `processBlock` even with the fixes
5. **Multiple Code Paths**: Scrubbing can be triggered from multiple places:
   - `setTimingInfo` → direct `setCurrentPosition` call
   - `processBlock` → position parameter mismatch detection
   - Normal playback → position parameter updates triggering false positives

## Current Code State

### SampleLoaderModuleProcessor::setTimingInfo
- Early return if module is timeline master ✓
- Early return if `timelineMasterLogicalId == UINT32_MAX` ✓
- Transport stuck detection ✓
- Range clamping ✓
- Updates `lastUiPosition` to prevent false triggers ✓

### SampleLoaderModuleProcessor::processBlock
- Ignores position parameter changes when transport sync enabled ✓
- Updates `lastUiPosition` when detecting transport sync updates ✓

### TempoClockModuleProcessor::processBlock
- Sets `timelineMasterLogicalId` to `UINT32_MAX` when timeline sync enabled but no source ✓
- Does NOT update transport position in this case ✓

### ModularSynthProcessor::processBlock
- Does NOT advance transport when `timelineMasterLogicalId != 0` ✓

## Why It's Still Not Working

### Hypothesis 1: Transport Position Not Frozen
- Even though we don't update transport position, it might still be changing from elsewhere
- Host transport sync might be active
- Another module might be updating transport

### Hypothesis 2: Initialization Issue
- When timeline sync is enabled but no source, transport might have a non-zero position
- SampleLoader's playhead might be at a different position
- The mismatch causes immediate scrubbing, and our checks don't catch it

### Hypothesis 3: Race Condition
- `setTimingInfo` and `processBlock` run at different times
- State changes between them cause false triggers
- `lastSyncedTransportPosition` might not be synchronized correctly

### Hypothesis 4: Threshold Issues
- The 512 sample threshold might be too small
- Floating point precision issues might cause constant small differences
- The stuck detection logic might not be working as expected

## Recommended Next Steps

1. **Add Debug Logging**: Log when scrubbing occurs, what triggered it, and the state values
2. **Reset Transport Position**: When timeline sync is enabled but no source, explicitly reset transport to 0
3. **Improve Initialization**: Ensure `lastSyncedTransportPosition` is initialized correctly
4. **Add State Flags**: Track whether we're in a "timeline sync no source" state and use it consistently
5. **Disable Sync Entirely**: When timeline sync enabled but no source, completely disable transport sync for SampleLoader (not just ignore updates)
6. **Separate Concerns**: Make transport sync a separate flag that's disabled when timeline sync is active but no source is selected

## Critical Missing Piece

The most likely issue is that **transport position is not actually frozen** - it might be:
- Initialized to a non-zero value
- Updated from another source (host, another module)
- Changing due to floating point precision issues
- Not properly synchronized between threads

We need to verify that transport position is truly frozen and not changing at all when timeline sync is enabled but no source is selected.

