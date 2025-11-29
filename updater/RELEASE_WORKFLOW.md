# Auto-Updater Workflow

## Single Source of Truth: VersionInfo.h

**Location**: `juce/Source/utils/VersionInfo.h`

This is your **only** place to update version numbers. Everything else is automated.

## Release Workflow

### 1. Update Version (ONLY place to edit!)

Edit `juce/Source/utils/VersionInfo.h`:

```cpp
static constexpr const char* VERSION_FULL = "0.6.3-beta";  // ← Change this
static constexpr int VERSION_MAJOR = 0;                     // ← And these
static constexpr int VERSION_MINOR = 6;
static constexpr int VERSION_PATCH = 3;
```

### 2. Build Your Application

```powershell
# Build in Release mode
cmake --build juce\build-ninja-release --config Release --target PresetCreatorApp
```

### 3. Generate Manifest (Automatic Version Detection!)

The script now **automatically reads** from `VersionInfo.h`:

```powershell
cd updater

# Generate manifest - version is auto-detected!
.\generate_manifest.ps1 `
    -BuildDir "..\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -Variant "cuda"
```

**Output**: `manifest.json` with version automatically pulled from VersionInfo.h

### 4. Deploy to OVH

```powershell
# Upload everything to your FTP server
.\deploy_update.ps1 `
    -BuildDir "..\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -FtpHost "ftp.pimpant.club" `
    -FtpUser "your-username" `
    -FtpPass "your-password" `
    -Variant "cuda"
```

**Upload Location**: `/public_html/pikon-raditsz/`

## FTP Directory Structure

After deployment, your server will have:

```
/public_html/
  └── pikon-raditsz/
      ├── manifest.json              ← Auto-generated with version from VersionInfo.h
      ├── .htaccess                  ← Security config
      ├── changelog.html             ← Manual (optional)
      └── 0.6.3/                     ← Auto-created from VersionInfo.h
          └── cuda/
              ├── Pikon Raditsz.exe
              ├── cudart64_110.dll
              └── ... (all files)
```

## What Gets Automated

✅ **Version number** - Read from VersionInfo.h  
✅ **Folder naming** - `0.6.3/` created automatically  
✅ **Manifest version** - `latestVersion: "0.6.3-beta"`  
✅ **Minimum version** - Calculated as `0.6.0`  
✅ **Release date** - Auto-generated timestamp  
✅ **File hashing** - SHA256 for all files  
✅ **Critical detection** - .exe/.dll marked as critical  

## What You Still Control

📝 **Changelog summary** - Edit in manifest.json after generation  
📝 **Excluded files** - Configure in generate_manifest.ps1  
📝 **Base URL** - Default is your OVH server  

## Example: Releasing Version 0.6.3

```powershell
# 1. Edit VersionInfo.h
#    Change VERSION_FULL to "0.6.3-beta"
#    Change VERSION_PATCH to 3

# 2. Build
cmake --build juce\build-ninja-release --config Release --target PresetCreatorApp

# 3. Generate (version auto-detected!)
cd updater
.\generate_manifest.ps1 -BuildDir "..\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" -Variant "cuda"

# 4. Review manifest.json
#    Update changelog summary if desired

# 5. Deploy
.\deploy_update.ps1 -BuildDir "..\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" -FtpHost "ftp.pimpant.club" -FtpUser "username" -FtpPass "password" -Variant "cuda"
```

## Verification

After deployment, test the updater:

1. Run your app
2. Click **Settings → Check for Updates...**
3. Should show: "Update available: 0.6.3-beta"

## Notes

- **VersionInfo.h is the ONLY source of truth**
- Scripts automatically parse and use the version
- No manual version editing in scripts needed
- Version consistency guaranteed across app and manifest
