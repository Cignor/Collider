# OVH Server Setup Guide for Pikon Raditsz Auto-Updater

This guide explains how to set up your OVH web hosting for the Pikon Raditsz auto-updater.

## Prerequisites

- OVH web hosting account with FTP/SFTP access
- SSL certificate enabled (Let's Encrypt is free on OVH)
- Your domain name configured

## Directory Structure

Create the following directory structure on your OVH server:

```
/public_html/
└── updates/
    ├── .htaccess                    (security configuration)
    ├── manifest.json                (latest version info)
    ├── changelog.html               (human-readable changelog)
    ├── versions/
    │   ├── 1.0.0/
    │   │   ├── standard/
    │   │   │   ├── Pikon Raditsz.exe
    │   │   │   └── *.dll
    │   │   ├── cuda/
    │   │   │   ├── Pikon Raditsz.exe
    │   │   │   ├── *.dll
    │   │   │   └── cuda*.dll
    │   │   ├── presets/
    │   │   │   └── *.xml
    │   │   └── docs/
    │   │       └── *.pdf
    │   ├── 1.0.1/
    │   │   └── ...
    │   └── 0.9.0/
    │       └── ...
    └── README.txt                   (optional, for humans)
```

## Setup Steps

### 1. Enable SSL Certificate

1. Log in to your OVH control panel
2. Go to "Multisite" section
3. Enable SSL for your domain (Let's Encrypt is free)
4. Wait for certificate activation (5-10 minutes)

### 2. Create Directory Structure

**Option A: Via FTP Client (FileZilla, WinSCP)**

1. Connect to your OVH server via FTP/SFTP
2. Navigate to `/public_html/`
3. Create folder: `updates`
4. Inside `updates`, create folder: `versions`

**Option B: Via SSH (if available)**

```bash
ssh your_username@your_domain.com
cd public_html
mkdir -p updates/versions
```

### 3. Upload .htaccess File

1. Upload the `.htaccess` file from `updater/.htaccess` to `/public_html/updates/`
2. Verify it's uploaded correctly

### 4. Test Directory Access

Visit these URLs in your browser:

- `https://yourdomain.com/updates/` - Should show 403 Forbidden (directory listing disabled)
- `https://yourdomain.com/updates/manifest.json` - Should show 404 (file doesn't exist yet)

If you see directory listings, the .htaccess file isn't working. Check:
- File is named exactly `.htaccess` (with leading dot)
- File is in the correct directory
- OVH has mod_rewrite enabled (usually enabled by default)

### 5. Upload Your First Release

Use the deployment script:

```powershell
# Generate manifest
.\updater\generate_manifest.ps1 `
    -BuildDir "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -Version "1.0.0" `
    -Variant "cuda" `
    -OutputFile "manifest_1.0.0_cuda.json"

# Deploy to OVH
.\updater\deploy_update.ps1 `
    -BuildDir "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release" `
    -Version "1.0.0" `
    -Variant "cuda" `
    -FtpServer "ftp.yourdomain.com" `
    -FtpUser "your_username" `
    -UseSecure
```

### 6. Create Main Manifest

After uploading all variants for a version, create the main `manifest.json`:

1. Combine manifests from all variants (standard + cuda)
2. Upload to `/public_html/updates/manifest.json`
3. This is the file the updater will check

### 7. Verify Deployment

Test these URLs:

```
https://yourdomain.com/updates/manifest.json
https://yourdomain.com/updates/versions/1.0.0/cuda/Pikon%20Raditsz.exe
https://yourdomain.com/updates/versions/1.0.0/presets/default.xml
```

All should be accessible (download or show content).

## FTP/SFTP Credentials

Your OVH credentials are typically:

- **Server**: `ftp.yourdomain.com` or `ssh.cluster0XX.hosting.ovh.net`
- **Username**: Your OVH FTP username (found in control panel)
- **Password**: Your FTP password
- **Port**: 
  - FTP: 21
  - SFTP: 22

## Recommended Tools

### For Manual Upload:
- **WinSCP** (Windows, SFTP/FTP) - https://winscp.net/
- **FileZilla** (Cross-platform, FTP) - https://filezilla-project.org/

### For Automated Deployment:
- Use the provided `deploy_update.ps1` script
- Requires WinSCP for SFTP support

## Storage Considerations

### Disk Space

Typical release sizes:
- Standard variant: ~50 MB
- CUDA variant: ~500 MB
- Keep 3-5 versions: ~2-3 GB total

Most OVH shared hosting plans include 100+ GB storage.

### Bandwidth

Estimated monthly bandwidth (100 active users):
- 100 users × 500 MB = 50 GB/month

Most OVH plans include unlimited bandwidth or 100+ GB/month.

## Security Best Practices

1. **Use HTTPS only** - Already enforced by .htaccess
2. **Disable directory listing** - Already configured
3. **Keep old versions** - For rollback capability
4. **Monitor access logs** - Check for unusual activity
5. **Regular backups** - OVH usually provides automatic backups

## Troubleshooting

### "403 Forbidden" on manifest.json

- File doesn't exist yet (expected before first deployment)
- Check file permissions (should be 644)

### "500 Internal Server Error"

- .htaccess syntax error
- Check error logs in OVH control panel
- Try removing .htaccess temporarily to isolate issue

### Files not accessible

- Check file permissions (644 for files, 755 for directories)
- Verify SSL certificate is active
- Check .htaccess isn't blocking access

### Slow downloads

- OVH shared hosting has bandwidth limits
- Consider upgrading to VPS if user base grows
- Or use CDN (Cloudflare free tier works with OVH)

## Maintenance

### Adding a New Version

1. Build new version locally
2. Run `generate_manifest.ps1` for each variant
3. Run `deploy_update.ps1` to upload
4. Update main `manifest.json`
5. Test with updater client

### Removing Old Versions

Keep at least 2-3 old versions for rollback:

```bash
# Via SSH
cd /public_html/updates/versions
rm -rf 0.8.0  # Remove very old version
```

### Monitoring

Check OVH control panel for:
- Disk space usage
- Bandwidth usage
- Access logs (see which files are downloaded most)

## Next Steps

After server setup:
1. Test manifest accessibility
2. Implement C++ updater client
3. Test end-to-end update flow
4. Deploy to beta testers

## Support

- OVH Documentation: https://docs.ovh.com/
- OVH Support: Via control panel
- This project: See `implementation_plan.md`
