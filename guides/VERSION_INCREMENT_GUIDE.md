# Version Increment Guide - Pikon Raditsz

**Date:** November 29, 2025  
**Current Version:** 0.6.2-beta  
**Target Version:** 0.6.5-beta

---

## Overview

This guide explains how to increment the application version number in **Pikon Raditsz**. The version system uses a **single source of truth** approach: update one file, and all other systems (auto-updater, manifest generation, UI display) automatically use the new version.

---

## Current Situation

### What Should Happen

When you increment the version:
1. **VersionInfo.h** should be updated with new version numbers
2. **Manifest generation** should automatically detect the new version
3. **Auto-updater** should recognize the new version from the manifest
4. **UI displays** should show the new version (splash screen, about dialog, etc.)

### What It's Currently Doing

✅ **Working correctly:**
- Version is centralized in `juce/Source/utils/VersionInfo.h`
- Manifest generation script (`quick_generate.ps1`) auto-detects version from VersionInfo.h
- Auto-updater reads version from manifest and compares with installed version
- UI components use `VersionInfo::getFullVersionString()` to display version

⚠️ **Potential issues:**
- VersionManager has a hardcoded default fallback (`"0.6.2"`) that should match VersionInfo.h
- If version strings don't match exactly, update checks may fail

---

## Version Number Structure

The version follows **semantic versioning** format: `MAJOR.MINOR.PATCH[-SUFFIX]`

**Example:** `0.6.5-beta`
- **MAJOR:** 0 (major API changes)
- **MINOR:** 6 (feature additions)
- **PATCH:** 5 (bug fixes, small changes)
- **SUFFIX:** `-beta` (pre-release identifier)

---

## Files That Need Updating

### 1. Primary Version Definition (REQUIRED)

**File:** `juce/Source/utils/VersionInfo.h`

**Location:** Lines 19-23

**Current values:**
```cpp
static constexpr const char* VERSION = "0.6";
static constexpr const char* VERSION_FULL = "0.6.2-beta";
static constexpr int         VERSION_MAJOR = 0;
static constexpr int         VERSION_MINOR = 6;
static constexpr int         VERSION_PATCH = 2;
```

**Update to 0.6.5:**
```cpp
static constexpr const char* VERSION = "0.6";           // Keep as-is (minor version)
static constexpr const char* VERSION_FULL = "0.6.5-beta"; // ← Change this
static constexpr int         VERSION_MAJOR = 0;           // Keep as-is
static constexpr int         VERSION_MINOR = 6;           // Keep as-is
static constexpr int         VERSION_PATCH = 5;           // ← Change this
```

**Why:** This is the **single source of truth**. All other systems read from here.

---

### 2. VersionManager Default Fallback (OPTIONAL - but recommended)

**File:** `juce/Source/updater/VersionManager.cpp`

**Location:** Line 7 (constructor)

**Current value:**
```cpp
: currentVersion("0.6.2") // Default version
```

**Update to:**
```cpp
: currentVersion("0.6.5") // Default version
```

**Why:** This is only used as a fallback if `installed_files.json` doesn't exist. It should match the patch version from VersionInfo.h to avoid confusion.

---

## Files That Auto-Detect Version (NO CHANGES NEEDED)

These files automatically read from `VersionInfo.h` or the manifest:

### ✅ Manifest Generation Script
- **File:** `updater/quick_generate.ps1`
- **Behavior:** Auto-detects `VERSION_FULL` from VersionInfo.h
- **Action:** No changes needed

### ✅ Auto-Updater System
- **Files:** `UpdateChecker.cpp`, `UpdateManager.cpp`
- **Behavior:** Reads version from manifest and compares with installed version
- **Action:** No changes needed

### ✅ UI Components
- **Files:** Any component using `VersionInfo::getFullVersionString()`
- **Behavior:** Automatically uses the latest value from VersionInfo.h
- **Action:** No changes needed

---

## Step-by-Step Version Increment Process

### Step 1: Update VersionInfo.h

1. Open `juce/Source/utils/VersionInfo.h`
2. Update `VERSION_FULL` to `"0.6.5-beta"` (or `"0.6.5"` if removing beta suffix)
3. Update `VERSION_PATCH` to `5`
4. Keep `VERSION`, `VERSION_MAJOR`, and `VERSION_MINOR` unchanged (unless doing a minor/major bump)

**Example for 0.6.5:**
```cpp
static constexpr const char* VERSION_FULL = "0.6.5-beta";
static constexpr int         VERSION_PATCH = 5;
```

### Step 2: Update VersionManager Fallback (Optional)

1. Open `juce/Source/updater/VersionManager.cpp`
2. Update the default version in the constructor to match the patch version
3. Change `"0.6.2"` to `"0.6.5"`

**Why optional:** This is only used if `installed_files.json` doesn't exist. But keeping it in sync prevents confusion.

### Step 3: Rebuild Application

```powershell
# Build in Release mode
cmake --build juce\build-ninja-release --config Release --target PresetCreatorApp
```

**Why:** The new version constants are compiled into the executable.

### Step 4: Generate New Manifest

```powershell
# Run the manifest generator (auto-detects version from VersionInfo.h)
.\updater\quick_generate.ps1
```

**What happens:**
- Script reads `VERSION_FULL` from VersionInfo.h
- Generates `manifest.json` with version `0.6.5-beta`
- All file hashes are calculated for the new build

