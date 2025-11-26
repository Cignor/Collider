# Video Module Initialization After XML Load - Technical Guide

**Target Audience:** AI assistants and developers working on video processing modules in this JUCE-based modular synthesizer.

---

## Problem Statement

When loading a preset from XML that contains a video processing chain (e.g., `VideoFileLoaderModule` → `VideoDrawImpactModuleProcessor`), the video does not appear in the impact node until the user clicks play, stops, and plays again. This creates a poor user experience where the video preview is not immediately available after loading.

### Expected Behavior

After loading a preset from XML:
1. Video file should open immediately
2. Video processing threads should start
3. First frame should be available for preview
4. User should see video on first play click (no stop/play cycle required)

### Actual Behavior

After loading a preset from XML:
1. Video file path is restored via `setExtraStateTree()`
2. Threads start in `prepareToPlay()`
3. **Video does not appear until play → stop → play cycle**
4. User must manually trigger playback twice to see video

---

## Root Cause Analysis

### The Load Sequence

When a preset is loaded from XML, the following sequence occurs:

```
1. ModularSynthProcessor::setStateInformation()
   ├─ Parse XML
   ├─ Create modules (constructor)
   ├─ setExtraStateTree() for each module
   │  └─ VideoFileLoaderModule: Sets videoFileToLoad
   │  └─ VideoDrawImpactModuleProcessor: Restores drawing keyframes
   ├─ Restore APVTS parameters
   ├─ Restore connections
   └─ commitChanges()

2. AudioEngine::prepareToPlay() (called when user clicks play)
   └─ Calls prepareToPlay() on all modules
      ├─ VideoFileLoaderModule::prepareToPlay()
      │  └─ startThread() → Thread begins checking videoFileToLoad
      └─ VideoDrawImpactModuleProcessor::prepareToPlay()
         └─ startThread() → Thread begins waiting for frames
```

### The Problem

**VideoDrawImpactModuleProcessor** has an early exit condition that prevents it from even attempting to get frames:

```cpp
void VideoDrawImpactModuleProcessor::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        if (sourceId == 0) { wait(50); continue; }  // ❌ PROBLEM: Exits early
        
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (frame.empty()) { wait(100); continue; }
        // ... process frame
    }
}
```

**Why this fails:**
1. When loading from XML, `prepareToPlay()` is called **before** connections are fully established in the audio graph
2. `currentSourceId` is set via `processBlock()` from the input connection
3. But `processBlock()` only runs when audio is playing
4. So on first play, `sourceId` might still be 0, causing the thread to wait indefinitely
5. Even after `sourceId` is set, there's a race condition where the video source hasn't produced frames yet

### How ColorTrackerModule Solves This

**ColorTrackerModule** does NOT check `sourceId == 0` before attempting to get frames:

```cpp
void ColorTrackerModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);  // ✅ Always tries
        
        if (!frame.empty())
        {
            // Cache last good frame
            frame.copyTo(lastFrameBgr);
        }
        else
        {
            // Use cached frame when no fresh frames available
            if (!lastFrameBgr.empty())
                frame = lastFrameBgr.clone();
        }
        
        if (!frame.empty())
        {
            // Process frame
        }
        
        wait(33);
    }
}
```

**Key differences:**
1. ✅ Always attempts to get frames (even if `sourceId == 0`)
2. ✅ Handles empty frames gracefully (uses cached frame or skips processing)
3. ✅ No early exit that prevents frame acquisition attempts
4. ✅ Works even when connections aren't established yet

---

## Solution Strategy

### Approach 1: Remove Early Exit (Recommended)

Remove the `sourceId == 0` check and handle empty frames gracefully, similar to `ColorTrackerModule`:

```cpp
void VideoDrawImpactModuleProcessor::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        // Handle empty frames gracefully
        if (frame.empty())
        {
            wait(100);  // Wait longer when no frame available
            continue;
        }
        
        // Process frame...
    }
}
```

**Benefits:**
- ✅ Works immediately after load (no play/stop/play cycle)
- ✅ Handles timing issues gracefully
- ✅ Consistent with `ColorTrackerModule` pattern
- ✅ Simple, low-risk change

**Trade-offs:**
- ⚠️ Will attempt to get frames even when `sourceId == 0` (but `VideoFrameManager::getFrame(0)` safely returns empty frame)
- ⚠️ Slightly less efficient (but negligible impact)

### Approach 2: Cache Last Frame (Like ColorTrackerModule)

Add frame caching to handle paused/loading scenarios:

```cpp
cv::Mat lastFrameBgr;  // Member variable
juce::CriticalSection frameLock;

void VideoDrawImpactModuleProcessor::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        if (!frame.empty())
        {
            const juce::ScopedLock lk(frameLock);
            frame.copyTo(lastFrameBgr);
        }
        else
        {
            const juce::ScopedLock lk(frameLock);
            if (!lastFrameBgr.empty())
                frame = lastFrameBgr.clone();
        }
        
        if (!frame.empty())
        {
            // Process frame...
        }
        
        wait(33);
    }
}
```

