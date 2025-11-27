# Sample Loader Random Button Queue System Implementation Plan

## Executive Summary

**Objective**: Implement a queue-based deferred randomization system for the Sample Loader module's Random button to prevent crashes when switching samples during active playback.

**Current Problem**: The `randomizeSample()` function immediately calls `loadSample()`, which creates a new `SampleVoiceProcessor` and stages it via `newSampleProcessor.store()`. The audio thread can swap this processor mid-render, causing out-of-bounds array access crashes when the new sample has different length than the old one.

**Proposed Solution**: Defer sample loading until playback is safe to swap (similar to `SampleSfxModuleProcessor` pattern). Queue the random selection and only execute `loadSample()` when the current sample finishes or when explicitly triggered.

---

## Current Architecture Analysis

### Current Flow (Problematic)
```
User presses Random button
  ↓
randomizeSample() [UI thread]
  ↓
loadSample(randomFile) [UI thread]
  ↓
createSampleProcessor() [UI thread]
  ↓
newSampleProcessor.store(newProcessor) [UI thread]
  ↓
[Next audio callback]
  ↓
processBlock() [Audio thread]
  ↓
newSampleProcessor.exchange(nullptr) [Audio thread - SWAPS IMMEDIATELY]
  ↓
renderBlock() [Audio thread - CRASH if readPosition > new sample length]
```

### Target Flow (Safe)
```
User presses Random button
  ↓
randomizeSample() [UI thread]
  ↓
queueRandomSample(randomFile) [UI thread - QUEUE ONLY]
  ↓
[Audio thread continues with old processor]
  ↓
processBlock() [Audio thread]
  ↓
checkRandomQueue() [Audio thread - checks if safe to swap]
  ↓
  IF (currentProcessor->isPlaying == false OR at safe boundary)
    loadSample(queuedFile)
    createSampleProcessor()
    swap processor
  ELSE
    keep current processor (defer swap)
```

---

## Implementation Plan

### Phase 1: Add Queue Infrastructure

#### 1.1 Add Member Variables to `SampleLoaderModuleProcessor.h`

**Location**: Private section, after existing sample management variables

**New Members**:
```cpp
// Random sample queue (thread-safe)
juce::CriticalSection randomQueueLock;
juce::File queuedRandomSample;  // Single file queue (replaces on new random press)
std::atomic<bool> hasQueuedRandom { false };
std::atomic<bool> randomSwapPending { false };  // Flag for UI feedback
```

**Risk**: Low - Simple atomic/CS additions, no breaking changes

**Difficulty**: ⭐ (Trivial)

---

#### 1.2 Modify `randomizeSample()` to Queue Instead of Load

**Location**: `SampleLoaderModuleProcessor.cpp` line ~1393

**Current Code**:
```cpp
void SampleLoaderModuleProcessor::randomizeSample()
{
    // ... file discovery logic ...
    juce::File randomFile = audioFiles[rng.nextInt(audioFiles.size())];
    DBG("[Sample Loader] Randomizing to: " + randomFile.getFullPathName());
    loadSample(randomFile);  // ← IMMEDIATE LOAD (PROBLEM)
}
```

**New Code**:
```cpp
void SampleLoaderModuleProcessor::randomizeSample()
{
    if (currentSamplePath.isEmpty())
        return;

    juce::File currentFile(currentSamplePath);
    juce::File parentDir = currentFile.getParentDirectory();
    
    if (!parentDir.exists() || !parentDir.isDirectory())
        return;
        
    // Get all audio files in the same directory
    juce::Array<juce::File> audioFiles;
    parentDir.findChildFiles(
        audioFiles, juce::File::findFiles, true, "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

    if (audioFiles.size() <= 1)
        return;

    // Remove current file from the list
    for (int i = audioFiles.size() - 1; i >= 0; --i)
    {
        if (audioFiles[i].getFullPathName() == currentSamplePath)
        {
            audioFiles.remove(i);
            break;
        }
    }

    if (audioFiles.isEmpty())
        return;

    // Pick a random file
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());
    juce::File randomFile = audioFiles[rng.nextInt(audioFiles.size())];

    // QUEUE the random sample instead of loading immediately
    {
        const juce::ScopedLock lock(randomQueueLock);
        queuedRandomSample = randomFile;
        hasQueuedRandom.store(true);
        randomSwapPending.store(true);
    }
    
    juce::Logger::writeToLog(
        "[Sample Loader] Queued random sample: " + randomFile.getFullPathName() + 
        " (will load when playback is safe)");
}
```

