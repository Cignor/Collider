#!/usr/bin/env pwsh
# Archive OpenCV CUDA Build Cache (DEBUG Configuration)
# Preserves 30+ minutes of CUDA compilation time

# Ensure we're in the project root
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
Set-Location $projectRoot

$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$archivePath = "Scripts_ps1_CUDA\opencv_cuda_cache_debug_archive_$timestamp"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Cache Archive Creator" -ForegroundColor Cyan
Write-Host "  [DEBUG Configuration]" -ForegroundColor Magenta
Write-Host "========================================`n" -ForegroundColor Cyan
Write-Host "Working directory: $projectRoot" -ForegroundColor Gray

# Verify source exists (Debug build)
if (-not (Test-Path "juce\build\_deps\opencv-build\lib\Debug\opencv_world4130d.lib")) {
    Write-Host "ERROR: OpenCV CUDA Debug build not found!" -ForegroundColor Red
    Write-Host "Expected: juce\build\_deps\opencv-build\lib\Debug\opencv_world4130d.lib" -ForegroundColor Yellow
    Write-Host "Build the Debug configuration first before archiving." -ForegroundColor Yellow
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

# Capture CMake generator information for reference (helps restore command)
$cmakeCachePath = "juce\build\CMakeCache.txt"
if (Test-Path $cmakeCachePath) {
    Write-Host "  [INFO] Recording CMake generator details..." -ForegroundColor Cyan
    $cacheContent = Get-Content $cmakeCachePath
    $generator = ($cacheContent | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' }) -replace '^CMAKE_GENERATOR:INTERNAL=', ''
    $platform  = ($cacheContent | Where-Object { $_ -match '^CMAKE_GENERATOR_PLATFORM:INTERNAL=' }) -replace '^CMAKE_GENERATOR_PLATFORM:INTERNAL=', ''
    $toolset   = ($cacheContent | Where-Object { $_ -match '^CMAKE_GENERATOR_TOOLSET:INTERNAL=' }) -replace '^CMAKE_GENERATOR_TOOLSET:INTERNAL=', ''
    $infoLines = @(
        "Generator: $generator",
        "Platform:  $platform",
        "Toolset:   $toolset"
    )
    $infoLines | Set-Content (Join-Path $archivePath "cmake-generator-info.txt")
}

# Archive FFmpeg DLL (Debug)
New-Item -ItemType Directory -Force -Path "$archivePath\bin\Debug" | Out-Null
if (Test-Path "juce\build\bin\Debug\opencv_videoio_ffmpeg4130_64.dll") {
    Write-Host "  [FILE] FFmpeg DLL (Debug)..." -ForegroundColor Cyan
    Copy-Item -Force "juce\build\bin\Debug\opencv_videoio_ffmpeg4130_64.dll" "$archivePath\bin\Debug\"
}

# Calculate total size
$totalSize = (Get-ChildItem -Recurse -File $archivePath | 
              Measure-Object -Property Length -Sum).Sum / 1GB
$totalSizeRounded = [math]::Round($totalSize, 2)

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Archive Created Successfully (DEBUG)" -ForegroundColor Green
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
Write-Host ("     .\restore_opencv_cache_debug.ps1 " + $archivePath) -ForegroundColor Gray

Write-Host "`nThis archive saves you 30+ minutes of CUDA compilation!" -ForegroundColor Green
Write-Host "`n💡 Pro Tip: OpenCV config is now in juce/cmake/OpenCVConfig.cmake" -ForegroundColor Yellow
Write-Host "   Modifying main CMakeLists.txt (adding nodes) won't invalidate OpenCV cache!" -ForegroundColor Gray

