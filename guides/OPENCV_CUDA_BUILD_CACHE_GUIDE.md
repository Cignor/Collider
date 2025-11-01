# 🚀 OpenCV CUDA Build Cache - Complete Preservation Guide

**Why this matters:** The OpenCV CUDA build takes 30+ minutes and compiles hundreds of CUDA files. Preserving the cache saves massive time on future rebuilds.

---

## 📦 What to Archive (Total: ~3.2 GB)

### 🔴 CRITICAL - Must Archive

**1. `juce/build/_deps/opencv-build/` (2.75 GB)**
- **This is the crown jewel** - contains ALL compiled CUDA object files
- Includes the final `opencv_world4130.lib` (1.26 GB)
- Hundreds of `.cu.obj` files from CUDA compilation
- All intermediate build artifacts
- **Without this, you rebuild everything from scratch (30+ min)**

**2. `juce/build/_deps/opencv-src/` (320 MB)**
- OpenCV source code (cloned from GitHub)
- CMake will re-fetch if missing, but saves download time
- Version-locked to `GIT_TAG 4.x` in your CMakeLists.txt

**3. `juce/build/_deps/opencv_contrib-src/` (90 MB)**
- OpenCV contrib modules source (CUDA modules live here)
- Includes `cudev`, `cudafilters`, `cudaimgproc`, etc.
- Also re-fetchable, but good to cache

### 🟡 RECOMMENDED - High Value

**4. `juce/build/CMakeCache.txt` (873 KB)**
- Stores all CMake configuration variables
- Speeds up reconfiguration significantly
- Small file, definitely worth keeping

**5. `juce/build/bin/Release/opencv_videoio_ffmpeg4130_64.dll` (27 MB)**
- FFmpeg video backend for OpenCV
- Built during OpenCV compilation
- Auto-copies to your app directories

### 🟢 OPTIONAL - Less Important

**6. `juce/build/_deps/opencv-subbuild/`**
- FetchContent metadata (tiny)
- Recreated automatically if missing

**7. `juce/build/_deps/opencv_contrib-build/`**
- Usually empty or minimal
- Contrib modules compile into opencv-build

---

## 💾 How to Archive the Cache

### Option 1: Quick Archive (Recommended)

```powershell
# Navigate to your project
cd H:\0000_CODE\01_collider_pyo

# Create archive directory
New-Item -ItemType Directory -Force -Path "opencv_cuda_cache_archive"

# Copy the critical directories
Copy-Item -Recurse -Force "juce\build\_deps\opencv-build" "opencv_cuda_cache_archive\"
Copy-Item -Recurse -Force "juce\build\_deps\opencv-src" "opencv_cuda_cache_archive\"
Copy-Item -Recurse -Force "juce\build\_deps\opencv_contrib-src" "opencv_cuda_cache_archive\"
Copy-Item -Force "juce\build\CMakeCache.txt" "opencv_cuda_cache_archive\" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "opencv_cuda_cache_archive\bin\Release"
Copy-Item -Force "juce\build\bin\Release\opencv_videoio_ffmpeg4130_64.dll" "opencv_cuda_cache_archive\bin\Release\" -ErrorAction SilentlyContinue

Write-Host "Archive created in: opencv_cuda_cache_archive\" -ForegroundColor Green
```

### Option 2: Compress for Storage

```powershell
# Create a timestamped compressed archive
$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$archiveName = "opencv_cuda_cache_$timestamp.zip"

# Compress (takes a few minutes for 3.2 GB)
Compress-Archive -Path "opencv_cuda_cache_archive\*" -DestinationPath $archiveName -CompressionLevel Optimal

Write-Host "Compressed archive: $archiveName" -ForegroundColor Green
Write-Host "You can now delete opencv_cuda_cache_archive\ folder" -ForegroundColor Yellow
```

**Storage tip:** Store the compressed archive (~1-1.5 GB after compression) on:
- External drive
- Cloud storage (Google Drive, OneDrive, etc.)
- Network share
- Different partition (safety from C: drive issues)

---

## 🔄 How to Restore from Archive

### When to Restore