### Step 5: Verify Version Consistency

Check that all systems show the same version:

1. **In-code version:**
   ```cpp
   VersionInfo::getFullVersionString() // Should return "0.6.5-beta"
   ```

2. **Manifest version:**
   ```json
   {
     "latestVersion": "0.6.5-beta",
     ...
   }
   ```

3. **UI display:** Splash screen, about dialog, update dialog should all show `0.6.5-beta`

---

## Version Bump Scenarios

### Patch Version (0.6.2 → 0.6.5)
**Use case:** Bug fixes, small improvements

**Changes needed:**
- ✅ Update `VERSION_FULL` to `"0.6.5-beta"`
- ✅ Update `VERSION_PATCH` to `5`
- ✅ Update VersionManager fallback to `"0.6.5"`

**Keep unchanged:**
- `VERSION` = `"0.6"`
- `VERSION_MAJOR` = `0`
- `VERSION_MINOR` = `6`

---

### Minor Version (0.6.5 → 0.7.0)
**Use case:** New features, backward-compatible changes

**Changes needed:**
- ✅ Update `VERSION_FULL` to `"0.7.0-beta"`
- ✅ Update `VERSION` to `"0.7"`
- ✅ Update `VERSION_MINOR` to `7`
- ✅ Update `VERSION_PATCH` to `0`
- ✅ Update VersionManager fallback to `"0.7.0"`

**Keep unchanged:**
- `VERSION_MAJOR` = `0`

---

### Major Version (0.7.0 → 1.0.0)
**Use case:** Breaking changes, major rewrites

**Changes needed:**
- ✅ Update `VERSION_FULL` to `"1.0.0"` (remove `-beta` for stable release)
- ✅ Update `VERSION` to `"1.0"`
- ✅ Update `VERSION_MAJOR` to `1`
- ✅ Update `VERSION_MINOR` to `0`
- ✅ Update `VERSION_PATCH` to `0`
- ✅ Update VersionManager fallback to `"1.0.0"`
- ✅ Consider updating `BUILD_TYPE` from `"Beta Test Release"` to `"Stable Release"`

---

## Troubleshooting

### Problem: Manifest shows wrong version

**Symptoms:**
- Manifest has old version (e.g., `0.6.2-beta`) even after updating VersionInfo.h

**Solution:**
1. Verify VersionInfo.h was saved with correct values
2. Rebuild the application (version constants are compiled in)
3. Regenerate manifest: `.\updater\quick_generate.ps1`
4. Check manifest.json - should show `"latestVersion": "0.6.5-beta"`

---

### Problem: Auto-updater doesn't detect new version

**Symptoms:**
- Update dialog shows "You are up to date" even though new version exists

**Possible causes:**
1. **Manifest not uploaded:** Manifest.json on FTP server has old version
2. **Version string mismatch:** Manifest version format doesn't match installed version format
3. **Cached manifest:** Client is using cached manifest with old version

**Solution:**
1. Verify manifest.json on FTP server has correct version
2. Check version comparison logic in `UpdateChecker::compareVersionStrings()`
3. Clear manifest cache: Delete `%APPDATA%\Pikon Raditsz\manifest_cache.json`

---

### Problem: UI shows wrong version

**Symptoms:**
- Splash screen or about dialog shows old version

**Solution:**
1. Verify VersionInfo.h has correct values
2. Rebuild the application completely (clean build)
3. Check that UI components use `VersionInfo::getFullVersionString()` (not hardcoded strings)

---

## Best Practices

### ✅ DO:
- **Update VersionInfo.h first** before building
- **Keep VersionManager fallback in sync** with VersionInfo.h
- **Rebuild after version change** before generating manifest
- **Test version display** in UI after incrementing
- **Verify manifest version** matches VersionInfo.h before uploading

### ❌ DON'T:
- **Don't hardcode version strings** in multiple places
- **Don't forget to rebuild** after changing VersionInfo.h
- **Don't skip VersionManager fallback update** (causes confusion)
- **Don't upload manifest** without verifying version is correct

---

## Quick Reference: Version Increment Checklist

For **0.6.2 → 0.6.5**:

- [ ] Update `VERSION_FULL` in `VersionInfo.h` to `"0.6.5-beta"`
- [ ] Update `VERSION_PATCH` in `VersionInfo.h` to `5`
- [ ] Update VersionManager default in `VersionManager.cpp` to `"0.6.5"`
- [ ] Rebuild application: `cmake --build juce\build-ninja-release --config Release`
- [ ] Generate manifest: `.\updater\quick_generate.ps1`
- [ ] Verify manifest.json shows `"latestVersion": "0.6.5-beta"`
- [ ] Test UI displays correct version (splash screen, about dialog)
- [ ] Upload manifest and files to FTP server
- [ ] Test auto-updater detects new version

---

## Summary

**Single Source of Truth:** `juce/Source/utils/VersionInfo.h`

**Update Process:**
1. Edit VersionInfo.h (change `VERSION_FULL` and `VERSION_PATCH`)
2. Update VersionManager.cpp fallback (optional but recommended)
3. Rebuild application
4. Generate manifest (auto-detects version)
5. Verify and deploy

**Everything else is automatic!** The manifest generator, auto-updater, and UI components all read from VersionInfo.h or the generated manifest.

---

**Last Updated:** November 29, 2025  
**Next Version:** 0.6.5-beta

