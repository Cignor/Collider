#!/usr/bin/env pwsh
# OpenCV CUDA Cache Integrity Verification Script
# Checks if cached OpenCV build files are valid and complete

param(
    [switch]$Verbose,
    [switch]$Deep
)

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "     OpenCV CUDA Cache Integrity Verification" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

$opencvBuildDir = "juce\build\_deps\opencv-build"
$opencvSourceDir = "juce\build\_deps\opencv-src"
$contribSourceDir = "juce\build\_deps\opencv_contrib-src"

$errors = @()
$warnings = @()
$passed = 0
$failed = 0

# ============================================================================
# Helper Functions
# ============================================================================

function Test-DirectoryExists {
    param([string]$path, [string]$name)
    
    if (Test-Path $path -PathType Container) {
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " $name exists"
        return $true
    } else {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " $name not found: $path"
        $script:errors += "$name not found"
        return $false
    }
}

function Test-FileExists {
    param([string]$path, [string]$name)
    
    if (Test-Path $path -PathType Leaf) {
        $size = (Get-Item $path).Length
        $sizeKB = [math]::Round($size / 1KB, 2)
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " $name ($sizeKB KB)"
        $script:passed++
        return $true
    } else {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " $name not found"
        $script:errors += "$name missing: $path"
        $script:failed++
        return $false
    }
}

function Test-FileMinSize {
    param([string]$path, [string]$name, [int64]$minSizeKB)
    
    if (Test-Path $path -PathType Leaf) {
        $size = (Get-Item $path).Length
        $sizeKB = [math]::Round($size / 1KB, 2)
        $sizeMB = [math]::Round($size / 1MB, 2)
        
        if ($size -ge ($minSizeKB * 1KB)) {
            Write-Host "[OK]" -ForegroundColor Green -NoNewline
            Write-Host " $name ($sizeMB MB)"
            $script:passed++
            return $true
        } else {
            Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
            Write-Host " $name is too small ($sizeKB KB, expected >$minSizeKB KB)"
            $script:warnings += "$name is suspiciously small"
            $script:failed++
            return $false
        }
    } else {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " $name not found"
        $script:errors += "$name missing: $path"
        $script:failed++
        return $false
    }
}

