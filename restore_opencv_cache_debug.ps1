#!/usr/bin/env pwsh
# Restore OpenCV CUDA Build Cache (DEBUG Configuration)
# Recovers 30+ minutes of CUDA compilation time

param(
    [string]$ArchivePath = ""
)

# Ensure we're in the project root
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
Set-Location $projectRoot

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Cache Restore Tool" -ForegroundColor Cyan
Write-Host "  [DEBUG Configuration]" -ForegroundColor Magenta
Write-Host "========================================`n" -ForegroundColor Cyan

Write-Host "Working directory: $projectRoot" -ForegroundColor Gray

# Warn if an existing CMake cache is present (user should delete before reconfigure)
$existingCache = "juce\build\CMakeCache.txt"
if (Test-Path $existingCache) {
    Write-Host "Note: Existing CMakeCache.txt detected (will not be restored automatically)." -ForegroundColor Yellow
    Write-Host "      Delete it before running cmake -S/-B to avoid generator mismatches." -ForegroundColor Yellow
}

# Find most recent archive if not specified
if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
    $archives = Get-ChildItem -Path "Scripts_ps1_CUDA" -Directory -Filter "opencv_cuda_cache_debug_archive_*" | 
                Sort-Object Name -Descending
    
    if ($archives.Count -eq 0) {
        Write-Host "ERROR: No Debug archives found in Scripts_ps1_CUDA/" -ForegroundColor Red
        Write-Host "Run .\archive_opencv_cache_debug.ps1 first to create an archive." -ForegroundColor Yellow
        exit 1
    }
    
    $ArchivePath = $archives[0].FullName
    Write-Host "Using most recent archive: $($archives[0].Name)" -ForegroundColor Green
} else {
    # Handle relative path
    if (-not [System.IO.Path]::IsPathRooted($ArchivePath)) {
        $ArchivePath = Join-Path "Scripts_ps1_CUDA" $ArchivePath
    }
}

# Verify archive exists
if (-not (Test-Path $ArchivePath)) {
    Write-Host "ERROR: Archive not found: $ArchivePath" -ForegroundColor Red
    exit 1
}

# Verify archive has critical files (Debug)
$criticalFile = Join-Path $ArchivePath "opencv-build\lib\Debug\opencv_world4130d.lib"
if (-not (Test-Path $criticalFile)) {
    Write-Host "ERROR: Archive is incomplete or corrupted!" -ForegroundColor Red
    Write-Host "Missing: $criticalFile" -ForegroundColor Yellow
    exit 1
}

Write-Host "Archive verified successfully" -ForegroundColor Green
Write-Host "`nPreparing build directories...`n" -ForegroundColor Yellow

# Ensure build directory exists
New-Item -ItemType Directory -Force -Path "juce\build\_deps" | Out-Null
New-Item -ItemType Directory -Force -Path "juce\build\bin\Debug" | Out-Null

Write-Host "Restoring cache..." -ForegroundColor Yellow

# Restore opencv-build (critical)
if (Test-Path "$ArchivePath\opencv-build") {
    Write-Host "  [REMOVE] Removing old opencv-build..." -ForegroundColor Yellow
    if (Test-Path "juce\build\_deps\opencv-build") {
        Remove-Item -Recurse -Force "juce\build\_deps\opencv-build" -ErrorAction SilentlyContinue
    }
    
    $sizeGB = (Get-ChildItem -Recurse -File "$ArchivePath\opencv-build" -ErrorAction SilentlyContinue | 
               Measure-Object -Property Length -Sum).Sum / 1GB
    $sizeRounded = [math]::Round($sizeGB, 2)
    Write-Host ("  [RESTORE] opencv-build (" + $sizeRounded + " GB)...") -ForegroundColor Green
    Copy-Item -Recurse -Force "$ArchivePath\opencv-build" "juce\build\_deps\"
}

# Restore opencv-src (optional but recommended)
if (Test-Path "$ArchivePath\opencv-src") {
    Write-Host "  [REMOVE] Removing old opencv-src..." -ForegroundColor Yellow
    if (Test-Path "juce\build\_deps\opencv-src") {
        Remove-Item -Recurse -Force "juce\build\_deps\opencv-src" -ErrorAction SilentlyContinue
    }
    
    $sizeGB = (Get-ChildItem -Recurse -File "$ArchivePath\opencv-src" -ErrorAction SilentlyContinue | 
               Measure-Object -Property Length -Sum).Sum / 1GB
    $sizeRounded = [math]::Round($sizeGB, 2)
    Write-Host ("  [RESTORE] opencv-src (" + $sizeRounded + " GB)...") -ForegroundColor Cyan
    Copy-Item -Recurse -Force "$ArchivePath\opencv-src" "juce\build\_deps\"
}

# Restore opencv_contrib-src (optional)
if (Test-Path "$ArchivePath\opencv_contrib-src") {
    Write-Host "  [REMOVE] Removing old opencv_contrib-src..." -ForegroundColor Yellow
    if (Test-Path "juce\build\_deps\opencv_contrib-src") {
        Remove-Item -Recurse -Force "juce\build\_deps\opencv_contrib-src" -ErrorAction SilentlyContinue
    }
    
    $sizeGB = (Get-ChildItem -Recurse -File "$ArchivePath\opencv_contrib-src" -ErrorAction SilentlyContinue | 
               Measure-Object -Property Length -Sum).Sum / 1GB
    $sizeRounded = [math]::Round($sizeGB, 2)
    Write-Host ("  [RESTORE] opencv_contrib-src (" + $sizeRounded + " GB)...") -ForegroundColor Cyan
    Copy-Item -Recurse -Force "$ArchivePath\opencv_contrib-src" "juce\build\_deps\"
}

# Restore FFmpeg DLL (Debug)
if (Test-Path "$ArchivePath\bin\Debug\opencv_videoio_ffmpeg4130_64.dll") {
    Write-Host "  [RESTORE] FFmpeg DLL (Debug)..." -ForegroundColor Cyan
    Copy-Item -Force "$ArchivePath\bin\Debug\opencv_videoio_ffmpeg4130_64.dll" "juce\build\bin\Debug\"
}

# Display stored generator information if available (for reference)
$generatorInfo = Join-Path $ArchivePath "cmake-generator-info.txt"
if (Test-Path $generatorInfo) {
    Write-Host "`nStored CMake generator information:" -ForegroundColor Yellow
    Get-Content $generatorInfo | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Gray }
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Cache Restored Successfully (DEBUG)" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "  1. Reconfigure CMake (uses restored OpenCV binaries):" -ForegroundColor White
Write-Host "     cmake -S juce -B juce/build -G ""Visual Studio 17 2022"" -A x64 -DCMAKE_BUILD_TYPE=Debug" -ForegroundColor Gray
Write-Host "`n  2. Build project (5-10 minutes, no CUDA rebuild):" -ForegroundColor White
Write-Host "     cmake --build juce/build --config Debug" -ForegroundColor Gray

Write-Host "`nOpenCV CUDA will NOT rebuild - using cached version!" -ForegroundColor Green
Write-Host "Saved: 30+ minutes of compilation time" -ForegroundColor Cyan

Write-Host "`n💡 Pro Tip: OpenCV config is now in juce/cmake/OpenCVConfig.cmake" -ForegroundColor Yellow
Write-Host "   You can safely modify main CMakeLists.txt (add nodes, etc.) without triggering OpenCV rebuilds!" -ForegroundColor Gray

