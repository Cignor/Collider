# OpenCV CUDA - Migration Guide

**From:** Inline FetchContent Build (rebuilds on CMake changes)  
**To:** Standalone Build System (never rebuilds!)

**Date:** November 10, 2025

---

## 🎯 Why Migrate?

### Problem (Before)
```
Add new node to CMakeLists.txt
    ↓
Reconfigure CMake
    ↓
OpenCV detects change
    ↓
😱 Full OpenCV CUDA rebuild (30-45 minutes)
    ↓
🤬 Repeat for every change!
```

### Solution (After)
```
Build OpenCV once (45 minutes, ONE TIME)
    ↓
Add 100 nodes to CMakeLists.txt
    ↓
Reconfigure CMake (30 seconds)
    ↓
😎 OpenCV sees pre-built library
    ↓
🎉 No rebuild! (5-10 minutes total)
```

**Time saved per change:** 40+ minutes  
**Time saved after 10 changes:** ~7 hours!

---

## 📋 Migration Steps

### Step 1: Verify Current System (Optional)

Check if you have inline build:

```powershell
Test-Path juce\build\_deps\opencv-build\lib\Release\opencv_world4130.lib
```

**If True:** You have inline build → Migrate recommended  
**If False:** Fresh setup → Skip to Step 2

### Step 2: Archive Current OpenCV Build (Optional Safety)

If you want to keep your current build as backup:

```powershell
# OLD system (if you have it)
.\archive_opencv_cache.ps1

# This creates a backup you can restore if needed
```

### Step 3: Build Standalone OpenCV

```powershell
# Build OpenCV in isolation (45 minutes)
.\build_opencv_cuda_once.ps1
```

**What happens:**
- Fetches OpenCV 4.x + contrib
- Builds with CUDA 13.0 + cuDNN
- Installs to `opencv_cuda_install/`
- **Completely isolated from main project**

**Output to look for:**
```
========================================
  OpenCV CUDA Standalone Builder
========================================
[1/3] Configuring CMake...
[2/3] Building OpenCV with CUDA (this takes 30-45 minutes)...
  Grab a coffee, this will take a while...
[3/3] Installing to opencv_cuda_install...
✓ Release build completed in 00:42:15

Installation Complete!
  ✓ opencv_world4130.lib: 458.3 MB
  ✓ Headers installed
```

### Step 4: Clean Main Project Build

```powershell
# Remove old build (forces full reconfigure)
Remove-Item -Recurse -Force juce\build

# Or just remove CMake cache
Remove-Item juce\build\CMakeCache.txt
```

### Step 5: Reconfigure Main Project

```powershell
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
```

**Expected output (SUCCESS):**
```
========================================
  Found Pre-Built OpenCV with CUDA!
========================================
Location: H:/0000_CODE/01_collider_pyo/opencv_cuda_install

✓ Using cached build (no rebuild needed!)
  You can modify CMakeLists.txt without rebuilding OpenCV!
```

**If you see "Pre-Built OpenCV NOT Found":**
- Check Step 3 completed successfully
- Verify `opencv_cuda_install\lib\opencv_world4130.lib` exists

### Step 6: Build Main Project

```powershell
cmake --build juce/build --config Release
```

**Expected time:** 5-10 minutes (no OpenCV rebuild!)

### Step 7: Test Application

```powershell
cd juce\build\Source\Release
.\ColliderApp.exe
```

**Verify OpenCV CUDA works:**
- Load a webcam module
- Check console for CUDA messages
- Verify frame processing works

### Step 8: Archive Standalone Build (Recommended)

```powershell
.\archive_opencv_standalone.ps1
```

**Creates backup** (~1.5-2 GB) for:
- System failures
- Team distribution
- CI/CD pipelines

### Step 9: Clean Up Old Build (Optional)

If Step 6-7 successful and you don't need the old build:

```powershell
# Remove old inline build cache (saves 2-3 GB)
Remove-Item -Recurse -Force juce\build\_deps\opencv-*
```

---

## ✅ Verification Checklist

After migration, verify everything works:

- [ ] `opencv_cuda_install/` directory exists
- [ ] `opencv_cuda_install/lib/opencv_world4130.lib` exists (~450 MB)
- [ ] `opencv_cuda_install/include/opencv2/opencv.hpp` exists
- [ ] Main project builds successfully (5-10 minutes)
- [ ] Application runs and loads video modules
- [ ] CUDA messages appear in console
- [ ] Created archive with `archive_opencv_standalone.ps1`
- [ ] Added `.gitignore` entries (see `GITIGNORE_RECOMMENDATIONS.md`)

---

## 🔄 Rollback (If Needed)

If something goes wrong, you can rollback:

### Option A: Restore Old Inline Build

```powershell
# If you created archive in Step 2
.\restore_opencv_cache.ps1

# Rebuild main project (will use inline OpenCV)
Remove-Item -Recurse -Force juce\build
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
cmake --build juce/build --config Release
```

### Option B: Start Fresh

```powershell
# Delete everything
Remove-Item -Recurse -Force opencv_cuda_install
Remove-Item -Recurse -Force opencv_build\build
Remove-Item -Recurse -Force juce\build

# Rebuild from scratch (inline build)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
cmake --build juce/build --config Release
```

**Note:** The CMakeLists.txt has fallback logic - if standalone build doesn't exist, it automatically uses inline build!

---

## 📊 Before vs. After Comparison