**Benefits:**
- ✅ Provides fallback frame when source is temporarily unavailable
- ✅ Better user experience (shows last frame instead of blank)
- ✅ Matches `ColorTrackerModule` pattern exactly

**Trade-offs:**
- ⚠️ Requires additional memory for cached frame
- ⚠️ Slightly more complex

### Approach 3: Resolve Source ID from Connection Graph (Advanced)

For modules that need immediate source ID resolution during XML load, resolve the source ID from the connection graph before `processBlock()` runs:

```cpp
class VideoProcessingModule : public ModuleProcessor
{
private:
    juce::uint32 cachedResolvedSourceId { 0 };  // Cache resolved source ID
    // ... other members ...
};

void VideoProcessingModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        
        // CRITICAL FIX for XML load: If sourceId is 0 (processBlock hasn't run yet),
        // use cached resolved source ID, or try to resolve it from the connection graph
        if (sourceId == 0)
        {
            // First try cached resolved ID
            if (cachedResolvedSourceId != 0)
            {
                sourceId = cachedResolvedSourceId;
            }
            // If no cache, try to resolve from connection graph
            else if (parentSynth != nullptr)
            {
                auto snapshot = parentSynth->getConnectionSnapshot();
                if (snapshot && !snapshot->empty())
                {
                    // Resolve our logical ID first
                    juce::uint32 myLogicalId = storedLogicalId;
                    if (myLogicalId == 0)
                    {
                        for (const auto& info : parentSynth->getModulesInfo())
                        {
                            if (parentSynth->getModuleForLogical(info.first) == this)
                            {
                                myLogicalId = info.first;
                                storedLogicalId = myLogicalId;
                                break;
                            }
                        }
                    }
                    
                    // Find connection to our input channel 0
                    if (myLogicalId != 0)
                    {
                        for (const auto& conn : *snapshot)
                        {
                            if (conn.dstLogicalId == myLogicalId && conn.dstChan == 0)
                            {
                                sourceId = conn.srcLogicalId;
                                cachedResolvedSourceId = sourceId; // Cache it
                                break;
                            }
                        }
                    }
                }
                
                // FALLBACK: If connection resolution didn't work, try to find ANY video source with frames
                if (sourceId == 0)
                {
                    for (const auto& info : parentSynth->getModulesInfo())
                    {
                        juce::String moduleType = info.second.toLowerCase();
                        if (moduleType.contains("video") || moduleType.contains("webcam") || 
                            moduleType == "video_file_loader")
                        {
                            cv::Mat testFrame = VideoFrameManager::getInstance().getFrame(info.first);
                            if (!testFrame.empty())
                            {
                                sourceId = info.first;
                                cachedResolvedSourceId = sourceId;
                                break;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // If processBlock() has set sourceId, clear cached resolved ID (no longer needed)
            if (cachedResolvedSourceId != 0 && cachedResolvedSourceId != sourceId)
                cachedResolvedSourceId = 0;
        }
        
        // Fetch frame (always attempt, even if sourceId == 0)
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        // ... rest of processing ...
    }
}
```

**Benefits:**
- ✅ Resolves source ID immediately during XML load
- ✅ Works before `processBlock()` runs
- ✅ Caches resolved ID to avoid repeated lookups
- ✅ Fallback search ensures frames are found even if connection graph isn't ready
- ✅ Automatically switches to `processBlock()`-provided ID when available

**Trade-offs:**
- ⚠️ More complex implementation
- ⚠️ Requires access to `parentSynth` and `getConnectionSnapshot()`
- ⚠️ Slightly more overhead (but negligible)

**When to use:**
- Use this approach for modules that need immediate frame access during XML load
- Combine with Approach 1 and 2 for best results
- Particularly useful for modules with heavy initialization (e.g., ML models) that need frames while loading

### Approach 4: Ensure Video Opens in prepareToPlay

Ensure `VideoFileLoaderModule` opens the video file immediately in `prepareToPlay()`:

```cpp
void VideoFileLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // ... existing code ...
    
    startThread(juce::Thread::Priority::normal);
    
    // CRITICAL: If videoFileToLoad is set (from XML load), ensure it opens immediately
    if (videoFileToLoad.existsAsFile() && !currentVideoFile.existsAsFile())
    {
        // Don't wait for thread - open synchronously (blocking, but only on first load)
        // OR: Set a flag that thread checks immediately
        needsImmediateOpen.store(true);
    }
}
```

**Benefits:**
- ✅ Video opens before first frame request
- ✅ Reduces race condition window

**Trade-offs:**
- ⚠️ Blocking operation in `prepareToPlay()` (bad for audio thread)
- ⚠️ Doesn't solve the `sourceId == 0` problem in downstream modules
- ⚠️ More complex synchronization

---

## Recommended Solution

**Use Approach 1** (Remove Early Exit) combined with **Approach 2** (Frame Caching) for the best user experience. For modules that need immediate source ID resolution, also use **Approach 3** (Connection Graph Resolution):

### Basic Solution (Simple Modules)

For simple video processing modules:

