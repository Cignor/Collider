# Plan: Create "Pikon Raditsz Audio" Build Without OpenCV (No CUDA/CUDNN Required)

**Date:** 2025-01-XX  
**Status:** Planning Phase  
**Goal:** Create a build variant that excludes OpenCV (and thus CUDA/CUDNN) to allow non-NVIDIA users to run the software, while keeping FFmpeg, animation, physics, and all other non-OpenCV features.

---

## Executive Summary

Create a new executable target (`PikonRaditszAudioApp`) that:
- ✅ Includes all core audio processing modules
- ✅ Includes MIDI modules  
- ✅ Includes TTS/Piper support
- ✅ Includes preset creator UI (ImGui-based)
- ✅ Includes animation system (glTF/FBX loaders)
- ✅ Includes physics modules (Box2D)
- ❌ Excludes OpenCV-dependent modules (video processing - ~20 files)
- ❌ Excludes FFmpeg (only used by VideoFileLoaderModule for video audio extraction)
- ❌ Excludes CUDA/CUDNN dependencies (not needed without OpenCV)

**Key Insight:** Only ~20 source files need to be excluded (OpenCV video modules). Everything else (audio, MIDI, animation, physics) can stay.

**Critical Requirement:** Must NOT break existing `ColliderApp` and `PresetCreatorApp` builds that require OpenCV.

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM** ⚠️

**Risk Factors:**
1. **CUDA is REQUIRED at top level** - Line 192: `find_package(CUDAToolkit REQUIRED)` will fail if CUDA not installed
2. **OpenCV configuration is complex** - Currently configured globally before targets
3. **Module registration** - Need to ensure OpenCV modules aren't registered in audio-only build
4. **Shared infrastructure** - VideoFrameManager, CameraEnumerator might be referenced elsewhere
5. **Conditional compilation** - Need to handle `#if WITH_CUDA_SUPPORT` guards properly

**Mitigating Factors:**
- ✅ Clear separation: Only video processing modules use OpenCV
- ✅ FFmpeg is independent of OpenCV (separate library)
- ✅ Animation system doesn't use OpenCV
- ✅ Physics doesn't use OpenCV
- ✅ Most audio modules don't use OpenCV

---

## Difficulty Levels

### Level 1: Basic (Low Risk) ✅
- Make CUDA optional (not REQUIRED)
- Make OpenCV optional (only configure if needed)
- Create new target without OpenCV modules
- Basic conditional linking

**Estimated Time:** 3-4 hours  
**Risk:** Low-Medium

### Level 2: Intermediate (Medium Risk) ⚠️
- Refactor OpenCV configuration to be target-specific
- Handle module registration conditionally
- Test that existing targets still work
- Handle VideoFrameManager dependencies

**Estimated Time:** 4-6 hours  
**Risk:** Medium

### Level 3: Advanced (High Risk) 🔴
- Refactor shared video infrastructure
- Handle all edge cases in module registration
- Extensive testing of all scenarios
- Fix any cross-dependencies

**Estimated Time:** 6-8 hours  
**Risk:** Medium-High

---

## Confidence Rating

### Overall Confidence: **75%** 

**Strong Points:**
- ✅ Clear module separation - OpenCV modules are well-identified
- ✅ FFmpeg is independent (can stay)
- ✅ Animation system is independent (can stay)
- ✅ Physics is independent (can stay)
- ✅ CMake supports conditional target configuration
- ✅ Existing code uses `#if WITH_CUDA_SUPPORT` guards

**Weak Points:**
- ⚠️ CUDA is hardcoded as REQUIRED (line 192)
- ⚠️ OpenCV configuration happens before targets are defined
- ⚠️ VideoFrameManager might be referenced in non-video code
- ⚠️ Module registration might need conditional logic

---

## Detailed Analysis

### 1. OpenCV-Dependent Modules to Exclude

**Base Class:**
```
Source/audio/modules/OpenCVModuleProcessor.h  (base class for OpenCV modules)
```

