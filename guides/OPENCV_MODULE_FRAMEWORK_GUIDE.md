# OpenCV Module Framework Guide

## Overview

This guide explains the `OpenCVModuleProcessor` base class framework, which provides a robust, real-time-safe architecture for integrating computer vision into your modular synthesizer.

## Architecture

### The Threading Model

The framework uses a **three-thread architecture** to ensure real-time audio safety:

```
┌─────────────────────┐
│   Video Thread      │  Low-priority background thread
│   (OpenCV runs here)│  • Captures video frames
│                     │  • Performs CV analysis
└──────────┬──────────┘  • No real-time constraints
           │
           │ Lock-Free FIFO (ResultStruct)
           ▼
┌─────────────────────┐
│   Audio Thread      │  Real-time thread
│   (processBlock)    │  • Reads analysis results
│                     │  • Generates CV signals
└──────────┬──────────┘  • MUST NOT BLOCK
           │
           │ (no direct connection)
           │
┌──────────▼──────────┐
│   GUI Thread        │  Main UI thread
│   (getLatestFrame)  │  • Displays video preview
│                     │  • Uses locked access
└─────────────────────┘  • No real-time constraints
```

### Key Design Principles

1. **Lock-Free Audio Path**: The audio thread NEVER blocks waiting for video data. It uses `juce::AbstractFifo` for wait-free reads.

2. **Minimal Data Transfer**: Only small `ResultStruct` objects (a few floats) cross thread boundaries, not large video frames.

3. **Template Flexibility**: Each module defines its own `ResultStruct` to transfer exactly the data it needs.

4. **Separation of Concerns**: OpenCV code is completely isolated from audio code.

---

## Creating a New Vision Module

### Step 1: Define Your Result Structure

Create a simple POD (Plain Old Data) struct to hold your analysis results:

```cpp
struct MyModuleResult
{
    float valueA = 0.0f;
    float valueB = 0.0f;
    // Add whatever data your module needs to output
};
```

**Rules for ResultStruct:**
- Keep it small (a few floats/ints)
- Make it copyable
- No pointers, no heap allocations
- No complex objects (no juce::String, std::vector, etc.)

### Step 2: Inherit from OpenCVModuleProcessor

```cpp
class MyVisionModuleProcessor : public OpenCVModuleProcessor<MyModuleResult>
{
public:
    MyVisionModuleProcessor()
        : OpenCVModuleProcessor<MyModuleResult>("MyModuleName")
    {
        // Configure your I/O buses
        BusesProperties buses;
        buses.addBus(false, "Output", 
                     juce::AudioChannelSet::discreteChannels(2), true);
        setBusesLayout(buses);
    }

    const juce::String getName() const override 
    { 
        return "My Vision Module"; 
    }

protected:
    // Implement the two pure virtual methods...
};
```

### Step 3: Implement processFrame()

This is where your OpenCV code lives. It runs on the background thread:

```cpp
MyModuleResult processFrame(const cv::Mat& inputFrame) override
{
    MyModuleResult result;

    // Your OpenCV analysis code here
    cv::Mat gray;
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    
    // Example: Calculate average brightness
    cv::Scalar meanBrightness = cv::mean(gray);
    result.valueA = static_cast<float>(meanBrightness[0]) / 255.0f;

    // More analysis...
    result.valueB = /* ... */;

    return result;
}
```

**Guidelines:**
- ✅ DO: Use any OpenCV functions
- ✅ DO: Store member variables for frame-to-frame tracking
- ✅ DO: Take your time - this thread is low-priority
- ❌ DON'T: Access audio buffers
- ❌ DON'T: Try to synchronize with audio timing

### Step 4: Implement consumeResult()

This is where you convert vision data to CV signals. It runs on the audio thread:

```cpp
void consumeResult(const MyModuleResult& result, 
                   juce::AudioBuffer<float>& outputBuffer) override
{
    if (outputBuffer.getNumChannels() < 2)
        return;

    const int numSamples = outputBuffer.getNumSamples();

    // Output result.valueA on channel 0
    std::fill(outputBuffer.getWritePointer(0), 
              outputBuffer.getWritePointer(0) + numSamples, 
              result.valueA);

    // Output result.valueB on channel 1
    std::fill(outputBuffer.getWritePointer(1), 
              outputBuffer.getWritePointer(1) + numSamples, 
              result.valueB);
}
```

