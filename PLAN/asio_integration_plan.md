# ASIO Integration Implementation Plan

## Executive Summary

This document outlines the complete implementation plan for integrating Steinberg's ASIO (Audio Stream Input/Output) support into the Pikon Raditsz (PresetCreatorApp) and Collider Audio Engine applications. ASIO will provide ultra-low latency audio performance, critical for real-time audio processing, especially when combined with the existing video processing capabilities (pose estimation, webcam modules, etc.).

---

## 1. Current State Analysis

### 1.1 Existing Audio Architecture

**Current JUCE Modules Linked:**
- `juce::juce_audio_devices` - Device management
- `juce::juce_audio_processors` - Plugin hosting
- `juce::juce_audio_utils` - Audio utilities
- `juce::juce_audio_formats` - Audio file I/O
- `juce::juce_dsp` - DSP operations

**Current Audio Backend (Windows):**
- **Default**: Windows Audio Session API (WASAPI) or DirectSound
- **Typical Latency**: 20-100ms depending on buffer size and driver
- **No ASIO**: Currently using standard Windows audio APIs only

### 1.2 Why ASIO is Critical for This Project

Given the application's unique requirements:

1. **Real-time CV-to-Audio Mapping**: Pose estimation, hand tracking, face tracking modules generate CV signals that need immediate audio response
2. **Low-Latency Requirement**: Video processing at 30-60 FPS requires audio buffer sizes of 128-256 samples to maintain sync
3. **Professional Audio Interface Support**: Users with audio interfaces (Focusrite, Universal Audio, RME, etc.) expect ASIO support
4. **Multi-channel I/O**: Modular synth architecture benefits from multi-channel audio interfaces
5. **VST3 Plugin Hosting**: VST plugins expect ASIO-level latencies for proper performance

**Expected Improvements:**
- Latency: 20-100ms → 2-10ms (at 44.1kHz, 128 sample buffer)
- Audio-Video Sync: Significantly improved
- Professional Workflow: Industry-standard integration

---

## 2. Prerequisites and Dependencies

### 2.1 ASIO SDK Acquisition

**Source**: Steinberg ASIO SDK  
**License**: Proprietary (free for developers, redistribution allowed)  
**Download**: https://www.steinberg.net/asiosdk

**Required Files from SDK:**
```
asiosdk/
├── common/
│   ├── asio.h              # Main ASIO interface
│   ├── asio.cpp            # ASIO implementation
│   ├── asiodrivers.h       # Driver enumeration
│   ├── asiodrivers.cpp     # Driver enumeration implementation
│   ├── asiolist.h          # Driver list management
│   ├── asiolist.cpp
│   └── combase.h           # COM interface base
├── host/
│   ├── asiodrvr.h          # Driver wrapper
│   └── asiodrvr.cpp
└── ASIO SDK Release Notes.pdf
```

**Installation Location** (Recommended):
```
h:\0000_CODE\01_collider_pyo\vendor\asiosdk\
```

### 2.2 Legal Compliance

**ASIO Trademark Usage:**
- ✓ Can use "ASIO support" in feature lists
- ✓ Can redistribute ASIO SDK headers/source in your build
- ✗ Cannot use "ASIO" as part of your product name
- ✗ Cannot imply official Steinberg endorsement

**Redistribution:**
- ASIO SDK code can be compiled into your application
- No separate ASIO runtime DLL distribution needed
- Users install ASIO drivers separately (from hardware manufacturers)

### 2.3 System Requirements

**Operating System Support:**
- Windows Vista and later (primary target)
- macOS: JUCE uses CoreAudio instead (already optimal)
- Linux: JUCE uses ALSA/JACK (ASIO not applicable)

**Development Requirements:**
- Visual Studio 2019 or later (for Windows builds)
- CMake 3.22+ (already in use)
- JUCE 7.0.9 (already in use)

---

## 3. Implementation Strategy

### 3.1 Integration Approach

**Method**: JUCE Built-in ASIO Support

JUCE's `juce_audio_devices` module already contains ASIO integration code, but it's **conditionally compiled** based on:
1. Platform detection (Windows only)
2. `JUCE_ASIO` preprocessor definition
3. Availability of ASIO SDK headers

**Integration Level**: Zero new ASIO code needed - just configuration!

### 3.2 Configuration Hierarchy

```
CMakeLists.txt (Build Configuration)
    ↓
JUCE Module Configuration (Preprocessor Defines)
    ↓
JUCE Audio Device Manager (Runtime Selection)
    ↓
User Settings (Audio Preferences UI)
    ↓
ASIO Driver (Hardware Interface)
```

### 3.3 Build System Changes

The integration requires modifications to:
1. **CMakeLists.txt** (PRIMARY)
   - Add ASIO SDK path declaration
   - Enable JUCE_ASIO preprocessor definition
   - Add ASIO SDK includes to target

2. **Audio Device Initialization Code** (SECONDARY - UI improvement)
   - Optional: Set ASIO as preferred backend
   - Optional: Add ASIO-specific settings UI

3. **User Documentation** (TERTIARY)
   - Setup guide for ASIO drivers
   - Troubleshooting section

---

## 4. Detailed Implementation Steps

### Phase 1: SDK Setup and Verification

#### Step 1.1: Download and Extract ASIO SDK

**Action**: Acquire latest ASIO SDK from Steinberg

**Process**:
1. Navigate to https://www.steinberg.net/asiosdk
2. Accept license agreement
3. Download SDK (typically named `asiosdk_[version].zip`)
4. Extract to vendor directory

**Expected Directory Structure**:
```
h:\0000_CODE\01_collider_pyo\
├── vendor\
│   ├── asiosdk\
│   │   ├── common\           # ASIO interface headers/source
│   │   ├── host\             # ASIO driver wrapper
│   │   ├── ASIO SDK License.txt
│   │   └── ASIO SDK Release Notes.pdf
│   ├── ffmpeg\
│   ├── piper\
│   └── cuda\
```

#### Step 1.2: Verify SDK Contents

**Required Files Checklist**:
- [ ] `common/asio.h`
- [ ] `common/asio.cpp`
- [ ] `common/asiodrivers.h`
- [ ] `common/asiodrivers.cpp`
- [ ] `common/asiolist.h`
- [ ] `common/asiolist.cpp`
- [ ] `host/asiodrvr.h`
- [ ] `host/asiodrvr.cpp`

**Validation**:
```cmd
dir /b h:\0000_CODE\01_collider_pyo\vendor\asiosdk\common\asio.h
dir /b h:\0000_CODE\01_collider_pyo\vendor\asiosdk\common\asio.cpp
```

If files are missing, SDK extraction failed or version is incorrect.

---

### Phase 2: CMake Configuration

