#!/usr/bin/env pwsh
# OpenCV Configuration Files Regeneration Script
# Quickly regenerates CMakeCache.txt and cvconfig.h without rebuilding

param(
    [switch]$Verify
)

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "    OpenCV Configuration Files Regeneration" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "NOTE: CMakeCache.txt and cvconfig.h are optional files that" -ForegroundColor Yellow
Write-Host "      are NOT created by FetchContent builds. You only need" -ForegroundColor Yellow
Write-Host "      them if modifying OpenCV source code." -ForegroundColor Yellow
Write-Host ""

$opencvBuildDir = "juce\build\_deps\opencv-build"

# ============================================================================
# Check if OpenCV build directory exists
# ============================================================================

if (-not (Test-Path $opencvBuildDir -PathType Container)) {
    Write-Host "[ERROR] OpenCV build directory not found!" -ForegroundColor Red
    Write-Host "  Expected: $opencvBuildDir" -ForegroundColor Gray
    Write-Host ""
    Write-Host "You need to run full CMake configure first:" -ForegroundColor Yellow
    Write-Host "  cmake -B juce/build -S juce -G 'Visual Studio 17 2022'" -ForegroundColor Cyan
    Write-Host ""
    exit 1
}

# ============================================================================
# Check which files are missing
# ============================================================================

$missingFiles = @()

Write-Host "Checking current status..." -ForegroundColor Yellow
Write-Host ""

$cacheFile = "$opencvBuildDir\CMakeCache.txt"
$configFile = "$opencvBuildDir\cvconfig.h"

if (Test-Path $cacheFile) {
    Write-Host "[OK]" -ForegroundColor Green -NoNewline
    Write-Host " CMakeCache.txt exists"
} else {
    Write-Host "[MISSING]" -ForegroundColor Yellow -NoNewline
    Write-Host " CMakeCache.txt"
    $missingFiles += "CMakeCache.txt"
}

if (Test-Path $configFile) {
    Write-Host "[OK]" -ForegroundColor Green -NoNewline
    Write-Host " cvconfig.h exists"
} else {
    Write-Host "[MISSING]" -ForegroundColor Yellow -NoNewline
    Write-Host " cvconfig.h"
    $missingFiles += "cvconfig.h"
}

Write-Host ""

if ($missingFiles.Count -eq 0) {
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host "  All configuration files already exist!" -ForegroundColor Green
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "No regeneration needed." -ForegroundColor Green
    Write-Host ""
    exit 0
}

# Additional check: If only CMakeCache.txt and cvconfig.h are missing,
# they're actually not regenerable via FetchContent
if ($missingFiles.Count -eq 2 -and 
    $missingFiles -contains "CMakeCache.txt" -and 
    $missingFiles -contains "cvconfig.h") {
    Write-Host "================================================================" -ForegroundColor Yellow
    Write-Host "  These files are NOT needed!" -ForegroundColor Yellow
    Write-Host "================================================================" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "CMakeCache.txt and cvconfig.h are NOT created by FetchContent." -ForegroundColor Cyan
    Write-Host "They're only needed if you're modifying OpenCV source code." -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Your OpenCV cache is HEALTHY if you have:" -ForegroundColor Green
    Write-Host "  [OK] opencv_world4130.lib (the compiled library)" -ForegroundColor Green
    Write-Host "  [OK] opencv2/opencv_modules.hpp (the headers)" -ForegroundColor Green
    Write-Host ""
    Write-Host "Run verification to confirm:" -ForegroundColor Yellow
    Write-Host "  .\verify_opencv_cache.ps1" -ForegroundColor Cyan
    Write-Host ""
    exit 0
}

# ============================================================================
# Regenerate files
# ============================================================================

Write-Host "================================================================" -ForegroundColor Yellow
Write-Host "  Regenerating $($missingFiles.Count) file(s)..." -ForegroundColor Yellow
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host ""

Write-Host "Running CMake configure (this will take 10-30 seconds)..." -ForegroundColor Cyan
Write-Host ""

# Run CMake configure
$startTime = Get-Date

try {
    # Use direct invocation with properly quoted generator
    $ErrorActionPreference = "Continue"
    
    # Redirect output to files
    cmake -B juce/build -S juce -G "Visual Studio 17 2022" > cmake_regen_output.txt 2> cmake_regen_error.txt
    $exitCode = $LASTEXITCODE
    
    $duration = (Get-Date) - $startTime
    
    if ($exitCode -eq 0) {
        Write-Host ""
        Write-Host "[OK] CMake configure completed successfully" -ForegroundColor Green
        Write-Host "     Duration: $([math]::Round($duration.TotalSeconds, 1)) seconds" -ForegroundColor Gray
        Write-Host ""
    } else {
        Write-Host ""
        Write-Host "[ERROR] CMake configure failed!" -ForegroundColor Red
        Write-Host ""
        
        if (Test-Path "cmake_regen_error.txt") {
            $errorContent = Get-Content "cmake_regen_error.txt" -Raw -ErrorAction SilentlyContinue
            if ($errorContent) {
                Write-Host "Error output:" -ForegroundColor Yellow
                Write-Host $errorContent -ForegroundColor Gray
            }
        }
        
        Write-Host ""
        Write-Host "Try running manually:" -ForegroundColor Yellow
        Write-Host '  cmake -B juce/build -S juce -G "Visual Studio 17 2022"' -ForegroundColor Cyan
        Write-Host ""
        
        # Cleanup temp files
        Remove-Item "cmake_regen_output.txt" -Force -ErrorAction SilentlyContinue
        Remove-Item "cmake_regen_error.txt" -Force -ErrorAction SilentlyContinue
        
        exit 1
    }
} catch {
    Write-Host ""
    Write-Host "[ERROR] Failed to run CMake: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    exit 1
}

# Cleanup temp files
Remove-Item "cmake_regen_output.txt" -Force -ErrorAction SilentlyContinue
Remove-Item "cmake_regen_error.txt" -Force -ErrorAction SilentlyContinue

# ============================================================================
# Verify regeneration
# ============================================================================

Write-Host "Verifying regenerated files..." -ForegroundColor Yellow
Write-Host ""

$success = $true

foreach ($file in $missingFiles) {
    $filePath = if ($file -eq "CMakeCache.txt") { $cacheFile } else { $configFile }
    
    if (Test-Path $filePath) {
        $size = (Get-Item $filePath).Length
        $sizeKB = [math]::Round($size / 1KB, 2)
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " $file regenerated ($sizeKB KB)"
    } else {
        Write-Host "[FAIL]" -ForegroundColor Red -NoNewline
        Write-Host " $file was not created"
        $success = $false
    }
}

Write-Host ""

# ============================================================================
# Final status
# ============================================================================

if ($success) {
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host "  SUCCESS! All files regenerated" -ForegroundColor Green
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "OpenCV configuration files are now complete." -ForegroundColor Green
    Write-Host ""
    
    # Auto-verify if requested
    if ($Verify) {
        Write-Host "Running verification..." -ForegroundColor Cyan
        Write-Host ""
        & .\verify_opencv_cache.ps1
    } else {
        Write-Host "Tip: Run verification to confirm everything is good:" -ForegroundColor Yellow
        Write-Host "  .\verify_opencv_cache.ps1" -ForegroundColor Cyan
        Write-Host ""
    }
    
    exit 0
} else {
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host "  FAILED - Some files were not regenerated" -ForegroundColor Red
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "This may indicate a CMake configuration issue." -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