**Risk**: Low - Only changes behavior of Random button, no API changes

**Difficulty**: ⭐⭐ (Easy)

**Potential Issues**:
- UI might need feedback that random is "queued" vs "active"
- Multiple rapid Random presses will overwrite queue (acceptable behavior)

---

### Phase 2: Add Queue Processing in Audio Thread

#### 2.1 Create `processRandomQueue()` Method

**Location**: `SampleLoaderModuleProcessor.cpp` (new private method)

**Implementation**:
```cpp
void SampleLoaderModuleProcessor::processRandomQueue()
{
    // Check if we have a queued random sample
    if (!hasQueuedRandom.load())
        return;
    
    // Determine if it's safe to swap
    bool isSafeToSwap = false;
    SampleVoiceProcessor* currentProcessor = nullptr;
    {
        const juce::ScopedLock lock(processorSwapLock);
        currentProcessor = sampleProcessor.get();
    }
    
    if (currentProcessor == nullptr)
    {
        // No processor = always safe (nothing playing)
        isSafeToSwap = true;
    }
    else
    {
        // Safe to swap if:
        // 1. Not currently playing, OR
        // 2. Playing but at start of loop (readPosition near startSamplePos)
        const bool notPlaying = !currentProcessor->isPlaying;
        const double currentPos = currentProcessor->getCurrentPosition();
        const double startPos = currentProcessor->startSamplePos;
        const double posDelta = std::abs(currentPos - startPos);
        const bool atLoopStart = (posDelta < 100.0); // Within 100 samples of start
        
        isSafeToSwap = notPlaying || atLoopStart;
    }
    
    if (!isSafeToSwap)
    {
        // Not safe yet - defer until next block
        return;
    }
    
    // Safe to swap - get queued file and load it
    juce::File fileToLoad;
    {
        const juce::ScopedLock lock(randomQueueLock);
        if (!hasQueuedRandom.load())
            return; // Queue was cleared
        
        fileToLoad = queuedRandomSample;
        hasQueuedRandom.store(false);
        randomSwapPending.store(false);
    }
    
    // Load the queued sample (this will create new processor and stage it)
    if (fileToLoad.existsAsFile())
    {
        juce::Logger::writeToLog(
            "[Sample Loader] Processing queued random sample: " + fileToLoad.getFullPathName());
        loadSample(fileToLoad);
    }
}
```

**Risk**: Medium - Logic complexity, need to handle edge cases

**Difficulty**: ⭐⭐⭐ (Moderate)

**Potential Issues**:
- `atLoopStart` threshold (100 samples) might be too strict/loose depending on sample rate
- Need to ensure `getCurrentPosition()` is thread-safe (it reads `readPosition` which is not atomic)
- Race condition: processor could start playing between check and load

**Mitigation**:
- Use more conservative check: only swap when `!isPlaying`
- Add retry mechanism: if swap fails, re-queue
- Consider using `processorSwapLock` to synchronize check and swap

---

#### 2.2 Integrate Queue Processing into `processBlock()`

**Location**: `SampleLoaderModuleProcessor.cpp` line ~223, early in `processBlock()`

**Insertion Point**: After processor swap but before audio rendering

**Code**:
```cpp
void SampleLoaderModuleProcessor::processBlock(...)
{
    // ... existing processor swap code ...
    
    // Process random queue if safe to swap
    processRandomQueue();
    
    // Refresh processor pointer in case queue processing swapped it
    {
        const juce::ScopedLock lock(processorSwapLock);
        currentProcessor = sampleProcessor.get();
    }
    
    if (currentProcessor == nullptr || currentSample == nullptr)
    {
        outBus.clear();
        return;
    }
    
    // ... rest of processBlock ...
}
```

**Risk**: Low - Non-breaking addition, only adds safety check

**Difficulty**: ⭐⭐ (Easy)

**Potential Issues**:
- Double processor refresh might be redundant (already done above)
- Need to ensure queue processing doesn't add significant latency

---

### Phase 3: Handle Edge Cases

#### 3.1 Clear Queue on Manual Sample Load

**Location**: `loadSample()` method (both overloads)

**Action**: Clear random queue when user manually loads a sample (not from queue)

