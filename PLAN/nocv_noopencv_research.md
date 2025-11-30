# Research: "Pikon Raditsz NoCUDA.exe" - No OpenCV Edition

## Executive Summary
Creating a **NoCUDA = No OpenCV** build would eliminate ALL video/computer vision functionality, creating a pure **audio/MIDI/sequencing workstation**. This is **highly feasible** but removes 15 modules from the application.

---

## Philosophy: Two Distinct Products

### **"Pikon Raditsz"** (CUDA Build - Current)
**Identity**: Audio-Visual Creative Workstation
- Full modular synthesis
- Complete computer vision suite
- Real-time video processing
- AI-powered detection (humans, poses, faces, hands, objects)
- **Target Users**: Visual artists, VJs, interactive installations, multimedia performers
- **Download**: 2.2 GB

### **"Pikon Raditsz NoCUDA"** (Audio-Only Build - Proposed)
**Identity**: Pure Modular Audio Workstation  
- Full modular synthesis (100% feature parity)
- All audio effects
- Complete MIDI/OSC/sequencing
- VST hosting
- Timeline/automation
- TTS integration
- **NO** video/camera/CV modules
- **Target Users**: Musicians, sound designers, traditional DAW users, laptop/mobile producers
- **Download**: ~600 MB (73% smaller!)

---

## Modules That Would Be REMOVED

### **Video I/O Modules** (3 modules)
```
❌ WebcamLoaderModule          - Live camera input
❌ VideoFileLoaderModule        - Video file playback  
❌ VideoDrawImpactModuleProcessor - Draw on video with physics
```

### **Computer Vision Detection** (10 modules)
```
❌ MovementDetectorModule       - Motion detection & tracking
❌ HumanDetectorModule          - Full-body human detection
❌ PoseEstimatorModule          - Skeletal pose tracking (17 keypoints)
❌ ObjectDetectorModule         - YOLO-based object detection
❌ HandTrackerModule            - Hand landmark tracking (21 points)
❌ FaceTrackerModule            - Face landmark tracking (468 points)
❌ ColorTrackerModule           - Color-based object tracking
❌ ContourDetectorModule        - Edge/contour detection
❌ CropVideoModule              - Region-of-interest video cropping
❌ VideoFXModule                - Video effects (blur, sharpen, etc.)
```

### **Base Class** (1 file)
```
❌ OpenCVModuleProcessor.h      - Base class for all CV modules
```

**Total Removed**: 14 modules + 1 base class = **15 items**

---

## Modules That Would REMAIN

### **Core Audio** ✅
- AudioInputModule
- VCO (Voltage Controlled Oscillator)
- VCF (Voltage Controlled Filter)  
- VCA (Voltage Controlled Amplifier)
- NoiseModule
- PolyVCO (Polyphonic oscillator)
- ShapingOscillator

### **Synthesis & Modulation** ✅
- LFO (Low Frequency Oscillator)
- ADSR (Envelope)
- FunctionGenerator
- Granulator
- Waveshaper
- MultiBandShaper
- HarmonicShaper

### **Effects** ✅
- Delay, Reverb, Chorus, Phaser
- Compressor, Limiter, Gate, Drive
- BitCrusher, DeCrackle
- GraphicEQ, FrequencyGraph
- VocalTractFilter
- TimePitch (time stretching/pitch shifting)

### **Mixing & Routing** ✅
- Mixer, CVMixer, TrackMixer
- PanVol
- Attenuverter
- Value
- Reroute (cable management)
- Inlet/Outlet (meta-modules)
- MetaModule

### **Sequencing & Control** ✅
- StepSequencer
- MultiSequencer
- SnapshotSequencer
- StrokeSequencer (drawing-based sequencing)
- TempoClockModule
- BPMMonitor, TapTempo
- Timeline

### **MIDI** ✅
- MIDIPlayer (file playback)
- MIDICV (MIDI to CV)
- MIDIFaders, MIDIKnobs, MIDIButtons
- MIDIJogWheel, MIDIPad
- MidiLogger

