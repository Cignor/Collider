# VideoFileLoaderModule Unsynced Pause/Resume - Current Status & Diagnosis

## Problem Statement

**Issue**: In `VideoFileLoaderModule`, when the module is **unsynced from transport**, pause/resume functionality is not working correctly. When you pause the video and then press play, it resets to position 0 instead of resuming from the paused position.

**Working Reference**: `SampleLoaderModuleProcessor` has the same pause/resume logic and it works perfectly. We've been adapting that working pattern to `VideoFileLoaderModule`.

## Key Architectural Differences

### SampleLoaderModuleProcessor (Working)
- **Single-threaded**: Audio thread only
- **Direct position control**: `setTimingInfo()` directly calls `safeProcessor->setCurrentPosition()`
- **Immediate effect**: Position change is immediate and atomic

### VideoFileLoaderModule (Broken)
- **Multi-threaded**: Transport thread (`setTimingInfo()`) + Video thread (`run()`)
- **Master clock system**: Uses `currentAudioSamplePosition` (atomic int64) as master clock
- **Asynchronous sync**: Video thread syncs to master clock asynchronously
- **Audio reader**: Uses `FFmpegAudioReader` that can be reloaded during `prepareToPlay()`

## What We've Implemented (Following SampleLoaderModuleProcessor Pattern)

### 1. Added Member Variables (VideoFileLoaderModule.h)
```cpp
TransportCommand lastTransportCommand { TransportCommand::Stop };
std::atomic<double> pausedNormalizedPosition { -1.0 }; // -1.0 = invalid, >= 0.0 = saved paused position
```

### 2. Rewrote UNSYNCED Mode Section in `setTimingInfo()` (Lines ~1368-1563)

Following the exact pattern from `SampleLoaderModuleProcessor` lines 1004-1117:

**Rising Edge (Resume)**:
- Restores `pausedNormalizedPosition` → `currentAudioSamplePosition` BEFORE setting `playing = true`
- Updates master clock, audio position, video position
- Clears saved position after restoring
- **NEW**: Sets `playing.store(true)` at end of rising edge block (line 1458)

**Falling Edge (Pause/Stop)**:
- **Pause command**: Saves current position to `pausedNormalizedPosition`, sets `isStopped = false`
- **Stop transition**: Resets to range start, clears saved position, sets `isStopped = true`
- **NEW**: Sets `playing.store(false)` at end of falling edge block (line 1562)

### 3. Removed Conflicting Logic
- Deleted old helper functions: `handlePauseRequest()`, `handleStopRequest()`, `snapshotPlaybackStateForResume()`
- Removed `resumeAfterPrepare` flag (obsolete)
- Cleaned up `run()` thread - removed play edge detection (now only processes `pendingSeekNormalized`)
- Simplified `prepareToPlay()` - no longer participates in pause/resume

### 4. Fixed `loadAudioFromVideo()` Position Preservation (Lines 648-674)

**Problem Discovered**: When resuming from pause, `prepareToPlay()` can trigger video file reload, and `loadAudioFromVideo()` was unconditionally resetting `currentAudioSamplePosition` to 0.

**Fix Applied**:
```cpp
// Check if we're currently playing and have a valid position
if (isCurrentlyPlaying && !isCurrentlyStopped && currentMasterClock > 0)
{
    // Preserve the position - don't reset master clock
    audioReadPosition = (double)currentMasterClock;
    updateLastKnownNormalizedFromSamples(currentMasterClock);
}
else
{
    // New file load or explicit stop - reset to beginning
    currentAudioSamplePosition.store(0);
    // ...
}
```

### 5. Added Varispeed/BPM Sync (Synced Mode)
- Added varispeed calculation: `varispeedSpeed = (bpm / 120.0) * knobVal`
- Video speed now adapts to tempo changes when synced

## Current Status: What's Working vs. What's Not

### ✅ Working
- **Pause logic**: Position is correctly saved when pausing (logs confirm)
- **Resume logic in `setTimingInfo()`**: Position is correctly restored (logs confirm: "Restored from paused position: 72480 samples")
- **Varispeed/BPM sync**: Working correctly in synced mode
- **UI buttons**: Direct manipulation of `playing` flag works

### ❌ Still Broken
- **Resume from pause**: After position is restored, something resets it to 0
- **Spacebar/Global transport**: May not be updating local `playing` flag correctly

## Recent Fixes Applied

### Fix 1: Edge Detection + Playing Flag Update
**Location**: `setTimingInfo()` unsynced section (lines 1458, 1562)

**Problem**: When using spacebar/global transport, edge detection was based on `state.isPlaying`, but local `playing` flag wasn't being updated, causing mismatch.

**Solution**: 
- Added `playing.store(true)` at end of rising edge block
- Added `playing.store(false)` at end of falling edge block

This ensures spacebar updates the local `playing` flag to match transport state.

### Fix 2: `loadAudioFromVideo()` Position Preservation
**Location**: `loadAudioFromVideo()` (lines 678-724)

**Problem**: File reload during `prepareToPlay()` was resetting master clock to 0, destroying restored position.

**Initial Solution**: Check if currently playing before resetting - preserve position if resuming from pause.

**Expert's Analysis**: The issue was that `updateLastKnownNormalizedFromSamples()` was being called (or could be called) before `audioReaderLengthSamples` was set. When `audioReaderLengthSamples` is 0, the normalization calculation fails (division by zero).