**Code Addition**:
```cpp
void SampleLoaderModuleProcessor::loadSample(const juce::File& file)
{
    // Clear any pending random queue when manually loading
    {
        const juce::ScopedLock lock(randomQueueLock);
        hasQueuedRandom.store(false);
        randomSwapPending.store(false);
        queuedRandomSample = juce::File();
    }
    
    // ... existing loadSample logic ...
}
```

**Risk**: Low - Prevents stale queue entries

**Difficulty**: ⭐ (Trivial)

---

#### 3.2 Clear Queue on Module Reset/Stop

**Location**: `reset()`, `forceStop()`, `handleStopRequest()` methods

**Action**: Clear queue when playback is forcefully stopped

**Code Addition**:
```cpp
void SampleLoaderModuleProcessor::reset()
{
    // Clear random queue on reset
    {
        const juce::ScopedLock lock(randomQueueLock);
        hasQueuedRandom.store(false);
        randomSwapPending.store(false);
    }
    
    // ... existing reset logic ...
}
```

**Risk**: Low - Prevents queue from persisting across resets

**Difficulty**: ⭐ (Trivial)

---

#### 3.3 Handle Queue on Transport Stop/Pause

**Location**: `setTimingInfo()`, transport command handlers

**Action**: Process queue immediately when transport stops (safe swap point)

**Code Addition**:
```cpp
void SampleLoaderModuleProcessor::setTimingInfo(const TransportState& state)
{
    // ... existing transport logic ...
    
    if (transportStopEdge)
    {
        // Transport stopped - safe to process random queue
        processRandomQueue();
        
        // ... existing stop handling ...
    }
}
```

**Risk**: Medium - Need to ensure `processRandomQueue()` is safe to call from message thread

**Difficulty**: ⭐⭐⭐ (Moderate)

**Potential Issues**:
- `processRandomQueue()` calls `loadSample()` which may do file I/O
- File I/O on message thread could cause UI freeze
- Need to ensure thread safety

**Mitigation**:
- Only call `processRandomQueue()` from audio thread
- Use flag to signal audio thread to process queue on next block

---

### Phase 4: UI Feedback (Optional but Recommended)

#### 4.1 Add Queue Status to UI

**Location**: `drawParametersInNode()` if PRESET_CREATOR_UI is defined

**Action**: Show visual indicator when random is queued

**Implementation**:
```cpp
#if defined(PRESET_CREATOR_UI)
void SampleLoaderModuleProcessor::drawParametersInNode(...)
{
    // ... existing UI code ...
    
    // Random button with queue status
    bool randomQueued = randomSwapPending.load();
    if (randomQueued)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 0.0f, 0.5f)); // Yellow tint
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 0.0f, 0.7f));
    }
    
    if (ImGui::Button("Random", ImVec2(itemWidth * 0.48f, 0)))
    {
        randomizeSample();
        onModificationEnded();
    }
    
    if (randomQueued)
    {
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextDisabled("(queued)");
    }
    
    // ... rest of UI ...
}
#endif
```

**Risk**: Low - UI-only change, no audio thread impact

**Difficulty**: ⭐⭐ (Easy)

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM** ⚠️

**Breakdown**:
- **Crash Risk**: **LOW** ✅ - Queue system prevents mid-render swaps
- **Data Loss Risk**: **NONE** ✅ - No state persistence changes
- **Performance Risk**: **LOW** ✅ - Minimal overhead (one atomic check per block)
- **Regression Risk**: **MEDIUM** ⚠️ - Changes Random button behavior (deferred vs immediate)
- **Thread Safety Risk**: **MEDIUM** ⚠️ - Need careful synchronization between UI and audio threads

### Specific Risks

1. **Race Condition in `processRandomQueue()`**
   - **Risk**: Processor state could change between safety check and load
   - **Mitigation**: Use `processorSwapLock` to synchronize check and swap
   - **Severity**: Medium

2. **Queue Staleness**
   - **Risk**: Queue could contain invalid file if directory changes
   - **Mitigation**: Validate file exists before loading
   - **Severity**: Low

3. **Multiple Rapid Random Presses**
   - **Risk**: Queue gets overwritten, user might not see expected sample
   - **Mitigation**: Acceptable behavior (last press wins), or implement queue with multiple entries
   - **Severity**: Low

