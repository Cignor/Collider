#!/usr/bin/env pwsh
# Restore Standalone OpenCV CUDA Installation
# Restores pre-built OpenCV (saves 30+ minutes of compilation)

param(
    [Parameter(Mandatory=$false)]
    [string]$ArchivePath
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV Standalone Restore Tool" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Find archive if not specified
if (-not $ArchivePath) {
    $archives = Get-ChildItem -Directory -Filter "opencv_standalone_*" | 
                Sort-Object LastWriteTime -Descending
    
    if ($archives.Count -eq 0) {
        Write-Host "ERROR: No standalone archives found!" -ForegroundColor Red
        Write-Host "Looking for: opencv_standalone_*" -ForegroundColor Yellow
        Write-Host "`nCreate one with: .\archive_opencv_standalone.ps1" -ForegroundColor Cyan
        exit 1
    }
    
    $ArchivePath = $archives[0].Name
    Write-Host "Using most recent archive: $ArchivePath" -ForegroundColor Green
}

# Verify archive exists
if (-not (Test-Path $ArchivePath)) {
    Write-Host "ERROR: Archive not found: $ArchivePath" -ForegroundColor Red
    exit 1
}

# Verify archive contains installation
$archiveHasRelease = Test-Path "$ArchivePath\opencv_cuda_install\lib\opencv_world4130.lib"
$archiveHasDebug = Test-Path "$ArchivePath\opencv_cuda_install\lib\opencv_world4130d.lib"

if (-not $archiveHasRelease -and -not $archiveHasDebug) {
    Write-Host "ERROR: Archive is incomplete or corrupted!" -ForegroundColor Red
    Write-Host "Missing: opencv_cuda_install\lib\opencv_world4130.lib or opencv_world4130d.lib" -ForegroundColor Yellow
    exit 1
}

Write-Host "Archive verified successfully" -ForegroundColor Green
Write-Host "`nFound configurations:" -ForegroundColor Yellow
if ($archiveHasRelease) {
    Write-Host "  ✓ Release: opencv_world4130.lib" -ForegroundColor Green
}
if ($archiveHasDebug) {
    Write-Host "  ✓ Debug:   opencv_world4130d.lib" -ForegroundColor Green
}
Write-Host ""

# Restore installation directory
$installDir = "opencv_cuda_install"
if (Test-Path $installDir) {
    Write-Host "Removing existing installation..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $installDir
}

Write-Host "Restoring OpenCV installation..." -ForegroundColor Cyan
$size = (Get-ChildItem -Recurse -File "$ArchivePath\opencv_cuda_install" -ErrorAction SilentlyContinue |
         Measure-Object -Property Length -Sum).Sum / 1GB
Write-Host ("  [INSTALL] opencv_cuda_install (" + [math]::Round($size, 2) + " GB)") -ForegroundColor Green

Copy-Item -Recurse -Force "$ArchivePath\opencv_cuda_install" $installDir

# Optionally restore build cache
if (Test-Path "$ArchivePath\opencv_build_backup") {
    Write-Host "`nRestoring build cache (optional)..." -ForegroundColor Cyan
    
    $buildDir = "opencv_build\build"
    if (Test-Path $buildDir) {
        Write-Host "  Removing old build cache..." -ForegroundColor Gray
        Remove-Item -Recurse -Force $buildDir
    }
    
    New-Item -ItemType Directory -Force -Path "opencv_build" | Out-Null
    
    $size = (Get-ChildItem -Recurse -File "$ArchivePath\opencv_build_backup" -ErrorAction SilentlyContinue |
             Measure-Object -Property Length -Sum).Sum / 1GB
    Write-Host ("  [BUILD]   opencv_build\build (" + [math]::Round($size, 2) + " GB)") -ForegroundColor Cyan
    
    Copy-Item -Recurse -Force "$ArchivePath\opencv_build_backup" $buildDir
}

# Verify restoration
Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Restoration Complete!" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

Write-Host "Verification:" -ForegroundColor Yellow

# Check Release library
$releaseLibPath = "$installDir\lib\opencv_world4130.lib"
if (Test-Path $releaseLibPath) {
    $libSize = (Get-Item $releaseLibPath).Length / 1MB
    Write-Host "  ✓ opencv_world4130.lib:  $([math]::Round($libSize, 1)) MB (Release)" -ForegroundColor Cyan
}

# Check Debug library
$debugLibPath = "$installDir\lib\opencv_world4130d.lib"
if (Test-Path $debugLibPath) {
    $libSize = (Get-Item $debugLibPath).Length / 1MB
    Write-Host "  ✓ opencv_world4130d.lib: $([math]::Round($libSize, 1)) MB (Debug)" -ForegroundColor Cyan
}

# Verify at least one exists
if (-not (Test-Path $releaseLibPath) -and -not (Test-Path $debugLibPath)) {
    Write-Host "  ✗ ERROR: No library files found!" -ForegroundColor Red
    exit 1
}

# Check headers
$includePath = "$installDir\include\opencv2\opencv.hpp"
if (Test-Path $includePath) {
    Write-Host "  ✓ Headers restored" -ForegroundColor Cyan
} else {
    Write-Host "  ✗ WARNING: Headers might be missing!" -ForegroundColor Yellow
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  Next Steps" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

Write-Host "1. Build your main project (OpenCV will be found automatically):" -ForegroundColor White
Write-Host "   cmake -S juce -B juce/build -G ""Visual Studio 17 2022"" -A x64" -ForegroundColor Gray
Write-Host "   cmake --build juce/build --config Release`n" -ForegroundColor Gray

Write-Host "2. Modify CMakeLists.txt as much as you want!" -ForegroundColor White
Write-Host "   OpenCV will NEVER rebuild! 🎉`n" -ForegroundColor Green

Write-Host "✓ OpenCV CUDA restored and ready to use!" -ForegroundColor Green
Write-Host "  Saved 30+ minutes of compilation time!" -ForegroundColor Cyan