**Video Source Modules:**
```
Source/video/VideoFrameManager.h
Source/video/CameraEnumerator.h
Source/audio/modules/WebcamLoaderModule.h
Source/audio/modules/WebcamLoaderModule.cpp
Source/audio/modules/VideoFileLoaderModule.h
Source/audio/modules/VideoFileLoaderModule.cpp
```

**Video Processing Modules:**
```
Source/audio/modules/VideoFXModule.h
Source/audio/modules/VideoFXModule.cpp
Source/audio/modules/VideoDrawImpactModuleProcessor.h
Source/audio/modules/VideoDrawImpactModuleProcessor.cpp
```

**Computer Vision Modules:**
```
Source/audio/modules/MovementDetectorModule.h
Source/audio/modules/MovementDetectorModule.cpp
Source/audio/modules/HumanDetectorModule.h
Source/audio/modules/HumanDetectorModule.cpp
Source/audio/modules/PoseEstimatorModule.h
Source/audio/modules/PoseEstimatorModule.cpp
Source/audio/modules/HandTrackerModule.h
Source/audio/modules/HandTrackerModule.cpp
Source/audio/modules/FaceTrackerModule.h
Source/audio/modules/FaceTrackerModule.cpp
Source/audio/modules/ObjectDetectorModule.h
Source/audio/modules/ObjectDetectorModule.cpp
Source/audio/modules/ColorTrackerModule.h
Source/audio/modules/ColorTrackerModule.cpp
Source/audio/modules/ContourDetectorModule.h
Source/audio/modules/ContourDetectorModule.cpp
Source/audio/modules/CropVideoModule.h
Source/audio/modules/CropVideoModule.cpp
```

**Total:** ~20 source files to exclude

---

### 2. Modules That Can Stay (Non-OpenCV)

**FFmpeg (Audio Only):**
- `Source/audio/modules/FFmpegAudioReader.h`
- `Source/audio/modules/FFmpegAudioReader.cpp`
- `Source/audio/modules/TimeStretcherAudioSource.h`
- `Source/audio/modules/TimeStretcherAudioSource.cpp`

**Note:** ✅ **VERIFIED** - FFmpegAudioReader is ONLY used by VideoFileLoaderModule. Since VideoFileLoaderModule is excluded, FFmpegAudioReader and FFmpeg libraries can also be excluded from audio-only build.

**Animation System:**
- All animation files (glTF/FBX loaders) - ✅ Keep
- `Source/audio/modules/AnimationModuleProcessor.h/cpp` - ✅ Keep

**Physics:**
- `Source/audio/modules/PhysicsModuleProcessor.h/cpp` - ✅ Keep
- `Source/audio/modules/StrokeSequencerModuleProcessor.h/cpp` - ✅ Keep

**All Other Audio Modules:**
- VCO, VCF, VCA, LFO, ADSR, etc. - ✅ Keep
- FX modules (Delay, Reverb, Chorus, etc.) - ✅ Keep
- MIDI modules - ✅ Keep
- Sequencers - ✅ Keep
- Utilities - ✅ Keep

---

### 3. Dependencies Analysis

#### CUDA/CUDNN
- **Required by:** OpenCV (for GPU acceleration)
- **Used by:** OpenCV modules only
- **Action:** Make optional, only configure if OpenCV needed

#### OpenCV
- **Required by:** Video processing modules only
- **Action:** Make optional, only configure if any target needs it

#### FFmpeg
- **Required by:** FFmpegAudioReader (used by VideoFileLoaderModule)
- **Status:** Independent library, doesn't require OpenCV
- **Action:** Keep for audio-only build (if FFmpegAudioReader is used elsewhere)
- **Verification Needed:** Check if FFmpegAudioReader is used by non-video modules

#### Animation Libraries (tinygltf, ufbx, glm)
- **Required by:** Animation system only
- **Status:** Independent, doesn't require OpenCV
- **Action:** ✅ Keep

#### Box2D
- **Required by:** PhysicsModuleProcessor, StrokeSequencerModuleProcessor
- **Status:** Independent, doesn't require OpenCV
- **Action:** ✅ Keep

---

### 4. Current CMake Structure Issues

