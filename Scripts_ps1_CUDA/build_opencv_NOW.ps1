#!/usr/bin/env pwsh
# Build OpenCV with CUDA - SHORT PATH VERSION

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Release", "Debug", "Both")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Use SHORT paths at drive root
$buildRoot = "C:\ocv_build"
$installRoot = "C:\ocv_install"
$projectRoot = $PSScriptRoot | Split-Path
$finalInstall = Join-Path $projectRoot "opencv_cuda_install"

Write-Host "Build location:   $buildRoot (temp)" -ForegroundColor White
Write-Host "Temp install:     $installRoot (temp)" -ForegroundColor White
Write-Host "Final location:   $finalInstall" -ForegroundColor White
Write-Host ""

# Clean previous builds
if (Test-Path $buildRoot) {
    Write-Host "Cleaning previous build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildRoot
}
if (Test-Path $installRoot) {
    Write-Host "Cleaning previous install directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $installRoot
}

# Create directories
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
New-Item -ItemType Directory -Force -Path $installRoot | Out-Null

# Download OpenCV source if not exists
$opencvSrc = Join-Path $buildRoot "opencv"
$opencvContribSrc = Join-Path $buildRoot "opencv_contrib"

if (!(Test-Path $opencvSrc)) {
    Write-Host "[1/6] Cloning OpenCV..." -ForegroundColor Cyan
    git clone --depth 1 --branch 4.x https://github.com/opencv/opencv.git $opencvSrc
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to clone OpenCV" -ForegroundColor Red
        exit 1
    }
}

if (!(Test-Path $opencvContribSrc)) {
    Write-Host "[2/6] Cloning OpenCV Contrib..." -ForegroundColor Cyan
    git clone --depth 1 --branch 4.x https://github.com/opencv/opencv_contrib.git $opencvContribSrc
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to clone OpenCV Contrib" -ForegroundColor Red
        exit 1
    }
}

# Find CUDA
Write-Host "[3/6] Detecting CUDA..." -ForegroundColor Cyan

if (Test-Path "C:\CUDA") {
    $cudaPath = "C:\CUDA"
    Write-Host "  Using junction: $cudaPath" -ForegroundColor Green
}
elseif (Test-Path "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0") {
    $cudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0"
    Write-Host "  Found CUDA: $cudaPath" -ForegroundColor Yellow
}
elseif ($env:CUDA_PATH) {
    $cudaPath = $env:CUDA_PATH
    Write-Host "  Found CUDA from env: $cudaPath" -ForegroundColor Green
}
else {
    Write-Host "[ERROR] CUDA not found!" -ForegroundColor Red
    exit 1
}

# Convert to forward slashes for CMake
$cudaPathCMake = $cudaPath -replace '\\', '/'
$env:CUDA_PATH = $cudaPathCMake
$env:CUDA_BIN_PATH = "$cudaPathCMake/bin"

# Function to build configuration
function Build-OpenCV {
    param([string]$Config)
    
    $buildDir = Join-Path $buildRoot "build_$Config"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    
    Write-Host ""
    Write-Host "[4/6] Configuring OpenCV ($Config)..." -ForegroundColor Cyan
    
    $opencvContribModules = "$opencvContribSrc\modules" -replace '\\', '/'
    $nvccPath = "$cudaPathCMake/bin/nvcc.exe"
    
    $cmakeArgs = @(
        "-S", $opencvSrc,
        "-B", $buildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_INSTALL_PREFIX=$installRoot",
        "-DCMAKE_PREFIX_PATH=$cudaPathCMake",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DBUILD_opencv_world=ON",
        "-DBUILD_TESTS=OFF",
        "-DBUILD_PERF_TESTS=OFF",
        "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_opencv_apps=OFF",
        "-DENABLE_PRECOMPILED_HEADERS=OFF",
        "-DBUILD_WITH_STATIC_CRT=OFF",
        "-DWITH_IPP=OFF",
        "-DWITH_CUDA=ON",
        "-DWITH_CUDNN=ON",
        "-DWITH_NVCUVID=OFF",
        "-DWITH_NVCUVENC=OFF",
        "-DCUDA_TOOLKIT_ROOT_DIR=$cudaPathCMake",
        "-DCUDA_HOST_COMPILER=cl.exe",
        "-DCUDA_NVCC_EXECUTABLE=$nvccPath",
        "-DCUDAToolkit_ROOT=$cudaPathCMake",
        "-DCUDAToolkit_BIN_DIR=$cudaPathCMake/bin",
        "-DCUDAToolkit_INCLUDE_DIR=$cudaPathCMake/include",
        "-DCUDA_ARCH_BIN=8.6;8.9;12.0",
        "-DCUDA_ARCH_PTX=12.0",
        "-DOPENCV_EXTRA_MODULES_PATH=$opencvContribModules",
        "-DOPENCV_DNN_CUDA=ON",
        "-DBUILD_opencv_dnn=ON",
        "-DOPENCV_CUDA_ARCH_BIN=8.6;8.9;12.0"
    )
    
    # Run CMake from within Visual Studio environment
    $vcvarsPath = "C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"
    $cmakeArgsStr = ($cmakeArgs | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $cmakeCmd = "cmake $cmakeArgsStr"
    
    Write-Host "  Running CMake in VS environment..." -ForegroundColor Gray
    cmd /c "`"$vcvarsPath`" >nul && $cmakeCmd"
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "[5/6] Building OpenCV ($Config) - this takes 30-45 minutes..." -ForegroundColor Cyan
    $buildStart = Get-Date
    
    # Build in VS environment
    $buildCmd = "cmake --build `"$buildDir`" --config $Config --parallel"
    cmd /c "`"$vcvarsPath`" >nul && $buildCmd"
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed!" -ForegroundColor Red
        exit 1
    }
    
    $buildEnd = Get-Date
    $buildTime = $buildEnd - $buildStart
    Write-Host "  Build completed in $([int]$buildTime.TotalMinutes) minutes" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "[6/6] Installing..." -ForegroundColor Cyan
    
    # Install in VS environment
    $installCmd = "cmake --build `"$buildDir`" --config $Config --target install"
    cmd /c "`"$vcvarsPath`" >nul && $installCmd"
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Install failed!" -ForegroundColor Red
        exit 1
    }
}

# Build requested configurations
if ($Configuration -eq "Both") {
    Build-OpenCV "Release"
    Build-OpenCV "Debug"
} else {
    Build-OpenCV $Configuration
}

# Move to final location
Write-Host ""
Write-Host "Moving to final location: $finalInstall" -ForegroundColor Cyan
if (Test-Path $finalInstall) {
    Remove-Item -Recurse -Force $finalInstall
}
Move-Item $installRoot $finalInstall

# Cleanup temp build directory
Write-Host "Cleaning up build directory..." -ForegroundColor Cyan
Remove-Item -Recurse -Force $buildRoot

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  SUCCESS!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "OpenCV installed to: $finalInstall" -ForegroundColor White
Write-Host ""