Restore the cache when:
- ❌ You accidentally deleted `juce/build/`
- 🔄 Moving to a new machine
- 🧹 After a clean rebuild
- 📁 Cloning the project fresh

### Restoration Steps

```powershell
# Step 1: Ensure build directory structure exists
cd H:\0000_CODE\01_collider_pyo
New-Item -ItemType Directory -Force -Path "juce\build\_deps"

# Step 2: Restore from uncompressed archive
Copy-Item -Recurse -Force "opencv_cuda_cache_archive\opencv-build" "juce\build\_deps\"
Copy-Item -Recurse -Force "opencv_cuda_cache_archive\opencv-src" "juce\build\_deps\"
Copy-Item -Recurse -Force "opencv_cuda_cache_archive\opencv_contrib-src" "juce\build\_deps\"
Copy-Item -Force "opencv_cuda_cache_archive\CMakeCache.txt" "juce\build\" -ErrorAction SilentlyContinue

# Step 3: Restore FFmpeg DLL
New-Item -ItemType Directory -Force -Path "juce\build\bin\Release"
Copy-Item -Force "opencv_cuda_cache_archive\bin\Release\opencv_videoio_ffmpeg4130_64.dll" "juce\build\bin\Release\" -ErrorAction SilentlyContinue

# Step 4: Reconfigure CMake (uses cached build)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# Step 5: Build (should be FAST - just your app, not OpenCV)
cmake --build juce/build --config Release

Write-Host "Restored from cache successfully!" -ForegroundColor Green
```

**Expected time after restore:** 5-10 minutes (just your app code, not OpenCV)

---

## 🎯 Key Elements to NEVER Change

### In `juce/CMakeLists.txt`

These settings are **locked in** to your cached build. Changing them forces a full OpenCV rebuild:

#### 🔴 CRITICAL - Don't Change These

```cmake
# OpenCV version - MUST match your cache
FetchContent_Declare(
  opencv
  GIT_REPOSITORY https://github.com/opencv/opencv.git
  GIT_TAG        4.x  # ⚠️ Changing this = full rebuild (30+ min)
)

FetchContent_Declare(
  opencv_contrib
  GIT_REPOSITORY https://github.com/opencv/opencv_contrib.git
  GIT_TAG        4.x  # ⚠️ Must match opencv tag
)

# CUDA Architecture - Changes force recompilation
set(CUDA_ARCH_BIN "8.6;8.9;12.0")  # ⚠️ Your GPU targets
set(CUDA_ARCH_PTX "12.0")

# Core OpenCV flags - Locked to your build
set(WITH_CUDA ON)
set(WITH_CUDNN ON)  # Or OFF if you built without cuDNN
set(OPENCV_DNN_CUDA ON)  # Or OFF
set(WITH_NVIDIA_NPP ON)
```

#### 🟡 SAFE - Can Change Without Rebuild

```cmake
# Your app's source files - Add/remove freely
target_sources(ColliderApp PRIVATE
  Source/your_files.cpp
  # Add new files here - no OpenCV rebuild
)

# Your app's compile definitions
target_compile_definitions(ColliderApp PRIVATE
  YOUR_CUSTOM_FLAGS
)

# Your include paths
target_include_directories(ColliderApp PRIVATE
  your/paths
)
```

---

## 🧠 Understanding the Build System

### What Triggers OpenCV Rebuild

| Action | Rebuild? | Time Cost |
|--------|----------|-----------|
| Change OpenCV `GIT_TAG` | ✅ YES | 30+ min |
| Change CUDA architectures | ✅ YES | 30+ min |
| Change `WITH_CUDA` ON/OFF | ✅ YES | 30+ min |
| Change `WITH_CUDNN` ON/OFF | ✅ YES | 20+ min |
| Delete `juce/build/_deps/opencv-build/` | ✅ YES | 30+ min |
| Add your .cpp files | ❌ NO | 1-5 min |
| Modify your source code | ❌ NO | 1-5 min |
| Change CMake generator | ⚠️ MAYBE | 5-30 min |
| Update JUCE version | ❌ NO | 2-10 min |

### How CMake Caches Work

