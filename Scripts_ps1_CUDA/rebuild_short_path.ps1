#!/usr/bin/env pwsh
# Rebuild with SHORT path to fix cmd.exe length limit
# This script moves your build to a shorter path temporarily

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SHORT PATH BUILD FIX" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "This script rebuilds your project in a SHORT path" -ForegroundColor Yellow
Write-Host "to bypass Windows cmd.exe command line limits." -ForegroundColor Yellow
Write-Host ""

$projectRoot = "H:\0000_CODE\01_collider_pyo"
$sourceDir = Join-Path $projectRoot "juce"
$longBuildDir = Join-Path $sourceDir "build-ninja-release"
$shortBuildDir = "C:\build"  # MUCH shorter path!

Write-Host "Source:      $sourceDir" -ForegroundColor White
Write-Host "Old build:   $longBuildDir (TOO LONG - causes errors)" -ForegroundColor Red
Write-Host "New build:   $shortBuildDir (SHORT - fixes errors)" -ForegroundColor Green
Write-Host ""

# Check if OpenCV is pre-built
$opencvInstall = Join-Path $projectRoot "opencv_cuda_install"
$opencvPrebuilt = Test-Path (Join-Path $opencvInstall "lib\opencv_world4130.lib")

if (!$opencvPrebuilt) {
    Write-Host ""
    Write-Host "⚠️  WARNING: OpenCV is not pre-built!" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "OpenCV will be built from source, which will STILL hit the" -ForegroundColor Yellow
    Write-Host "command line length limit even with a short build path." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "RECOMMENDED: Build OpenCV first with:" -ForegroundColor Cyan
    Write-Host "  .\Scripts_ps1_CUDA\build_opencv_short.ps1" -ForegroundColor White
    Write-Host ""
    
    $response = Read-Host "Continue anyway? (y/N)"
    if ($response -ne "y" -and $response -ne "Y") {
        Write-Host "Aborted." -ForegroundColor Yellow
        exit 0
    }
}

# Clean short build directory
if (Test-Path $shortBuildDir) {
    Write-Host "Cleaning previous short build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $shortBuildDir
}

# Create short build directory
New-Item -ItemType Directory -Force -Path $shortBuildDir | Out-Null

Write-Host ""
Write-Host "[1/2] Configuring CMake with Ninja..." -ForegroundColor Cyan
Write-Host "  Initializing Visual Studio environment..." -ForegroundColor Gray

# Run CMake configuration in Visual Studio environment
$vcvarsPath = "C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"
$cmakeCmd = "cmake -S `"$sourceDir`" -B `"$shortBuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release"

$setupCmd = "`"$vcvarsPath`" && $cmakeCmd"
$result = cmd /c $setupCmd

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] CMake configuration failed!" -ForegroundColor Red
    Write-Host "Check the output above for errors." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "[2/2] Building PresetCreatorApp..." -ForegroundColor Cyan
Write-Host "  This may take a while..." -ForegroundColor Gray

$buildCmd = "cmake --build `"$shortBuildDir`" --target PresetCreatorApp"
$setupBuildCmd = "`"$vcvarsPath`" && $buildCmd"
$result = cmd /c $setupBuildCmd

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] Build failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "If you still see 'command line is too long' errors," -ForegroundColor Yellow
    Write-Host "you MUST build OpenCV separately first:" -ForegroundColor Yellow
    Write-Host "  .\Scripts_ps1_CUDA\build_opencv_short.ps1" -ForegroundColor White
    Write-Host ""
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  SUCCESS!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Your executable is at:" -ForegroundColor White
Write-Host "  $shortBuildDir\Source\PresetCreatorApp.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "To build again, use:" -ForegroundColor White
Write-Host "  cmake --build C:\build --target PresetCreatorApp" -ForegroundColor Gray
Write-Host ""