### **Sample Playback** ✅
- SampleLoader
- SampleSfx
- RecordModule

### **Logic & Math** ✅
- MathModule, MapRange, Comparator
- LogicModule, ClockDivider
- Quantizer, SAndH (Sample & Hold)
- RateModule, RandomModule
- SequentialSwitch, LagProcessor

### **Utility** ✅
- ScopeModule (oscilloscope)
- DebugModule, InputDebugModule
- CommentModule
- BestPracticeNode

### **Special** ✅
- TTSPerformerModule (Text-to-Speech)
- VstHostModule (3rd party plugins)
- PhysicsModule (Box2D physics)
- AnimationModule (skeletal animation - GLTF/FBX)

**Total Remaining**: ~70+ audio modules ✅

---

## Dependencies Analysis

### **Would Be REMOVED**
```
❌ OpenCV (entire library)       -150 MB DLL → Gone!
❌ CUDA Toolkit runtime          -500 MB → Gone!
❌ cuDNN                         -800 MB → Gone!
❌ NPP (NVIDIA Perf Primitives)  (included in CUDA)
```

**Total Savings**: ~1.45 GB

### **Would REMAIN**
```
✅ JUCE framework                 (core UI/audio)
✅ FFmpeg                         (audio/video codecs - still needed for audio file reading)
✅ SoundTouch                     (time stretching)
✅ Rubberband                     (time/pitch processing)
✅ Piper TTS + ONNX Runtime       (text-to-speech)
✅ Box2D                          (physics engine)
✅ GLM, tinygltf, ufbx            (3D math, animation)
✅ ImGui + ImNodes                (UI framework)
```

---

## Code Changes Required

### **1. Add `WITH_OPENCV` Preprocessor Definition**

#### CMakeLists.txt Addition
```cmake
# New option (lines ~190)
option(ENABLE_OPENCV "Build with OpenCV (enables video/CV modules)" ON)

# Conditional OpenCV integration (replace lines 192-465)
if(ENABLE_OPENCV)
    find_package(CUDAToolkit REQUIRED)  # CUDA still required for OpenCV
    # ... existing OpenCV build logic ...
    set(OPENCV_ENABLED TRUE)
else()
    message(STATUS "OpenCV disabled - building audio-only variant")
    set(OPENCV_ENABLED FALSE)
endif()

# Add preprocessor definition (line ~896, ~1349)
$<$<BOOL:${OPENCV_ENABLED}>:WITH_OPENCV=1>
```

### **2. Conditional File Inclusion in CMakeLists.txt**

#### Wrap Video Module Sources
```cmake
# Lines 827-858: Make conditional
if(OPENCV_ENABLED)
    Source/video/VideoFrameManager.h
    Source/video/CameraEnumerator.h
    Source/audio/modules/WebcamLoaderModule.h
    Source/audio/modules/WebcamLoaderModule.cpp
    Source/audio/modules/VideoFileLoaderModule.h
    Source/audio/modules/VideoFileLoaderModule.cpp
    # ... all 15 CV modules ...
endif()
```

**Affected Lines**: 
- ColliderApp: Lines 827-858 (~30 lines)
- PresetCreatorApp: Lines 1231-1262 (~30 lines)

### **3. Wrap Includes with Preprocessor Guards**

#### Modify Headers (15 files)
All OpenCV-dependent modules already partially use `#if WITH_CUDA_SUPPORT`.

**Change to**: `#if WITH_OPENCV`

Example (`WebcamLoaderModule.h`):
```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#if WITH_OPENCV  // NEW: Wrap entire file
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class WebcamLoaderModule : public ModuleProcessor
{
    // ... implementation ...
};
#endif // WITH_OPENCV
```

**Files to modify**: 15 headers + 15 implementations = **30 files**

### **4. Update Module Registry**

