# Research: Creating "Pikon Raditsz NoCUDA.exe"

## Executive Summary
Creating a NoCUDA build is **feasible but complex**. The codebase already has conditional compilation infrastructure in place via `#if WITH_CUDA_SUPPORT`, but OpenCV is tightly coupled to the build system and requires significant changes.

---

## Current CUDA Integration Points

### 1. **Preprocessor Macro: `WITH_CUDA_SUPPORT`**
- **Location**: Defined in `CMakeLists.txt` lines 470-486
- **Condition**: Set when **both** CUDA toolkit AND cuDNN are found
- **Usage**: ~60+ occurrences across 10 computer vision modules

### 2. **Affected Modules** (All wrapped in `#if WITH_CUDA_SUPPORT`)
```
✓ MovementDetectorModule
✓ HumanDetectorModule  
✓ PoseEstimatorModule
✓ ObjectDetectorModule
✓ HandTrackerModule
✓ FaceTrackerModule
✓ CropVideoModule
✓ ColorTrackerModule
✓ ContourDetectorModule
✓ VideoFXModule
```

**Good news**: All CV modules already have conditional compilation guards!

### 3. **Build Dependencies**

#### **Always Required** (Independent of CUDA)
- JUCE framework
- ImGui + ImNodes
- FFmpeg (video/audio codec)
- SoundTouch (time stretching)
- Piper TTS + ONNX Runtime
- Box2D, GLM, tinygltf, ufbx
- Rubberband (timestretching)

#### **CUDA-Specific Dependencies**
- **CUDA Toolkit** (line 192: `find_package(CUDAToolkit REQUIRED)`)
- **cuDNN** (lines 222-250: Required for DNN CUDA backend)
- **OpenCV with CUDA** (lines 252-465: Massive build configuration)
- **NPP** (NVIDIA Performance Primitives)

---

## Build System Changes Required

### **Option A: Conditional CUDA Build (Recommended)**

#### 1. **Add CMake Option**
```cmake
option(ENABLE_CUDA "Build with CUDA support" ON)
```

#### 2. **Make CUDA Dependencies Optional**
```cmake
# Current (REQUIRED):
find_package(CUDAToolkit REQUIRED)

# Proposed (OPTIONAL):
if(ENABLE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        # ... existing CUDA setup ...
    else()
        message(STATUS "CUDA requested but not found, building without CUDA")
        set(ENABLE_CUDA OFF)
    endif()
endif()
```

#### 3. **Use Pre-Built OpenCV WITHOUT CUDA**
**Critical Issue**: Current build uses **OpenCV with CUDA** (lines 268-450)

**Two approaches**:

**A. Dual OpenCV Installations** (Simpler)
- Pre-built OpenCV **with CUDA**: `opencv_cuda_install/`
- Pre-built OpenCV **without CUDA**: `opencv_nocuda_install/`
- CMake selects based on `ENABLE_CUDA` flag

**B. Build OpenCV Conditionally** (More flexible but slower)
- Modify OpenCV fetch/build logic to skip CUDA when `ENABLE_CUDA=OFF`
- Requires: `set(WITH_CUDA OFF)` and removing NPP/cuDNN config

#### 4. **Conditional Linking**
```cmake
# Lines 1379-1383: Make CUDA runtime optional
if(ENABLE_CUDA AND TARGET CUDAToolkit::cudart)
    target_link_libraries(PresetCreatorApp PRIVATE CUDAToolkit::cudart)
elseif(ENABLE_CUDA AND CUDA_CUDART_LIBRARY)
    target_link_libraries(PresetCreatorApp PRIVATE ${CUDA_CUDART_LIBRARY})
endif()
```

#### 5. **Update Include Paths** (Lines 1325-1331)
OpenCV CUDA contrib modules are conditionally included:
```cmake
$<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaimgproc/include>
```
**Already handled correctly!**

---

### **Option B: Separate CMake Configurations**

Create two build configurations:
- `build-ninja-release-cuda/` (existing)
- `build-ninja-release-nocuda/` (new)

**Pros**:
- Clean separation
- No risk of cross-contamination

**Cons**:
- More maintenance burden
- Duplicate build trees

---

## Deployment Implications

### **Executable Naming**
```
Current:  PresetCreatorApp.exe  (2.2 GB with CUDA DLLs)
Proposed: Pikon Raditsz.exe     (CUDA build)
          Pikon Raditsz NoCUDA.exe  (CPU-only build)
```

### **Distribution Size Comparison**

#### **CUDA Build** (~2.2 GB)
```
PresetCreatorApp.exe         ~50 MB
opencv_world4130.dll         ~150 MB
CUDA DLLs (cudart, NPP, etc) ~500 MB
cuDNN DLL                    ~800 MB
FFmpeg DLLs                  ~100 MB
Other dependencies           ~600 MB
```

#### **NoCUDA Build** (Estimated ~800 MB)
```
PresetCreatorApp_NoCUDA.exe  ~50 MB
opencv_world4130.dll         ~80 MB (no CUDA modules)
FFmpeg DLLs                  ~100 MB
Other dependencies           ~600 MB (minus CUDA runtime)
```

**Savings**: ~1.4 GB (60% smaller!)

---

## Code Impact Analysis

### **Changes Required**

#### 1. **Zero Code Changes** ✅
All CV modules already use `#if WITH_CUDA_SUPPORT` correctly!

#### 2. **CMakeLists.txt Changes** 
- Add `option(ENABLE_CUDA ...)` ~1 line
- Make `find_package(CUDAToolkit)` conditional ~10 lines
- Dual OpenCV path logic ~30 lines
- Conditional CUDA linking ~5 lines

