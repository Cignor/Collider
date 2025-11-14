#!/usr/bin/env pwsh
# Test OpenCV Build System
# Verifies the standalone build system works correctly

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Release", "Debug", "Both")]
    [string]$Configuration = "Both"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  OpenCV Build System Test Suite" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$testsPassed = 0
$testsFailed = 0
$warnings = 0

function Test-Condition {
    param(
        [string]$Name,
        [bool]$Condition,
        [string]$FailMessage = "",
        [bool]$IsWarning = $false
    )
    
    if ($Condition) {
        Write-Host "  ✓ $Name" -ForegroundColor Green
        $script:testsPassed++
        return $true
    } else {
        if ($IsWarning) {
            Write-Host "  ⚠ $Name" -ForegroundColor Yellow
            if ($FailMessage) { Write-Host "    $FailMessage" -ForegroundColor Gray }
            $script:warnings++
            return $false
        } else {
            Write-Host "  ✗ $Name" -ForegroundColor Red
            if ($FailMessage) { Write-Host "    $FailMessage" -ForegroundColor Gray }
            $script:testsFailed++
            return $false
        }
    }
}

# Test 1: Check standalone OpenCV installation
Write-Host "[1/5] Checking Standalone OpenCV Installation" -ForegroundColor Yellow
Write-Host ""

$hasRelease = Test-Path "opencv_cuda_install\lib\opencv_world4130.lib"
$hasDebug = Test-Path "opencv_cuda_install\lib\opencv_world4130d.lib"
$hasHeaders = Test-Path "opencv_cuda_install\include\opencv2\opencv.hpp"

Test-Condition "OpenCV installation directory exists" (Test-Path "opencv_cuda_install")
Test-Condition "OpenCV Release library exists" $hasRelease "" $true
Test-Condition "OpenCV Debug library exists" $hasDebug "" $true
Test-Condition "OpenCV headers installed" $hasHeaders

if (-not $hasRelease -and -not $hasDebug) {
    Write-Host "`n❌ CRITICAL: No OpenCV libraries found!" -ForegroundColor Red
    Write-Host "   Run: .\build_opencv_cuda_once.ps1`n" -ForegroundColor Yellow
    exit 1
}

# Test 2: Check library sizes
Write-Host "`n[2/5] Verifying Library Sizes" -ForegroundColor Yellow
Write-Host ""

if ($hasRelease) {
    $releaseSize = (Get-Item "opencv_cuda_install\lib\opencv_world4130.lib").Length / 1MB
    $releaseSizeOK = $releaseSize -gt 400 -and $releaseSize -lt 600
    Test-Condition "Release library size reasonable ($([math]::Round($releaseSize, 1)) MB)" $releaseSizeOK
}

if ($hasDebug) {
    $debugSize = (Get-Item "opencv_cuda_install\lib\opencv_world4130d.lib").Length / 1MB
    $debugSizeOK = $debugSize -gt 400 -and $debugSize -lt 800
    Test-Condition "Debug library size reasonable ($([math]::Round($debugSize, 1)) MB)" $debugSizeOK
}

# Test 3: CMake configuration test
Write-Host "`n[3/5] Testing CMake Configuration" -ForegroundColor Yellow
Write-Host ""

Write-Host "  Configuring CMake (this may take a moment)..." -ForegroundColor Gray

# Clean CMake cache to force reconfiguration
if (Test-Path "juce\build\CMakeCache.txt") {
    Remove-Item "juce\build\CMakeCache.txt" -Force
}

# Capture CMake output
$cmakeOutput = & cmake -S juce -B juce/build -G "Visual Studio 17 2022" -A x64 2>&1 | Out-String

# Check for success messages
$foundPrebuilt = $cmakeOutput -match "Found Pre-Built OpenCV with CUDA"
$foundRelease = $cmakeOutput -match "Release configuration available"
$foundDebug = $cmakeOutput -match "Debug configuration available"

Test-Condition "CMake configuration succeeded" ($LASTEXITCODE -eq 0)
Test-Condition "CMake detected pre-built OpenCV" $foundPrebuilt

if ($hasRelease) {
    Test-Condition "CMake detected Release configuration" $foundRelease
}
if ($hasDebug) {
    Test-Condition "CMake detected Debug configuration" $foundDebug
}