**FetchContent Caching:**
```
juce/build/_deps/
├── opencv-subbuild/          # FetchContent metadata
├── opencv-src/               # Git clone (reused if GIT_TAG unchanged)
├── opencv-build/             # Compiled artifacts (GOLD!)
├── opencv_contrib-src/       # Contrib git clone
└── opencv_contrib-build/     # Contrib artifacts
```

**The Magic:** When you run `FetchContent_MakeAvailable(opencv)`:
1. Checks if `opencv-src/` exists and matches `GIT_TAG` → **Skips git clone**
2. Checks if `opencv-build/` exists with valid Makefiles → **Skips CMake configure**
3. Checks if `opencv-build/lib/Release/opencv_world4130.lib` exists → **Skips compilation**
4. If all checks pass → **Instant (seconds)**

**This is why your cache is so valuable!**

---

## ⚠️ Critical Gotchas & Troubleshooting

### Problem 1: "File structure different than expected"

**Cause:** Switching between OpenCV versions (e.g., 4.11.0 ↔ 4.x)

**Solution:**
```powershell
# Delete OpenCV cache (forces fresh build with new version)
Remove-Item -Recurse -Force juce\build\_deps\opencv-*
cmake --build juce/build --config Release
```

### Problem 2: "cannot open input file 'x64.lib'"

**Cause:** `CUDA_LIBRARIES` set to a directory instead of library files

**Fix:** Make sure this line is NOT in your CMakeLists.txt:
```cmake
# ❌ WRONG - causes x64.lib error
set(CUDA_LIBRARIES "${CUDAToolkit_LIBRARY_DIR}" CACHE PATH "" FORCE)

# ✅ CORRECT - let OpenCV find libraries automatically
# (just don't set CUDA_LIBRARIES at all)
```

### Problem 3: "LNK1181: cannot open input file '...generated_*.cu.obj'"

**Cause:** MSBuild/NVCC race condition - linker runs before CUDA compilation finishes

**Solution:** Retry the build 2-5 times:
```powershell
# Simple retry loop
for ($i = 1; $i -le 5; $i++) {
    cmake --build juce/build --config Release --target opencv_world
    if ($LASTEXITCODE -eq 0) { break }
    Start-Sleep -Seconds 2
}
```

**Why it happens:** MSBuild's parallel compilation doesn't properly wait for NVCC (CUDA compiler) to finish before linking.

### Problem 4: "opencv_videoio_ffmpeg411_64.dll not found"

**Cause:** OpenCV version changed but CMakeLists.txt has hardcoded DLL name

**Solution:** Update DLL version in CMakeLists.txt:
```cmake
# Check actual DLL name
dir juce\build\bin\Release\opencv_videoio*.dll

# Update CMakeLists.txt to match
"${CMAKE_BINARY_DIR}/bin/$<CONFIG>/opencv_videoio_ffmpeg4130_64.dll"
#                                                    ^^^^ Match your version
```

### Problem 5: "cuDNN not found"

**Check cuDNN location:**
```powershell
# Option 1: Integrated into CUDA (recommended)
Test-Path "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\include\cudnn.h"

# Option 2: Standalone installation
Test-Path "C:\Program Files\NVIDIA\CUDNN\v9.14\include\cudnn.h"
```

**Fix in CMakeLists.txt:** Your current code already handles both locations!

---

## 📋 Complete Rebuild Checklist

Use this when you need to rebuild from scratch (new machine, corrupted build, etc.):

### Fresh Build (No Cache)

```powershell
# 1. Clean slate
cd H:\0000_CODE\01_collider_pyo
Remove-Item -Recurse -Force juce\build -ErrorAction SilentlyContinue

# 2. Configure
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# 3. Build OpenCV with retry logic
for ($i = 1; $i -le 10; $i++) {
    Write-Host "Attempt $i/10..." -ForegroundColor Yellow
    cmake --build juce/build --config Release --target opencv_world
    if ($LASTEXITCODE -eq 0) { break }
    Start-Sleep -Seconds 2
}

# 4. Build your applications
cmake --build juce/build --config Release

# 5. Archive the cache immediately!
# (Use archive script from earlier)
```

**Time:** 30-45 minutes first time