File: `Source/preset_creator/ModuleRegistry.cpp` (or wherever modules are registered)

```cpp
#if WITH_OPENCV
    registerModule("Webcam Loader", []() { return new WebcamLoaderModule(); });
    registerModule("Video File Loader", []() { return new VideoFileLoaderModule(); });
    // ... all 15 CV modules ...
#endif
```

### **5. Update ImGui Insert Menu**

File: `Source/preset_creator/ImGuiNodeEditorComponent.cpp`

Add category filtering:
```cpp
if (ImGui::BeginMenu("Video & CV"))
{
#if WITH_OPENCV
    if (ImGui::MenuItem("Webcam Loader")) { /* ... */ }
    if (ImGui::MenuItem("Human Detector")) { /* ... */ }
    // ... other CV modules ...
#else
    ImGui::TextDisabled("(Not available in NoCUDA build)");
    ImGui::TextDisabled("Download full version for CV features");
#endif
    ImGui::EndMenu();
}
```

### **6. Update CudaSafeQuery**

File: `Source/preset_creator/CudaSafeQuery.cpp`

```cpp
#if WITH_OPENCV
    // Existing CUDA query code
#else
void safeCudaDeviceQuery(int* deviceCount, int* success) noexcept
{
    *deviceCount = 0;
    *success = 0;
    // No OpenCV = No CUDA, always return 0
}
#endif
```

---

## Build System Changes

### **CMake Changes Summary**
```diff
Line ~190:
+ option(ENABLE_OPENCV "Build with OpenCV (video/CV modules)" ON)

Lines 192-465:
+ if(ENABLE_OPENCV)
      # Existing CUDA + OpenCV logic
+ else()
+     set(OPENCV_ENABLED FALSE)
+     set(CUDA_SUPPORT_ENABLED FALSE)
+ endif()

Lines 827-858 (ColliderApp):
+ if(OPENCV_ENABLED)
      # Video module sources
+ endif()

Lines 1231-1262 (PresetCreatorApp):
+ if(OPENCV_ENABLED)
      # Video module sources
+ endif()

Lines 896, 1349:
  $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:WITH_CUDA_SUPPORT=1>
+ $<$<BOOL:${OPENCV_ENABLED}>:WITH_OPENCV=1>

Lines 912, 1367:
- ${OPENCV_LINK_LIBS}  # Always linked
+ $<$<BOOL:${OPENCV_ENABLED}>:${OPENCV_LINK_LIBS}>  # Conditional

Lines 925-929, 1379-1383:
# Make CUDA runtime linking conditional on OPENCV_ENABLED
+ if(OPENCV_ENABLED AND TARGET CUDAToolkit::cudart)
- if(TARGET CUDAToolkit::cudart)
```

**Total CMake Changes**: ~80 lines

---

## Distribution Size Breakdown

### **Current CUDA Build** (2.2 GB)
```
PresetCreatorApp.exe          50 MB
opencv_world4130.dll         150 MB  ← OpenCV with CUDA
cudart64_*.dll                50 MB  ← CUDA Runtime
npp*.dll (12 files)          400 MB  ← NVIDIA Performance Primitives
cudnn64_*.dll                800 MB  ← Deep Neural Network library
FFmpeg DLLs                  100 MB
onnxruntime.dll              200 MB  ← TTS engine
Other DLLs                   450 MB
─────────────────────────────────
Total                       2200 MB
```

### **Proposed NoCUDA Build** (600 MB)
```
PresetCreatorApp.exe          50 MB
FFmpeg DLLs                  100 MB  ← Still needed for audio
onnxruntime.dll              200 MB  ← TTS still available
soundtouch.dll                10 MB
box2d (static linked)          —
Other DLLs                   240 MB
─────────────────────────────────
Total                        600 MB
```

**Savings**: 1.6 GB (73% reduction) 🎉

---

## User Experience Implications

