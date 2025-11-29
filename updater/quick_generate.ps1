# Quick Generate - Wrapper script for manifest generation
# This script uses your preferred settings and automatically detects version from VersionInfo.h

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Quick Manifest Generator" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Version will be auto-detected from VersionInfo.h" -ForegroundColor Yellow
Write-Host ""

# Your build directory
$buildDir = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release"

# Your variant
$variant = "cuda"

# Folders to exclude (your custom exclusions)
$excludeFolders = @(
    "VST",              # VST plugins (user-installed, not part of app)
    "models",           # AI/ML models (optional, user choice)
    "Samples",          # Sample libraries (too large, optional content)
    "video",            # Video files (user-generated content)
    "record",           # Recorded audio (user-generated content)
    "TTSPERFORMER",     # TTS performer cache (user-generated)
    "juce\logs",        # Log files (user-generated)
    "logs"              # Log files (user-generated, alternate location)
)

# File patterns to exclude
$excludePatterns = @(
    "*.log",            # Exclude all log files
    "*.tmp",            # Temporary files
    "*.pdb",            # Debug symbols
    "*.ilk",            # Incremental linker files
    "*.exp",            # Export files
    "*.lib",            # Library files
    "*\logs\*",         # Exclude anything in logs folders
    ".*",               # Exclude root dotfiles (hidden/config)
    "*\.*",             # Exclude dotfiles in subdirectories
    "imgui.ini",        # ImGui config file (user-specific)
    "PikonUpdater.exe", # Updater tool (shipped with app, shouldn't be updated via updater)
    "manifest.json"     # Manifest file itself (metadata, not a distributable file)
)

# Base URL for your OVH server
$baseUrl = "https://pimpant.club/pikon-raditsz"

# Output file - save to build directory (exe folder) instead of root
$outputFile = Join-Path $buildDir "manifest.json"

Write-Host "Configured exclusions:" -ForegroundColor Yellow
Write-Host "  Folders: $($excludeFolders -join ', ')" -ForegroundColor Gray
Write-Host "  Patterns: $($excludePatterns -join ', ')" -ForegroundColor Gray
Write-Host ""

# Call the main generate script with your settings
& "$PSScriptRoot\generate_manifest.ps1" `
    -BuildDir $buildDir `
    -Variant $variant `
    -BaseUrl $baseUrl `
    -OutputFile $outputFile `
    -ExcludeFolders $excludeFolders `
    -ExcludePatterns $excludePatterns

Write-Host ""
Write-Host "Quick generate complete!" -ForegroundColor Green
Write-Host "Manifest saved to: $outputFile" -ForegroundColor Green
Write-Host "  (Build directory: $buildDir)" -ForegroundColor Gray
Write-Host ""