function Get-DirectorySize {
    param([string]$path)
    
    if (Test-Path $path) {
        $size = (Get-ChildItem -Path $path -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
        return $size
    }
    return 0
}

# ============================================================================
# Test 1: Check Build Directory Structure
# ============================================================================

Write-Host "Test 1: Build Directory Structure" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

if (-not (Test-DirectoryExists $opencvBuildDir "OpenCV build directory")) {
    Write-Host ""
    Write-Host "[ERROR] OpenCV build directory not found!" -ForegroundColor Red
    Write-Host "  Expected: $opencvBuildDir" -ForegroundColor Gray
    Write-Host "  Run CMake configure first: cmake -B juce/build -S juce" -ForegroundColor Yellow
    exit 1
}

Test-DirectoryExists $opencvSourceDir "OpenCV source directory" | Out-Null
Test-DirectoryExists $contribSourceDir "OpenCV contrib source directory" | Out-Null

Write-Host ""

# ============================================================================
# Test 2: Critical Build Artifacts
# ============================================================================

Write-Host "Test 2: Critical Build Artifacts" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

# Critical: Must exist for linking
$criticalFiles = @{
    "opencv_world library" = "$opencvBuildDir\lib\Release\opencv_world4130.lib"
    "opencv2 config" = "$opencvBuildDir\opencv2\opencv_modules.hpp"
}

foreach ($entry in $criticalFiles.GetEnumerator()) {
    Test-FileExists $entry.Value $entry.Key | Out-Null
}

# Optional: Only needed for rebuilding OpenCV (not for linking)
$optionalFiles = @{
    "CMake cache" = "$opencvBuildDir\CMakeCache.txt"
    "OpenCV config header" = "$opencvBuildDir\cvconfig.h"
}

foreach ($entry in $optionalFiles.GetEnumerator()) {
    if (Test-Path $entry.Value -PathType Leaf) {
        $size = (Get-Item $entry.Value).Length
        $sizeKB = [math]::Round($size / 1KB, 2)
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " $($entry.Key) ($sizeKB KB)"
        $script:passed++
    } else {
        Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
        Write-Host " $($entry.Key) not found (regenerate with: cmake -B juce/build -S juce)"
        $script:warnings += "$($entry.Key) missing (can be regenerated)"
    }
}

Write-Host ""

# ============================================================================
# Test 3: CUDA Compilation Artifacts
# ============================================================================

Write-Host "Test 3: CUDA Compilation Artifacts" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

$cudaDir = "$opencvBuildDir\modules\world"

# Check for CUDA object files
$cudaObjPattern = "*.cu.obj"
$cudaObjFiles = Get-ChildItem -Path $cudaDir -Filter $cudaObjPattern -Recurse -ErrorAction SilentlyContinue

if ($cudaObjFiles.Count -gt 0) {
    Write-Host "[OK]" -ForegroundColor Green -NoNewline
    Write-Host " Found $($cudaObjFiles.Count) CUDA object files (.cu.obj)"
    $script:passed++
} else {
    Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
    Write-Host " No CUDA object files found (build may be incomplete)"
    $script:warnings += "No CUDA object files found"
    $script:failed++
}

# Check for CUDA intermediate files
$cudaIntFiles = @(
    "tmpxft_*.cpp",
    "*.compute_*.cudafe*.cpp",
    "*.fatbin.c"
)

$cudaIntCount = 0
foreach ($pattern in $cudaIntFiles) {
    $found = Get-ChildItem -Path $cudaDir -Filter $pattern -Recurse -ErrorAction SilentlyContinue
    $cudaIntCount += $found.Count
}

if ($cudaIntCount -gt 0) {
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " Found $cudaIntCount CUDA intermediate files"
} else {
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " No CUDA intermediate files (cleaned build)"
}

Write-Host ""

# ============================================================================
# Test 4: Main Library File Size and Integrity
# ============================================================================

Write-Host "Test 4: Main Library Integrity" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

# opencv_world should be at least 200 MB for a full CUDA build
Test-FileMinSize "$opencvBuildDir\lib\Release\opencv_world4130.lib" "opencv_world4130.lib" 200000 | Out-Null

# Check if library is locked (being written to)
$libPath = "$opencvBuildDir\lib\Release\opencv_world4130.lib"
if (Test-Path $libPath) {
    try {
        $stream = [System.IO.File]::Open($libPath, 'Open', 'Read', 'None')
        $stream.Close()
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " Library file is not locked"
        $script:passed++
    } catch {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " Library file is locked (build in progress?)"
        $script:errors += "Library file is locked"
        $script:failed++
    }
}

Write-Host ""

# ============================================================================
# Test 5: CUDA Module Libraries
# ============================================================================

Write-Host "Test 5: CUDA Module Libraries" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

$cudaModules = @(
    "cudaarithm",
    "cudabgsegm",
    "cudafilters",
    "cudaimgproc",
    "cudawarping",
    "cudaobjdetect",
    "cudaoptflow"
)

$cudaModuleDir = "$opencvBuildDir\lib\Release"
$cudaModulesFound = 0

foreach ($module in $cudaModules) {
    $modulePath = "$cudaModuleDir\opencv_$module*.lib"
    if (Get-ChildItem -Path $modulePath -ErrorAction SilentlyContinue) {
        if ($Verbose) {
            Write-Host "[OK]" -ForegroundColor Green -NoNewline
            Write-Host " $module module found"
        }
        $cudaModulesFound++
        $script:passed++
    } else {
        if ($Verbose) {
            Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
            Write-Host " $module module not found (may be in opencv_world)"
        }
    }
}

Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
Write-Host " $cudaModulesFound/$($cudaModules.Count) CUDA modules detected"

Write-Host ""

# ============================================================================
# Test 6: Build Configuration Verification
# ============================================================================

Write-Host "Test 6: Build Configuration" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

$cmakeCachePath = "$opencvBuildDir\CMakeCache.txt"
if (Test-Path $cmakeCachePath) {
    $cacheContent = Get-Content $cmakeCachePath -Raw
    
    # Check CUDA settings
    $checks = @{
        "WITH_CUDA:BOOL=ON" = "CUDA support enabled"
        "OPENCV_DNN_CUDA:BOOL=ON" = "DNN CUDA backend enabled"
        "WITH_CUDNN:BOOL=ON" = "cuDNN support enabled"
        "CUDA_ARCH_BIN" = "CUDA architectures specified"
    }
    
    foreach ($entry in $checks.GetEnumerator()) {
        if ($cacheContent -match [regex]::Escape($entry.Key)) {
            Write-Host "[OK]" -ForegroundColor Green -NoNewline
            Write-Host " $($entry.Value)"
            $script:passed++
        } else {
            Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
            Write-Host " $($entry.Value) not found in cache"
            $script:warnings += $entry.Value + " not configured"
            $script:failed++
        }
    }
}

Write-Host ""

# ============================================================================
# Test 7: Build Timestamp Check
# ============================================================================

Write-Host "Test 7: Build Timestamps" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

if (Test-Path "$opencvBuildDir\lib\Release\opencv_world4130.lib") {
    $libFile = Get-Item "$opencvBuildDir\lib\Release\opencv_world4130.lib"
    $age = (Get-Date) - $libFile.LastWriteTime
    
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " Library last modified: $($libFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " Age: $([math]::Round($age.TotalDays, 1)) days ($([math]::Round($age.TotalHours, 1)) hours)"
    
    if ($age.TotalHours -lt 1) {
        Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
        Write-Host " Recent build (less than 1 hour old)"
    } elseif ($age.TotalDays -gt 30) {
        Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
        Write-Host " Build is over 30 days old, may be outdated"
        $script:warnings += "Build is over 30 days old"
    }
}

Write-Host ""

# ============================================================================
# Test 8: Deep Integrity Check (Optional)
# ============================================================================

if ($Deep) {
    Write-Host "Test 8: Deep Integrity Check" -ForegroundColor Yellow
    Write-Host "---------------------------------------------------------------" -ForegroundColor Gray
    
    # Count all object files
    $objFiles = Get-ChildItem -Path $opencvBuildDir -Filter "*.obj" -Recurse -ErrorAction SilentlyContinue
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " Total object files: $($objFiles.Count)"
    
    # Check for zero-byte files
    $zeroByte = $objFiles | Where-Object { $_.Length -eq 0 }
    if ($zeroByte.Count -gt 0) {
        Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
        Write-Host " Found $($zeroByte.Count) zero-byte object files (may indicate corruption)"
        $script:warnings += "Zero-byte object files found"
        if ($Verbose) {
            $zeroByte | ForEach-Object {
                Write-Host "  - $($_.FullName)" -ForegroundColor Gray
            }
        }
    } else {
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " No zero-byte object files found"
        $script:passed++
    }
    
    # Calculate total cache size
    $totalSize = Get-DirectorySize $opencvBuildDir
    $totalSizeGB = [math]::Round($totalSize / 1GB, 2)
    Write-Host "[INFO]" -ForegroundColor Cyan -NoNewline
    Write-Host " Total cache size: $totalSizeGB GB"
    
    Write-Host ""
}

# ============================================================================
# Test 9: Verify Library Can Be Linked (Quick Test)
# ============================================================================

Write-Host "Test 9: Linkability Test" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor Gray

$libPath = "$opencvBuildDir\lib\Release\opencv_world4130.lib"
if (Test-Path $libPath) {
    try {
        # Try to read the library file header
        $bytes = [System.IO.File]::ReadAllBytes($libPath)
        
        if ($bytes.Length -gt 0) {
            # Check for valid lib/archive signature
            $signature = [System.Text.Encoding]::ASCII.GetString($bytes[0..7])
            if ($signature -match "!<arch>") {
                Write-Host "[OK]" -ForegroundColor Green -NoNewline
                Write-Host " Library file has valid archive signature"
                $script:passed++
            } else {
                Write-Host "[WARN]" -ForegroundColor Yellow -NoNewline
                Write-Host " Library file may be corrupted (invalid signature)"
                $script:warnings += "Library file has invalid signature"
                $script:failed++
            }
        }
    } catch {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " Cannot read library file: $($_.Exception.Message)"
        $script:errors += "Cannot read library file"
        $script:failed++
    }
}

Write-Host ""

# ============================================================================
# Summary Report
# ============================================================================

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "                    VERIFICATION SUMMARY" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "Passed: " -NoNewline
Write-Host $passed -ForegroundColor Green

Write-Host "Failed: " -NoNewline
if ($failed -gt 0) {
    Write-Host $failed -ForegroundColor Red
} else {
    Write-Host $failed -ForegroundColor Green
}

Write-Host "Warnings: " -NoNewline
if ($warnings.Count -gt 0) {
    Write-Host $warnings.Count -ForegroundColor Yellow
} else {
    Write-Host $warnings.Count -ForegroundColor Green
}

Write-Host ""

if ($errors.Count -gt 0) {
    Write-Host "Critical Errors:" -ForegroundColor Red
    foreach ($error in $errors) {
        Write-Host "  - $error" -ForegroundColor Red
    }
    Write-Host ""
}

if ($warnings.Count -gt 0) {
    Write-Host "Warnings:" -ForegroundColor Yellow
    foreach ($warning in $warnings) {
        Write-Host "  - $warning" -ForegroundColor Yellow
    }
    Write-Host ""
}

# ============================================================================
# Final Verdict
# ============================================================================

if ($errors.Count -eq 0 -and $failed -le 2) {
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host "  VERDICT: OpenCV cache is HEALTHY" -ForegroundColor Green
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host ""
    
    if ($warnings.Count -gt 0 -and $warnings -match "CMake cache|config header") {
        Write-Host "Note: Some optional files are missing but can be regenerated:" -ForegroundColor Yellow
        Write-Host "  cmake -B juce/build -S juce -G 'Visual Studio 17 2022'" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "These files are only needed if rebuilding OpenCV itself." -ForegroundColor Gray
        Write-Host ""
    }
    
    Write-Host "You can proceed with building your project." -ForegroundColor Green
    Write-Host ""
    exit 0
} elseif ($errors.Count -gt 0 -or $failed -gt 5) {
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host "  VERDICT: OpenCV cache is CORRUPTED" -ForegroundColor Red
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Critical files are missing or corrupted!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Recommended actions:" -ForegroundColor Yellow
    Write-Host "  1. Delete the build directory and reconfigure:" -ForegroundColor White
    Write-Host "     Remove-Item -Path juce\build\_deps\opencv-build -Recurse -Force" -ForegroundColor Gray
    Write-Host "     cmake -B juce/build -S juce -G 'Visual Studio 17 2022'" -ForegroundColor Gray
    Write-Host "  2. Or restore from a backup if available" -ForegroundColor White
    Write-Host ""
    exit 1
} else {
    Write-Host "================================================================" -ForegroundColor Yellow
    Write-Host "  VERDICT: OpenCV cache has minor issues" -ForegroundColor Yellow
    Write-Host "================================================================" -ForegroundColor Yellow
    Write-Host ""
    
    if ($warnings.Count -gt 0 -and $warnings -match "CMake cache|config header") {
        Write-Host "Some optional files are missing but can be regenerated:" -ForegroundColor Yellow
        Write-Host "  cmake -B juce/build -S juce -G 'Visual Studio 17 2022'" -ForegroundColor Cyan
        Write-Host ""
    } else {
        Write-Host "The cache may work, but consider rebuilding if you encounter errors." -ForegroundColor Yellow
        Write-Host ""
    }
    
    exit 0
}