### **Install Flow**
```
User downloads "Pikon Raditsz NoCUDA.exe" installer

On first launch:
┌────────────────────────────────────────┐
│  Welcome to Pikon Raditsz (Audio)      │
│                                        │
│  This is the lightweight audio-only    │
│  edition (600 MB).                     │
│                                        │
│  ✓ Full modular synthesis              │
│  ✓ All audio effects                   │
│  ✓ MIDI/OSC/sequencing                 │
│  ✓ VST hosting                         │
│  ✓ Timeline & automation               │
│                                        │
│  ✗ Video/camera modules                │
│  ✗ Computer vision (AI detection)      │
│                                        │
│  Need CV features?                     │
│  [Download Full Version] (2.2 GB)      │
│                                        │
│  [Continue]                            │
└────────────────────────────────────────┘
```

### **Settings Dialog**
```
⚙ Settings

System Information:
├─ Version: 0.6.5-beta (NoCUDA)
├─ Build Type: Audio Workstation
├─ OpenCV: ❌ Not Compiled
├─ CUDA: ❌ Not Compiled
└─ GPU: Not Required

[Upgrade to Full Version]
```

### **Insert Menu** (Right-click canvas)
```
📋 Insert Node
├─ 📢 Audio I/O
├─ 🎵 Synthesis
├─ 🎛️ Effects
├─ 🎹 MIDI
├─ 🔢 Logic & Math
├─ 📊 Utility
├─ 📹 Video & CV ──────────────┐
│   └─ (Not available in       │
│       NoCUDA build)           │
│   [Upgrade to Full Version]  │
└───────────────────────────────┘
```

---

## Manifest & Update System

### **Dual Manifest Structure**
```json
{
  "version": "0.6.5-beta",
  "variants": {
    "full": {
      "name": "Pikon Raditsz (Full)",
      "description": "Audio-Visual Workstation with AI Computer Vision",
      "size_mb": 2200,
      "requires_nvidia_gpu": true,
      "files": {
        "PresetCreatorApp.exe": { "hash": "...", "size": 50000000 },
        "opencv_world4130.dll": { "hash": "...", "size": 150000000 },
        "cudart64_13.dll": { "hash": "...", "size": 50000000 },
        // ... 30+ files
      }
    },
    "nocuda": {
      "name": "Pikon Raditsz NoCUDA (Audio)",
      "description": "Pure Modular Audio Workstation (No Video/CV)",
      "size_mb": 600,
      "requires_nvidia_gpu": false,
      "files": {
        "PresetCreatorApp.exe": { "hash": "...", "size": 50000000 },
        "onnxruntime.dll": { "hash": "...", "size": 200000000 },
        // ... 10 files
      }
    }
  }
}
```

### **Updater Changes**
```cpp
// Detect current variant
BuildVariant getCurrentVariant()
{
    #if WITH_OPENCV
        return BuildVariant::Full;
    #else
        return BuildVariant::NoCUDA;
    #endif
}

// Only download files for matching variant
void updateToVersion(const Manifest& manifest)
{
    auto variant = getCurrentVariant();
    auto& variantFiles = manifest.variants[variant].files;
    
    for (auto& file : variantFiles)
    {
        downloadIfNeeded(file);
    }
}

// Allow cross-variant upgrade
void offerFullVersionUpgrade()
{
    if (getCurrentVariant() == BuildVariant::NoCUDA)
    {
        showDialog("Upgrade to Full Version?",
                   "Get video/CV features (additional 1.6 GB download)");
    }
}
```

---

## Testing Strategy

### **Build Verification**
1. **Compile Test**
   ```powershell
   cmake -B build-nocuda -G Ninja -DENABLE_OPENCV=OFF
   ninja -C build-nocuda
   ```
   
   **Expected**: Clean build, no OpenCV symbols

2. **Size Test**
   ```powershell
   Measure-Command { Get-ChildItem build-nocuda/*.exe,*.dll | 
                      Measure-Object -Property Length -Sum }
   ```
   
   **Expected**: < 700 MB total