#### Problem 1: CUDA REQUIRED (Line 192)
```cmake
find_package(CUDAToolkit REQUIRED)  # ❌ Fails if CUDA not installed
```

**Solution:** Make optional:
```cmake
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(CUDA_AVAILABLE TRUE)
    # ... configure CUDA ...
else()
    set(CUDA_AVAILABLE FALSE)
    message(STATUS "CUDA not found - OpenCV GPU features will be disabled")
endif()
```

#### Problem 2: OpenCV Configured Globally (Lines 252-465)
OpenCV is configured before any targets are defined, affecting all targets.

**Solution:** Make OpenCV configuration conditional:
```cmake
# Only configure OpenCV if any target needs it
set(BUILD_OPENCV_TARGETS FALSE)
if(TARGET ColliderApp OR TARGET PresetCreatorApp)
    set(BUILD_OPENCV_TARGETS TRUE)
endif()

if(BUILD_OPENCV_TARGETS)
    # ... existing OpenCV configuration ...
endif()
```

**Alternative:** Configure OpenCV always, but only link to targets that need it.

#### Problem 3: OpenCV Includes Added Globally (Line 265)
```cmake
include_directories(SYSTEM ${CUDAToolkit_INCLUDE_DIRS})  # ❌ Affects all targets
```

**Solution:** Remove global include, add per-target:
```cmake
# Remove global include
# Add to targets that need it:
target_include_directories(ColliderApp PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_include_directories(PresetCreatorApp PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
```

---

## Implementation Strategy

### Phase 1: Make Dependencies Optional (CRITICAL)

**Step 1.1: Make CUDA Optional**
```cmake
# Change line 192 from:
find_package(CUDAToolkit REQUIRED)

# To:
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(CUDA_AVAILABLE TRUE)
    message(STATUS "✓ CUDA found: ${CUDAToolkit_VERSION}")
    # ... existing CUDA configuration (lines 194-217) ...
else()
    set(CUDA_AVAILABLE FALSE)
    message(STATUS "CUDA not found - OpenCV GPU features will be disabled")
endif()
```

**Step 1.2: Make OpenCV Conditional**
Wrap OpenCV configuration in conditional:
```cmake
# Determine if any target needs OpenCV
set(BUILD_WITH_OPENCV FALSE)
if(TARGET ColliderApp OR TARGET PresetCreatorApp)
    set(BUILD_WITH_OPENCV TRUE)
endif()

if(BUILD_WITH_OPENCV)
    # ... existing OpenCV configuration (lines 252-465) ...
else()
    message(STATUS "OpenCV disabled - video features will not be available")
    set(OPENCV_LINK_LIBS "")
    set(opencv_SOURCE_DIR "")
    set(OPENCV_BUILD_DIR "")
endif()
```

**Step 1.3: Remove Global CUDA Includes**
```cmake
# Remove line 265:
# include_directories(SYSTEM ${CUDAToolkit_INCLUDE_DIRS})

# Add per-target instead (in target configuration sections)
```

**Step 1.4: Make FFmpeg Optional**
Change FATAL_ERROR to WARNING:
```cmake
# Change line 556 from:
message(FATAL_ERROR "FFmpeg NOT FOUND!")

# To:
if(NOT FFMPEG_FOUND)
    message(WARNING "FFmpeg not found - video audio extraction will be disabled")
endif()
```

---

### Phase 2: Create Audio-Only Target

**Step 2.1: Define Target**
```cmake
juce_add_gui_app(PikonRaditszAudioApp
    PRODUCT_NAME "Pikon Raditsz Audio"
    VERSION "0.1.0"
    COMPANY_NAME "Collider"
    ICON_BIG "${CMAKE_SOURCE_DIR}/../icons/pikon_raditsz_icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/../icons/pikon_raditsz_icon.png"
)
```

**Step 2.2: Add Source Files**
Copy from PresetCreatorApp, but exclude:
- All OpenCV-dependent modules (listed in section 1)
- VideoFrameManager.h
- CameraEnumerator.h

