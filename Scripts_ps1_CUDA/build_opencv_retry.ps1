#!/usr/bin/env pwsh
# OpenCV CUDA Build Retry Script
# Handles MSBuild/NVCC race conditions by automatically retrying

param(
    [int]$MaxRetries = 10,
    [string]$Config = "Release"
)

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "         OpenCV CUDA Build with Auto-Retry" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Handling MSBuild/NVCC race conditions..." -ForegroundColor Yellow
Write-Host "Max retries: $MaxRetries" -ForegroundColor Gray
Write-Host ""

$buildPath = "juce\build"

for ($i = 1; $i -le $MaxRetries; $i++) {
    Write-Host "---------------------------------------------------------------" -ForegroundColor Gray
    Write-Host "Attempt $i of $MaxRetries" -ForegroundColor Yellow
    Write-Host "---------------------------------------------------------------" -ForegroundColor Gray
    
    $ErrorActionPreference = "Continue"
    $tempOutFile = [System.IO.Path]::GetTempFileName()
    $tempErrFile = [System.IO.Path]::GetTempFileName()
    
    $process = Start-Process -FilePath "cmake" -ArgumentList "--build",$buildPath,"--config",$Config,"--target","opencv_world" -NoNewWindow -Wait -PassThru -RedirectStandardOutput $tempOutFile -RedirectStandardError $tempErrFile
    $exitCode = $process.ExitCode
    $stdout = Get-Content -Path $tempOutFile -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content -Path $tempErrFile -Raw -ErrorAction SilentlyContinue
    $errorOutput = $stdout + $stderr
    Remove-Item -Path $tempOutFile -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $tempErrFile -Force -ErrorAction SilentlyContinue
    
    if ($exitCode -eq 0) {
        Write-Host ""
        Write-Host "[OK] OpenCV built successfully!" -ForegroundColor Green
        
        Write-Host ""
        Write-Host "Building full project..." -ForegroundColor Cyan
        cmake --build $buildPath --config $Config
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host ""
            Write-Host "================================================================" -ForegroundColor Green
            Write-Host "         BUILD SUCCESSFUL!" -ForegroundColor Green
            Write-Host "================================================================" -ForegroundColor Green
            exit 0
        } else {
            Write-Host ""
            Write-Host "[ERROR] Full project build failed (not a race condition)" -ForegroundColor Red
            exit 1
        }
    } else {
        if ($errorOutput -match "MSB6003|being used by another process|Permission denied") {
            Write-Host "  -> Race condition detected, retrying..." -ForegroundColor Yellow
            Start-Sleep -Seconds 2
        } else {
            Write-Host ""
            Write-Host "[ERROR] Build failed with non-race-condition error:" -ForegroundColor Red
            Write-Host $errorOutput -ForegroundColor Gray
            exit 1
        }
    }
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Red
Write-Host "         BUILD FAILED AFTER $MaxRetries ATTEMPTS" -ForegroundColor Red
Write-Host "================================================================" -ForegroundColor Red
Write-Host ""
Write-Host "Try one of these solutions:" -ForegroundColor Yellow
Write-Host "  1. Close other programs and try again" -ForegroundColor White
Write-Host "  2. Run: cmake --build juce/build --config Release -- /m:1" -ForegroundColor White
Write-Host "     (Disables parallel compilation)" -ForegroundColor Gray
Write-Host "  3. Delete juce/build/ and reconfigure with:" -ForegroundColor White
Write-Host "     cmake -B juce/build -S juce -G 'Visual Studio 17 2022'" -ForegroundColor Gray
Write-Host ""
exit 1
