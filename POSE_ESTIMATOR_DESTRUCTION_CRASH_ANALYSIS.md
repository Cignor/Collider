# Pose Estimator Module Destruction Crash - Analysis

## Problem Summary

When loading a new preset after playing a preset with a `pose_estimator` module (specifically `video_drumbox.xml`), the application crashes. The crash occurs during module destruction when switching presets.

## Root Cause

The crash is caused by **improper cleanup of CUDA resources** when the `PoseEstimatorModule` is destroyed. This is similar to the model switch crash documented in `POSE_ESTIMATOR_MODEL_SWITCH_CRASH_FIX.md`, but occurs during module destruction instead of model switching.

### Sequence of Events

1. **First preset loads successfully** (`video_drumbox.xml`)
   - Contains a `pose_estimator` module with CUDA/GPU enabled
   - Processing thread starts and loads CUDA model
   - Thread processes video frames at ~15 FPS using CUDA kernels

2. **User loads second preset** (`sample_scrubbing.xml`)
   - Does NOT contain a `pose_estimator` module
   - `clearAll()` is called to remove all modules from the first preset

3. **Module destruction sequence**:
   - `clearAll()` removes all nodes via `internalGraph->removeNode()`
   - JUCE calls `releaseResources()` on `PoseEstimatorModule`
   - `releaseResources()` signals thread exit and waits up to 5 seconds
   - Module destructor runs: `~PoseEstimatorModule()` calls `stopThread(5000)`
   - **CRASH**: `cv::dnn::Net` object (holding CUDA resources) is destroyed

4. **Why it crashes**:
   - CUDA operations may still be queued or executing when the net is destroyed
   - The processing thread might be in the middle of a frame processing operation
   - Destroying the net while CUDA kernels are running corrupts the CUDA context
   - This causes a crash in OpenCV's CUDA cleanup code

## Current Implementation Issues

### Destructor (`PoseEstimatorModule::~PoseEstimatorModule()`)
```cpp
PoseEstimatorModule::~PoseEstimatorModule() { stopThread(5000); }
```

**Problems**:
- Only stops the thread, doesn't clean up CUDA resources properly
- Doesn't set `modelLoaded = false` to prevent new CUDA operations
- Doesn't wait for in-flight CUDA operations to complete
- Doesn't explicitly clear the `cv::dnn::Net` before destruction

### releaseResources()
```cpp
void PoseEstimatorModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}
```

**Problems**:
- Signals thread exit but doesn't ensure frame processing stops
- Doesn't set `modelLoaded = false` to prevent new operations
- Doesn't wait for CUDA operations to complete
- Doesn't explicitly clear the net

### Thread Loop (`run()`)
- The thread loop checks `modelLoaded` before processing frames
- However, when destruction happens, the net might be destroyed while:
  - A frame is being processed (inside the mutex-protected section)
  - CUDA kernels are still executing asynchronously

## Solution

Apply similar cleanup logic as the model switch fix, but for module destruction:

1. **Stop frame processing immediately**
   - Set `modelLoaded = false` to prevent new CUDA operations from starting
   - This causes the thread loop to skip frame processing

2. **Wait for in-flight operations to complete**
   - Wait a short period (100-200ms) to allow queued CUDA operations to finish
   - The thread loop will naturally skip processing when `modelLoaded = false`

3. **Explicitly destroy the net under mutex protection**
   - Acquire `netMutex` to ensure no operations are using the net
   - Explicitly clear/destroy the net: `net = cv::dnn::Net()`
   - This triggers CUDA cleanup while we still control the lifecycle

4. **Stop the thread**
   - Signal thread exit and wait for it to stop

## Implementation Details

### Modified `releaseResources()`
```cpp
void PoseEstimatorModule::releaseResources()
{
    // Just stop the thread - don't destroy resources here (they'll be reused on next prepareToPlay)
    // Only the destructor should do final CUDA cleanup
    signalThreadShouldExit();
    stopThread(5000);
}
```

**IMPORTANT**: `releaseResources()` is called during normal operation (pause/stop), so it must NOT:
- Block the audio thread with sleeps
- Destroy CUDA resources (they need to persist for resume)
- Set modelLoaded=false (model should remain loaded for reuse)

### Modified Destructor
```cpp
PoseEstimatorModule::~PoseEstimatorModule()
{
    // Ensure cleanup happens (in case releaseResources wasn't called)
    {
        std::lock_guard<std::mutex> lock(netMutex);
        modelLoaded = false;
    }
    juce::Thread::sleep(200);
    {
        std::lock_guard<std::mutex> lock(netMutex);
        if (!net.empty())
            net = cv::dnn::Net();
    }
    stopThread(5000);
}
```

### Thread Loop Behavior
The existing thread loop already checks `modelLoaded`:
- When `modelLoaded = false`, it skips frame processing
- This naturally prevents new CUDA operations from starting
- The wait period gives queued operations time to complete

## Files to Modify

1. **`juce/Source/audio/modules/PoseEstimatorModule.cpp`**
   - Modify `releaseResources()` method (lines ~238-242)
   - Modify `~PoseEstimatorModule()` destructor (line ~230)

## Testing

To test the fix:
1. Load `video_drumbox.xml` preset (contains pose_estimator with CUDA)
2. Play the preset (ensure video is processing and pose detection is working)
3. Load a different preset (e.g., `sample_scrubbing.xml` which has no pose_estimator)
4. Should load smoothly without crash

## Additional Considerations

- The wait time (200ms) might need adjustment based on testing
- Similar issues might exist in other CUDA-enabled modules (ColorTracker, ObjectDetector, etc.)
- Consider applying similar cleanup patterns to other modules that use CUDA resources

