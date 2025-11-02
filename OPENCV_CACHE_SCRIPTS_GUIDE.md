# OpenCV CUDA Cache Management Scripts

Three PowerShell scripts to manage and verify your OpenCV CUDA build cache.

---

## 📋 Scripts Overview

### 1. `verify_opencv_cache.ps1` - Cache Integrity Checker
**Purpose:** Verify that your cached OpenCV CUDA build is complete and not corrupted

**When to use:**
- ✅ Before starting a build to check cache health
- ✅ After a failed build to diagnose issues
- ✅ After restoring from backup
- ✅ When experiencing weird build errors

### 2. `regenerate_opencv_config.ps1` - Config File Regenerator ⚡
**Purpose:** Quickly regenerate `CMakeCache.txt` and `cvconfig.h` without rebuilding (10-30 seconds!)

**When to use:**
- ✅ When verification shows optional files missing
- ✅ After restoring OpenCV cache from backup
- ✅ When you need config files but don't want to rebuild

### 3. `build_opencv_retry.ps1` - Retry Build with Race Condition Handling
**Purpose:** Automatically retry OpenCV CUDA compilation when MSBuild/NVCC race conditions occur

**When to use:**
- ✅ During initial OpenCV build
- ✅ When you see "file is being used by another process" errors
- ✅ After modifying OpenCV configuration

---

## 🚀 Quick Start

### **Step 1: Verify Cache Before Building**
```powershell
cd H:\0000_CODE\01_collider_pyo
.\verify_opencv_cache.ps1
```

**Example output:**
```
================================================================
     OpenCV CUDA Cache Integrity Verification
================================================================

Test 1: Build Directory Structure
---------------------------------------------------------------
[OK] OpenCV build directory exists
[OK] OpenCV source directory exists
[OK] OpenCV contrib source directory exists

Test 2: Critical Build Artifacts
---------------------------------------------------------------
[OK] opencv_world library (245.32 MB)
[OK] CMake cache (198.45 KB)
[OK] opencv2 config (12.45 KB)
[OK] OpenCV config header (8.23 KB)

...

================================================================
  ✓ VERDICT: OpenCV cache appears HEALTHY
================================================================

You can proceed with building your project.
```

### **Step 2a: Regenerate Config Files (if needed)**
If verification shows optional files missing:
```powershell
.\regenerate_opencv_config.ps1
```

### **Step 2b: Build with Retry (if cache is healthy or doesn't exist)**
```powershell
.\build_opencv_retry.ps1
```

---

## 📖 Detailed Usage

### `verify_opencv_cache.ps1`

#### **Basic Usage:**
```powershell
.\verify_opencv_cache.ps1
```

#### **Verbose Mode (show all checks):**
```powershell
.\verify_opencv_cache.ps1 -Verbose
```

#### **Deep Scan (check all object files):**
```powershell
.\verify_opencv_cache.ps1 -Deep
```

#### **Combined:**
```powershell
.\verify_opencv_cache.ps1 -Verbose -Deep
```

---

### `regenerate_opencv_config.ps1` ⚡

#### **Basic Usage:**
```powershell
.\regenerate_opencv_config.ps1
```

#### **Regenerate and Verify:**
```powershell
.\regenerate_opencv_config.ps1 -Verify
```

**What it does:**
1. Checks if `CMakeCache.txt` and `cvconfig.h` exist
2. If missing, runs CMake configure to regenerate them (10-30 seconds)
3. Verifies the files were created successfully
4. Optionally runs verification afterward

**Benefits:**
- ⚡ **Fast**: 10-30 seconds vs 30-60 minutes for full rebuild
- 🎯 **Targeted**: Only regenerates config files, doesn't touch compiled code
- ✅ **Safe**: Doesn't affect your existing `.lib` files

---

### What It Checks

#### **Test 1: Build Directory Structure**
- ✅ OpenCV build directory exists
- ✅ OpenCV source directory exists
- ✅ OpenCV contrib source directory exists