Keep:
- All audio modules (VCO, VCF, FX, etc.)
- All MIDI modules
- Animation modules
- Physics modules
- Preset creator UI
- FFmpegAudioReader (if used elsewhere - needs verification)

**Step 2.3: Configure Dependencies**
```cmake
target_link_libraries(PikonRaditszAudioApp PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_cryptography
    imgui_impl_juce
    soundtouch
    box2d                    # ✅ Keep (physics)
    glm::glm                 # ✅ Keep (animation)
    tinygltf                 # ✅ Keep (animation)
    ufbx_static              # ✅ Keep (animation)
    # NO OpenCV libraries
    # NO CUDA libraries
    # NO FFmpeg libraries (only used by VideoFileLoaderModule)
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_TARGET}>
    # Piper TTS libraries
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)
```

**Step 2.4: Configure Includes**
```cmake
target_include_directories(PikonRaditszAudioApp PRIVATE
    ${imgui_fc_SOURCE_DIR}
    ${imgui_fc_SOURCE_DIR}/backends
    ${imnodes_fc_SOURCE_DIR}
    ${imgui_juce_fc_SOURCE_DIR}
    ${PIPER_DIR}
    ${ONNXRUNTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/source/SoundTouch
    ${ufbx_SOURCE_DIR}
    # NO OpenCV includes
    # NO CUDA includes
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_INCLUDE_DIR}>
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/common>
    $<$<BOOL:${ASIO_SDK_FOUND}>:${ASIO_SDK_DIR}/host>
)
```

**Step 2.5: Set Compile Definitions**
```cmake
target_compile_definitions(PikonRaditszAudioApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PikonRaditszAudioApp,PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PikonRaditszAudioApp,VERSION>"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    IMGUI_IMPL_JUCE_BEZEL=0
    IMGUI_ENABLE_VIEWPORTS
    IMGUI_DEFINE_MATH_OPERATORS
    IMNODES_NAMESPACE=ImNodes
    IMNODES_STATIC_DEFINE
    PRESET_CREATOR_UI=1
    AUDIO_ONLY_BUILD=1              # NEW: Signal no OpenCV
    WITH_CUDA_SUPPORT=0             # Explicitly disable CUDA
    $<$<BOOL:${USE_RUBBERBAND}>:USE_RUBBERBAND=1>
    $<$<NOT:$<BOOL:${USE_RUBBERBAND}>>:USE_RUBBERBAND=0>
    $<$<BOOL:${ASIO_SDK_FOUND}>:JUCE_ASIO=1>
)
```

---

### Phase 3: Handle Module Registration

**Problem:** Module registration might try to register OpenCV modules even in audio-only build.

**Solution:** Add conditional compilation in module registration code:
```cpp
// In module registration code:
#ifndef AUDIO_ONLY_BUILD
    // Register OpenCV modules only if not audio-only
    registerModule<WebcamLoaderModule>();
    registerModule<VideoFileLoaderModule>();
    // ... etc ...
#endif
```

**Location:** Need to find where modules are registered (likely in ModularSynthProcessor or similar).

---

### Phase 4: Verify Existing Targets Still Work

**Step 4.1: Test ColliderApp**
- Ensure CUDA/OpenCV still configured
- Ensure all video modules compile
- Ensure linking works

**Step 4.2: Test PresetCreatorApp**
- Ensure CUDA/OpenCV still configured
- Ensure all video modules compile
- Ensure linking works

**Step 4.3: Test PikonRaditszAudioApp**
- Verify no CUDA/OpenCV dependencies
- Verify no OpenCV modules included
- Verify audio modules work
- Verify animation works
- Verify physics works

---

## Potential Problems & Solutions

### Problem 1: CUDA REQUIRED Breaks Build
**Symptom:** Build fails if CUDA not installed  
**Solution:** Make CUDA optional (see Phase 1, Step 1.1)  
**Risk:** Medium - Need to test all conditional paths

### Problem 2: OpenCV Configuration Complexity
**Symptom:** OpenCV config runs before targets defined  
**Solution:** Configure OpenCV only if any target needs it (see Phase 1, Step 1.2)  
**Risk:** Medium - Need to ensure existing targets still get OpenCV

