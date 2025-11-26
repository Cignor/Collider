# Pause/Resume Position Preservation - Technical Guide

**Target Audience:** AI assistants and developers working on playback modules that need to preserve playback position when pausing and resuming.

---

## Problem Statement

### The Core Issue

When a user pauses playback and then resumes, the playhead should continue from where it was paused, not reset to the start position. However, several edge cases can cause position loss:

1. **Audio Device Changes**: When `prepareToPlay()` is called (e.g., audio device changes, graph updates), new processors may be created, losing the paused position.
2. **Command Confusion**: Pause vs Stop commands were not distinguished, causing resets on pause.
3. **Mode Mismatch**: Synced vs Unsynced modes required different position initialization strategies.

### Expected Behavior

**Unsynced Mode:**
- **Pause**: Freeze playback at current position (save position)
- **Resume**: Continue from saved position
- **Stop**: Rewind to range start

**Synced Mode:**
- **Pause**: Freeze playback at current position
- **Resume**: Continue from current transport position (not saved position)
- **Stop**: Rewind to range start
- Position always follows transport timeline

---

## Reference Implementation: SampleLoaderModuleProcessor

This guide documents the complete fix applied to `SampleLoaderModuleProcessor` and provides a strategy for applying similar fixes to `VideoFileLoaderModule`.

### Architecture Overview

`SampleLoaderModuleProcessor` uses an internal `SampleVoiceProcessor` that handles actual sample playback. The processor can be recreated when `prepareToPlay()` is called, which was causing position loss.

**Key Components:**
- `SampleVoiceProcessor`: Internal playback engine (can be recreated)
- `pausedPosition`: Saved position when paused (unsynced mode only)
- `lastTransportCommand`: Tracks previous transport command to detect transitions
- `createSampleProcessor()`: Factory method that initializes new processors

---

## Solution Implementation

### 1. State Tracking Members

**File:** `juce/Source/audio/modules/SampleLoaderModuleProcessor.h`

Added member variables to track pause state:

```cpp
TransportCommand lastTransportCommand { TransportCommand::Stop }; // Track last command
double pausedPosition { -1.0 }; // Saved position when paused (unsynced mode only)
```

**Key Points:**
- `pausedPosition` uses `-1.0` as invalid value (position is always >= 0.0)
- `lastTransportCommand` tracks transitions, not just current state
- Both are members, not atomics (accessed from audio thread only)

### 2. Pause/Resume Logic in setTimingInfo()

**File:** `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` (lines 865-1118)

**UNSYNCED MODE Logic:**

```cpp
// === UNSYNCED MODE ===
else
{
    // 3. Handle RISING EDGE FIRST (Resume from Pause)
    // CRITICAL: Restore saved position BEFORE setting isPlaying
    if (risingEdge)
    {
        if (pausedPosition >= 0.0 && currentSample)
        {
            // Restore saved position
            safeProcessor->setCurrentPosition(pausedPosition);
            readPosition = pausedPosition;
            lastReadPosition = pausedPosition;
            
            // Update UI
            if (positionParam) {
                const double totalSamples = (double)currentSample->stereo.getNumSamples();
                float normPos = (float)(pausedPosition / totalSamples);
                positionParam->store(normPos);
                lastUiPosition = normPos;
            }
            
            pausedPosition = -1.0; // Clear saved position
        }
        lastTransportCommand = state.lastCommand.load();
    }

    // 1. Always mirror Transport Play State
    safeProcessor->isPlaying = state.isPlaying;

    // 2. Handle STOP Command (Rewind) - Only on explicit Stop transition
    if (fallingEdge)
    {
        const TransportCommand currentCommand = state.lastCommand.load();

        // FIRST CHECK: If it's Pause, SAVE current position and freeze
        if (currentCommand == TransportCommand::Pause)
        {
            pausedPosition = safeProcessor->getCurrentPosition();
            lastTransportCommand = currentCommand;
        }
        // SECOND CHECK: Only reset if command TRANSITIONS to Stop
        else
        {
            const TransportCommand previousCommand = lastTransportCommand;
            if (currentCommand == TransportCommand::Stop && 
                previousCommand != TransportCommand::Stop)
            {
                // Explicit Stop: Rewind to range start
                if (currentSample)
                {
                    const double totalSamples = (double)currentSample->stereo.getNumSamples();
                    float sNorm = rangeStartParam ? rangeStartParam->load() : 0.0f;
                    double sSamp = sNorm * totalSamples;
                    
                    safeProcessor->setCurrentPosition(sSamp);
                    pausedPosition = -1.0; // Clear saved position
                    
                    if (positionParam) {
                        positionParam->store(sNorm);
                        lastUiPosition = sNorm;
                    }
                }
            }
            lastTransportCommand = currentCommand;
        }
    }
    else
    {
        lastTransportCommand = state.lastCommand.load();
    }
}
```

