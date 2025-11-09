#!/usr/bin/env pwsh
# Archive OpenCV CUDA Build Cache (Debug)
# Preserves lengthy CUDA compilation time for the Debug configuration

$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$archivePath = "opencv_cuda_cache_debug_archive_$timestamp"
$configuration = "Debug"
$opencvLibName = "opencv_world4130d.lib"
$ffmpegDllCandidates = @(
    "opencv_videoio_ffmpeg4130_64d.dll",
    "opencv_videoio_ffmpeg4130_64.dll"
)

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Cache Archive Creator (Debug)" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Verify source exists
$opencvLibPath = "juce\build\_deps\opencv-build\lib\$configuration\$opencvLibName"
if (-not (Test-Path $opencvLibPath)) {
    Write-Host "ERROR: OpenCV CUDA Debug build not found!" -ForegroundColor Red
    Write-Host ("Missing: " + $opencvLibPath) -ForegroundColor Yellow
    Write-Host "Build the Debug configuration before archiving." -ForegroundColor Yellow
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

# Archive FFmpeg DLL (Debug)
New-Item -ItemType Directory -Force -Path "$archivePath\bin\$configuration" | Out-Null

$ffmpegCopied = $false
foreach ($dllName in $ffmpegDllCandidates) {
    $dllPath = "juce\build\bin\$configuration\$dllName"
    if (Test-Path $dllPath) {
        Write-Host ("  [FILE] " + $dllName + "...") -ForegroundColor Cyan
        Copy-Item -Force $dllPath "$archivePath\bin\$configuration\"
        $ffmpegCopied = $true
        break
    }
}

if (-not $ffmpegCopied) {
    Write-Host "  [SKIP] FFmpeg DLL not found for Debug configuration." -ForegroundColor Yellow
}

# Calculate total size
$totalSize = (Get-ChildItem -Recurse -File $archivePath |
              Measure-Object -Property Length -Sum).Sum / 1GB
$totalSizeRounded = [math]::Round($totalSize, 2)

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Debug Archive Created Successfully" -ForegroundColor Green
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