### Problem 3: Module Registration
**Symptom:** OpenCV modules registered in audio-only build  
**Solution:** Add `#ifndef AUDIO_ONLY_BUILD` guards in registration code  
**Risk:** Medium - Need to find registration location

### Problem 4: VideoFrameManager Dependencies
**Symptom:** Non-video code references VideoFrameManager  
**Solution:** Check for references, add conditional compilation if needed  
**Risk:** Low-Medium - VideoFrameManager likely only used by video modules

### Problem 5: FFmpegAudioReader Usage
**Symptom:** FFmpegAudioReader only used by VideoFileLoaderModule  
**Solution:** If true, exclude FFmpegAudioReader from audio-only build  
**Risk:** Low - Easy to verify and fix

### Problem 6: Global CUDA Includes
**Symptom:** CUDA includes affect all targets  
**Solution:** Remove global include, add per-target (see Phase 1, Step 1.3)  
**Risk:** Low - Straightforward change

### Problem 7: DLL Copying
**Symptom:** POST_BUILD commands copy OpenCV/CUDA DLLs to all targets  
**Solution:** Make DLL copying conditional per target  
**Risk:** Medium - Need to update all POST_BUILD commands

### Problem 8: Build Time
**Symptom:** Three executables take longer to build  
**Solution:** Use parallel builds, incremental compilation  
**Risk:** Low - CMake handles this

---

## Verification Checklist

### Pre-Implementation
- [ ] Verify FFmpegAudioReader usage (only VideoFileLoaderModule?)
- [ ] Find module registration code location
- [ ] Check for VideoFrameManager references outside video modules
- [ ] List all `#if WITH_CUDA_SUPPORT` guards
- [ ] Verify no OpenCV includes in non-video modules

### During Implementation
- [ ] Make CUDA optional (test with/without CUDA)
- [ ] Make OpenCV conditional (test video vs audio targets)
- [ ] Create audio-only target (verify compiles)
- [ ] Add module registration guards
- [ ] Test ColliderApp still builds
- [ ] Test PresetCreatorApp still builds

### Post-Implementation
- [ ] All three executables build successfully
- [ ] Audio-only executable runs without CUDA/OpenCV
- [ ] Video executables still work with CUDA/OpenCV
- [ ] No runtime errors from missing DLLs
- [ ] Preset files load correctly (video vs audio)
- [ ] Module registration works correctly per build

---

## File Changes Required

### CMakeLists.txt Changes

1. **Lines 188-486:** CUDA/CUDNN configuration
   - Make `find_package(CUDAToolkit)` optional
   - Wrap CUDA config in conditional

2. **Lines 252-465:** OpenCV configuration
   - Make conditional based on target needs
   - Don't configure for audio-only target

3. **Line 265:** Global CUDA includes
   - Remove global include
   - Add per-target includes

4. **Lines 520-628:** FFmpeg configuration
   - Change `FATAL_ERROR` to `WARNING`
   - Make linking conditional

5. **Lines 663-1013:** ColliderApp target
   - Add CUDA includes (if removed globally)
   - Keep OpenCV linking

6. **Lines 1019-1446:** PresetCreatorApp target
   - Add CUDA includes (if removed globally)
   - Keep OpenCV linking

7. **New Section:** PikonRaditszAudioApp target
   - Create new target
   - Exclude OpenCV modules
   - Exclude CUDA/OpenCV dependencies
   - Keep FFmpeg (if needed), animation, physics

8. **Lines 1488-1645:** POST_BUILD commands
   - Make DLL copying conditional per target
   - Don't copy CUDA/OpenCV DLLs to audio-only target

### Source Code Changes (If Needed)

**Potential Changes:**
- Add `#ifndef AUDIO_ONLY_BUILD` guards in module registration
- Verify no OpenCV includes in shared headers
- Check VideoFrameManager usage

**Files to Check:**
- Module registration code (likely in ModularSynthProcessor)
- Shared utility files
- Base module classes

---

## Testing Strategy