#### Step 2.1: Add ASIO SDK Path to CMakeLists.txt

**Location**: `juce/CMakeLists.txt` (after line 596, before FFmpeg configuration)

**Purpose**: Define ASIO SDK location as a CMake variable

**Code Block to Add**:
```cmake
# ==============================================================================
# ASIO SDK Configuration (Windows-only low-latency audio)
# ==============================================================================
set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/../vendor/asiosdk" CACHE PATH "Path to ASIO SDK")

if(WIN32)
    if(EXISTS "${ASIO_SDK_DIR}/common/asio.h")
        set(ASIO_SDK_FOUND TRUE)
        message(STATUS "✓ ASIO SDK found at ${ASIO_SDK_DIR}")
        message(STATUS "  - ASIO low-latency audio support will be enabled")
    else()
        set(ASIO_SDK_FOUND FALSE)
        message(WARNING 
            "ASIO SDK not found at ${ASIO_SDK_DIR}\n"
            "Low-latency ASIO audio support will NOT be available.\n"
            "Download from https://www.steinberg.net/asiosdk\n"
            "Extract to: ${ASIO_SDK_DIR}"
        )
    endif()
else()
    # ASIO is Windows-only (macOS uses CoreAudio, Linux uses ALSA/JACK)
    set(ASIO_SDK_FOUND FALSE)
    message(STATUS "ASIO not applicable on this platform (Windows-only)")
endif()
```