4. **UI Thread Calling `processRandomQueue()`**
   - **Risk**: File I/O on message thread could freeze UI
   - **Mitigation**: Only call from audio thread, use flag for message thread signaling
   - **Severity**: Medium

---

## Difficulty Levels

### Level 1: Minimal Implementation ⭐⭐
**Scope**: Basic queue, swap only when `!isPlaying`
- Add queue variables
- Modify `randomizeSample()` to queue
- Add simple `processRandomQueue()` (swap only when stopped)
- Integrate into `processBlock()`

**Time Estimate**: 2-3 hours
**Risk**: Low
**Confidence**: 95%

---

### Level 2: Safe Boundary Detection ⭐⭐⭐
**Scope**: Level 1 + detect loop start for seamless swaps
- All Level 1 changes
- Add loop start detection in `processRandomQueue()`
- Handle edge cases (no loop, different loop points)

**Time Estimate**: 4-6 hours
**Risk**: Medium
**Confidence**: 80%

---

### Level 3: Full Integration ⭐⭐⭐⭐
**Scope**: Level 2 + UI feedback + transport integration
- All Level 2 changes
- Add UI queue status indicator
- Integrate with transport stop/pause
- Clear queue on manual loads/resets

**Time Estimate**: 6-8 hours
**Risk**: Medium-High
**Confidence**: 70%

---

## Confidence Rating

### Overall Confidence: **85%** 🎯

### Strong Points ✅

1. **Proven Pattern**: `SampleSfxModuleProcessor` already uses this queue approach successfully
2. **Minimal Changes**: Only affects Random button behavior, no core architecture changes
3. **Defensive**: Queue system is inherently safer than immediate swap
4. **Backward Compatible**: Existing `loadSample()` API unchanged
5. **Incremental**: Can implement in phases, test at each level

### Weak Points ⚠️

1. **Behavior Change**: Random button no longer immediate (could confuse users expecting instant swap)
2. **Timing Uncertainty**: Swap timing depends on playback state (might feel "laggy")
3. **Edge Case Complexity**: Loop detection, transport sync, pause/resume scenarios need careful handling
4. **Testing Coverage**: Need extensive testing with various sample lengths, loop states, transport modes

### Mitigation Strategies

1. **User Communication**: Add UI feedback showing "queued" state
2. **Fallback**: If queue sits too long (>5 seconds), force swap on next block boundary
3. **Testing Plan**: Create test cases for all edge cases before implementation
4. **Gradual Rollout**: Implement Level 1 first, test thoroughly, then add Level 2/3 features

---

## Potential Problems & Solutions

### Problem 1: Queue Never Processes (Sample Always Playing)
**Scenario**: User presses Random, but sample is looping infinitely, so queue never processes.

**Solutions**:
- **A**: Force swap after N blocks (e.g., 100 blocks ≈ 2 seconds at 48kHz/512)
- **B**: Add "force swap" flag that swaps on next block boundary regardless of state
- **C**: Only queue if `!isPlaying`, otherwise show error message

**Recommendation**: Solution A (timeout-based force swap)

---

### Problem 2: Race Condition: Processor Starts Playing Between Check and Load
**Scenario**: `processRandomQueue()` checks `!isPlaying`, but before `loadSample()` completes, user triggers playback.

**Solutions**:
- **A**: Hold `processorSwapLock` during entire check+load sequence
- **B**: Use atomic flag to prevent playback start during swap
- **C**: Accept race (rare, and new processor will have correct state)

**Recommendation**: Solution A (lock during swap)

---

### Problem 3: Queue Contains Stale File (File Deleted/Moved)
**Scenario**: User presses Random, file gets queued, then file is deleted before queue processes.

**Solutions**:
- **A**: Validate file exists in `processRandomQueue()` before loading
- **B**: Validate file exists when queueing (but file could still be deleted)
- **C**: Both A and B

**Recommendation**: Solution C (defense in depth)

---

### Problem 4: Multiple Random Presses Overwrite Queue
**Scenario**: User presses Random 5 times rapidly, only last sample loads.

**Solutions**:
- **A**: Accept behavior (last press wins) - simplest
- **B**: Implement queue with multiple entries (FIFO)
- **C**: Debounce Random button (ignore presses within 100ms)

**Recommendation**: Solution A for Level 1, Solution B for Level 3 if users request it

---