3. **Launch Test**
   - Application starts without CUDA/OpenCV DLLs
   - No crashes, no missing DLL errors
   - Settings shows "OpenCV: Not Compiled"

### **Functional Tests**
- ✅ All audio modules work
- ✅ VST hosting works
- ✅ MIDI I/O functional
- ✅ Preset save/load works
- ✅ Timeline/sequencing works
- ✅ TTS (text-to-speech) works (uses ONNX, not OpenCV)
- ❌ Video modules do not appear in insert menu
- ❌ Loading preset with CV modules shows graceful error

### **Compatibility Matrix**
```
Hardware        | Full Build | NoCUDA Build
─────────────────────────────────────────────
NVIDIA GPU      |    ✅     |      ✅
AMD GPU         |    ⚠️      |      ✅
Intel GPU       |    ⚠️      |      ✅
No GPU (CPU)    |    ❌     |      ✅
```
⚠️ = Runs but video features disabled/slow
❌ = Crashes or won't run

---

## Marketing & Positioning

### **Download Page**
```
┌─────────────────────────────────────────────┐
│ Choose Your Edition                         │
├─────────────────────────────────────────────┤
│                                             │
│ 🎥 Pikon Raditsz (Full)                     │
│    Audio-Visual Creative Suite              │
│    • Modular synthesis                      │
│    • Video processing                       │
│    • AI computer vision                     │
│    • Real-time VJ tools                     │
│    Requires: NVIDIA GPU                     │
│    Download: 2.2 GB                         │
│    [Download]                               │
│                                             │
│ ───────────────────────────────────────────  │
│                                             │
│ 🎵 Pikon Raditsz NoCUDA (Audio)             │
│    Pure Modular Audio Workstation           │
│    • Complete synthesis engine              │
│    • All audio effects                      │
│    • MIDI/OSC control                       │
│    • VST hosting                            │
│    Works on: Any PC (no GPU needed)         │
│    Download: 600 MB                         │
│    [Download]                               │
│                                             │
└─────────────────────────────────────────────┘
```

### **Product Tiers**
```
Feature                  | NoCUDA (Audio) | Full (AV)
────────────────────────────────────────────────────
Price                    |     Free       |    Free
Modular Synthesis        |      ✅        |     ✅
Audio Effects (30+)      |      ✅        |     ✅
MIDI/OSC                 |      ✅        |     ✅
Sequencing               |      ✅        |     ✅
VST Hosting              |      ✅        |     ✅
Text-to-Speech           |      ✅        |     ✅
Animation (GLTF/FBX)     |      ✅        |     ✅
Video I/O                |      ❌        |     ✅
Computer Vision (AI)     |      ❌        |     ✅
Live Camera              |      ❌        |     ✅
Object Detection         |      ❌        |     ✅
Pose Tracking            |      ❌        |     ✅
────────────────────────────────────────────────────
Download Size            |    600 MB     |   2.2 GB
GPU Required             |      No       |  NVIDIA
Target Users             |  Musicians    |  VJs/Artists
```

---

## Implementation Effort

### **Phase 1: CMake Refactoring** (4 hours)
- Add `ENABLE_OPENCV` option
- Wrap OpenCV fetch/build in conditional
- Make file inclusion conditional
- Update compile definitions
- Make linking conditional

### **Phase 2: Code Modifications** (6 hours)
- Wrap 30 files with `#if WITH_OPENCV`
- Update module registry
- Modify insert menu (ImGui)
- Update Settings dialog
- Add "upgrade to full version" prompts

### **Phase 3: Build & Test** (3 hours)
- Build NoCUDA variant
- Verify size < 700 MB
- Test all audio modules
- Verify graceful degradation
- Test on non-NVIDIA hardware

### **Phase 4: Updater Integration** (4 hours)
- Dual manifest support
- Variant detection
- Cross-variant upgrade flow
- Update documentation