### Scenario: Add 5 new modules to project

#### Before (Inline Build)
```
Day 1: Initial build           → 45 min
Day 2: Add AudioModule1         → 42 min (OpenCV rebuilds!)
Day 3: Add VideoModule1         → 40 min (OpenCV rebuilds!)
Day 4: Add FXModule1            → 38 min (OpenCV rebuilds!)
Day 5: Add AnalysisModule1      → 41 min (OpenCV rebuilds!)
Day 6: Add ControlModule1       → 39 min (OpenCV rebuilds!)
─────────────────────────────────────────
Total: 245 minutes (4 hours 5 minutes)
```

#### After (Standalone Build)
```
Day 1: Build OpenCV once        → 45 min (one-time!)
Day 1: Initial build            → 6 min
Day 2: Add AudioModule1         → 5 min (no OpenCV rebuild!)
Day 3: Add VideoModule1         → 6 min (no OpenCV rebuild!)
Day 4: Add FXModule1            → 5 min (no OpenCV rebuild!)
Day 5: Add AnalysisModule1      → 6 min (no OpenCV rebuild!)
Day 6: Add ControlModule1       → 5 min (no OpenCV rebuild!)
─────────────────────────────────────────
Total: 78 minutes (1 hour 18 minutes)

SAVED: 167 minutes (2 hours 47 minutes!)
```

---

## 🆘 Troubleshooting Migration Issues

### Issue: "Pre-Built OpenCV NOT Found" after Step 3

**Cause:** Standalone build failed or incomplete

**Fix:**
```powershell
# Check if library exists
Test-Path opencv_cuda_install\lib\opencv_world4130.lib

# If False, rebuild
Remove-Item -Recurse -Force opencv_cuda_install
.\build_opencv_cuda_once.ps1

# Check build log for errors
```

### Issue: Link errors like "unresolved external symbol cv::..."

**Cause:** Main project using wrong OpenCV or mixed Debug/Release

**Fix:**
```powershell
# For Release build issues
cmake --build juce/build --config Release --target opencv_world
cmake --build juce/build --config Release

# For Debug build issues
.\build_opencv_cuda_once.ps1 -Configuration Debug
cmake --build juce/build --config Debug
```

### Issue: Application crashes with OpenCV errors

**Cause:** DLLs not copied or wrong OpenCV version

**Fix:**
```powershell
# Check DLLs were copied
Test-Path juce\build\Source\Release\opencv_videoio_ffmpeg4130_64.dll

# If missing, rebuild
cmake --build juce/build --config Release
```

### Issue: Want to go back to inline build

**Fix:**
```powershell
# Just delete standalone installation
Remove-Item -Recurse -Force opencv_cuda_install

# Reconfigure (will automatically use inline build)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
```

**The CMakeLists.txt has automatic fallback!**

---

## 🎓 Understanding the New System

### Architecture

```
┌─────────────────────────────────────────────────────┐
│  opencv_build/                  (Standalone Builder) │
│  ├── CMakeLists.txt            (Isolated build)      │
│  ├── OpenCVConfig.cmake.in    (find_package)         │
│  └── build/                    (Build cache)         │
│      └── _deps/                                      │
│          ├── opencv-src/       (Source)              │
│          ├── opencv-build/     (Build artifacts)     │
│          └── opencv_contrib-src/ (Contrib)           │
└─────────────────────────────────────────────────────┘
                        ↓ Builds once
┌─────────────────────────────────────────────────────┐
│  opencv_cuda_install/          (Pre-built OpenCV)   │
│  ├── lib/                                            │
│  │   ├── opencv_world4130.lib  (Release)            │
│  │   └── opencv_world4130d.lib (Debug, optional)    │
│  └── include/                                        │
│      └── opencv2/...                                 │
└─────────────────────────────────────────────────────┘
                        ↓ Found by
┌─────────────────────────────────────────────────────┐
│  juce/CMakeLists.txt           (Main Project)       │
│  Checks if pre-built exists:                         │
│  ✓ Yes → Use IMPORTED target (no rebuild)           │
│  ✗ No  → FetchContent build (fallback)              │
└─────────────────────────────────────────────────────┘
```

### Key Differences

| Aspect | Inline Build | Standalone Build |
|--------|--------------|------------------|
| Location | `juce/build/_deps/` | `opencv_cuda_install/` |
| CMake Project | Part of main | Separate |
| Rebuild Trigger | CMakeLists.txt changes | Only if deleted |
| Build Time | 30-45 min per change | 45 min once |
| Cache | Fragile | Persistent |
| Distribution | Via Git or archive | Via archive |

---

## 📚 Related Documentation

| Document | Purpose |
|----------|---------|
| `guides/OPENCV_CUDA_NO_REBUILD_GUIDE.md` | Complete system documentation |
| `OPENCV_QUICK_REFERENCE.md` | Quick command reference |
| `GITIGNORE_RECOMMENDATIONS.md` | Git configuration |
| `OPENCV_MIGRATION_GUIDE.md` | This file |
| `guides/CUDA_GPU_IMPLEMENTATION_SUMMARY.md` | GPU acceleration guide |

---

## 🎉 Success!

After migration:
- ✅ OpenCV built once, reused forever
- ✅ Add nodes without 30-45 minute rebuilds
- ✅ Archive for backup and team distribution
- ✅ Faster development workflow
- ✅ More coffee time ☕

**Questions?** See troubleshooting sections or documentation files above.