**Total**: ~50 lines of CMake changes

#### 3. **CudaSafeQuery.cpp** (Already Exists!)
File: `Source/preset_creator/CudaSafeQuery.cpp`
- **Purpose**: Safe CUDA device detection without crashing
- **Already handles** no-CUDA scenario gracefully
- **No changes needed**

---

## Risks & Challenges

### **High Risk**
1. **OpenCV Build Complexity** ⚠️
   - OpenCV is the most complex dependency (45-minute build)
   - Requires maintaining **two** pre-built versions
   - Need to ensure both are from same OpenCV version

2. **Binary Size Management**
   - Need to distribute **two** complete builds
   - Update system needs to handle both variants
   - Manifest files need `nocuda` variant

### **Medium Risk**
3. **Testing Matrix Doubles**
   - Every feature needs testing on both builds
   - CUDA modules need graceful degradation messages
   - Settings menu needs "CUDA not available" handling (already done!)

4. **User Confusion**
   - Which build should they download?
   - What if they download wrong one?
   - Need clear documentation

### **Low Risk**
5. **Maintenance Burden**
   - CMake changes are one-time
   - Code already has `#if WITH_CUDA_SUPPORT` guards
   - Conditional compilation is well-tested pattern

---

## Recommended Implementation Phases

### **Phase 1: Preparation** (1-2 hours)
1. Build pre-built OpenCV **without** CUDA
   - Modify `build_opencv_cuda_once.ps1`
   - Set `WITH_CUDA=OFF`, `WITH_CUDNN=OFF`
   - Install to `opencv_nocuda_install/`

2. Test that build produces smaller DLL
   - Verify ~80 MB vs ~150 MB

### **Phase 2: CMake Refactoring** (2-3 hours)
1. Add `option(ENABLE_CUDA "Build with CUDA support" ON)`
2. Wrap `find_package(CUDAToolkit)` in conditional
3. Add dual OpenCV selection logic:
   ```cmake
   if(ENABLE_CUDA)
       set(OPENCV_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/../opencv_cuda_install")
   else()
       set(OPENCV_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/../opencv_nocuda_install")
   endif()
   ```
4. Make CUDA runtime linking conditional

### **Phase 3: Build & Test** (1-2 hours)
1. Build NoCUDA version:
   ```powershell
   cmake -B build-ninja-release-nocuda -G Ninja -DENABLE_CUDA=OFF
   ninja -C build-ninja-release-nocuda
   ```

2. Verify:
   - Application launches
   - Non-CUDA modules work (audio, MIDI, etc.)
   - CUDA modules show "Not Available" gracefully
   - Settings menu displays "CUDA: Not Compiled"

3. Size verification:
   - Total distribution < 1 GB
   - Runs on systems without NVIDIA GPU

### **Phase 4: Distribution** (2-3 hours)
1. Update manifest generator to create `nocuda` variant
2. Modify updater to handle two build flavors
3. Create install script that detects GPU and suggests appropriate build
4. Update deployment documentation

---

## Manifest & Updater Implications

### **Current Manifest Structure**
```json
{
  "version": "0.6.5-beta",
  "files": {
    "PresetCreatorApp.exe": { "hash": "...", "size": 50000000 },
    "opencv_world4130.dll": { "hash": "...", "size": 150000000 }
  }
}
```

### **Proposed Dual Manifest**
```json
{
  "version": "0.6.5-beta",
  "variants": {
    "cuda": {
      "files": { /* CUDA build files */ }
    },
    "nocuda": {
      "files": { /* NoCUDA build files */ }
    }
  }
}
```

**Updater Changes**:
- Detect which variant is currently installed
- Only download files for that variant
- Allow users to switch variants (requires full re-download)

---

## Alternative: Lazy CUDA Loading

Instead of two builds, use **runtime detection**:

```cpp
// Check if CUDA is available at runtime
bool cudaAvailable = false;
#if WITH_CUDA_SUPPORT
    int deviceCount = 0;
    int success = 0;
    safeCudaDeviceQuery(&deviceCount, &success);
    cudaAvailable = (success && deviceCount > 0);
#endif

if (!cudaAvailable) {
    // Disable CUDA modules from insert menu
    // Show warning in Settings
}
```

**Pros**:
- Single build
- User gets best experience for their hardware

**Cons**:
- Still ships 2.2 GB (CUDA DLLs included even if unused)
- Wastes bandwidth for users without NVIDIA GPUs

---

## Conclusion

### **Feasibility**: ✅ **Highly Feasible**
- Code already has guards in place
- CMake changes are straightforward
- OpenCV can be pre-built without CUDA

### **Effort Estimate**: **6-10 hours**
- OpenCV build: 2 hours
- CMake changes: 3 hours
- Testing: 2 hours  
- Deployment updates: 3 hours

### **Value Proposition**:
- **60% smaller download** (2.2 GB → 0.8 GB)
- **Broader compatibility** (works on AMD, Intel GPUs)
- **Faster download/install** for non-GPU users
- **Professional appearance** (shows you care about all users)

### **Recommendation**: ✅ **Implement**
The infrastructure is already 90% there. This is a high-value, low-risk improvement that significantly broadens your user base.

---

## Next Steps if Proceeding

1. **Decide on approach**:
   - Option A: Dual OpenCV installations (recommended)
   - Option B: Dual CMake configs
   - Alternative: Lazy loading (not recommended)

2. **Build NoCUDA OpenCV** (~2 hours)

3. **Modify CMake** (~3 hours)

4. **Update deployment script** (~3 hours)

5. **Test both builds** (~2 hours)

6. **Document for users** (~1 hour)

**Total**: 11 hours = ~1.5 days of work
