# Pikon Raditsz Auto-Updater

This directory contains the infrastructure for the Pikon Raditsz auto-updater system.

## Files

- **`manifest_schema.json`** - JSON schema defining the manifest format
- **`example_manifest.json`** - Example manifest showing real-world usage
- **`generate_manifest.ps1`** - PowerShell script to generate manifests from build output
- **`deploy_update.ps1`** - PowerShell script to deploy updates to OVH server
- **`.htaccess`** - Apache configuration for OVH server security
- **`OVH_SETUP_GUIDE.md`** - Complete guide for setting up OVH server

## Quick Start

### 1. Generate Manifest

```powershell
.\generate_manifest.ps1 `
    -BuildDir "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -Version "1.0.0" `
    -Variant "cuda" `
    -OutputFile "manifest.json"
```

### 2. Deploy to OVH

```powershell
.\deploy_update.ps1 `
    -BuildDir "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -Version "1.0.0" `
    -Variant "cuda" `
    -FtpServer "ftp.yourdomain.com" `
    -FtpUser "your_username" `
    -UseSecure
```

## Implementation Status

### Phase 1: Infrastructure ✅ COMPLETE
- [x] Manifest schema design
- [x] Manifest generator script
- [x] Deployment script
- [x] OVH server setup guide
- [x] Security configuration (.htaccess)

### Phase 2: Core Updater Logic (C++) - TODO
- [ ] UpdateChecker class
- [ ] FileDownloader class
- [ ] HashVerifier class
- [ ] UpdateApplier class
- [ ] VersionManager class

### Phase 3: UI Components - TODO
- [ ] UpdateAvailableDialog
- [ ] DownloadProgressDialog
- [ ] UpdateSettingsPanel

### Phase 4: Helper Executable - TODO
- [ ] Updater.exe project
- [ ] File replacement logic
- [ ] Rollback mechanism

## Documentation

See `../implementation_plan.md` for the complete implementation plan.

## Next Steps

1. Set up OVH server (see `OVH_SETUP_GUIDE.md`)
2. Test manifest generation with current build
3. Implement C++ updater classes (Phase 2)
4. Build UI components (Phase 3)
5. Create helper executable (Phase 4)