**Guidelines:**
- ✅ DO: Write to output buffers
- ✅ DO: Use the `result` parameter
- ✅ DO: Keep processing minimal and fast
- ❌ DON'T: Call OpenCV functions
- ❌ DON'T: Allocate memory
- ❌ DON'T: Use locks or blocking operations

---

## Example: Movement Detector Module

See `MovementDetectorModuleProcessor.h` for a complete, working example that:

1. **Defines a custom result**: `MovementResult` with 3 float values
2. **Implements processFrame()**: Uses optical flow to detect motion
3. **Implements consumeResult()**: Outputs motion data as CV signals

**Output pins:**
- Pin 0: Motion Amount (0.0 - 1.0)
- Pin 1: Horizontal Flow (-1.0 - 1.0)
- Pin 2: Vertical Flow (-1.0 - 1.0)

---

## Advanced Features

### Accessing Video Preview in GUI

The base class provides `getLatestFrame()` for UI display:

```cpp
void MyComponent::paint(juce::Graphics& g)
{
    juce::Image frame = myModule->getLatestFrame();
    if (!frame.isNull())
    {
        g.drawImage(frame, getLocalBounds().toFloat());
    }
}
```

### Configuring Frame Rate

Edit the `wait(66)` call in `OpenCVModuleProcessor::run()`:
- `wait(33)` ≈ 30 FPS
- `wait(66)` ≈ 15 FPS (default, good balance)
- `wait(100)` ≈ 10 FPS (very CPU-friendly)

### Handling Camera Errors

The current implementation opens camera 0 by default. To customize:

```cpp
// In your derived class, before calling OpenCVModuleProcessor constructor:
// You could add a camera index parameter and pass it to base class
```

Future versions could add camera selection and error reporting.

---

## Integration with Your Project

### Adding to CMakeLists.txt

The modules are header-only templates, so no changes are needed to `CMakeLists.txt` unless you create `.cpp` implementation files.

### Registering with PinDatabase

Add your new module to `PinDatabase.cpp`:

```cpp
case ModuleType::MovementDetector:
    return std::make_unique<MovementDetectorModuleProcessor>();
```

### Adding to UI

Update your node editor to include the new module type in the available modules list.

---

## Performance Considerations

### CPU Usage

Each OpenCV module runs its own video capture and processing thread. Guidelines:

- **1-2 modules**: No issues on modern CPUs
- **3-4 modules**: Monitor CPU usage
- **5+ modules**: Consider sharing video source between modules

### Memory Usage

Video frames are ~2MB each (1920×1080 BGRA). The framework keeps:
- 1 frame in the video thread
- 1 frame for GUI display
- 16 small `ResultStruct` copies in FIFO

Total per module: ~4-5 MB

---

## Troubleshooting

### "Camera not found" on startup

**Cause**: `cv::VideoCapture::open(0)` failed

**Solutions:**
- Check if another application is using the camera
- Try a different camera index (1, 2, etc.)
- Verify OpenCV was built with video support

### Module causes audio dropouts

**Cause**: `consumeResult()` is doing too much work

**Solutions:**
- Move complex calculations to `processFrame()`
- Simplify signal generation
- Use profiling to identify bottlenecks

### Video frame is corrupted or wrong colors

**Cause**: Color space conversion issue

**Solutions:**
- Verify `cv::COLOR_BGR2BGRA` is correct for your camera
- Check `memcpy` size calculation in `updateGuiFrame()`

---

## Future Enhancements

Potential improvements to the framework:

1. **Shared Video Source**: Multiple modules reading from one camera
2. **Camera Selection UI**: Choose camera from available devices
3. **Frame Rate Control**: Per-module FPS settings
4. **Recording**: Save processed video to disk
5. **Network Streaming**: Send video over network (OSC/WebRTC)
6. **GPU Acceleration**: Use OpenCV's CUDA/OpenCL support

---

## See Also

- `OpenCVModuleProcessor.h` - Base class implementation
- `MovementDetectorModuleProcessor.h` - Example concrete module
- `AnimationModuleProcessor.cpp` - Similar async architecture for 3D animation
- OpenCV Documentation: https://docs.opencv.org/4.9.0/


