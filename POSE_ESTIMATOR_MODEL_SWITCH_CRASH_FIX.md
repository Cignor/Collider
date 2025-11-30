# Pose Estimator Model Switch Crash - Root Cause and Solution

## The Problem

When switching models in `PoseEstimatorModule` while video is actively processing (connected video input), the application crashes during model loading. The crash occurs in `cv::dnn::readNetFromCaffe()` when loading the new model.

**Key observation**: Model switching works fine when no video is connected. It only crashes when video is actively being processed.

## Root Cause Analysis

### The Core Issue

The crash happens because:

1. **Video processing loop is actively running** at ~15 FPS, continuously:
   - Fetching frames from `VideoFrameManager`
   - Processing frames through CUDA-enabled `cv::dnn::Net`
   - CUDA kernels are executing on the GPU

2. **When model switch is requested**:
   - The old `cv::dnn::Net` object is destroyed (releases CUDA resources)
   - New `cv::dnn::Net` is created via `readNetFromCaffe()` (allocates CUDA resources)
   - This happens **while CUDA operations from the old net may still be queued/executing**

3. **CUDA context corruption**:
   - Destroying net while CUDA kernels are still running causes context corruption
   - Creating new net while old context is corrupted causes crash
   - This is a **race condition** between CUDA operations and net destruction/creation

### Why Previous Attempts Failed

Multiple approaches were tried, all of which failed:

1. **Mutex protection**: Protected net access with mutex - doesn't help because CUDA operations are asynchronous
2. **Thread stopping/restarting**: Stopped processing thread, loaded model, restarted thread - too complex, still crashed
3. **Delays/waiting**: Added `wait(100)`, `wait(200)` etc. - timing-based solution is unreliable, still crashed
4. **Explicit net destruction**: Separated `net = cv::dnn::Net()` from `loadModel()` - helped but still crashed because video was still being processed
5. **Global CUDA cache**: Cached `getCudaEnabledDeviceCount()` to prevent concurrent queries - good optimization but didn't fix the crash

The fundamental issue: **We never actually stopped the video processing before switching models**. Even with `modelLoaded = false`, the loop continued fetching frames, and CUDA operations from previous frames were still executing.

## The Simple Solution

**Mute video input → Mute video frame processing → Change model → Resume video**

The solution is to:
1. **Stop fetching/processing video frames** when switch is detected
2. **Wait for CUDA operations to complete** (by not starting new ones)
3. **Switch the model** (destroy old net, create new net)
4. **Resume video processing**

### Implementation Strategy

```cpp
while (!threadShouldExit())
{
    // Check for model switch
    int toLoad = requestedModelIndex.exchange(-1);
    if (toLoad != -1)
    {
        // STEP 1: Mute video input - stop fetching frames
        // (Skip all frame operations below)
        
        // STEP 2: Mute video frame processing - ensure no CUDA ops start
        {
            std::lock_guard<std::mutex> lock(netMutex);
            modelLoaded = false;  // Prevents all frame processing
            if (!net.empty())
                net = cv::dnn::Net();  // Destroy old net
        }
        
        // STEP 3: Wait - gives CUDA time to finish cleanup
        // Since modelLoaded=false, loop will skip frame ops and wait
        wait(100);
        wait(100);  // Multiple waits to ensure CUDA cleanup
        
        // STEP 4: Change model
        {
            std::lock_guard<std::mutex> lock(netMutex);
            loadModel(toLoad);  // Creates new net
        }
        
        continue;  // Skip frame processing this iteration
    }
    
    // If no model, skip frame operations
    if (!modelLoaded)
    {
        wait(33);
        continue;
    }
    
    // STEP 5: Resume video - normal frame processing
    // Fetch frames, process with net, etc.
    // ...
}
```

**Key points**:
- `modelLoaded = false` **before** destroying net ensures no new CUDA operations start
- Destroy old net **explicitly** (`net = cv::dnn::Net()`) to trigger CUDA cleanup
- **Wait** while `modelLoaded = false` so loop skips frame operations (giving CUDA time)
- Load new model **after** wait period
- Loop naturally resumes video processing when `modelLoaded = true`

## Files to Modify

### Primary File
- `juce/Source/audio/modules/PoseEstimatorModule.cpp`
  - Function: `PoseEstimatorModule::run()` (the main processing loop)
  - Lines: ~280-310 (model switch handling)
  - Lines: ~400+ (frame processing - should be skipped when `modelLoaded = false`)

### Key Variables
- `modelLoaded` (bool): Flag that controls whether video processing happens
- `net` (cv::dnn::Net): The CUDA-enabled neural network object
- `netMutex` (std::mutex): Protects `net` from concurrent access
- `requestedModelIndex` (std::atomic<int>): Signal from UI thread to switch models

## Testing

To test the fix:
1. Load a preset with Pose Estimator node connected to video input
2. Ensure video is playing/processing (should see pose detection working)
3. Switch models via the UI dropdown
4. Should switch smoothly without crash

If it still crashes:
- Increase wait time (try 200ms, 300ms)
- Check that frame fetching is actually skipped when `modelLoaded = false`
- Verify no other threads are accessing CUDA during model switch

## Additional Notes

- The global CUDA cache (`CudaDeviceCountCache`) is still valuable - it prevents concurrent CUDA queries, but doesn't fix the core race condition
- Other modules (ColorTracker, ObjectDetector, etc.) have similar issues but may not crash because they don't switch models dynamically
- Consider applying similar pattern to other modules if they support runtime model switching