1. Remove the `if (sourceId == 0)` early exit
2. Add frame caching like `ColorTrackerModule`
3. Handle empty frames gracefully throughout the processing pipeline

This ensures:
- ✅ Video appears immediately after load
- ✅ Works even when connections aren't established yet
- ✅ Handles temporary frame unavailability gracefully
- ✅ Consistent with existing patterns in the codebase

### Advanced Solution (Complex Modules)

For modules with heavy initialization (e.g., ML models) or that need immediate frame access:

1. Implement Approach 1 and 2 (remove early exit, add frame caching)
2. **Add connection graph resolution** (Approach 3):
   - Resolve source ID from `getConnectionSnapshot()` when `sourceId == 0`
   - Cache the resolved source ID to avoid repeated lookups
   - Add fallback to search all video source modules
   - Always fetch and cache frames before checking if model/processing is ready
   - Update GUI independently of passthrough (even if `frameId == 0`)
3. **Ensure logical ID resolution happens continuously** in the loop, not just once at thread start

This ensures:
- ✅ Source ID is resolved immediately during XML load
- ✅ Frames are available even before `processBlock()` runs
- ✅ GUI updates immediately, even if passthrough isn't ready yet
- ✅ Works reliably even with complex initialization sequences

**Example:** `PoseEstimatorModule` uses this advanced approach to ensure video appears immediately after XML load, even while the ML model is still loading.

---

## Implementation Checklist

### Basic Implementation (Simple Modules)

- [ ] Remove `if (sourceId == 0)` check from `run()` method
- [ ] Add `lastFrameBgr` member variable and `frameLock` critical section
- [ ] Implement frame caching logic (copy on success, use cache on empty)
- [ ] Ensure frame fetching happens before any processing checks
- [ ] Test loading preset with video chain
- [ ] Verify video appears on first play click
- [ ] Verify no play/stop/play cycle required
- [ ] Test with paused video source
- [ ] Test with disconnected video source

### Advanced Implementation (Complex Modules)

- [ ] All items from Basic Implementation
- [ ] Add `cachedResolvedSourceId` member variable
- [ ] Implement connection graph resolution in `run()` loop
- [ ] Resolve logical ID continuously in loop (not just once)
- [ ] Add fallback video source search
- [ ] Ensure GUI updates happen independently of passthrough
- [ ] Test with XML load scenarios
- [ ] Test with delayed model/initialization
- [ ] Verify caching works correctly

---

## Related Modules

This pattern should be applied to all video processing modules that depend on upstream video sources:

- ✅ `ColorTrackerModule` - Implements basic pattern (Approach 1 + 2)
- ✅ `PoseEstimatorModule` - Implements advanced pattern (Approach 1 + 2 + 3)
- ❌ `VideoDrawImpactModuleProcessor` - Needs fix
- ❓ `VideoFXModule` - Should be checked
- ❓ `CropVideoModule` - Should be checked
- ❓ Other video processing modules - Should be audited

---

## Testing Procedure

1. **Create a preset** with `VideoFileLoaderModule` → `VideoDrawImpactModuleProcessor`
2. **Save the preset** to XML
3. **Close and reopen** the application
4. **Load the preset** from XML
5. **Click play** (first time)
6. **Expected:** Video should appear immediately in the impact node
7. **Actual (before fix):** Video does not appear until play → stop → play

---

## Code References

- `juce/Source/audio/modules/ColorTrackerModule.cpp` - Basic implementation (Approach 1 + 2)
- `juce/Source/audio/modules/PoseEstimatorModule.cpp` - Advanced implementation (Approach 1 + 2 + 3)
- `juce/Source/audio/modules/VideoDrawImpactModuleProcessor.cpp` - Needs fix
- `juce/Source/audio/modules/VideoFileLoaderModule.cpp` - Video source module
- `juce/Source/audio/graph/ModularSynthProcessor.cpp` - Load sequence, `getConnectionSnapshot()`
- `juce/Source/video/VideoFrameManager.h` - Frame sharing system

## Key Learnings from PoseEstimatorModule Fix

1. **Connection Graph Resolution**: `getConnectionSnapshot()` is available during XML load and can be used to resolve source IDs before `processBlock()` runs.

2. **Caching is Critical**: Cache both the resolved source ID and the last good frame to avoid repeated lookups and provide fallback frames.

3. **Continuous Resolution**: Don't resolve logical ID or source ID just once at thread start. Keep resolving in the loop until successful, as `parentSynth` and connection graph may not be ready immediately.

4. **Frame Fetching Before Checks**: Always fetch and cache frames before checking if processing is ready (e.g., model loaded). This ensures frames are available even during initialization.

5. **GUI Independent of Passthrough**: Update GUI frames even if `frameId == 0` (passthrough not ready). This ensures visual feedback immediately.

6. **Fallback Search**: If connection resolution fails, search all video source modules for one producing frames. This handles edge cases where connection graph isn't ready yet.

7. **Timing Matters**: During XML load, `prepareToPlay()` is called, starting threads, but `processBlock()` hasn't run yet. The thread must be able to work with `sourceId == 0` until `processBlock()` sets it.

---

**End of Guide**

