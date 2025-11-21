# Module Auto-Start Prevention - Detailed Solution

**Date**: 2025-01-XX  
**Issue**: Modules auto-starting on load/init (Inconsistencies #3, #12, #13, #15, #18)  
**Status**: Solution Design

---

## Problem Summary

Modules are starting playback automatically when they shouldn't, specifically:
- During patch loading
- During module initialization
- When clips/samples are loaded
- When reset() is called

This happens because:
1. `reset()` methods unconditionally set `isPlaying = true`
2. Modules call `reset()` during initialization
3. Some modules auto-start when content is loaded
4. No check of transport state before starting

---

## Solution Strategy

### Three-Layer Defense Approach

1. **Layer 1: Prevent at Source** - Fix `reset()` methods to not auto-start
2. **Layer 2: Check Transport State** - Verify transport before starting
3. **Layer 3: Force Stop After Load** - Explicitly stop all modules after patch load

---

## Solution 1: Fix reset() Methods (Primary Fix)

### Problem
`reset()` methods unconditionally set `isPlaying = true`, which causes auto-start.

### Solution
Separate `reset()` into two methods:
- `reset()` - Resets position/state but **does NOT start playback**
- `resetAndPlay()` - Resets position/state **and starts playback**

### Implementation

#### Step 1: Update SampleVoiceProcessor

**File**: `juce/Source/audio/voices/SampleVoiceProcessor.h`

```cpp
// OLD:
void reset() override { readPosition = startSamplePos; timePitch.reset(); isPlaying = true; }

// NEW:
void reset() override { 
    readPosition = startSamplePos; 
    timePitch.reset(); 
    isPlaying = false; // Don't auto-start, let transport control it
}

// NEW METHOD:
void resetAndPlay() {
    readPosition = startSamplePos;
    timePitch.reset();
    isPlaying = true; // Explicit start
}
```

#### Step 2: Update SampleLoaderModuleProcessor

**File**: `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`

```cpp
// OLD reset() implementation:
void SampleLoaderModuleProcessor::reset()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->reset(); // This used to set isPlaying = true
    }
    // ... rest of reset logic
}

// NEW reset() implementation:
void SampleLoaderModuleProcessor::reset()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->reset(); // Now only resets position, doesn't start
    }
    // ... rest of reset logic
    // isPlaying remains false (or current state)
}

// NEW METHOD for explicit start:
void SampleLoaderModuleProcessor::resetAndPlay()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->resetAndPlay(); // Explicitly start
    }
    // ... rest of reset logic
}
```

#### Step 3: Update All reset() Call Sites

**During Initialization/Patch Load** - Use `reset()` (doesn't start):
```cpp
// In prepareToPlay() or setStateInformation():
newProcessor->reset(); // Only resets position, doesn't start playback
```

**On User Trigger** - Use `resetAndPlay()` (explicitly starts):
```cpp
// When user clicks play or trigger input fires:
if (shouldStartPlayback) {
    currentProcessor->resetAndPlay(); // Explicitly start
}
```

**During Scrubbing** - Use `reset()` (preserves state):
```cpp
// When scrubbing position:
sampleProcessor->reset(); // Reset position, keep current playing state
if (wasPlaying) {
    sampleProcessor->isPlaying = true; // Restore if was playing
}
```

### Files to Modify

1. `juce/Source/audio/voices/SampleVoiceProcessor.h` - Fix reset()
2. `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` - Update reset() calls
3. `juce/Source/audio/modules/SampleLoaderModuleProcessor.h` - Add resetAndPlay()
4. Any other modules with similar reset() behavior

### Risk: Medium
### Difficulty: Medium
### Confidence: High (85%)

---

## Solution 2: Add Transport State Check (Secondary Fix)

### Problem
Even with fixed `reset()`, modules might start playback in other ways (e.g., when clips are loaded).

### Solution
Add transport state check before starting playback in all auto-start scenarios.

### Implementation

#### Step 1: Add Helper Method to ModuleProcessor Base Class

**File**: `juce/Source/audio/modules/ModuleProcessor.h`

```cpp
class ModuleProcessor : public juce::AudioProcessor
{
    // ... existing code ...
    
protected:
    // Helper to check if transport is playing
    bool isTransportPlaying() const {
        if (auto* parent = getParent()) {
            return parent->getTransportState().isPlaying;
        }
        return false; // Default to stopped if no parent
    }
    
    // Helper to check if should start playback
    bool shouldStartPlayback() const {
        // Only start if transport is playing OR sync is disabled
        if (!syncToTransport) {
            return true; // Independent modules can start
        }
        return isTransportPlaying(); // Synced modules follow transport
    }
};
```

#### Step 2: Update TTSPerformer startPlayback()

**File**: `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`

```cpp
// OLD:
void TTSPerformerModuleProcessor::startPlayback()
{
    float trimStartNorm = apvts.getRawParameterValue("trimStart")->load();
    int trimStart = (int) std::floor(trimStartNorm * selectedClip->audio.getNumSamples());
    readPosition = (double)juce::jlimit(0, selectedClip->audio.getNumSamples()-1, trimStart);
    isPlaying = true; // Always starts
}

// NEW:
void TTSPerformerModuleProcessor::startPlayback()
{
    float trimStartNorm = apvts.getRawParameterValue("trimStart")->load();
    int trimStart = (int) std::floor(trimStartNorm * selectedClip->audio.getNumSamples());
    readPosition = (double)juce::jlimit(0, selectedClip->audio.getNumSamples()-1, trimStart);
    
    // Only start if transport is playing (for synced modules)
    // OR if this is an explicit user trigger (not auto-start)
    if (shouldStartPlayback() || isExplicitUserTrigger) {
        isPlaying = true;
    } else {
        isPlaying = false; // Don't auto-start if transport is stopped
    }
}
```

#### Step 3: Update SampleLoader Trigger Handling

**File**: `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`

```cpp
// OLD (around line 596):
if (triggerRisingEdge) {
    reset(); // This used to set isPlaying = true
}

// NEW:
if (triggerRisingEdge) {
    // Check transport state before starting
    if (shouldStartPlayback()) {
        resetAndPlay(); // Explicitly start
    } else {
        reset(); // Just reset position, don't start
    }
}
```

#### Step 4: Update TTSPerformer setStateInformation()

**File**: `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`

```cpp
// OLD (around line 1697):
void TTSPerformerModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // ... restore state ...
    if (selectedClipId.isNotEmpty()) {
        selectClipByKey(selectedClipId); // This might call startPlayback()
    }
}

// NEW:
void TTSPerformerModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // ... restore state ...
    if (selectedClipId.isNotEmpty()) {
        // Set flag to prevent auto-start during patch load
        bool wasAutoStartEnabled = allowAutoStart;
        allowAutoStart = false; // Disable auto-start during load
        
        selectClipByKey(selectedClipId); // Won't auto-start now
        
        allowAutoStart = wasAutoStartEnabled; // Restore flag
    }
}

// In selectClipByKey() or startPlayback():
void TTSPerformerModuleProcessor::startPlayback()
{
    // ... position setup ...
    
    // Check if auto-start is allowed (not during patch load)
    if (!allowAutoStart) {
        isPlaying = false; // Don't start during patch load
        return;
    }
    
    // Normal start logic
    if (shouldStartPlayback()) {
        isPlaying = true;
    }
}
```

### Files to Modify

1. `juce/Source/audio/modules/ModuleProcessor.h` - Add helper methods
2. `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp` - Add transport check
3. `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` - Add transport check
4. `juce/Source/audio/modules/VideoFileLoaderModule.cpp` - Add initialization check

### Risk: Medium
### Difficulty: Medium
### Confidence: High (80%)

---

## Solution 3: Force Stop After Patch Load (Safety Net)

### Problem
Even with fixes above, some edge cases might slip through. Need a safety net.

### Solution
After patch load completes, explicitly force all modules to stop.

### Implementation

#### Step 1: Add Force Stop Method to ModularSynthProcessor

**File**: `juce/Source/audio/graph/ModularSynthProcessor.h`

```cpp
class ModularSynthProcessor : public juce::AudioProcessorGraph
{
    // ... existing code ...
    
    // Force all modules to stop (regardless of sync settings)
    void forceStopAllModules();
};
```

**File**: `juce/Source/audio/graph/ModularSynthProcessor.cpp`

```cpp
void ModularSynthProcessor::forceStopAllModules()
{
    auto currentProcessors = activeAudioProcessors.load();
    if (!currentProcessors) return;
    
    for (const auto& modulePtr : *currentProcessors)
    {
        if (!modulePtr) continue;
        
        // Try to stop module-specific playback
        if (auto* sampleLoader = dynamic_cast<SampleLoaderModuleProcessor*>(modulePtr.get()))
        {
            sampleLoader->forceStop(); // Add this method
        }
        else if (auto* ttsPerformer = dynamic_cast<TTSPerformerModuleProcessor*>(modulePtr.get()))
        {
            ttsPerformer->stopPlayback(); // Already exists
        }
        else if (auto* videoLoader = dynamic_cast<VideoFileLoaderModule*>(modulePtr.get()))
        {
            videoLoader->setPlaying(false); // Add this method
        }
        // Add other module types as needed
    }
}
```

#### Step 2: Add Force Stop Methods to Modules

**File**: `juce/Source/audio/modules/SampleLoaderModuleProcessor.h`

```cpp
class SampleLoaderModuleProcessor : public ModuleProcessor
{
    // ... existing code ...
    
    // Force stop (used after patch load)
    void forceStop() {
        if (sampleProcessor) {
            sampleProcessor->isPlaying = false;
        }
        isPlaying.store(false);
    }
};
```

#### Step 3: Call After Patch Load

**File**: `juce/Source/audio/graph/ModularSynthProcessor.cpp`

```cpp
void ModularSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // ... existing patch load code ...
    
    juce::Logger::writeToLog("[STATE] Calling commitChanges()...");
    commitChanges();
    
    // CRITICAL: Force stop all modules after patch load
    // This ensures no modules are playing even if they auto-started during load
    setPlaying(false); // Set transport to stopped
    forceStopAllModules(); // Force all modules to stop
    
    // Broadcast stopped state to all modules
    if (auto processors = activeAudioProcessors.load())
    {
        TransportState stoppedState = m_transportState;
        stoppedState.isPlaying = false;
        for (const auto& modulePtr : *processors)
        {
            if (modulePtr) {
                modulePtr->setTimingInfo(stoppedState);
            }
        }
    }
    
    juce::Logger::writeToLog("[STATE] Restore complete - all modules stopped.");
}
```

### Files to Modify

1. `juce/Source/audio/graph/ModularSynthProcessor.h` - Add forceStopAllModules()
2. `juce/Source/audio/graph/ModularSynthProcessor.cpp` - Implement and call
3. `juce/Source/audio/modules/SampleLoaderModuleProcessor.h` - Add forceStop()
4. `juce/Source/audio/modules/VideoFileLoaderModule.h` - Add setPlaying() if needed

### Risk: Low
### Difficulty: Easy-Medium
### Confidence: High (90%)

---

## Solution 4: Add Initialization Flag (Prevent During Load)

### Problem
Need to distinguish between "user wants to start" vs "auto-start during load".

### Solution
Add a flag that indicates we're in "patch load mode" and prevent auto-start during this time.

### Implementation

#### Step 1: Add Flag to ModularSynthProcessor

**File**: `juce/Source/audio/graph/ModularSynthProcessor.h`

```cpp
class ModularSynthProcessor : public juce::AudioProcessorGraph
{
    // ... existing code ...
    
private:
    std::atomic<bool> isRestoringState { false }; // Flag for patch load
};
```

#### Step 2: Set Flag During Patch Load

**File**: `juce/Source/audio/graph/ModularSynthProcessor.cpp`

```cpp
void ModularSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    isRestoringState.store(true); // Set flag at start
    
    // ... existing patch load code ...
    
    isRestoringState.store(false); // Clear flag at end
}

// Add public getter
bool ModularSynthProcessor::isRestoringState() const {
    return isRestoringState.load();
}
```

#### Step 3: Check Flag in Modules

**File**: `juce/Source/audio/modules/ModuleProcessor.h`

```cpp
class ModuleProcessor : public juce::AudioProcessor
{
protected:
    bool isDuringPatchLoad() const {
        if (auto* parent = getParent()) {
            return parent->isRestoringState();
        }
        return false;
    }
    
    bool shouldStartPlayback() const {
        // Never auto-start during patch load
        if (isDuringPatchLoad()) {
            return false;
        }
        
        // Normal logic
        if (!syncToTransport) {
            return true;
        }
        return isTransportPlaying();
    }
};
```

### Files to Modify

1. `juce/Source/audio/graph/ModularSynthProcessor.h` - Add flag
2. `juce/Source/audio/graph/ModularSynthProcessor.cpp` - Set/clear flag
3. `juce/Source/audio/modules/ModuleProcessor.h` - Add check

### Risk: Low
### Difficulty: Easy
### Confidence: High (95%)

---

## Recommended Implementation Order

### Phase 1: Foundation (Do First)
1. **Solution 4**: Add initialization flag
   - Quick and easy
   - Provides foundation for other fixes
   - Low risk

### Phase 2: Core Fix (Critical)
2. **Solution 1**: Fix reset() methods
   - Addresses root cause
   - Most modules affected
   - Medium risk, but necessary

### Phase 3: Safety Net (Important)
3. **Solution 3**: Force stop after load
   - Catches any edge cases
   - Low risk
   - Quick to implement

### Phase 4: Additional Protection (Nice to Have)
4. **Solution 2**: Add transport state checks
   - Extra protection
   - Handles edge cases
   - Medium risk

---

## Complete Code Example: SampleLoaderModuleProcessor

Here's how all solutions work together:

```cpp
// In SampleLoaderModuleProcessor.h
class SampleLoaderModuleProcessor : public ModuleProcessor
{
    void reset() override; // Resets position, doesn't start
    void resetAndPlay(); // Resets position and starts
    void forceStop(); // Force stop (used after patch load)
};

// In SampleLoaderModuleProcessor.cpp
void SampleLoaderModuleProcessor::reset()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->reset(); // Now only resets position
    }
    // isPlaying remains false (or current state)
}

void SampleLoaderModuleProcessor::resetAndPlay()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->resetAndPlay(); // Explicitly starts
    }
    isPlaying.store(true);
}

void SampleLoaderModuleProcessor::forceStop()
{
    if (sampleProcessor) {
        sampleProcessor->isPlaying = false;
    }
    isPlaying.store(false);
}

// In processBlock() - trigger handling:
if (triggerRisingEdge)
{
    // Check if during patch load
    if (isDuringPatchLoad()) {
        reset(); // Just reset, don't start
    }
    // Check transport state
    else if (shouldStartPlayback()) {
        resetAndPlay(); // Explicitly start
    } else {
        reset(); // Just reset, don't start
    }
}

// In prepareToPlay() or setStateInformation():
newProcessor->reset(); // Only resets position, doesn't start
```

---

## Testing Checklist

After implementation, test:

- [ ] Load patch with SampleLoader - should NOT auto-play
- [ ] Load patch with TTSPerformer - should NOT auto-play
- [ ] Load patch with VideoFileLoader - should NOT auto-play
- [ ] Click Play button - modules should start
- [ ] Press spacebar - modules should start
- [ ] Trigger input fires - module should start (if transport playing)
- [ ] Trigger input fires while stopped - module should NOT start
- [ ] Load patch while playing - should stop after load
- [ ] Load patch while stopped - should remain stopped
- [ ] Scrubbing while stopped - should remain stopped
- [ ] Scrubbing while playing - should continue playing
- [ ] reset() called during init - should NOT start
- [ ] resetAndPlay() called - should start

---

## Risk Assessment

### Overall Risk: **MEDIUM** (5/10)

**Mitigations**:
- Incremental implementation (one solution at a time)
- Comprehensive testing after each phase
- Force stop provides safety net
- Backward compatible (old code still works)

**Potential Issues**:
- Some modules might not start when user expects (mitigated by explicit start methods)
- Need to update many modules (mitigated by base class helpers)
- Edge cases in syncToTransport=false modules (mitigated by force stop)

---

## Confidence Rating

### Overall Confidence: **HIGH** (85%)

**Strong Points**:
- Clear problem identification
- Multiple layers of defense
- Incremental approach
- Force stop as safety net

**Weak Points**:
- Many modules to update
- Need to find all reset() call sites
- Some modules might have unique behavior

---

## Timeline Estimate

- **Solution 4** (Flag): 1-2 hours
- **Solution 1** (reset() fix): 4-6 hours
- **Solution 3** (Force stop): 2-3 hours
- **Solution 2** (Transport checks): 3-4 hours
- **Testing**: 3-4 hours

**Total**: 13-19 hours

---

## Conclusion

This multi-layered approach ensures modules never auto-start when they shouldn't:

1. **Flag prevents auto-start during load** (Solution 4)
2. **reset() doesn't auto-start** (Solution 1)
3. **Transport checks prevent unwanted starts** (Solution 2)
4. **Force stop catches any edge cases** (Solution 3)

Together, these solutions provide robust protection against auto-start issues.

