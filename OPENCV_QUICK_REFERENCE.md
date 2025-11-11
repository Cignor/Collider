# OpenCV CUDA Quick Reference

## 🚀 First Time Setup

```powershell
# Build OpenCV once (30-45 minutes, ONE TIME)
.\build_opencv_cuda_once.ps1

# Optional: Build both Release and Debug
.\build_opencv_cuda_once.ps1 -Configuration Both

# Then build your project (5-10 minutes)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
cmake --build juce/build --config Release

# Or Debug
cmake --build juce/build --config Debug --target PresetCreatorApp
```

**Result:** Add nodes freely - OpenCV NEVER rebuilds! 🎉

---

## 📋 Common Commands

### Build OpenCV

```powershell
# Release (default)
.\build_opencv_cuda_once.ps1

# Debug
.\build_opencv_cuda_once.ps1 -Configuration Debug

# Both
.\build_opencv_cuda_once.ps1 -Configuration Both
```

### Archive & Restore

```powershell
# Backup OpenCV build
.\archive_opencv_standalone.ps1

# Restore from backup
.\restore_opencv_standalone.ps1

# Compress archive
Compress-Archive opencv_standalone_* opencv_cuda.zip
```

### Build Main Project

```powershell
# Configure (finds pre-built OpenCV)
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build juce/build --config Release

# Build Debug (requires Debug OpenCV)
cmake --build juce/build --config Debug --target PresetCreatorApp

# Clean rebuild (OpenCV not affected!)
Remove-Item -Recurse -Force juce\build
cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64
cmake --build juce/build --config Release
```

### Test Build System

```powershell
# Run comprehensive test suite
.\test_opencv_build_system.ps1

# Test specific configuration
.\test_opencv_build_system.ps1 -Configuration Release
.\test_opencv_build_system.ps1 -Configuration Debug
```

---

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| "Pre-Built OpenCV NOT Found" | `.\build_opencv_cuda_once.ps1` |
| Debug build link errors | `.\build_opencv_cuda_once.ps1 -Configuration Debug` |
| Want to rebuild OpenCV | `Remove-Item -Recurse opencv_cuda_install` then rebuild |
| Corrupted installation | Same as above |
| Test if system works | `.\test_opencv_build_system.ps1` |
| OpenCV still rebuilding | Delete `juce\build\CMakeCache.txt`, reconfigure |

---

## 📂 Key Files

| File/Directory | Purpose |
|----------------|---------|
| `opencv_cuda_install/` | Pre-built OpenCV (don't delete!) |
| `opencv_build/` | Standalone builder |
| `build_opencv_cuda_once.ps1` | Build OpenCV script |
| `archive_opencv_standalone.ps1` | Backup script |
| `restore_opencv_standalone.ps1` | Restore script |
| `test_opencv_build_system.ps1` | Test suite (verifies everything works) |
| `guides/OPENCV_CUDA_NO_REBUILD_GUIDE.md` | Full documentation |

---

## ⚠️ Important Notes

- **Do NOT delete** `opencv_cuda_install/` unless you want to rebuild
- **Do archive** before system upgrades or major changes
- **Build time:** 30-45 minutes for OpenCV (one-time)
- **Main project:** 5-10 minutes (every time, but OpenCV doesn't rebuild!)

---

## 🎯 Workflow

```
1. Build OpenCV (once)       → 45 min ⏰
2. Archive it (backup)       → 2 min
3. Build main project        → 5 min
4. Add 100 nodes             → 5 min each (no OpenCV rebuild!)
5. Profit! 🎉
```

**See full guide:** `guides/OPENCV_CUDA_NO_REBUILD_GUIDE.md`