### **Phase 5: Marketing Materials** (2 hours)
- Update website with two editions
- Create comparison table
- Write system requirements
- FAQ about differences

**Total Time**: 19 hours (~2.5 days)

---

## Risks & Challenges

### **⚠️ High Risk**
1. **User Confusion**
   - Users download wrong version
   - Expect features that aren't there
   - **Mitigation**: Clear product naming, comparison table

2. **Preset Compatibility**
   - Full build presets may use CV modules
   - NoCUDA build can't load them
   - **Mitigation**: Graceful degradation, "missing module" warnings

3. **Support Burden**
   - Two builds = 2x support complexity
   - Bug reports need variant identification
   - **Mitigation**: Version stamp shows build variant

### **Medium Risk**
4. **Build Matrix Complexity**
   - CI/CD needs to build both variants
   - Testing matrix doubles
   - **Mitigation**: Automated builds, shared test suite

5. **Update System Complexity**
   - Dual manifest management
   - Cross-variant migration
   - **Mitigation**: Extensive testing, rollback mechanism

### **Low Risk**
6. **Code Maintenance**
   - Additional `#if WITH_OPENCV` guards
   - Conditional compilation
   - **Mitigation**: Well-scoped changes, clear documentation

---

## Alternative: Single Build with Runtime Detection

Instead of two builds, detect OpenCV availability at runtime:

```cpp
bool hasOpenCV = false;
#if WITH_OPENCV
    hasOpenCV = true;
#endif

if (!hasOpenCV) {
    // Hide CV modules from insert menu
    // Show upgrade prompt
}
```

**Pros**:
- Single build to maintain
- Users can "unlock" features by downloading DLLs

**Cons**:
- Still ships 2.2 GB (waste of bandwidth for audio-only users)
- Complex DLL loading logic
- Security concerns (DLL injection)

**Verdict**: ❌ Not recommended vs. dual build approach

---

## Conclusion

### **Feasibility**: ✅ **Highly Feasible**
- Clean separation between audio and video subsystems
- Code already partially structured for this
- CMake has good conditional compilation support

### **Value Proposition**:
- **73% smaller download** (2.2 GB → 0.6 GB)
- **Broader hardware compatibility** (works without NVIDIA GPU)
- **Faster install** for musicians who don't need CV
- **Professional product line** (audio vs. audio-visual editions)
- **Lower barrier to entry** for new users

### **Trade-offs**:
- ❌ Removes 15 modules (all video/CV)
- ⚠️ Preset compatibility issues
- 📈 Increases support complexity
- 🔧 Requires dual build pipeline

### **Recommendation**: ✅ **Strongly Recommended**

This creates a **focused, lightweight audio workstation** that competes with traditional DAWs/modular synths while maintaining the full-featured **audio-visual edition** for advanced users.

**Target Markets**:
- **NoCUDA**: Bedroom producers, laptop musicians, students, traditional DAW users
- **Full**: VJs, installation artists, live performers, multimedia creators

---

## Next Steps if Proceeding

1. **Prototype NoCUDA build** (~4 hours)
   - Add CMake option
   - Test compilation without OpenCV

2. **Wrap CV modules** (~6 hours)
   - Add `#if WITH_OPENCV` to 30 files
   - Update module registry

3. **UI/UX updates** (~3 hours)
   - Insert menu filtering
   - Settings dialog updates
   - "Upgrade" prompts

4. **Build & distribute** (~4 hours)
   - Set up dual build pipeline
   - Create two installer packages
   - Update manifest system

5. **Documentation** (~2 hours)
   - System requirements
   - Feature comparison
   - Migration guide

**Total**: 19 hours = 2.5 days of focused development

**Return on Investment**:
- 73% smaller download = 3x faster installation
- Works on 100% of PCs (vs ~30% with NVIDIA GPUs)
- Professional product positioning
- Competitive with traditional audio software