# Test 4: Build tests (if requested)
if ($Configuration -eq "Release" -or $Configuration -eq "Both") {
    Write-Host "`n[4/5] Testing Release Build" -ForegroundColor Yellow
    Write-Host ""
    
    if ($hasRelease -or $hasDebug) {
        Write-Host "  Building PresetCreatorApp (Release)..." -ForegroundColor Gray
        $buildStart = Get-Date
        
        $buildOutput = & cmake --build juce/build --config Release --target PresetCreatorApp 2>&1 | Out-String
        $buildSuccess = $LASTEXITCODE -eq 0
        
        $buildEnd = Get-Date
        $buildTime = ($buildEnd - $buildStart).TotalMinutes
        
        Test-Condition "Release build succeeded" $buildSuccess
        
        if ($buildSuccess) {
            $fastBuild = $buildTime -lt 15
            Test-Condition "Build completed quickly ($([math]::Round($buildTime, 1)) min)" $fastBuild "" $true
            
            $noOpenCVRebuild = -not ($buildOutput -match "Building.*opencv")
            Test-Condition "OpenCV did not rebuild" $noOpenCVRebuild
        }
        
        # Check output binary
        $exePath = "juce\build\Source\preset_creator\Release\PresetCreatorApp.exe"
        if (Test-Condition "PresetCreatorApp.exe created" (Test-Path $exePath)) {
            $exeSize = (Get-Item $exePath).Length / 1MB
            Write-Host "    Binary size: $([math]::Round($exeSize, 1)) MB" -ForegroundColor Gray
        }
    } else {
        Write-Host "  ⚠ Skipping Release build test (no Release library)" -ForegroundColor Yellow
        $warnings++
    }
}

if ($Configuration -eq "Debug" -or $Configuration -eq "Both") {
    Write-Host "`n[5/5] Testing Debug Build" -ForegroundColor Yellow
    Write-Host ""
    
    if ($hasDebug) {
        Write-Host "  Building PresetCreatorApp (Debug)..." -ForegroundColor Gray
        $buildStart = Get-Date
        
        $buildOutput = & cmake --build juce/build --config Debug --target PresetCreatorApp 2>&1 | Out-String
        $buildSuccess = $LASTEXITCODE -eq 0
        
        $buildEnd = Get-Date
        $buildTime = ($buildEnd - $buildStart).TotalMinutes
        
        Test-Condition "Debug build succeeded" $buildSuccess
        
        if ($buildSuccess) {
            $fastBuild = $buildTime -lt 20
            Test-Condition "Build completed quickly ($([math]::Round($buildTime, 1)) min)" $fastBuild "" $true
            
            $noOpenCVRebuild = -not ($buildOutput -match "Building.*opencv")
            Test-Condition "OpenCV did not rebuild" $noOpenCVRebuild
        }
        
        # Check output binary
        $exePath = "juce\build\Source\preset_creator\Debug\PresetCreatorApp.exe"
        if (Test-Condition "PresetCreatorApp.exe created" (Test-Path $exePath)) {
            $exeSize = (Get-Item $exePath).Length / 1MB
            Write-Host "    Binary size: $([math]::Round($exeSize, 1)) MB" -ForegroundColor Gray
        }
    } elseif ($hasRelease) {
        Write-Host "  ⚠ Debug library not available, testing with Release fallback..." -ForegroundColor Yellow
        
        $buildOutput = & cmake --build juce/build --config Debug --target PresetCreatorApp 2>&1 | Out-String
        $buildSuccess = $LASTEXITCODE -eq 0
        
        Test-Condition "Debug build succeeded (using Release OpenCV)" $buildSuccess "" $true
        
        if ($buildSuccess) {
            Write-Host "    Note: Using Release OpenCV library for Debug build" -ForegroundColor Gray
            Write-Host "    Build Debug OpenCV: .\build_opencv_cuda_once.ps1 -Configuration Debug" -ForegroundColor Gray
        }
    } else {
        Write-Host "  ⚠ Skipping Debug build test (no libraries)" -ForegroundColor Yellow
        $warnings++
    }
}

# Summary
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  Test Results" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$totalTests = $testsPassed + $testsFailed
$successRate = if ($totalTests -gt 0) { ($testsPassed / $totalTests) * 100 } else { 0 }

Write-Host "  Passed:   $testsPassed" -ForegroundColor Green
Write-Host "  Failed:   $testsFailed" -ForegroundColor $(if ($testsFailed -eq 0) { "Green" } else { "Red" })
Write-Host "  Warnings: $warnings" -ForegroundColor Yellow
Write-Host "  Success:  $([math]::Round($successRate, 1))%" -ForegroundColor Cyan

if ($testsFailed -eq 0) {
    Write-Host "`n✅ All critical tests passed!" -ForegroundColor Green
    Write-Host "   The OpenCV standalone build system is working correctly!" -ForegroundColor Green
    
    if ($warnings -gt 0) {
        Write-Host "`n⚠  $warnings warning(s) - see details above" -ForegroundColor Yellow
    }
    
    Write-Host "`n🎉 You can now add nodes without rebuilding OpenCV!" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host "`n❌ $testsFailed test(s) failed!" -ForegroundColor Red
    Write-Host "   Please review the errors above." -ForegroundColor Yellow
    exit 1
}