**Rationale**:
- Graceful degradation if SDK is missing
- Clear status messages during CMake configuration
- Platform-aware (doesn't try to use ASIO on macOS/Linux)

#### Step 2.2: Configure JUCE ASIO Module

**Location**: Immediately after ASIO SDK detection block

**Purpose**: Tell JUCE where to find ASIO SDK headers

**IMPORTANT - VERIFIED JUCE INTEGRATION METHOD**:

After checking JUCE 7.0.9 documentation and CMake API, there is **NO** `juce_set_asio_sdk_path()` function in JUCE. The correct approach is:

1. **Set ASIO SDK path as CMake variable** (for include directories)
2. **Define `JUCE_ASIO=1`** via `target_compile_definitions`
3. **Add ASIO SDK include paths** manually to targets

**Correct Code Block to Add** (verified approach):
```cmake
# Configure ASIO SDK paths for manual integration
if(ASIO_SDK_FOUND)
    # Store ASIO SDK paths as global variables for later use
    set(ASIO_SDK_INCLUDE_DIRS
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
        CACHE INTERNAL "ASIO SDK include directories"
    )
    
    message(STATUS "  - ASIO SDK includes configured: ${ASIO_SDK_INCLUDE_DIRS}")
    
    # NOTE: We will enable JUCE_ASIO=1 per-target later using target_compile_definitions
    # This is the proper JUCE approach - no juce_set_asio_sdk_path() exists
endif()
```

**Why this approach?**:
- JUCE's `juce_audio_devices` module checks for `JUCE_ASIO` preprocessor definition
- When defined, it looks for ASIO headers in the include path
- We must manually add ASIO SDK headers to target include directories
- This is the standard documented JUCE pattern

#### Step 2.3: Enable JUCE_ASIO for Application Targets

**Location**: `target_compile_definitions` sections for both apps

**For ColliderApp** (around line 889):
```cmake
target_compile_definitions(ColliderApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ColliderApp,PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ColliderApp,VERSION>"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    $<$<BOOL:${USE_RUBBERBAND}>:USE_RUBBERBAND=1>
    $<$<NOT:$<BOOL:${USE_RUBBERBAND}>>:USE_RUBBERBAND=0>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:WITH_CUDA_SUPPORT=1>
    # NEW: Enable ASIO if SDK is found
    $<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>
)
```

**For PresetCreatorApp** (around line 1334):
```cmake
target_compile_definitions(PresetCreatorApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PresetCreatorApp,PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PresetCreatorApp,VERSION>"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    IMGUI_IMPL_JUCE_BEZEL=0
    IMGUI_ENABLE_VIEWPORTS
    IMGUI_DEFINE_MATH_OPERATORS
    IMNODES_NAMESPACE=ImNodes
    IMNODES_STATIC_DEFINE
    PRESET_CREATOR_UI=1
    $<$<BOOL:${USE_RUBBERBAND}>:USE_RUBBERBAND=1>
    $<$<NOT:$<BOOL:${USE_RUBBERBAND}>>:USE_RUBBERBAND=0>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:WITH_CUDA_SUPPORT=1>
    # NEW: Enable ASIO if SDK is found
    $<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>
)
```

**Explanation**:
- `$<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>` is a CMake generator expression
- Only defines `JUCE_ASIO=1` when `ASIO_SDK_FOUND` is TRUE
- JUCE's `juce_audio_devices` module detects this and compiles ASIO backend

#### Step 2.4: Add ASIO SDK Include Directories (if using Manual Method)

**Location**: `target_include_directories` for both apps (if not using `juce_set_asio_sdk_path`)

**For ColliderApp** (around line 937):
```cmake
target_include_directories(ColliderApp PRIVATE
    # ... existing includes ...
    # NEW: ASIO SDK headers (Windows only, conditional)
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/host>
)
```

**For PresetCreatorApp** (around line 1296):
```cmake
target_include_directories(PresetCreatorApp PRIVATE
    # ... existing includes ...
    # NEW: ASIO SDK headers (Windows only, conditional)
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/host>
)
```

**CRITICAL VERIFICATION**: Ensure `juce::juce_audio_devices` is already linked!

Both applications should already have this link:
```cmake
target_link_libraries(ColliderApp PRIVATE
    # ... existing links ...
    juce::juce_audio_devices  # REQUIRED for ASIO support
)

target_link_libraries(PresetCreatorApp PRIVATE
    # ... existing links ...
    juce::juce_audio_devices  # REQUIRED for ASIO support
)
```

**Check current CMakeLists.txt**: Looking at line 900 and line 1353, verify these links exist:
- ColliderApp: Links `juce::juce_audio_utils` (which includes `juce_audio_devices`)
- PresetCreatorApp: Links `juce::juce_audio_devices` explicitly

✅ **CONFIRMED**: Both apps already link required JUCE audio modules!

---

### Phase 2.5: JUCE Module Compile Definitions (Advanced)

#### Understanding JUCE Module Configuration

**How JUCE Processes ASIO**:
1. `juce_audio_devices` module checks for `JUCE_ASIO` at compile time
2. If defined, includes ASIO-specific source files (`juce_audio_devices/native/juce_win32_ASIO.cpp`)
3. Expects ASIO headers in include path (we provide via `target_include_directories`)

**Module Source Files** (for reference):
```
juce/modules/juce_audio_devices/
├── juce_audio_devices.h
├── juce_audio_devices.cpp
└── native/
    ├── juce_win32_ASIO.cpp          # Enabled when JUCE_ASIO=1
    ├── juce_win32_DirectSound.cpp   # Default Windows audio
    ├── juce_win32_WASAPI.cpp        # Windows modern audio
    └── juce_mac_CoreAudio.cpp       # macOS (always enabled)
```

**Our Configuration**:
- `JUCE_ASIO=1` → Compiles `juce_win32_ASIO.cpp`
- ASIO SDK in include path → Provides `asio.h`, `asiodrivers.h`, etc.
- Result: ASIO backend available at runtime

---

### Phase 3: Application-Level Configuration (Optional but Recommended)

#### Step 3.1: Set ASIO as Preferred Audio Backend

**Purpose**: Make ASIO the default audio device type when available

**File to Modify**: `Source/preset_creator/PresetCreatorApplication.h`

**Current Audio Initialization** (typical pattern):
```cpp
// In MainComponent or Application initialization
audioDeviceManager.initialiseWithDefaultDevices(0, 2); // 0 inputs, 2 outputs
```

**Enhanced ASIO-Preferred Initialization** (verified JUCE pattern):
```cpp
// In PresetCreatorApplication::initialise() or MainComponent constructor

// JUCE AudioDeviceManager setup
juce::AudioDeviceManager audioDeviceManager;

#if JUCE_ASIO
    // Try ASIO first on Windows
    DBG("Attempting to initialize ASIO audio backend...");
    
    // Set device type to ASIO before initialization
    audioDeviceManager.setCurrentAudioDeviceType("ASIO", true);
    
    // Get current device type to verify it was set
    juce::String currentType = audioDeviceManager.getCurrentAudioDeviceType();
    DBG("Current audio device type: " + currentType);
    
    if (currentType == "ASIO")
    {
        // ASIO type is available, now try to initialize with default ASIO device
        juce::String error = audioDeviceManager.initialise(
            0,      // number of input channels
            2,      // number of output channels
            nullptr,  // no saved XML state
            true,   // select default device if no state
            {},     // preferred default device name (empty = first available)
            nullptr // preferred device setup (nullptr = use defaults)
        );
        
        if (error.isEmpty())
        {
            DBG("✓ ASIO audio initialized successfully");
            
            // Log device details
            if (auto* device = audioDeviceManager.getCurrentAudioDevice())
            {
                DBG("  Device: " + device->getName());
                DBG("  Type: " + device->getTypeName());
                DBG("  Sample Rate: " + juce::String(device->getCurrentSampleRate()) + " Hz");
                DBG("  Buffer Size: " + juce::String(device->getCurrentBufferSizeSamples()) + " samples");
                
                // Calculate and log latency
                double latencyMs = (device->getCurrentBufferSizeSamples() / device->getCurrentSampleRate()) * 1000.0;
                DBG("  Estimated Latency: " + juce::String(latencyMs, 2) + " ms");
            }
        }
        else
        {
            // ASIO initialization failed
            DBG("⚠ ASIO initialization failed: " + error);
            DBG("  Falling back to DirectSound/WASAPI...");
            
            // Fall back to Windows Audio
            audioDeviceManager.setCurrentAudioDeviceType("Windows Audio", true);
            juce::String fallbackError = audioDeviceManager.initialiseWithDefaultDevices(0, 2);
            
            if (fallbackError.isNotEmpty())
            {
                DBG("❌ Fallback audio initialization also failed: " + fallbackError);
                // Show error to user
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Audio Initialization Failed",
                    "Could not initialize audio system:\n\n" + fallbackError +
                    "\n\nPlease check your audio device settings.",
                    "OK"
                );
            }
        }
    }
    else
    {
        // ASIO type not available (no ASIO support compiled in or no drivers installed)
        DBG("ASIO device type not available, using default audio backend");
        audioDeviceManager.initialiseWithDefaultDevices(0, 2);
    }
#else
    // ASIO not compiled in, use default device (DirectSound/WASAPI on Windows)
    DBG("ASIO support not compiled in, using default audio backend");
    audioDeviceManager.initialiseWithDefaultDevices(0, 2);
#endif

// Continue with rest of application initialization...
```

**Where to Add**:
- **PresetCreatorApp**: `Source/preset_creator/PresetCreatorApplication.cpp` - in `initialise()` method
- **ColliderApp**: `Source/app/MainApplication.cpp` - in `initialise()` method

**Note**: Check if these apps already have audio device initialization code and modify accordingly.

**Graceful Degradation Benefits**:
✓ Works on systems without ASIO drivers (falls back to DirectSound/WASAPI)
✓ Works if ASIO is not compiled in (preprocessor guard)
✓ Provides detailed logging for troubleshooting
✓ Shows user-friendly error messages

#### Step 3.2: Audio Settings UI Enhancements

**Current State**: PresetCreatorApp likely has audio settings somewhere in UI

**Enhancement**: Show ASIO-specific settings panel

**JUCE Built-in Component**:
```cpp
// JUCE provides AudioDeviceSelectorComponent for audio settings
#include <juce_audio_utils/juce_audio_utils.h>

class AudioSettingsComponent : public juce::Component
{
public:
    AudioSettingsComponent(juce::AudioDeviceManager& adm)
        : audioSetupComp(adm,
                         0,     // minimum input channels
                         256,   // maximum input channels
                         0,     // minimum output channels
                         256,   // maximum output channels
                         true,  // show MIDI input options
                         true,  // show MIDI output selector
                         true,  // show channels as stereo pairs
                         false) // hide advanced options by default
    {
        addAndMakeVisible(audioSetupComp);
        setSize(500, 450);
    }
    
    void resized() override
    {
        audioSetupComp.setBounds(getLocalBounds());
    }
    
private:
    juce::AudioDeviceSelectorComponent audioSetupComp;
};
```

**This component automatically shows**:
- Device type selector (ASIO, DirectSound, WASAPI, etc.)
- Available devices for selected type
- Sample rate selection
- Buffer size selection (critical for latency)
- Input/output channel routing
- Test tone generator

**Where to Add**:
- Preferences/Settings dialog
- Main menu: "Audio → Audio Settings..."
- Toolbar button

#### Step 3.3: Buffer Size Recommendations

**ASIO Buffer Sizes and Latency** (at 48kHz):

| Buffer Size | Latency | Use Case |
|-------------|---------|----------|
| 32 samples  | 0.67ms  | Extreme (pro hardware only) |
| 64 samples  | 1.33ms  | Professional monitoring |
| 128 samples | 2.67ms  | **Recommended for this app** |
| 256 samples | 5.33ms  | Safe default |
| 512 samples | 10.67ms | High CPU loads |
| 1024 samples| 21.33ms | Fallback |

**For Video-Audio Sync** (30 FPS video):
- Frame interval: 33.33ms
- Recommended buffer: 128-256 samples
- This ensures audio update rate >> video frame rate

**Suggested Default Configuration**:
```cpp
// Set preferred ASIO buffer size
auto* currentDevice = audioDeviceManager.getCurrentAudioDevice();
if (currentDevice != nullptr && currentDevice->getTypeName() == "ASIO")
{
    auto setup = currentDevice->getActiveAudioDeviceSetup();
    setup.bufferSize = 256;  // Safe default (adjust based on testing)
    currentDevice->setAudioDeviceSetup(setup, true);
}
```

---

### Phase 4: User-Facing Documentation

#### Step 4.1: ASIO Driver Installation Guide

**Location**: `USER_MANUAL/ASIO_Setup_Guide.md`

**Content Structure**:
```markdown
# ASIO Setup Guide for Pikon Raditsz

## What is ASIO?
Brief explanation of ASIO benefits

## Installing ASIO Drivers

### If you have an audio interface:
- Check manufacturer website for latest ASIO drivers
- Links to common manufacturers (Focusrite, Presonus, etc.)

### If you DON'T have an audio interface:
- Use ASIO4ALL (free universal ASIO driver)
- Download link and installation instructions

## Configuring ASIO in Pikon Raditsz

1. Open Preferences/Audio Settings
2. Select "ASIO" from device type dropdown
3. Choose your ASIO device
4. Set buffer size (recommended: 256 samples)
5. Click "Test" to verify

## Troubleshooting

### "No ASIO devices found"
- Install ASIO drivers first
- Restart Pikon Raditsz after driver installation

### "Cannot open ASIO device"
- Close other applications using the device
- Check ASIO driver control panel settings

### Crackling/Dropouts
- Increase buffer size
- Close background applications
- Disable Wi-Fi/Bluetooth (reduces DPC latency)
```

#### Step 4.2: Update Main User Manual

**File**: `USER_MANUAL/Audio_Configuration.md`

**Additions**:
- ASIO benefits section
- Comparison table: ASIO vs DirectSound vs WASAPI
- Performance tips for low-latency usage
- Recommended settings for different use cases

#### Step 4.3: In-App Help Integration

**Tooltip/Help Text Examples**:

```cpp
// When hovering over ASIO device type
"ASIO provides ultra-low latency (2-10ms) for professional audio interfaces. "
"Requires ASIO drivers from your audio interface manufacturer."

// When hovering over buffer size slider
"Lower buffer size = lower latency but higher CPU usage. "
"Recommended: 128-256 samples for real-time video-to-audio processing."

// If ASIO initialization fails
"Could not initialize ASIO device. "
"Make sure your audio interface is connected and ASIO drivers are installed. "
"Click 'Help' for ASIO setup guide."
```

---

## 5. Testing and Verification Plan

### Phase 5.1: Build Verification

#### Test 5.1.1: CMake Configuration
```cmd
cd h:\0000_CODE\01_collider_pyo\juce\build-ninja-release
cmake --fresh -G Ninja -DCMAKE_BUILD_TYPE=Release ..
```

**Expected Output**:
```
✓ ASIO SDK found at h:/0000_CODE/01_collider_pyo/vendor/asiosdk
  - ASIO low-latency audio support will be enabled
```

**If SDK not found**:
```
WARNING: ASIO SDK not found at h:/0000_CODE/01_collider_pyo/vendor/asiosdk
Low-latency ASIO audio support will NOT be available.
[instructions...]
```

#### Test 5.1.2: Compilation Check
```cmd
ninja PresetCreatorApp
```

**Success Indicators**:
- `juce_audio_devices` compiles ASIO backend files
- No linker errors related to ASIO symbols
- Executable builds successfully

**JUCE-Specific Verification**:

After compilation, verify ASIO backend was compiled by checking for ASIO-related object files:

```cmd
# Check for ASIO object files in build directory (PowerShell)
Get-ChildItem -Path "build-ninja-release" -Recurse -Filter "*ASIO*.obj"

# Or using CMD
dir /s /b build-ninja-release\*ASIO*.obj
```

**Expected Output** (examples):
```
build-ninja-release\_deps\juce-build\modules\juce_audio_devices\juce_audio_devices_ASIO.obj
build-ninja-release\PresetCreatorApp_artefacts\...\juce_win32_ASIO.obj
```

If no ASIO object files are found:
- ❌ `JUCE_ASIO` is not defined
- ❌ ASIO SDK paths not in include directories
- ❌ CMake configuration failed

**Build Log Verification**:

Check compiler output for ASIO includes:
```cmd
ninja -v PresetCreatorApp 2>&1 | findstr "ASIO"
```

**Expected patterns**:
```
-DJUCE_ASIO=1
-I"h:/0000_CODE/01_collider_pyo/vendor/asiosdk/common"
-I"h:/0000_CODE/01_collider_pyo/vendor/asiosdk/host"
```

If these aren't present in compiler flags:
- Check `ASIO_SDK_FOUND` variable in CMakeCache.txt
- Verify CMake configuration step completed successfully
- Reconfigure: `cmake --fresh ..`

#### Test 5.1.3: Preprocessor Verification

Create temporary test file: `juce/Source/test_asio_enabled.cpp`
```cpp
#include <iostream>

int main() {
#if JUCE_ASIO
    std::cout << "JUCE_ASIO is ENABLED" << std::endl;
    return 0;
#else
    std::cout << "JUCE_ASIO is DISABLED" << std::endl;
    return 1;
#endif
}
```

Compile separately to verify `JUCE_ASIO` is defined.

### Phase 5.2: Runtime Testing

#### Test 5.2.1: Device Enumeration Test

**Precondition**: ASIO driver installed (ASIO4ALL or hardware driver)

**Test Steps**:
1. Launch PresetCreatorApp
2. Open Audio Settings dialog
3. Check device type dropdown

**Expected Result**:
- "ASIO" appears in device type list
- Can select ASIO
- ASIO devices are listed

**Failure Case**:
- If "ASIO" type missing → JUCE_ASIO not defined or SDK not found
- If no devices listed → No ASIO drivers installed

#### Test 5.2.2: Audio Initialization Test

```cpp
// Add debug logging to audio initialization
DBG("Current audio device type: " + 
    audioDeviceManager.getCurrentAudioDeviceType());
    
auto* device = audioDeviceManager.getCurrentAudioDevice();
if (device != nullptr)
{
    DBG("Device name: " + device->getName());
    DBG("Device type: " + device->getTypeName());
    DBG("Sample rate: " + String(device->getCurrentSampleRate()));
    DBG("Buffer size: " + String(device->getCurrentBufferSizeSamples()));
    DBG("Input latency: " + String(device->getInputLatencyInSamples()) + " samples");
    DBG("Output latency: " + String(device->getOutputLatencyInSamples()) + " samples");
}
```

**Expected Values** (ASIO):
- Buffer size: 64-512 samples (user configurable)
- Total latency: 2-15ms (depending on buffer size)

**Compare to DirectSound**:
- Buffer size: 1024-4096 samples
- Total latency: 40-100ms

#### Test 5.2.3: Latency Measurement Test

**Method**: Loopback test with oscilloscope or audio analysis tool

**Setup**:
1. Connect audio output to input (physical loopback cable)
2. Generate impulse in software
3. Measure time until impulse returns
4. Subtract hardware propagation delay

**Tools**:
- LatencyMon (Windows DPC latency)
- RTL Utility (round-trip latency)
- Oblique Audio RTL Utility

**Target Metrics**:
- ASIO @ 128 samples: < 5ms total latency
- ASIO @ 256 samples: < 8ms total latency

#### Test 5.2.4: Real-World Performance Test

**Test Scenario**: Pose Estimator → Audio Modulation

1. Load preset with PoseEstimatorModule → VCO
2. Enable webcam
3. Move hand in front of camera
4. Observe audio response time

**Subjective Evaluation**:
- ASIO: Audio should feel "immediate" - hand motion = instant pitch change
- DirectSound: Noticeable lag between motion and sound

**Quantitative Test**:
- Record video of hand motion + audio output
- Analyze frame-by-frame in video editor
- Measure frames between motion start and audio change
- Convert to milliseconds (30 FPS = 33.33ms per frame)

### Phase 5.3: Stress Testing

#### Test 5.3.1: CPU Load Test

**Method**: Load complex preset with many modules

**Monitor**:
- Windows Performance Monitor: CPU usage, DPC latency
- Audio device buffer underruns (clicks/pops)
- LatencyMon: highest measured DPC latency

**Pass Criteria**:
- No audio dropouts at 256 samples buffer
- DPC latency < 2ms
- CPU usage < 80% on target hardware

#### Test 5.3.2: Multi-Device Test

**Scenario**: Multiple ASIO devices installed

**Test**:
1. Install multiple ASIO drivers (e.g., ASIO4ALL + hardware driver)
2. Switch between devices in audio settings
3. Verify each device initializes correctly
4. Check for crashes or device conflicts

#### Test 5.3.3: Driver Compatibility Test

**Common ASIO Drivers to Test**:
- ASIO4ALL (universal driver)
- Focusrite USB (if available)
- Behringer/Presonus (if available)
- RME (professional tier)

**For each driver**:
- Test basic initialization
- Test buffer size changes
- Test sample rate changes
- Test device enable/disable cycle

---

## 6. Potential Issues and Mitigation

### Issue 6.1: ASIO SDK Not Found

**Symptom**: CMake warning, no ASIO support compiled in

**Cause**: SDK not downloaded or incorrect path

**Detection**:
```
WARNING: ASIO SDK not found at [path]
```

**Resolution**:
1. Verify extraction: `dir h:\0000_CODE\01_collider_pyo\vendor\asiosdk\common\asio.h`
2. Check CMake variable: `ASIO_SDK_DIR` in CMakeCache.txt
3. Reconfigure: `cmake --fresh ..`

**Prevention**:
- Clear setup documentation
- Automated SDK existence check in build script
- CI/CD verification (if applicable)

### Issue 6.2: ASIO Device Already in Use

**Symptom**: "Cannot open ASIO device" error at runtime

**Cause**: ASIO is **exclusive access** - only one app can use ASIO device at a time

**Common Culprits**:
- DAWs (Ableton, FL Studio, Reaper, etc.)
- Other JUCE apps
- Browser (if using ASIO4ALL with browser audio)

**Resolution**:
1. Close competing applications
2. Check ASIO driver control panel for locked processes
3. Implement better error message:
```cpp
if (audioInitError.contains("already in use"))
{
    AlertWindow::showMessageBox(
        AlertWindow::WarningIcon,
        "ASIO Device Busy",
        "The ASIO device is currently in use by another application.\n\n"
        "Please close other audio applications and try again.\n\n"
        "Falling back to DirectSound for now.",
        "OK"
    );
    // Fall back to DirectSound/WASAPI
    audioDeviceManager.setCurrentAudioDeviceType("Windows Audio", true);
}
```

### Issue 6.3: Buffer Underruns (Crackling)

**Symptom**: Intermittent clicks, pops, or audio dropouts

**Cause**: Audio callback taking too long to process

**Common Causes**:
1. Buffer size too small for CPU performance
2. High DPC latency (driver issues)
3. Expensive operations in audio callback
4. Memory allocations in audio thread

**Diagnostics**:
```cpp
// In audio callback (processBlock)
auto start = juce::Time::getHighResolutionTicks();

// ... process audio ...

auto end = juce::Time::getHighResolutionTicks();
auto elapsed = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;

if (elapsed > bufferDuration * 0.8) // Using >80% of available time
{
    DBG("WARNING: Audio callback took " + String(elapsed, 2) + 
        " ms (buffer duration: " + String(bufferDuration, 2) + " ms)");
}
```

**Resolution Priority**:
1. **Increase buffer size**: 128 → 256 → 512 samples
2. **Optimize audio code**: Profile with Very Sleepy or Visual Studio Profiler
3. **Check DPC latency**: Use LatencyMon, disable problematic drivers
4. **Reduce module count**: Simplify preset temporarily

**Prevention**:
- Default to safe buffer size (256 samples)
- Warn users before enabling very small buffers
- Profile all audio code paths
- Use lock-free data structures for inter-thread communication

### Issue 6.4: Sample Rate Mismatch

**Symptom**: Pitch shifted audio or "device won't open"

**Cause**: ASIO device hardware locked to specific sample rate

**Example**: Audio interface set to 96kHz, app requests 44.1kHz

**Detection**:
```cp
auto* device = audioDeviceManager.getCurrentAudioDevice();
auto requestedRate = 48000.0;
auto actualRate = device->getCurrentSampleRate();

if (std::abs(actualRate - requestedRate) > 0.1)
{
    DBG("Sample rate mismatch! Requested: " + String(requestedRate) + 
        " Hz, Got: " + String(actualRate) + " Hz");
}
```

**Resolution**:
1. Query device supported rates:
```cpp
auto rates = device->getAvailableSampleRates();
for (auto rate : rates)
    DBG("Supported rate: " + String(rate));
```

2. Use hardware's current rate instead of forcing specific rate
3. Provide sample rate selector in audio settings UI

**Best Practice**:
- Don't hard-code sample rates
- Adapt to device's preference
- Support common rates: 44.1kHz, 48kHz, 88.2kHz, 96kHz

### Issue 6.5: ASIO Control Panel Integration

**Symptom**: Users can't access ASIO driver settings (buffer size, routing, etc.)

**Cause**: ASIO drivers typically have their own control panel (separate from app)

**Solution**: Add "ASIO Control Panel" button

```cpp
// In audio settings UI
class AsioControlPanelButton : public juce::TextButton
{
public:
    AsioControlPanelButton(juce::AudioDeviceManager& adm) 
        : TextButton("ASIO Control Panel..."),
          deviceManager(adm)
    {
        onClick = [this]() { openAsioControlPanel(); };
    }
    
private:
    void openAsioControlPanel()
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device != nullptr && device->getTypeName() == "ASIO")
        {
            device->showControlPanel(); // JUCE built-in method!
        }
    }
    
    juce::AudioDeviceManager& deviceManager;
};
```

**This opens the driver's native control panel**:
- ASIO4ALL: Routing, buffer size, latency compensation
- Focusrite: Scarlett MixControl
- RME: TotalMix FX

---

## 7. Platform-Specific Considerations

### Windows-Specific

**Compiler Requirements**:
- Visual Studio 2019+ (MSVC v142 or later)
- Windows SDK 10.0+

**Runtime Requirements**:
- User must install ASIO drivers separately
- No runtime redistribution needed (compiled into exe)

**COM Dependencies**:
- ASIO uses COM for driver enumeration
- Already linked via JUCE (`ole32.lib` already in build)

### macOS (For Reference)

**No ASIO Support Needed**:
- CoreAudio is already ultra-low latency (comparable to ASIO)
- `JUCE_ASIO` will be undefined on macOS (correct behavior)

**JUCE automatically uses CoreAudio** on macOS:
- No code changes needed
- Latency comparable to Windows ASIO

### Linux (For Reference)

**ASIO Not Applicable**:
- Linux uses ALSA (lower-level) or JACK (pro audio)
- JACK provides similar low-latency capabilities

**JUCE ALSA Backend**:
- Direct ALSA access for low latency
- Or JACK for pro audio routing

---

## 8. Build Variants and Configuration Options

### 8.1: Optional ASIO Support (Graceful Degradation)

**Scenario**: Distribute single binary that works with or without ASIO drivers

**Current CMake Approach**: Already handles this!
```cmake
$<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>
```

**Runtime Behavior**:
- **If compiled with ASIO**: "ASIO" appears in device type list
- **If ASIO not compiled**: Only DirectSound/WASAPI available
- **No crashes either way**

**User Experience**:
- Users without ASIO drivers: Works fine with standard audio
- Users with ASIO interfaces: Get low-latency option

### 8.2: ASIO-Only Build (Advanced)

**Scenario**: Force ASIO, fail if not available (pro audio edition)

**CMake Modification**:
```cmake
option(REQUIRE_ASIO "Fail build if ASIO SDK not found" OFF)

if(REQUIRE_ASIO AND NOT ASIO_SDK_FOUND)
    message(FATAL_ERROR "ASIO SDK is required but not found!")
endif()
```

**Use Case**: Enterprise/Pro version that guarantees ASIO support

### 8.3: Disable DirectSound/WASAPI (Reduce Binary Size)

**Scenario**: ASIO-exclusive app (no Windows Audio fallback)

**CMake Addition**:
```cmake
target_compile_definitions(PresetCreatorApp PRIVATE
    JUCE_ASIO=1
    JUCE_DIRECTSOUND=0   # Disable DirectSound
    JUCE_WASAPI=0        # Disable WASAPI
)
```

**Warning**: App will fail on systems without ASIO drivers!

---

## 9. Performance Optimization Strategies

### 9.1: Audio Thread Priority

**Issue**: Windows may deprioritize audio thread

**Solution**: Elevate audio thread priority (JUCE does this automatically for ASIO)

**Verification**:
```cpp
// Check thread priority in audio callback
DBG("Audio thread priority: " + 
    String(GetThreadPriority(GetCurrentThread())));
```

**Expected**: `THREAD_PRIORITY_TIME_CRITICAL` (15)

### 9.2: DPC Latency Reduction

**Common Causes of High DPC Latency**:
1. Wi-Fi drivers (Intel wireless)
2. Bluetooth
3. GPU drivers
4. Antivirus software

**User Guidance**:
- Run LatencyMon to identify problematic drivers
- Disable Wi-Fi during critical recording
- Update all drivers
- Use wired network instead of Wi-Fi

### 9.3: CPU Core Affinity

**Advanced**: Pin audio thread to specific CPU core

**Trade-offs**:
- ✓ Reduces context switching
- ✗ Can reduce throughput
- ✗ May conflict with Windows power management

**Usually not necessary** - Windows scheduler handles this well

### 9.4: Pre-Allocated Buffers

**Critical for Low Latency**:
```cpp
// DON'T do this in audio callback:
std::vector<float> tempBuffer(numSamples); // ❌ Allocates memory!

// DO this instead:
class AudioProcessor {
    std::vector<float> tempBuffer; // Pre-allocated member
    
    void prepareToPlay(int samplesPerBlock, double sampleRate) override {
        tempBuffer.resize(samplesPerBlock); // Allocate once
    }
    
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override {
        // Use pre-allocated buffer (no allocation)
        // ... processing ...
    }
};
```

**Already good practice in your codebase** - verify with profiler

---

## 10. Distribution and Deployment

### 10.1: Binary Distribution

**Compiled Executable**:
- ASIO SDK code compiled into `.exe`
- No separate ASIO DLL needed
- SDK license allows redistribution of compiled code

**User Downloads Separately**:
- ASIO drivers (hardware-specific or ASIO4ALL)
- Installation instructions in user manual

### 10.2: Installer Recommendations

**NSIS/InnoSetup Script Additions**:
```nsis
[Messages]
ASIO drivers not detected. For best performance, install ASIO drivers:
- If you have an audio interface, download from manufacturer
- Otherwise, install ASIO4ALL: www.asio4all.org

[Icons]
Name: "ASIO Setup Guide"; Filename: "{app}\USER_MANUAL\ASIO_Setup_Guide.md"
```

### 10.3: Update Manifest File

**File**: Update manifest (if using auto-updater)

**New Entry Example**:
```json
{
  "version": "0.2.0",
  "changes": [
    "Added ASIO low-latency audio support",
    "Improved audio-video synchronization",
    "New: ASIO control panel access from audio settings"
  ],
  "files": {
    "Pikon Raditsz.exe": {
      "hash": "...",
      "size": 789012456  // Will be larger due to ASIO code
    }
  }
}
```

---

## 11. Release Checklist

### Pre-Release

- [ ] ASIO SDK extracted to `vendor/asiosdk`
- [ ] CMakeLists.txt modified with ASIO configuration
- [ ] Both applications compile successfully with `JUCE_ASIO=1`
- [ ] Audio settings UI includes ASIO device type
- [ ] User manual updated with ASIO setup guide
- [ ] Tested with at least one ASIO driver (ASIO4ALL minimum)

### Quality Assurance

- [ ] Latency measured < 10ms with 256 sample buffer
- [ ] No audio dropouts under normal load
- [ ] Device switching works (ASIO ↔ DirectSound)
- [ ] ASIO control panel button functional
- [ ] Pose-to-audio latency acceptable (< 1 frame at 30 FPS)

### Documentation

- [ ] ASIO_Setup_Guide.md written
- [ ] In-app help text added
- [ ] Troubleshooting section complete
- [ ] Known issues documented

### Distribution

- [ ] Installer includes ASIO setup guide
- [ ] Release notes mention ASIO support
- [ ] Website/marketing updated with low-latency claims
- [ ] Support team trained on ASIO troubleshooting

---

## 12. Future Enhancements

### 12.1: Advanced ASIO Features

**Multi-Channel Support**:
- Current: Likely stereo (2 channels)
- Enhancement: Support 8, 16, 32+ channels for complex routing
- Use Case: Multi-output modular patches

**ASIO Direct Monitoring**:
- Hardware-based zero-latency monitoring
- Bypass software path for input → output

**Sample Rate Conversion**:
- Automatic SRC when device rate != internal processing rate
- Use high-quality resampler (libsamplerate, JUCE built-in)

### 12.2: Profile System

**ASIO Performance Profiles**:
- **Ultra-Low Latency**: 64 samples (pro hardware only)
- **Balanced**: 256 samples (default)
- **Safe**: 512 samples (fallback)

User selects profile, app auto-configures buffer size and optimizations

### 12.3: Latency Compensation

**For Video-Audio Sync**:
```cpp
// Compensate for ASIO latency in video-to-audio path
auto latencySamples = audioDevice->getOutputLatencyInSamples();
auto latencyMs = (latencySamples / sampleRate) * 1000.0;

// Delay video processing by latencyMs to maintain sync
videoProcessor->setLatencyCompensation(latencyMs);
```

**Maintains perfect sync** even with very low audio latency

### 12.4: Performance Analytics

**Telemetry (Optional)**:
- Average audio callback duration
- Buffer underrun count
- DPC latency spikes
- Help diagnose user issues

---

## 13. Success Metrics

### Quantitative

- **Latency Reduction**: 50ms → 5ms (10x improvement)
- **User Adoption**: >70% of users with audio interfaces use ASIO
- **Stability**: <0.1% audio dropout rate in telemetry

### Qualitative

- **User Feedback**: "Audio feels instant now"
- **Professional Adoption**: Used in live performance scenarios
- **Competitive Advantage**: "Best-in-class latency for CV processing"

---

## 14. Conclusion

ASIO integration is a **high-impact, low-complexity** enhancement that will:

1. **Dramatically improve** audio-video synchronization (critical for CV modules)
2. **Enable professional workflows** (live performance, studio production)
3. **Differentiate** from competitors lacking low-latency support
4. **Require minimal code changes** (mostly configuration)

**Estimated Implementation Time**:
- Setup + Build Configuration: 2-4 hours
- Testing + Debugging: 4-8 hours
- Documentation: 2-3 hours
- **Total**: 1-2 days for complete integration

**Recommended Priority**: **HIGH** - Essential for professional-grade audio-visual application

---

## Appendix A: JUCE-Specific ASIO Integration Quick Reference

### A.1: Verified CMake Configuration Pattern

**Complete CMake Block** (single coherent addition):

```cmake
# ==============================================================================
# ASIO SDK Configuration (Windows-only low-latency audio)
# ==============================================================================
set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/../vendor/asiosdk" CACHE PATH "Path to ASIO SDK")

if(WIN32)
    if(EXISTS "${ASIO_SDK_DIR}/common/asio.h")
        set(ASIO_SDK_FOUND TRUE)
        message(STATUS "✓ ASIO SDK found at ${ASIO_SDK_DIR}")
        message(STATUS "  - ASIO low-latency audio support will be enabled")
        
        # Store ASIO SDK include paths
        set(ASIO_SDK_INCLUDE_DIRS
            "${ASIO_SDK_DIR}/common"
            "${ASIO_SDK_DIR}/host"
            CACHE INTERNAL "ASIO SDK include directories"
        )
    else()
        set(ASIO_SDK_FOUND FALSE)
        message(WARNING 
            "ASIO SDK not found at ${ASIO_SDK_DIR}\n"
            "Download from https://www.steinberg.net/asiosdk"
        )
    endif()
else()
    set(ASIO_SDK_FOUND FALSE)
    message(STATUS "ASIO not applicable (Windows-only)")
endif()
```

**Target Configuration** (for both apps):

```cmake
# Enable JUCE_ASIO preprocessor definition
target_compile_definitions(PresetCreatorApp PRIVATE
    # ... existing definitions ...
    $<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>
)

# Add ASIO SDK include directories
target_include_directories(PresetCreatorApp PRIVATE
    # ... existing includes ...
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/host>
)

# Verify juce::juce_audio_devices is linked (should already exist)
target_link_libraries(PresetCreatorApp PRIVATE
    # ... existing links ...
    juce::juce_audio_devices  # or juce::juce_audio_utils which includes it
)
```

### A.2: JUCE Audio Module Compilation Flow

```
CMake Configuration
    ↓
JUCE_ASIO=1 defined for target
    ↓
ASIO SDK headers in include path
    ↓
juce_audio_devices module compiles
    ↓
juce_audio_devices.cpp includes native/juce_win32_ASIO.cpp
    ↓
#if JUCE_ASIO block compiles ASIO backend
    ↓
ASIO support available at runtime
```

**Critical Files in JUCE**:
- `juce/modules/juce_audio_devices/juce_audio_devices.cpp` - Main module
- `juce/modules/juce_audio_devices/native/juce_win32_ASIO.cpp` - Windows ASIO backend
- Requires: `asio.h`, `asiodrivers.h`, `asiolist.h` from ASIO SDK

### A.3: Runtime ASIO Detection Pattern

**Minimal Test Code** (to verify ASIO is available):

```cpp
#include <JuceHeader.h>

void testAsioAvailability()
{
#if JUCE_ASIO
    juce::AudioDeviceManager adm;
    
    // Create device type list
    juce::OwnedArray<juce::AudioIODeviceType> types;
    adm.createAudioDeviceTypes(types);
    
    // Check for ASIO
    bool asioFound = false;
    for (auto* type : types)
    {
        DBG("Available device type: " + type->getTypeName());
        if (type->getTypeName() == "ASIO")
        {
            asioFound = true;
            
            // List ASIO devices
            juce::StringArray deviceNames = type->getDeviceNames();
            DBG("ASIO devices found: " + juce::String(deviceNames.size()));
            for (const auto& name : deviceNames)
                DBG("  - " + name);
        }
    }
    
    if (asioFound)
        DBG("✓ ASIO support is compiled in and drivers are installed");
    else
        DBG("⚠ ASIO compiled in but no drivers installed");
#else
    DBG("❌ ASIO support NOT compiled in (JUCE_ASIO not defined)");
#endif
}
```

### A.4: Integration Verification Checklist

**Pre-Build Checks**:
- [ ] ASIO SDK downloaded from Steinberg
- [ ] Extracted to `h:\0000_CODE\01_collider_pyo\vendor\asiosdk`
- [ ] File exists: `vendor/asiosdk/common/asio.h`
- [ ] CMake variable `ASIO_SDK_DIR` points to SDK location

**CMake Configuration Checks**:
- [ ] CMake outputs: "✓ ASIO SDK found at..."
- [ ] CMakeCache.txt contains `ASIO_SDK_FOUND:INTERNAL=TRUE`
- [ ] No warnings about missing ASIO SDK

**Build Verification**:
- [ ] Compiler flags include `-DJUCE_ASIO=1`
- [ ] Compiler flags include ASIO SDK include paths
- [ ] Object files created: `*ASIO*.obj` or `*asio*.obj`
- [ ] No linker errors related to ASIO symbols
- [ ] Executable builds successfully

**Runtime Verification**:
- [ ] Launch PresetCreatorApp
- [ ] Open Audio Settings dialog
- [ ] "ASIO" appears in device type dropdown
- [ ] ASIO devices are listed when ASIO type selected
- [ ] Can successfully initialize ASIO device
- [ ] Audio plays through ASIO device

**Performance Verification**:
- [ ] Buffer size adjustable (64-1024 samples)
- [ ] Latency < 10ms at 256 sample buffer
- [ ] No audio dropouts under normal load
- [ ] Pose estimation → audio response feels immediate

### A.5: Common JUCE+ASIO Mistakes to Avoid

❌ **WRONG**: Using global `include_directories()` for ASIO
```cmake
include_directories("${ASIO_SDK_DIR}/common")  # Don't do this
```

✅ **RIGHT**: Use `target_include_directories()` with conditional
```cmake
target_include_directories(MyApp PRIVATE
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>
)
```

---

❌ **WRONG**: Defining `JUCE_ASIO` without providing headers
```cmake
target_compile_definitions(MyApp PRIVATE JUCE_ASIO=1)  # Will fail at compile time
```

✅ **RIGHT**: Define AND add include paths
```cmake
target_compile_definitions(MyApp PRIVATE $<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>)
target_include_directories(MyApp PRIVATE $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>)
```

---

❌ **WRONG**: Assuming ASIO exists at runtime
```cpp
audioDeviceManager.setCurrentAudioDeviceType("ASIO", true);
audioDeviceManager.initialise(0, 2, nullptr, true);  // May fail!
```

✅ **RIGHT**: Check device type availability
```cpp
#if JUCE_ASIO
    audioDeviceManager.setCurrentAudioDeviceType("ASIO", true);
    if (audioDeviceManager.getCurrentAudioDeviceType() == "ASIO")
    {
        // ASIO available, safe to initialize
    }
#endif
```

---

❌ **WRONG**: Not linking `juce_audio_devices`
```cmake
target_link_libraries(MyApp PRIVATE juce::juce_core)  # ASIO won't work
```

✅ **RIGHT**: Link audio device module
```cmake
target_link_libraries(MyApp PRIVATE 
    juce::juce_audio_devices  # Required for ASIO
    # or juce::juce_audio_utils which includes it
)
```

### A.6: Debugging ASIO Integration Issues

**Issue**: CMake says "ASIO SDK not found"

**Check**:
```cmd
dir h:\0000_CODE\01_collider_pyo\vendor\asiosdk\common\asio.h
```
If file doesn't exist → SDK not properly extracted

---

**Issue**: Compilation errors: `asio.h not found`

**Check compile flags**:
```cmd
ninja -v PresetCreatorApp 2>&1 | findstr "asiosdk"
```
Should see `-I"h:/0000_CODE/01_collider_pyo/vendor/asiosdk/common"`

If missing → Check `target_include_directories` in CMakeLists.txt

---

**Issue**: "ASIO" not in device type list at runtime

**Possible causes**:
1. `JUCE_ASIO` not defined (check preprocessor)
2. No ASIO drivers installed (Install ASIO4ALL)
3. ASIO backend didn't compile (check for ASIO .obj files)

**Verify**:
```cpp
#if JUCE_ASIO
    DBG("JUCE_ASIO is defined");
#else
    DBG("JUCE_ASIO is NOT defined"); // ← Problem is here
#endif
```

---

**Issue**: ASIO device won't open (error at runtime)

**Common causes**:
- Device already in use by another app (close DAWs)
- Sample rate mismatch (check device settings)
- Buffer size not supported (try 256 samples)

**Debugging**:
```cpp
auto error = audioDeviceManager.initialise(/* ... */);
if (error.isNotEmpty())
{
    DBG("ASIO error: " + error);
    // Log detailed device info
    if (auto* device = audioDeviceManager.getCurrentAudioDevice())
    {
        DBG("Available sample rates:");
        for (auto rate : device->getAvailableSampleRates())
            DBG("  " + juce::String(rate) + " Hz");
            
        DBG("Available buffer sizes:");
        for (auto size : device->getAvailableBufferSizes())
            DBG("  " + juce::String(size) + " samples");
    }
}
```

---

## 15. References and Resources

### Official Documentation
- Steinberg ASIO SDK: https://www.steinberg.net/asiosdk
- JUCE Audio Devices Module: https://docs.juce.com/master/group__juce__audio__devices.html
- JUCE AudioDeviceManager: https://docs.juce.com/master/classAudioDeviceManager.html

### ASIO Drivers
- ASIO4ALL (Universal): https://www.asio4all.org
- Focusrite: https://focusrite.com/asio-driver
- Behringer: https://www.behringer.com/downloads

### Performance Tools
- LatencyMon: https://www.resplendence.com/latencymon
- DPC Latency Checker: https://www.thesycon.de/eng/latency_check.shtml

### Community Resources
- JUCE Forum: https://forum.juce.com
- ASIOVSTPlugins Reddit: r/audioengineering

---

**Document Version**: 1.0  
**Last Updated**: 2025-11-30  
**Author**: Implementation Plan for ASIO Integration  
**Status**: Ready for Review and Implementation