### Build Tests
1. **Clean build (CUDA installed)** - All three targets build
2. **Clean build (CUDA not installed)** - Audio-only target builds, video targets fail gracefully
3. **Incremental build** - Modify audio module, verify only affected targets rebuild
4. **Module registration** - Verify correct modules available per executable

### Runtime Tests
1. **Audio-only executable:**
   - Launch without CUDA/OpenCV DLLs
   - Load preset file (no video nodes)
   - Test MIDI input
   - Test audio processing
   - Test animation modules
   - Test physics modules
   - Test TTS functionality

2. **Video executables:**
   - Launch with CUDA/OpenCV DLLs
   - Load preset with video nodes
   - Test video processing
   - Test CUDA acceleration

### Integration Tests
1. **Preset compatibility** - Audio presets work in all executables
2. **Module availability** - Verify correct modules registered per executable
3. **DLL dependencies** - Use Dependency Walker to verify DLLs

---

## Success Criteria

✅ **Must Have:**
- Audio-only executable builds without CUDA/OpenCV
- Audio-only executable runs without CUDA/OpenCV DLLs
- Existing executables still build and work with OpenCV
- No regression in audio functionality
- Animation system works in audio-only build
- Physics modules work in audio-only build

✅ **Nice to Have:**
- Faster build time for audio-only executable
- Smaller executable size (no CUDA/OpenCV bloat)
- Clear error messages if video features accessed in audio-only build
- Graceful degradation if CUDA not available

---

## Rollback Plan

If implementation causes issues:

1. **Immediate Rollback:**
   - Revert CMakeLists.txt changes
   - Remove PikonRaditszAudioApp target
   - Restore CUDA REQUIRED

2. **Partial Rollback:**
   - Keep CUDA optional
   - Remove audio-only target
   - Restore original behavior for existing targets

3. **Alternative Approach:**
   - Use CMake options to control features per target
   - Keep single executable with feature flags
   - Less clean separation but safer

---

## Timeline Estimate

- **Planning:** ✅ Complete (this document)
- **Phase 1 (Dependencies Optional):** 3-4 hours
- **Phase 2 (Create Target):** 2-3 hours
- **Phase 3 (Module Registration):** 1-2 hours
- **Phase 4 (Testing):** 3-4 hours
- **Bug Fixes:** 2-4 hours

**Total Estimated Time:** 11-17 hours

---

## Next Steps

1. **Review this plan** - Get approval before proceeding
2. **Verify FFmpegAudioReader usage** - Check if used outside video modules
3. **Find module registration code** - Locate where modules are registered
4. **Create backup** - Commit current state before changes
5. **Implement Phase 1** - Make dependencies optional
6. **Test existing targets** - Ensure no regression
7. **Implement Phase 2** - Create audio-only target
8. **Implement Phase 3** - Handle module registration
9. **Test all targets** - Comprehensive testing
10. **Documentation** - Update build instructions

---

## Questions to Resolve

1. **Is FFmpegAudioReader used outside VideoFileLoaderModule?**
   - **Answer:** ✅ NO - Only used by VideoFileLoaderModule (verified via grep)
   - **Impact:** FFmpeg can be excluded from audio-only build (simpler!)

2. **Where is module registration code?**
   - **Action:** Search for module registration/factory code
   - **Impact:** Need to add conditional compilation guards

3. **Is VideoFrameManager referenced outside video modules?**
   - **Action:** Search for VideoFrameManager usage
   - **Impact:** May need conditional compilation

---

## Conclusion

This is a **medium-complexity** task with **medium risk**. The main challenges are:

1. Making CUDA optional without breaking existing builds
2. Conditionally configuring OpenCV per target
3. Handling module registration conditionally

The approach is sound, and the separation is clear: only video processing modules use OpenCV. FFmpeg, animation, and physics are independent and can stay.

**Recommendation:** Proceed with implementation in phases, testing between each phase. The estimated 11-17 hours is realistic given the complexity.

**Key Insight:** This is simpler than the original plan because we're only excluding OpenCV modules, not FFmpeg, animation, or physics. This makes the task more focused and lower risk.