**Critical Design Decisions:**

1. **Rising Edge First**: Position restoration happens BEFORE setting `isPlaying = true` to prevent race conditions.
2. **Command Transition Detection**: Only reset on transition TO Stop, not if Stop persists.
3. **Pause Saves Position**: On falling edge + Pause command, save current position immediately.
4. **Stop Clears Saved Position**: When stopping, clear any saved paused position.

**SYNCED MODE Logic:**

Synced mode is simpler - it doesn't save/restore paused position because position always follows transport:

```cpp
// === SYNCED MODE ===
if (syncToTransport.load())
{
    safeProcessor->isPlaying = state.isPlaying;
    
    if (state.isPlaying && currentSample && sampleSampleRate > 0)
    {
        // Calculate position from transport state
        // ... (complex position calculation based on sync mode) ...
        
        // Only seek on large jumps (discontinuity detection)
        if (transportJumped && circularDiff > 4800.0)
        {
            safeProcessor->setCurrentPosition(targetSamplePos);
            // ... update UI ...
        }
    }
}
```

### 3. Processor Initialization Fix

**File:** `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` (lines 1454-1514)

**The Critical Fix:** When `prepareToPlay()` creates a new `SampleVoiceProcessor`, initialize it to the correct position based on mode.

```cpp
// CRITICAL FIX: Initialize position based on sync mode
if (pausedPosition >= 0.0 && !syncToTransport.load())
{
    // UNSYNCED: Use saved paused position
    const double clampedPos = juce::jlimit(
        0.0, 
        (double)currentSample->stereo.getNumSamples(), 
        pausedPosition
    );
    newProcessor->setCurrentPosition(clampedPos);
}
else if (syncToTransport.load() && getParent())
{
    // SYNCED: Initialize from current transport position
    TransportState transport = getParent()->getTransportState();
    const double totalSamples = (double)currentSample->stereo.getNumSamples();
    double targetSamplePos = 0.0;
    
    // Use the same calculation logic as setTimingInfo for consistency
    // ... (complex position calculation matching setTimingInfo) ...
    
    targetSamplePos = juce::jlimit(0.0, totalSamples, targetSamplePos);
    newProcessor->setCurrentPosition(targetSamplePos);
}
else
{
    // Default: reset to range start
    newProcessor->resetPosition();
}
```

**Why This Matters:**

1. **Unsynced Mode**: If `prepareToPlay()` is called between pause and resume, the new processor starts at the saved paused position instead of resetting to 0.
2. **Synced Mode**: The new processor initializes to the current transport position, not a saved position (transport is the source of truth).
3. **Default**: Normal initialization resets to range start.

**Calculation Consistency:**

The synced mode calculation in `createSampleProcessor()` **must match** the calculation in `setTimingInfo()` to ensure consistency. Both handle:
- Absolute vs Relative sync modes
- Varispeed (BPM-based speed adjustment)
- Range start/end boundaries
- Loop wrapping logic

---

## Applying to VideoFileLoaderModule

### Current State Analysis

**File:** `juce/Source/audio/modules/VideoFileLoaderModule.cpp`

VideoFileLoaderModule already has some pause/resume infrastructure:

**Existing Mechanisms:**
- `resumeAfterPrepare` flag (similar to pausedPosition)
- `lastKnownNormalizedPosition` (saves normalized position)
- `handlePauseRequest()` and `handleStopRequest()` helpers
- Position restoration in `setTimingInfo()` (lines 1380-1451)