#### **Test 2: Critical Build Artifacts**
- ✅ `opencv_world4130.lib` exists
- ✅ CMake cache file exists
- ✅ Configuration headers exist

#### **Test 3: CUDA Compilation Artifacts**
- ✅ CUDA object files (`.cu.obj`) exist
- 📊 Count of CUDA intermediate files

#### **Test 4: Main Library Integrity**
- ✅ Library file is at least 200 MB (full CUDA build)
- ✅ Library file is not locked (not being written)

#### **Test 5: CUDA Module Libraries**
- ✅ CUDA modules detected (cudaarithm, cudafilters, etc.)

#### **Test 6: Build Configuration**
- ✅ `WITH_CUDA:BOOL=ON`
- ✅ `OPENCV_DNN_CUDA:BOOL=ON`
- ✅ `WITH_CUDNN:BOOL=ON`
- ✅ CUDA architectures specified

#### **Test 7: Build Timestamps**
- 📅 Shows when library was last built
- ⚠️ Warns if build is over 30 days old

#### **Test 8: Deep Integrity Check** (with `-Deep` flag)
- 📊 Counts all object files
- ⚠️ Detects zero-byte files (corruption indicator)
- 📊 Calculates total cache size

#### **Test 9: Linkability Test**
- ✅ Library file has valid archive signature
- ✅ File can be read without errors

---

## 🎯 Exit Codes

| Exit Code | Meaning | Action |
|-----------|---------|--------|
| **0** | ✅ Healthy or minor issues | Safe to build |
| **1** | ❌ Critical errors or corruption | Rebuild cache |

---

## 🔄 Typical Workflow

### **Scenario 1: First Build**

```powershell
# 1. Configure project
cmake -B juce/build -S juce -G "Visual Studio 17 2022"

# 2. Build with retry (handles race conditions)
.\build_opencv_retry.ps1

# 3. Verify the built cache
.\verify_opencv_cache.ps1 -Deep
```

### **Scenario 2: Resuming After Failure**

```powershell
# 1. Check if cache is corrupted
.\verify_opencv_cache.ps1

# 2a. If only config files missing, regenerate them
.\regenerate_opencv_config.ps1

# 2b. If healthy, retry build
.\build_opencv_retry.ps1

# 2c. If corrupted, clean and rebuild
Remove-Item -Path juce\build\_deps\opencv-build -Recurse -Force
cmake -B juce/build -S juce -G "Visual Studio 17 2022"
.\build_opencv_retry.ps1
```

### **Scenario 3: Before Important Build**

```powershell
# Quick health check
.\verify_opencv_cache.ps1

# If healthy, proceed with your build
cmake --build juce/build --config Release
```

---

## 🛠️ Interpreting Results

### **Healthy Cache**
```
Passed: 25
Failed: 0
Warnings: 0

✓ VERDICT: OpenCV cache appears HEALTHY
```
**Action:** Proceed with building

### **Minor Issues**
```
Passed: 22
Failed: 3
Warnings: 2

⚠ VERDICT: OpenCV cache has minor issues
```
**Action:** Try building, may need to rebuild if errors occur

### **Corrupted Cache**
```
Passed: 10
Failed: 15
Warnings: 5

❌ VERDICT: OpenCV cache appears CORRUPTED
```
**Action:** Delete `juce/build/_deps/opencv-build` and rebuild

---

## 🐛 Common Issues

### **Issue 1: "opencv_world library is too small"**
```
[WARN] opencv_world4130.lib is too small (50.2 MB, expected >200 MB)
```
**Cause:** Incomplete build or CUDA modules not compiled

**Fix:**
```powershell
.\build_opencv_retry.ps1  # Retry build
```

### **Issue 2: "No CUDA object files found"**
```
[WARN] No CUDA object files found (build may be incomplete)
```
**Cause:** CUDA compilation never ran or failed

**Fix:**
```powershell
# Reconfigure with CUDA
cmake -B juce/build -S juce -G "Visual Studio 17 2022"
.\build_opencv_retry.ps1
```

