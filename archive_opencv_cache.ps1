#!/usr/bin/env pwsh
# Archive OpenCV CUDA Build Cache (DEPRECATED - Use Standalone System!)
# 
# ⚠️  DEPRECATED: This script is for the OLD inline build system.
# 
# 🎉 NEW RECOMMENDED APPROACH:
#    Use the standalone OpenCV build system instead:
#    1. Build once: .\build_opencv_cuda_once.ps1
#    2. Archive:    .\archive_opencv_standalone.ps1
#    3. Restore:    .\restore_opencv_standalone.ps1
# 
# The new system prevents OpenCV from rebuilding when you modify CMakeLists.txt!
# 
# This old script still works if you're using the inline FetchContent build.

$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$archivePath = "opencv_cuda_cache_archive_$timestamp"

Write-Host "`n⚠️  DEPRECATED SCRIPT - Consider using standalone system!" -ForegroundColor Yellow
Write-Host "   Run: .\build_opencv_cuda_once.ps1 for better isolation`n" -ForegroundColor Cyan

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Cache Archive Creator (Old)" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Verify source exists
if (-not (Test-Path "juce\build\_deps\opencv-build\lib\Release\opencv_world4130.lib")) {
    Write-Host "ERROR: OpenCV CUDA build not found!" -ForegroundColor Red
    Write-Host "Build the project first before archiving." -ForegroundColor Yellow
    exit 1
}

Write-Host "Creating archive directory..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $archivePath | Out-Null

# Archive critical directories
$dirs = @(
    @{Name="opencv-build"; Path="juce\build\_deps\opencv-build"; Critical=$true},
    @{Name="opencv-src"; Path="juce\build\_deps\opencv-src"; Critical=$false},
    @{Name="opencv_contrib-src"; Path="juce\build\_deps\opencv_contrib-src"; Critical=$false}
)

foreach ($dir in $dirs) {
    if (Test-Path $dir.Path) {
        $sizeGB = (Get-ChildItem -Recurse -File $dir.Path -ErrorAction SilentlyContinue | 
                   Measure-Object -Property Length -Sum).Sum / 1GB
        $sizeRounded = [math]::Round($sizeGB, 2)
        
        if ($dir.Critical) {
            Write-Host ("  [CRITICAL] " + $dir.Name + " (" + $sizeRounded + " GB)...") -ForegroundColor Green
        } else {
            Write-Host ("  [OPTIONAL] " + $dir.Name + " (" + $sizeRounded + " GB)...") -ForegroundColor Cyan
        }
        
        Copy-Item -Recurse -Force $dir.Path "$archivePath\" -ErrorAction Stop
    } else {
        if ($dir.Critical) {
            Write-Host ("  [ERROR] " + $dir.Name + " - NOT FOUND (CRITICAL!)") -ForegroundColor Red
        } else {
            Write-Host ("  [SKIP] " + $dir.Name + " - Not found (optional)") -ForegroundColor Yellow
        }
    }
}

# Archive CMake cache
if (Test-Path "juce\build\CMakeCache.txt") {
    Write-Host "  [FILE] CMakeCache.txt..." -ForegroundColor Cyan
    Copy-Item -Force "juce\build\CMakeCache.txt" $archivePath
}

# Archive FFmpeg DLL
New-Item -ItemType Directory -Force -Path "$archivePath\bin\Release" | Out-Null
if (Test-Path "juce\build\bin\Release\opencv_videoio_ffmpeg4130_64.dll") {
    Write-Host "  [FILE] FFmpeg DLL..." -ForegroundColor Cyan
    Copy-Item -Force "juce\build\bin\Release\opencv_videoio_ffmpeg4130_64.dll" "$archivePath\bin\Release\"
}

# Calculate total size
$totalSize = (Get-ChildItem -Recurse -File $archivePath | 
              Measure-Object -Property Length -Sum).Sum / 1GB
$totalSizeRounded = [math]::Round($totalSize, 2)

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Archive Created Successfully" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

Write-Host "`nLocation: $archivePath" -ForegroundColor Cyan
Write-Host ("Size:     " + $totalSizeRounded + " GB") -ForegroundColor Cyan

Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "  1. Compress (optional):" -ForegroundColor White
Write-Host ("     Compress-Archive -Path '" + $archivePath + "\*' -DestinationPath '" + $archivePath + ".zip'") -ForegroundColor Gray
Write-Host "`n  2. Move to safe storage:" -ForegroundColor White
Write-Host "     - External drive" -ForegroundColor Gray
Write-Host "     - Cloud storage" -ForegroundColor Gray
Write-Host "     - Network share" -ForegroundColor Gray
Write-Host "`n  3. Test restoration (optional):" -ForegroundColor White
Write-Host ("     .\restore_opencv_cache.ps1 " + $archivePath) -ForegroundColor Gray

Write-Host "`nThis archive saves you 30+ minutes of CUDA compilation!" -ForegroundColor Green