**Key Differences from SampleLoaderModule:**

1. **Architecture**: VideoFileLoaderModule uses a separate thread (`run()`) for video decoding, not just audio thread callbacks.
2. **Position Storage**: Uses normalized position (0.0-1.0) stored in `lastKnownNormalizedPosition` (atomic float).
3. **Audio Reader**: Uses `FFmpegAudioReader` which is created in `loadAudioFromVideo()`, not `prepareToPlay()`.
4. **Master Clock**: Uses `currentAudioSamplePosition` as the master clock for both audio and video sync.

### What's Already Working

From log analysis, VideoFileLoaderModule already handles:
- Position saving on pause (line 687-710: `handlePauseRequest()`)
- Position restoration on resume (line 1390-1442: `setTimingInfo()` resume logic)
- Queuing seeks in `prepareToPlay()` (line 82-97)

### Potential Issues & Fixes Needed

#### Issue 1: Audio Reader Recreation

**Problem:** Unlike SampleLoaderModuleProcessor, VideoFileLoaderModule doesn't recreate the audio reader in `prepareToPlay()`. However, if the audio reader is recreated (e.g., in `loadAudioFromVideo()`), it might reset position.

**Current Behavior:**
- Audio reader is created once in `loadAudioFromVideo()`
- Position is set via `audioReadPosition` variable
- Master clock (`currentAudioSamplePosition`) persists across prepareToPlay()

**Fix Strategy:**
- Ensure `currentAudioSamplePosition` is preserved across `prepareToPlay()` calls
- When restoring position in `setTimingInfo()`, update both `audioReadPosition` and `currentAudioSamplePosition`
- If audio reader is recreated, restore position from `currentAudioSamplePosition`

**Code Location:** Check `loadAudioFromVideo()` (line 742-784) to ensure position is preserved.

#### Issue 2: Synced Mode Position Initialization

**Problem:** In synced mode, when `prepareToPlay()` is called, the position should initialize from transport, not from a saved paused position.

**Current Behavior:**
- `prepareToPlay()` queues a seek to `lastKnownNormalizedPosition` if `resumeAfterPrepare` is true
- This happens regardless of sync mode

**Fix Strategy:**
- Similar to SampleLoaderModuleProcessor, check sync mode in `prepareToPlay()`
- If synced: Initialize from transport position (may need to query transport state)
- If unsynced: Use saved paused position (current behavior is correct)

**Code Location:** `prepareToPlay()` (line 59-98)

**Proposed Fix:**

```cpp
void VideoFileLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // ... existing setup code ...
    
    // CRITICAL FIX: Initialize position based on sync mode
    const bool shouldResume = resumeAfterPrepare.load();
    if (shouldResume)
    {
        if (syncToTransport.load() && getParent())
        {
            // SYNCED: Initialize from current transport position
            TransportState transport = getParent()->getTransportState();
            if (audioReader && audioReader->lengthInSamples > 0)
            {
                // Calculate target sample position from transport
                const double sourceRate = sourceAudioSampleRate.load();
                if (sourceRate > 0.0 && transport.songPositionSeconds >= 0.0)
                {
                    const juce::int64 targetSamplePos = 
                        (juce::int64)(transport.songPositionSeconds * sourceRate);
                    const float targetNorm = 
                        (float)(targetSamplePos / (double)audioReader->lengthInSamples);
                    
                    currentAudioSamplePosition.store(targetSamplePos);
                    updateLastKnownNormalizedFromSamples(targetSamplePos);
                    pendingSeekNormalized.store(targetNorm);
                    needPreviewFrame.store(true);
                }
            }
        }
        else
        {
            // UNSYNCED: Use saved paused position (current behavior)
            const float savedPos = lastKnownNormalizedPosition.load();
            if (savedPos >= 0.0f)
            {
                pendingSeekNormalized.store(savedPos);
                needPreviewFrame.store(true);
            }
        }
    }
}
```

#### Issue 3: Position Restoration Timing

**Problem:** The position restoration in `setTimingInfo()` might conflict with `prepareToPlay()` queued seeks.