### **Issue 3: "Library file is locked"**
```
[FAIL] Library file is locked (build in progress?)
```
**Cause:** Another process is using the library file

**Fix:**
```powershell
# Stop any MSBuild processes
Get-Process MSBuild -ErrorAction SilentlyContinue | Stop-Process -Force

# Retry verification
.\verify_opencv_cache.ps1
```

### **Issue 4: "Zero-byte object files found"**
```
[WARN] Found 5 zero-byte object files (may indicate corruption)
```
**Cause:** Build was interrupted or disk write failed

**Fix:**
```powershell
# Clean and rebuild
cmake --build juce/build --config Release --target opencv_world --clean-first
.\build_opencv_retry.ps1
```

---

## 📊 Performance Tips

### **1. Verify Before Building**
Always run verification before starting a long build:
```powershell
.\verify_opencv_cache.ps1 && .\build_opencv_retry.ps1
```

### **2. Use Deep Scan Periodically**
Run deep scan once a week to catch creeping corruption:
```powershell
.\verify_opencv_cache.ps1 -Deep
```

### **3. Archive Healthy Cache**
After successful verification, archive the cache:
```powershell
# Verify first
.\verify_opencv_cache.ps1

# If healthy, archive
Compress-Archive -Path juce\build\_deps\opencv-build -DestinationPath opencv_cache_backup.zip
```

---

## 🎛️ Advanced Usage

### **Automated Build Pipeline**
```powershell
# Complete automated build with verification
$ErrorActionPreference = "Stop"

Write-Host "Step 1: Verify cache..." -ForegroundColor Cyan
.\verify_opencv_cache.ps1
if ($LASTEXITCODE -ne 0) {
    Write-Host "Cache corrupted, cleaning..." -ForegroundColor Yellow
    Remove-Item -Path juce\build\_deps\opencv-build -Recurse -Force -ErrorAction SilentlyContinue
    cmake -B juce/build -S juce -G "Visual Studio 17 2022"
}

Write-Host "Step 2: Build with retry..." -ForegroundColor Cyan
.\build_opencv_retry.ps1
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Step 3: Verify build..." -ForegroundColor Cyan
.\verify_opencv_cache.ps1 -Deep
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Build complete and verified!" -ForegroundColor Green
```

### **Scheduled Cache Health Check**
Add to Windows Task Scheduler to run daily:
```powershell
# Task Scheduler: Run daily at 2 AM
.\verify_opencv_cache.ps1 -Deep > opencv_health_log.txt
```

---

## 📚 Quick Reference

| Command | Purpose | Use When |
|---------|---------|----------|
| `.\verify_opencv_cache.ps1` | Quick health check | Before building |
| `.\verify_opencv_cache.ps1 -Verbose` | Detailed health check | Diagnosing issues |
| `.\verify_opencv_cache.ps1 -Deep` | Comprehensive scan | Weekly maintenance |
| `.\build_opencv_retry.ps1` | Build with auto-retry | Initial build or after failure |
| `.\build_opencv_retry.ps1 -MaxRetries 20` | More retry attempts | Persistent race conditions |

---

## ✅ Best Practices

1. **Always verify before building** - Saves time by catching corruption early
2. **Run deep scan weekly** - Catches creeping corruption
3. **Archive after successful build** - Quick restore if corruption occurs
4. **Monitor warnings** - They indicate potential future issues
5. **Clean build if >5 failures** - Don't waste time retrying corrupted cache

---

## 🎉 Summary

These scripts work together to:
- ✅ **Verify** cache integrity before building
- ✅ **Retry** compilation when race conditions occur
- ✅ **Detect** corruption early
- ✅ **Save time** by avoiding unnecessary rebuilds
- ✅ **Provide clear feedback** on cache health

**Typical usage:**
```powershell
# Verify → Build → Verify
.\verify_opencv_cache.ps1 && .\build_opencv_retry.ps1 && .\verify_opencv_cache.ps1 -Deep
```

Happy building! 🚀

