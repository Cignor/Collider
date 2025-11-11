# .gitignore Recommendations for OpenCV CUDA System

## Recommended Entries

Add these to your `.gitignore` file:

```gitignore
# ==============================================================================
# OpenCV CUDA Standalone Build System
# ==============================================================================

# Pre-built OpenCV installation (1.5-2 GB, rebuild with script)
opencv_cuda_install/

# OpenCV standalone build cache (2-3 GB, rebuild with script)
opencv_build/build/
opencv_build/*.user

# Standalone OpenCV archives (1.5-2 GB each)
opencv_standalone_*/
opencv_cuda_cache_archive_*/
opencv_cuda_cache_debug_archive_*/

# Compressed archives (optional - keep these if you want team distribution)
# opencv_standalone_*.zip

# ==============================================================================
# Main Project Build
# ==============================================================================

# Main project build directory
juce/build/
juce/*.user

# Visual Studio artifacts
.vs/
*.vcxproj.user
*.suo
*.sln
```

---

## What to Commit vs. Ignore

### ✅ DO Commit (Build System)

```
opencv_build/
├── CMakeLists.txt                    ← Commit (standalone builder)
├── OpenCVConfig.cmake.in             ← Commit (find_package config)
└── README.md                          ← Commit (optional docs)

build_opencv_cuda_once.ps1             ← Commit (build script)
archive_opencv_standalone.ps1          ← Commit (archive script)
restore_opencv_standalone.ps1          ← Commit (restore script)
guides/OPENCV_CUDA_NO_REBUILD_GUIDE.md ← Commit (documentation)
```

### ❌ DON'T Commit (Generated Artifacts)

```
opencv_cuda_install/                   ← Ignore (1.5-2 GB, generated)
opencv_build/build/                    ← Ignore (2-3 GB, build cache)
opencv_standalone_*/                   ← Ignore (archive folders)
juce/build/                            ← Ignore (main build)
```

---

## Optional: Commit Pre-Built OpenCV

### Pros
- Team members skip 45-minute build
- CI/CD pipelines run faster
- Guaranteed consistent OpenCV version

### Cons
- **1.5-2 GB added to repository**
- Git operations become slower
- Requires Git LFS for efficiency

### How to Enable (If Desired)

1. Remove from `.gitignore`:
   ```diff
   - opencv_cuda_install/
   ```

2. Use Git LFS (recommended for large files):
   ```bash
   git lfs track "opencv_cuda_install/**/*.lib"
   git lfs track "opencv_cuda_install/**/*.dll"
   ```

3. Commit:
   ```bash
   git add opencv_cuda_install/
   git commit -m "Add pre-built OpenCV CUDA"
   ```

**Recommendation:** Use archive distribution instead (see below).

---

## Recommended Approach: Archive Distribution

Instead of committing to Git, distribute archives:

### 1. Create Archive After Building

```powershell
.\build_opencv_cuda_once.ps1
.\archive_opencv_standalone.ps1
Compress-Archive opencv_standalone_* opencv_cuda_prebuilt.zip
```

### 2. Upload to Team Storage

- OneDrive / Google Drive / Dropbox
- Internal network share
- CI/CD artifact storage
- GitHub Releases (if public repo)

### 3. Team Members Download & Restore

```powershell
# Download opencv_cuda_prebuilt.zip from shared location
Expand-Archive opencv_cuda_prebuilt.zip -DestinationPath .
.\restore_opencv_standalone.ps1
```

### 4. CI/CD Integration

```yaml
# .github/workflows/build.yml
- name: Restore OpenCV from Cache
  uses: actions/cache@v3
  with:
    path: opencv_cuda_install
    key: opencv-cuda-${{ hashFiles('opencv_build/CMakeLists.txt') }}
    
- name: Build OpenCV if Not Cached
  if: steps.cache.outputs.cache-hit != 'true'
  run: .\build_opencv_cuda_once.ps1
```

---

## Migration from Old System

If you were using the old inline build system:

```diff
# OLD entries (can keep for backward compatibility)
juce/build/_deps/opencv-*
juce/build/bin/*/opencv_videoio_ffmpeg*.dll

# NEW entries (add these)
+ opencv_cuda_install/
+ opencv_build/build/
+ opencv_standalone_*/
```

---

## Full Recommended `.gitignore`

```gitignore
# ==============================================================================
# OpenCV CUDA Build System (Standalone)
# ==============================================================================
opencv_cuda_install/
opencv_build/build/
opencv_build/*.user
opencv_standalone_*/
opencv_cuda_cache_archive_*/
opencv_cuda_cache_debug_archive_*/

# ==============================================================================
# Main Project Build
# ==============================================================================
juce/build/
juce/*.user
*.vcxproj
*.vcxproj.filters
*.sln

# ==============================================================================
# Visual Studio
# ==============================================================================
.vs/
*.suo
*.user
*.userosscache
*.sln.docstates
x64/
Debug/
Release/

# ==============================================================================
# Build Artifacts
# ==============================================================================
*.obj
*.lib
*.dll
*.exe
*.pdb
*.ilk

# ==============================================================================
# OS Files
# ==============================================================================
.DS_Store
Thumbs.db
desktop.ini

# ==============================================================================
# Vendor Dependencies (if not pre-committed)
# ==============================================================================
vendor/ffmpeg/
vendor/piper/
vendor/onnxruntime/
```

---

## Summary

**Recommended setup:**
1. ✅ Commit build scripts (`build_opencv_cuda_once.ps1`, etc.)
2. ✅ Commit standalone builder (`opencv_build/CMakeLists.txt`)
3. ❌ Ignore pre-built artifacts (`opencv_cuda_install/`)
4. 📦 Distribute pre-built via archives (team/CI/CD)

**Result:** Fast clones, consistent builds, no wasted Git space!