### Incremental Build (With Cache)

```powershell
# Just build your changes
cmake --build juce/build --config Release
```

**Time:** 1-10 minutes (depending on what changed)

### Build with Restored Cache

```powershell
# 1. Restore cache (use restoration script from earlier)

# 2. Reconfigure (fast - uses cache)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# 3. Build (fast - just your app)
cmake --build juce/build --config Release
```

**Time:** 5-10 minutes

---

## 🎓 Advanced Tips

### Tip 1: Multiple Configurations

Keep separate caches for different builds:

```
project/
├── opencv_cache_release_cuda13/     # CUDA 13.0 Release
├── opencv_cache_release_cuda12/     # CUDA 12.x Release
└── opencv_cache_debug_cuda13/       # Debug build
```

Each has different compile flags/optimizations.

### Tip 2: Git Ignore Configuration

Add to `.gitignore`:
```gitignore
# Build directories (never commit!)
juce/build/
**/build/

# But keep archives separate
!opencv_cuda_cache_archive/
```

### Tip 3: Verification After Restore

```powershell
# Verify cache is valid
if (Test-Path "juce\build\_deps\opencv-build\lib\Release\opencv_world4130.lib") {
    $lib = Get-Item "juce\build\_deps\opencv-build\lib\Release\opencv_world4130.lib"
    if ($lib.Length -gt 1GB) {
        Write-Host "✓ Cache looks valid: $([math]::Round($lib.Length/1GB,2)) GB" -ForegroundColor Green
    }
}
```

### Tip 4: Quick Archive Script

Save this as `archive_opencv_cache.ps1`:

```powershell
#!/usr/bin/env pwsh
$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$archivePath = "opencv_cuda_cache_archive_$timestamp"

Write-Host "Creating OpenCV CUDA cache archive..." -ForegroundColor Cyan

New-Item -ItemType Directory -Force -Path $archivePath | Out-Null

# Copy critical directories
@("opencv-build", "opencv-src", "opencv_contrib-src") | ForEach-Object {
    $src = "juce\build\_deps\$_"
    if (Test-Path $src) {
        Write-Host "  Archiving $_..." -ForegroundColor Yellow
        Copy-Item -Recurse -Force $src "$archivePath\"
    }
}

# Copy cache file
Copy-Item -Force "juce\build\CMakeCache.txt" $archivePath -ErrorAction SilentlyContinue

# Copy FFmpeg DLL
New-Item -ItemType Directory -Force -Path "$archivePath\bin\Release" | Out-Null
Copy-Item -Force "juce\build\bin\Release\opencv_videoio_ffmpeg4130_64.dll" "$archivePath\bin\Release\" -ErrorAction SilentlyContinue

$size = (Get-ChildItem -Recurse -File $archivePath | Measure-Object -Property Length -Sum).Sum / 1GB
Write-Host "`n✓ Archive created: $archivePath" -ForegroundColor Green
Write-Host "  Size: $([math]::Round($size, 2)) GB" -ForegroundColor Cyan
Write-Host "`nCompress with: Compress-Archive -Path '$archivePath\*' -DestinationPath '$archivePath.zip'" -ForegroundColor Yellow
```

---

## 📚 Summary

### The Golden Rules

1. **Archive `opencv-build/` - It's 2.75 GB of GOLD** (30 min of CUDA compilation)
2. **Never change `GIT_TAG 4.x`** unless you want a full rebuild
3. **Archive immediately after successful build** - don't wait for disaster
4. **Test restore procedure** before you need it
5. **Keep archives on external storage** - not just in the same build folder

### Quick Reference

| Task | Command | Time |
|------|---------|------|
| Archive cache | Run `archive_opencv_cache.ps1` | 2-5 min |
| Restore cache | Copy archive to `_deps/` | 2-5 min |
| Fresh build | Delete build/, reconfigure, build | 30-45 min |
| Incremental build | `cmake --build` | 1-10 min |
| Build with cache | Restore, reconfigure, build | 5-10 min |

---

**🎯 Bottom line:** Those 2.75 GB in `opencv-build/` represent 30+ minutes of CUDA compilation. Treat it like precious cargo!

