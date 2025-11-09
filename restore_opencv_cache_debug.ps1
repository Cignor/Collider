#!/usr/bin/env pwsh
# Restore OpenCV CUDA Build Cache (Debug)
# Recovers lengthy CUDA compilation time for the Debug configuration

param(
    [Parameter(Mandatory=$false)]
    [string]$ArchivePath
)

$configuration = "Debug"
$opencvLibName = "opencv_world4130d.lib"
$ffmpegDllCandidates = @(
    "opencv_videoio_ffmpeg4130_64d.dll",
    "opencv_videoio_ffmpeg4130_64.dll"
)

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV CUDA Cache Restore Tool (Debug)" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Find archive if not specified
if (-not $ArchivePath) {
    $archives = Get-ChildItem -Directory -Filter "opencv_cuda_cache_debug_archive_*" | Sort-Object LastWriteTime -Descending

    if ($archives.Count -eq 0) {
        Write-Host "ERROR: No debug archives found!" -ForegroundColor Red
        Write-Host "Looking for: opencv_cuda_cache_debug_archive_*" -ForegroundColor Yellow
        Write-Host "Run archive_opencv_cache_debug.ps1 first." -ForegroundColor Yellow
        exit 1
    }

    $ArchivePath = $archives[0].Name
    Write-Host ("Using most recent debug archive: " + $ArchivePath) -ForegroundColor Cyan
}

# Verify archive exists
if (-not (Test-Path $ArchivePath)) {
    Write-Host ("ERROR: Archive not found: " + $ArchivePath) -ForegroundColor Red
    exit 1
}

# Verify archive contains critical files
$debugLibPath = "$ArchivePath\opencv-build\lib\$configuration\$opencvLibName"
if (-not (Test-Path $debugLibPath)) {
    Write-Host "ERROR: Archive is incomplete or corrupted!" -ForegroundColor Red
    Write-Host ("Missing: " + $debugLibPath) -ForegroundColor Yellow
    exit 1
}

Write-Host "Archive verified successfully" -ForegroundColor Green

# Create build directory structure
Write-Host "`nPreparing build directories..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path "juce\build\_deps" | Out-Null
New-Item -ItemType Directory -Force -Path "juce\build\bin\$configuration" | Out-Null

# Restore directories
$dirs = @("opencv-build", "opencv-src", "opencv_contrib-src")

Write-Host "`nRestoring cache..." -ForegroundColor Yellow
foreach ($dir in $dirs) {
    $srcPath = "$ArchivePath\$dir"
    $destPath = "juce\build\_deps\$dir"

    if (Test-Path $srcPath) {
        # Remove existing if present
        if (Test-Path $destPath) {
            Write-Host ("  [REMOVE] Removing old " + $dir + "...") -ForegroundColor Gray
            Remove-Item -Recurse -Force $destPath
        }

        $sizeGB = (Get-ChildItem -Recurse -File $srcPath -ErrorAction SilentlyContinue |
                   Measure-Object -Property Length -Sum).Sum / 1GB
        $sizeRounded = [math]::Round($sizeGB, 2)

        Write-Host ("  [RESTORE] " + $dir + " (" + $sizeRounded + " GB)...") -ForegroundColor Cyan
        Copy-Item -Recurse -Force $srcPath $destPath
    } else {
        Write-Host ("  [SKIP] " + $dir + " not in archive (optional)") -ForegroundColor Yellow
    }
}

# Restore CMake cache
if (Test-Path "$ArchivePath\CMakeCache.txt") {
    Write-Host "  [RESTORE] CMakeCache.txt..." -ForegroundColor Cyan
    Copy-Item -Force "$ArchivePath\CMakeCache.txt" "juce\build\"
}

# Restore FFmpeg DLL
$ffmpegRestored = $false
foreach ($dllName in $ffmpegDllCandidates) {
    $dllPath = "$ArchivePath\bin\$configuration\$dllName"
    if (Test-Path $dllPath) {
        Write-Host ("  [RESTORE] " + $dllName + "...") -ForegroundColor Cyan
        Copy-Item -Force $dllPath "juce\build\bin\$configuration\"
        $ffmpegRestored = $true
        break
    }
}

if (-not $ffmpegRestored) {
    Write-Host "  [SKIP] FFmpeg DLL not in archive (optional)" -ForegroundColor Yellow
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Debug Cache Restored Successfully" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "  1. Reconfigure CMake (fast - uses cache):" -ForegroundColor White
Write-Host '     cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug' -ForegroundColor Gray
Write-Host "`n  2. Build project (fast - uses cache):" -ForegroundColor White
Write-Host "     cmake --build juce/build --config Debug" -ForegroundColor Gray

Write-Host "`nOpenCV CUDA will NOT rebuild - using cached debug version!" -ForegroundColor Green
Write-Host "Saved: 30+ minutes of compilation time" -ForegroundColor Cyan