**Final Fix Applied**:
- When preserving position (line 678-691), we do NOT call `updateLastKnownNormalizedFromSamples()` because `audioReaderLengthSamples` is not yet set
- We only call `updateLastKnownNormalizedFromSamples()` AFTER the audio is loaded and `audioReaderLengthSamples` is set (line 724)
- Added logging to verify preserved position is correctly maintained through the reload process

### Fix 3: Default Sync Mode
**Location**: `processBlock()` (line ~728)

**Problem**: Default sync mode was `true`, causing spacebar to inadvertently enable sync (which resets position).

**Solution**: Changed default to `false` - sync mode only activates when explicitly enabled.

## Diagnostic Logging Added

We've added comprehensive logging to trace the execution flow:

1. **`setTimingInfo()` entry** (line 1345): Logs every call with state values
2. **UI button clicks** (lines 945-1007): Logs before/after state changes
3. **Edge detection** (line 1377): Logs rising/falling edges and commands
4. **Rising edge** (line 1392): Logs when resume is detected
5. **Falling edge** (line 1469): Logs when pause/stop is detected
6. **`loadAudioFromVideo()`** (lines 658, 673): Logs whether position is preserved or reset

## Key Questions for Expert

1. **Timing Issue**: Is `loadAudioFromVideo()` being called AFTER `setTimingInfo()` restores the position? The logs show position is restored correctly, but then something resets it.

2. **State Mismatch**: When `loadAudioFromVideo()` is called during resume, what are the values of:
   - `playing.load()` = ?
   - `isStopped.load()` = ?
   - `currentAudioSamplePosition.load()` = ?
   
   Our condition `isCurrentlyPlaying && !isCurrentlyStopped && currentMasterClock > 0` might not be catching the right state.

3. **Multiple Calls**: Is `loadAudioFromVideo()` being called multiple times? Maybe one call preserves, but a subsequent call resets?

4. **Thread Race**: Is there a race condition between:
   - `setTimingInfo()` restoring position
   - `prepareToPlay()` triggering reload
   - `loadAudioFromVideo()` checking state

5. **Alternative Approach**: Should we use a different mechanism to detect "resuming from pause" in `loadAudioFromVideo()`? Maybe:
   - Check if `pausedNormalizedPosition` was recently cleared?
   - Use a separate "resuming" flag?
   - Check if position was just restored (compare with expected value)?

## Code Locations

- **Main pause/resume logic**: `VideoFileLoaderModule.cpp` lines 1368-1563 (`setTimingInfo()` unsynced section)
- **Position preservation**: `VideoFileLoaderModule.cpp` lines 648-674 (`loadAudioFromVideo()`)
- **UI button handling**: `VideoFileLoaderModule.cpp` lines 945-1007
- **Reference implementation**: `SampleLoaderModuleProcessor.cpp` lines 1004-1117 (working pattern)

## Expected Behavior

**Unsynced Mode**:
- Pause → Save position to `pausedNormalizedPosition`
- Resume → Restore from `pausedNormalizedPosition` → Continue from paused position
- Stop → Reset to range start → Clear saved position

**Synced Mode**:
- Follows transport position (varispeed with BPM)
- Pause/stop handled by transport

## Expert's Root Cause Analysis ✅

**The Real Problem**: When `loadAudioFromVideo()` preserves position during resume, it was attempting to call `updateLastKnownNormalizedFromSamples()` before `audioReaderLengthSamples` was set. Since `audioReaderLengthSamples` was 0, the normalization calculation failed (division by zero), causing the position to be lost.

**The Sequence**:
1. `setTimingInfo()` correctly restores position to `currentAudioSamplePosition = 35520` ✅
2. `prepareToPlay()` is called (audio device restart on spacebar)
3. `loadAudioFromVideo()` checks condition and tries to preserve position ✅
4. **BUG**: If `updateLastKnownNormalizedFromSamples()` was called here, it would fail because `audioReaderLengthSamples = 0`
5. Audio loads, `audioReaderLengthSamples` is set
6. **FIX**: Now we call `updateLastKnownNormalizedFromSamples()` AFTER `audioReaderLengthSamples` is set

## Fix Applied

**Changes Made**:
1. **Line 689**: Added explicit comment that we do NOT call `updateLastKnownNormalizedFromSamples()` when preserving position (because `audioReaderLengthSamples` is not yet set)
2. **Line 724**: Ensured `updateLastKnownNormalizedFromSamples()` is called AFTER `audioReaderLengthSamples` is set (line 723)
3. **Line 724-728**: Added logging to verify preserved position is correctly maintained through reload

**Code Flow**:
```cpp
// When preserving position (line 678-691):
if (isCurrentlyPlaying && !isCurrentlyStopped && currentMasterClock > 0)
{
    // Preserve master clock - don't reset
    audioReadPosition = (double)currentMasterClock;
    // DON'T call updateLastKnownNormalizedFromSamples() here - lengthSamples is 0!
}

// After audio loads (line 719-728):
if (audioReader != nullptr && audioReader->lengthInSamples > 0)
{
    audioReaderLengthSamples.store((double)audioReader->lengthInSamples); // Set length FIRST
    updateLastKnownNormalizedFromSamples(preservedPosition); // NOW safe to normalize
}
```

## Next Steps

1. **Test the fix**: Verify that unsynced pause/resume now works correctly with spacebar
2. **Monitor logs**: Check that preserved position is correctly maintained through reload
3. **Verify UI buttons**: Ensure UI button pause/resume still works (should be unaffected)

The fix addresses the root cause identified by the expert: ensuring `audioReaderLengthSamples` is set before attempting normalization.

