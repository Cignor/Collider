# Add Missing DLLs to Pikon Raditsz Release
# This script ONLY copies missing CUDA and OpenCV DLLs
# It does NOT modify CMakeLists.txt

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Pikon Raditsz - Add Missing DLLs" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Target directory (your Pikon Raditsz release folder)
$TargetDir = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release"

# Source paths
$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin"
$OpenCVPath = "H:\0000_CODE\01_collider_pyo\opencv_cuda_install\bin"

# Verify target directory exists
if (-not (Test-Path $TargetDir)) {
    Write-Host "ERROR: Target directory not found!" -ForegroundColor Red
    Write-Host "Expected: $TargetDir" -ForegroundColor Yellow
    exit 1
}

Write-Host "Target: $TargetDir" -ForegroundColor Green
Write-Host ""

# Function to copy DLL with status
function Copy-DLL {
    param(
        [string]$SourcePath,
        [string]$DllName,
        [string]$Category,
        [bool]$Required = $true
    )
    
    $source = Join-Path $SourcePath $DllName
    $dest = Join-Path $TargetDir $DllName
    
    if (Test-Path $dest) {
        Write-Host "  [OK] $DllName (already exists)" -ForegroundColor Gray
        return $true
    }
    
    if (Test-Path $source) {
        Copy-Item $source -Destination $dest
        $size = (Get-Item $dest).Length / 1MB
        $sizeRounded = [math]::Round($size, 2)
        $sizeText = "$sizeRounded MB"
        Write-Host "  [OK] $DllName ($sizeText)" -ForegroundColor Green
        return $true
    }
    else {
        if ($Required) {
            Write-Host "  [X] $DllName NOT FOUND (required)" -ForegroundColor Red
            return $false
        }
        else {
            Write-Host "  - $DllName (optional, not found)" -ForegroundColor Yellow
            return $true
        }
    }
}

# Track if all required DLLs are found
$allFound = $true

# Copy CUDA DLLs
Write-Host "CUDA Runtime DLLs:" -ForegroundColor Cyan
$allFound = (Copy-DLL $CudaPath "cudart64_130.dll" "CUDA" $true) -and $allFound
$allFound = (Copy-DLL $CudaPath "cublas64_13.dll" "CUDA" $true) -and $allFound
$allFound = (Copy-DLL $CudaPath "cublasLt64_13.dll" "CUDA" $true) -and $allFound
Copy-DLL $CudaPath "cudnn64_9.dll" "CUDA" $false | Out-Null

Write-Host ""

# Copy OpenCV DLL
Write-Host "OpenCV DLLs:" -ForegroundColor Cyan
$allFound = (Copy-DLL $OpenCVPath "opencv_world4130.dll" "OpenCV" $true) -and $allFound

Write-Host ""

# Summary
Write-Host "========================================" -ForegroundColor Cyan
if ($allFound) {
    Write-Host " [OK] All required DLLs added successfully!" -ForegroundColor Green
}
else {
    Write-Host " [X] Some required DLLs are missing!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Missing DLLs must be installed:" -ForegroundColor Yellow
    Write-Host "  - CUDA Toolkit 13.0: https://developer.nvidia.com/cuda-downloads" -ForegroundColor White
    Write-Host "  - OpenCV (already built): Check opencv_cuda_install folder" -ForegroundColor White
}
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Show current folder contents
Write-Host "Current DLLs in Pikon Raditsz folder:" -ForegroundColor Yellow
Get-ChildItem $TargetDir -Filter "*.dll" | ForEach-Object {
    $size = $_.Length / 1MB
    $sizeRounded = [math]::Round($size, 2)
    $sizeText = "$sizeRounded MB"
    Write-Host "  - $($_.Name) ($sizeText)" -ForegroundColor White
}

Write-Host ""
$totalSize = (Get-ChildItem $TargetDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
$totalSizeRounded = [math]::Round($totalSize, 2)
Write-Host "Total folder size: $totalSizeRounded MB" -ForegroundColor Cyan
Write-Host ""
Write-Host "Ready for distribution!" -ForegroundColor Green
Write-Host "Just ZIP the entire Release folder and distribute." -ForegroundColor White
