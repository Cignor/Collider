# OpenCV CUDA Cache - Quick Reference Card

## 📦 Archive the Build (Do This Now!)

```powershell
cd H:\0000_CODE\01_collider_pyo
.\archive_opencv_cache.ps1
```

**Creates:** `opencv_cuda_cache_archive_YYYYMMDD_HHMM/` (~3.9 GB)  
**Time:** 2-5 minutes  
**Saves you:** 30+ minutes on future rebuilds

---

## 💾 Compress for Storage (Optional)

```powershell
$latest = (Get-ChildItem -Directory opencv_cuda_cache_archive_* | Sort-Object -Descending | Select -First 1).Name
Compress-Archive -Path "$latest\*" -DestinationPath "$latest.zip"
```

**Result:** ~1-1.5 GB compressed file

---

## 🔄 Restore from Archive

```powershell
# Automatic (uses latest archive)
.\restore_opencv_cache.ps1

# Or specify archive
.\restore_opencv_cache.ps1 opencv_cuda_cache_archive_20251101_1733
```

Then rebuild:
```powershell
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build juce/build --config Release
```

**Time:** 5-10 minutes (vs 30+ without cache)

---

## ⚠️ What Forces a Rebuild

| Change This | Rebuild Time |
|------------|--------------|
| OpenCV `GIT_TAG 4.x` → `4.11.0` | 30+ min |
| `WITH_CUDA ON` → `OFF` | 30+ min |
| CUDA architectures `8.6;8.9;12.0` | 30+ min |
| Delete `juce/build/_deps/opencv-build/` | 30+ min |
| Your .cpp files | 1-5 min ✅ |
| CMakeLists.txt (non-OpenCV parts) | 1-5 min ✅ |

---

## 🎯 Critical Files

**Must Archive:**
- `opencv-build/` (2.75 GB) ⭐ **CRITICAL**
- `opencv-src/` (320 MB)
- `opencv_contrib-src/` (90 MB)

**Bonus:**
- `CMakeCache.txt` (873 KB)
- `opencv_videoio_ffmpeg4130_64.dll` (27 MB)

---

## 🆘 Quick Troubleshooting

### Build fails with "cannot open x64.lib"
```powershell
# Check CMakeLists.txt - this line should NOT exist:
# set(CUDA_LIBRARIES "${CUDAToolkit_LIBRARY_DIR}" ...)
# If it exists, remove it
```

### Build fails with "cannot open generated_*.cu.obj"
```powershell
# MSBuild race condition - just retry 2-5 times
for ($i=1; $i -le 5; $i++) {
    cmake --build juce/build --config Release --target opencv_world
    if ($LASTEXITCODE -eq 0) { break }
}
```

### Want to force clean rebuild
```powershell
Remove-Item -Recurse -Force juce\build
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
cmake --build juce/build --config Release
```

---

## 📚 Full Documentation

- **Complete Guide:** `OPENCV_CUDA_BUILD_CACHE_GUIDE.md`
- **Build Summary:** `BUILD_SUCCESS_SUMMARY.md`
- **CUDA Integration:** `CUDA_OPENCV_INTEGRATION_SUMMARY.md`

---

**Bottom Line:** Archive now, thank yourself later! 🎉