**Current Behavior:**
- `prepareToPlay()` queues a seek
- `setTimingInfo()` also restores position on play edge
- Race condition possible

**Fix Strategy:**
- Ensure position restoration happens in the correct order
- Clear `resumeAfterPrepare` after restoration to prevent double-seek
- The current code already does this (line 1441), but verify timing

#### Issue 4: Master Clock Persistence

**Problem:** `currentAudioSamplePosition` should persist across `prepareToPlay()` calls, even when audio reader is recreated.

**Current Behavior:**
- Master clock is atomic, so it persists
- Audio reader position is restored from master clock in `setTimingInfo()`

**Fix Strategy:**
- Verify `currentAudioSamplePosition` is NOT reset in `loadAudioFromVideo()` when resuming
- Only reset master clock on explicit stop or file load (not on pause)

**Code Location:** `loadAudioFromVideo()` (line 749) resets to 0 - this is correct for new file loads, but verify it's not called unnecessarily.

---

## Testing Strategy

### Test Cases

1. **Unsynced Pause/Resume:**
   - Play video → Pause at position X → Resume → Verify position = X
   
2. **Unsynced Pause + prepareToPlay():**
   - Play video → Pause → Change audio device (triggers prepareToPlay) → Resume → Verify position preserved

3. **Synced Pause/Resume:**
   - Play video (synced) → Pause → Resume → Verify position follows transport (not saved position)

4. **Synced Pause + prepareToPlay():**
   - Play video (synced) → Pause → Change audio device → Resume → Verify position matches transport

5. **Stop vs Pause:**
   - Play → Pause → Verify position saved
   - Play → Stop → Verify position resets to start
   - Pause → Stop → Verify position resets (not restored on next play)

6. **Mode Switching:**
   - Unsynced pause → Switch to synced → Resume → Verify position follows transport
   - Synced pause → Switch to unsynced → Resume → Verify position restored

### Logging Points

Add debug logging at:
- Position save (on pause)
- Position restore (on resume)
- prepareToPlay() initialization
- Mode switch events

---

## Implementation Checklist

### For VideoFileLoaderModule

- [ ] **Add sync mode check in prepareToPlay()**
  - Check `syncToTransport` before using saved position
  - Initialize from transport position if synced
  
- [ ] **Verify master clock persistence**
  - Ensure `currentAudioSamplePosition` is not reset unnecessarily
  - Preserve across `prepareToPlay()` calls
  
- [ ] **Review position restoration order**
  - Ensure `setTimingInfo()` restoration doesn't conflict with queued seeks
  - Clear flags in correct order
  
- [ ] **Test audio reader recreation**
  - Verify position is restored if reader is recreated during pause
  - Ensure master clock is source of truth
  
- [ ] **Add comprehensive logging**
  - Log position save/restore operations
  - Log mode transitions
  - Log prepareToPlay() initialization decisions

---

## Key Principles

1. **Master Clock as Source of Truth**: In VideoFileLoaderModule, `currentAudioSamplePosition` is the master clock. All position operations should update this first, then derive other positions from it.

2. **Mode-Aware Initialization**: Always check sync mode before initializing position. Synced mode uses transport, unsynced mode uses saved position.

3. **Command Transition Detection**: Only reset on explicit Stop transition, not on persistent Stop state. Distinguish Pause from Stop commands.

4. **Order Matters**: Restore position BEFORE setting playing state to prevent race conditions.

5. **Consistency**: Position calculations in initialization must match calculations in runtime updates.

---

## Related Files

- `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` - Reference implementation
- `juce/Source/audio/modules/SampleLoaderModuleProcessor.h` - State tracking members
- `juce/Source/audio/modules/VideoFileLoaderModule.cpp` - Target for fixes
- `juce/Source/audio/modules/VideoFileLoaderModule.h` - State tracking members
- `juce/Source/audio/modules/ModuleProcessor.h` - TransportCommand enum definition

---

## Notes

- VideoFileLoaderModule's architecture (separate thread) adds complexity but the principles remain the same.
- The master clock (`currentAudioSamplePosition`) approach is actually cleaner than SampleLoaderModuleProcessor's approach.
- Both modules should converge on similar pause/resume behavior for consistency.

