#!/usr/bin/env pwsh
# Set System CUDA_PATH to use junction (requires Administrator)

#Requires -RunAsAdministrator

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Fix System CUDA Environment Variable" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$junctionPath = "C:\CUDA"

if (!(Test-Path $junctionPath)) {
    Write-Host "[ERROR] Junction C:\CUDA doesn't exist!" -ForegroundColor Red
    Write-Host "Run .\Scripts_ps1_CUDA\fix_cuda_path.ps1 first" -ForegroundColor Yellow
    exit 1
}

# Get current system CUDA_PATH
$currentPath = [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine")
Write-Host "Current system CUDA_PATH:" -ForegroundColor Yellow
Write-Host "  $currentPath" -ForegroundColor White
Write-Host ""

# Set to junction path
Write-Host "Setting system CUDA_PATH to: C:\CUDA" -ForegroundColor Cyan
[Environment]::SetEnvironmentVariable("CUDA_PATH", "C:\CUDA", "Machine")

# Verify
$newPath = [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine")
Write-Host ""
Write-Host "New system CUDA_PATH:" -ForegroundColor Green
Write-Host "  $newPath" -ForegroundColor White
Write-Host ""

Write-Host "========================================" -ForegroundColor Green
Write-Host "  Done!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "⚠️  IMPORTANT: Close and reopen PowerShell for changes to take effect!" -ForegroundColor Yellow
Write-Host ""
Write-Host "Then run: .\Scripts_ps1_CUDA\build_opencv_short.ps1" -ForegroundColor White
Write-Host ""

