#!/usr/bin/env pwsh
# Archive Standalone OpenCV CUDA Installation
# Backs up the pre-built OpenCV (saves 30+ minutes of compilation)

param(
    [Parameter(Mandatory=$false)]
    [string]$ArchiveName
)

$ErrorActionPreference = "Stop"

if (-not $ArchiveName) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmm"
    $ArchiveName = "opencv_standalone_$timestamp"
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV Standalone Archive Creator" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$installDir = "opencv_cuda_install"
$buildDir = "opencv_build\build"

# Check what configurations exist
$hasRelease = Test-Path "$installDir\lib\opencv_world4130.lib"
$hasDebug = Test-Path "$installDir\lib\opencv_world4130d.lib"

if (-not $hasRelease -and -not $hasDebug) {
    Write-Host "ERROR: Standalone OpenCV installation not found!" -ForegroundColor Red
    Write-Host "Expected: $installDir\lib\opencv_world4130.lib or opencv_world4130d.lib" -ForegroundColor Yellow
    Write-Host "`nRun: .\build_opencv_cuda_once.ps1" -ForegroundColor Cyan
    exit 1
}

# Show what we found
Write-Host "Detected configurations:" -ForegroundColor Yellow
if ($hasRelease) {
    $releaseSize = (Get-Item "$installDir\lib\opencv_world4130.lib").Length / 1MB
    Write-Host ("  ✓ Release: opencv_world4130.lib (" + [math]::Round($releaseSize, 1) + " MB)") -ForegroundColor Green
}
if ($hasDebug) {
    $debugSize = (Get-Item "$installDir\lib\opencv_world4130d.lib").Length / 1MB
    Write-Host ("  ✓ Debug:   opencv_world4130d.lib (" + [math]::Round($debugSize, 1) + " MB)") -ForegroundColor Green
}
Write-Host ""

Write-Host "Creating archive: $ArchiveName" -ForegroundColor Yellow
Write-Host ""

New-Item -ItemType Directory -Force -Path $ArchiveName | Out-Null

# Archive the installation directory (most important)
if (Test-Path $installDir) {
    $size = (Get-ChildItem -Recurse -File $installDir -ErrorAction SilentlyContinue |
             Measure-Object -Property Length -Sum).Sum / 1GB
    Write-Host ("  [INSTALL] opencv_cuda_install (" + [math]::Round($size, 2) + " GB)") -ForegroundColor Green
    Copy-Item -Recurse -Force $installDir "$ArchiveName\" -ErrorAction Stop
}

# Archive the build directory (optional, for cache restoration)
if (Test-Path $buildDir) {
    $size = (Get-ChildItem -Recurse -File $buildDir -ErrorAction SilentlyContinue |
             Measure-Object -Property Length -Sum).Sum / 1GB
    Write-Host ("  [BUILD]   opencv_build\build (" + [math]::Round($size, 2) + " GB)") -ForegroundColor Cyan
    Copy-Item -Recurse -Force $buildDir "$ArchiveName\opencv_build_backup" -ErrorAction Stop
}

# Create metadata file
$configurations = @()
if ($hasRelease) { $configurations += "Release" }
if ($hasDebug) { $configurations += "Debug" }
$configStr = $configurations -join " + "

$metadata = @"
OpenCV CUDA Standalone Archive
Created: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
OpenCV Version: 4.13.0-dev (4.x branch)
CUDA Version: 13.0
Configurations: $configStr
Architecture: x64

Contents:
$(if ($hasRelease) { "  ✓ opencv_world4130.lib  (Release)" } else { "" })
$(if ($hasDebug) { "  ✓ opencv_world4130d.lib (Debug)" } else { "" })

Installation Directory: opencv_cuda_install/
Build Cache: opencv_build_backup/

To restore:
  .\restore_opencv_standalone.ps1 $ArchiveName
"@

$metadata | Out-File "$ArchiveName\README.txt" -Encoding UTF8

# Calculate total size
$totalSize = (Get-ChildItem -Recurse -File $ArchiveName |
              Measure-Object -Property Length -Sum).Sum / 1GB

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Archive Created Successfully" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

Write-Host "Location: $ArchiveName" -ForegroundColor Cyan
Write-Host ("Size:     " + [math]::Round($totalSize, 2) + " GB") -ForegroundColor Cyan

Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "  1. Compress (optional):" -ForegroundColor White
Write-Host "     Compress-Archive -Path '$ArchiveName\*' -DestinationPath '$ArchiveName.zip'" -ForegroundColor Gray

Write-Host "`n  2. Move to safe storage:" -ForegroundColor White
Write-Host "     - External drive" -ForegroundColor Gray
Write-Host "     - Cloud storage" -ForegroundColor Gray
Write-Host "     - Network share" -ForegroundColor Gray

Write-Host "`n  3. Restore on another machine:" -ForegroundColor White
Write-Host "     .\restore_opencv_standalone.ps1 $ArchiveName" -ForegroundColor Gray

Write-Host "`n✓ This archive contains a complete OpenCV CUDA build!" -ForegroundColor Green
Write-Host "  No need to rebuild OpenCV ever again!" -ForegroundColor Cyan

