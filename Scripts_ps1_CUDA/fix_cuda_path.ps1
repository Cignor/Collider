#!/usr/bin/env pwsh
# Create junction point to CUDA to avoid "Program Files" space issue

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  CUDA Path Fix (No Spaces)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (!$isAdmin) {
    Write-Host "⚠️  This script requires Administrator privileges to create junction points." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Please run PowerShell as Administrator and try again:" -ForegroundColor White
    Write-Host "  1. Right-click PowerShell" -ForegroundColor Gray
    Write-Host "  2. Select 'Run as Administrator'" -ForegroundColor Gray
    Write-Host "  3. Run: .\Scripts_ps1_CUDA\fix_cuda_path.ps1" -ForegroundColor Gray
    Write-Host ""
    exit 1
}

# Find CUDA installation
$cudaOriginal = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0"
$cudaJunction = "C:\CUDA"

if (!(Test-Path $cudaOriginal)) {
    Write-Host "[ERROR] CUDA not found at: $cudaOriginal" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please verify your CUDA installation path." -ForegroundColor Yellow
    exit 1
}

# Create junction point
if (Test-Path $cudaJunction) {
    Write-Host "Junction already exists: $cudaJunction" -ForegroundColor Yellow
    
    # Check if it's pointing to the right place
    $target = (Get-Item $cudaJunction).Target
    if ($target -eq $cudaOriginal) {
        Write-Host "  ✓ Already pointing to: $cudaOriginal" -ForegroundColor Green
    } else {
        Write-Host "  ⚠️  Pointing to different location: $target" -ForegroundColor Yellow
        $response = Read-Host "Remove and recreate? (y/N)"
        if ($response -eq "y" -or $response -eq "Y") {
            cmd /c rmdir $cudaJunction
        } else {
            Write-Host "Aborted." -ForegroundColor Yellow
            exit 0
        }
    }
}

if (!(Test-Path $cudaJunction)) {
    Write-Host "Creating junction point..." -ForegroundColor Cyan
    Write-Host "  From: $cudaJunction" -ForegroundColor White
    Write-Host "  To:   $cudaOriginal" -ForegroundColor White
    
    cmd /c mklink /J $cudaJunction $cudaOriginal
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to create junction point!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "  ✓ Junction created successfully!" -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  CUDA Path Fixed!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "You can now use: C:\CUDA" -ForegroundColor White
Write-Host "Instead of:      C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0" -ForegroundColor Gray
Write-Host ""
Write-Host "Next step: Build OpenCV with:" -ForegroundColor Cyan
Write-Host "  .\Scripts_ps1_CUDA\build_opencv_short.ps1" -ForegroundColor White
Write-Host ""

