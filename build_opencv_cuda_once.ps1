#!/usr/bin/env pwsh
# Build OpenCV with CUDA Once - Standalone Builder
# This script builds OpenCV in isolation so the main project never rebuilds it

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Release", "Debug", "Both")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Standalone Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check for CUDA
Write-Host "Checking for CUDA..." -ForegroundColor Yellow
try {
    $cudaVersion = & nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] NVIDIA GPU detected: $cudaVersion" -ForegroundColor Green
    } else {
        Write-Host "  [WARNING] nvidia-smi failed" -ForegroundColor Yellow
        Write-Host "  OpenCV CUDA build may fail!" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "  [WARNING] nvidia-smi not found or no NVIDIA GPU" -ForegroundColor Yellow
    Write-Host "  OpenCV CUDA build may fail!" -ForegroundColor Yellow
}

Write-Host "[DEBUG] After CUDA check" -ForegroundColor DarkGray

$installDir = Join-Path $PSScriptRoot "opencv_cuda_install"
$buildDirRoot = Join-Path $PSScriptRoot "opencv_build"
$buildDir = Join-Path $buildDirRoot "build"

Write-Host "[DEBUG] installDir = $installDir" -ForegroundColor DarkGray
Write-Host "[DEBUG] buildDir   = $buildDir" -ForegroundColor DarkGray

Write-Host ""
Write-Host "Configuration:" -ForegroundColor Cyan
Write-Host "  Build:   $Configuration" -ForegroundColor White
Write-Host "  Install: $installDir" -ForegroundColor White
Write-Host ""

# Function to build a configuration
function Build-OpenCVConfiguration {
    param([string]$Config)
    
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "  Building OpenCV ($Config)" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host ""
    
    # Step 1: Configure
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-S", "opencv_build",
        "-B", $buildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$Config"
    )
    
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "[ERROR] CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "[2/3] Building OpenCV with CUDA (this takes 30-45 minutes)..." -ForegroundColor Cyan
    Write-Host "  Grab a coffee, this will take a while..." -ForegroundColor Yellow
    
    $buildStart = Get-Date
    & cmake --build $buildDir --config $Config -j
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "[ERROR] Build failed!" -ForegroundColor Red
        exit 1
    }
    $buildEnd = Get-Date
    $buildTime = $buildEnd - $buildStart
    
    Write-Host ""
    Write-Host "[3/3] Installing to $installDir..." -ForegroundColor Cyan
    & cmake --build $buildDir --config $Config --target install
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "[ERROR] Installation failed!" -ForegroundColor Red
        exit 1
    }
    
    $hours = [int]$buildTime.TotalHours
    $minutes = $buildTime.Minutes
    $seconds = $buildTime.Seconds
    $timeStr = "{0:D2}:{1:D2}:{2:D2}" -f $hours, $minutes, $seconds
    
    Write-Host ""
    Write-Host "[OK] $Config build completed in $timeStr" -ForegroundColor Green
}

# Build requested configurations
if ($Configuration -eq "Both") {
    Build-OpenCVConfiguration "Release"
    Write-Host ""
    Write-Host ""
    Build-OpenCVConfiguration "Debug"
}
else {
    Build-OpenCVConfiguration $Configuration
}

# Verify installation
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

$libPath = Join-Path $installDir "lib" "opencv_world4130.lib"
if (Test-Path $libPath) {
    $libSize = (Get-Item $libPath).Length / 1MB
    $libSizeRounded = [math]::Round($libSize, 1)
    Write-Host "  [OK] opencv_world4130.lib: $libSizeRounded MB" -ForegroundColor Cyan
}
else {
    Write-Host "  [WARNING] opencv_world4130.lib not found!" -ForegroundColor Yellow
}

$includePath = Join-Path $installDir "include" "opencv2" "opencv.hpp"
if (Test-Path $includePath) {
    Write-Host "  [OK] Headers installed" -ForegroundColor Cyan
}
else {
    Write-Host "  [WARNING] Headers not found!" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Installed to: $installDir" -ForegroundColor White

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Next Steps" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "1. The main project will automatically find this OpenCV" -ForegroundColor White
Write-Host "   (no more rebuilds when you modify CMakeLists.txt!)" -ForegroundColor Green
Write-Host ""

Write-Host "2. Build your main project:" -ForegroundColor White
Write-Host '   cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64' -ForegroundColor Gray
Write-Host "   cmake --build juce/build --config Release" -ForegroundColor Gray
Write-Host ""

Write-Host "3. Optional: Archive this OpenCV build for backup:" -ForegroundColor White
Write-Host "   .\archive_opencv_standalone.ps1" -ForegroundColor Gray
Write-Host ""

Write-Host "[SUCCESS] OpenCV CUDA is now isolated from your main project!" -ForegroundColor Green
Write-Host "          Add all the nodes you want without rebuilding OpenCV!" -ForegroundColor Cyan
Write-Host ""