### Problem 5: Queue Processing Adds Latency
**Scenario**: `processRandomQueue()` does file I/O, adding delay to audio callback.

**Solutions**:
- **A**: Queue processing is lightweight (just file path + loadSample call, which already exists)
- **B**: Move file validation to background thread (overkill for this use case)
- **C**: Profile and optimize if needed

**Recommendation**: Solution A (should be negligible, but profile to confirm)

---

## Testing Plan

### Unit Tests
1. ✅ Queue stores file path correctly
2. ✅ Queue clears on manual load
3. ✅ Queue clears on reset
4. ✅ `processRandomQueue()` only swaps when safe
5. ✅ Queue processes when `!isPlaying`
6. ✅ Queue defers when `isPlaying`

### Integration Tests
1. ✅ Random during playback → queues correctly
2. ✅ Random when stopped → loads immediately
3. ✅ Random during loop → swaps at loop start
4. ✅ Multiple rapid Random presses → last one wins
5. ✅ Random → manual load → queue clears
6. ✅ Random → transport stop → queue processes
7. ✅ Random with deleted file → handles gracefully

### Stress Tests
1. ✅ Rapid Random presses (10x/second) for 30 seconds
2. ✅ Random while scrubbing position
3. ✅ Random while transport syncing
4. ✅ Random with very short samples (< 1 second)
5. ✅ Random with very long samples (> 5 minutes)
6. ✅ Random switching between drastically different lengths (100 samples → 1M samples)

### Edge Cases
1. ✅ Random when no sample loaded
2. ✅ Random when only one file in directory
3. ✅ Random when current file is only file
4. ✅ Random with invalid file path
5. ✅ Random with corrupted audio file
6. ✅ Random during pause (should queue, process on resume/stop)

---

## Implementation Checklist

### Phase 1: Infrastructure
- [ ] Add `randomQueueLock`, `queuedRandomSample`, `hasQueuedRandom`, `randomSwapPending` to header
- [ ] Modify `randomizeSample()` to queue instead of load
- [ ] Add logging for queue operations

### Phase 2: Queue Processing
- [ ] Implement `processRandomQueue()` method
- [ ] Integrate into `processBlock()` after processor swap
- [ ] Add safety checks (file exists, processor state)

### Phase 3: Edge Cases
- [ ] Clear queue in `loadSample()` (manual loads)
- [ ] Clear queue in `reset()` and `forceStop()`
- [ ] Handle queue on transport stop (optional)

### Phase 4: UI Feedback (Optional)
- [ ] Add queue status indicator in UI
- [ ] Visual feedback when random is queued
- [ ] Tooltip explaining deferred behavior

### Phase 5: Testing
- [ ] Unit tests for queue operations
- [ ] Integration tests for all scenarios
- [ ] Stress tests for rapid presses
- [ ] Edge case validation

---

## Success Criteria

1. ✅ **No Crashes**: Random button during playback no longer causes access violations
2. ✅ **Queue Works**: Random samples load correctly when playback is safe
3. ✅ **User Experience**: Queue behavior is intuitive (with UI feedback)
4. ✅ **Performance**: No noticeable latency added to audio processing
5. ✅ **Backward Compatible**: Manual sample loading still works as before

---

## Rollback Plan

If implementation causes issues:

1. **Quick Rollback**: Revert `randomizeSample()` to call `loadSample()` directly
2. **Partial Rollback**: Keep queue infrastructure but always process immediately (defeats purpose but maintains code structure)
3. **Alternative**: Use existing bounds-checking fixes only (less safe but proven to work)

---

## References

- `SampleSfxModuleProcessor::queueNextSample()` - Proven queue pattern
- `SampleSfxModuleProcessor::processTriggerQueue()` - Queue processing example
- `SampleLoaderModuleProcessor::createSampleProcessor()` - Processor creation logic
- `SampleVoiceProcessor::renderBlock()` - Where crashes occur (needs bounds checks)

---

## Notes

- This plan prioritizes **safety over immediacy** - Random button will feel slightly delayed but will never crash
- Consider adding a user preference: "Immediate Random" (risky) vs "Safe Random" (queued) - default to Safe
- Future enhancement: Pre-load queued sample in background thread to reduce perceived delay

---

**Plan Created**: 2025-01-XX  
**Estimated Completion**: 4-8 hours (depending on difficulty level chosen)  
**Priority**: High (fixes critical crash bug)

