# Verify that files in Release directory match manifest hashes
param(
    [Parameter(Mandatory = $false)]
    [string]$ManifestPath = "manifest.json",
    
    [Parameter(Mandatory = $false)]
    [string]$ReleaseDir = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release",
    
    [Parameter(Mandatory = $false)]
    [int]$SampleCount = 10
)

Write-Host "==================================" -ForegroundColor Cyan
Write-Host " Hash Verification Diagnostic" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host ""

# Load manifest
if (-not (Test-Path $ManifestPath)) {
    Write-Host "ERROR: Manifest not found at: $ManifestPath" -ForegroundColor Red
    exit 1
}

$manifestJson = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$variant = $manifestJson.variants.cuda

if ($null -eq $variant) {
    Write-Host "ERROR: 'cuda' variant not found in manifest" -ForegroundColor Red
    exit 1
}

$files = $variant.files | Get-Member -MemberType NoteProperty | Select-Object -ExpandProperty Name

Write-Host "Total files in manifest: $($files.Count)" -ForegroundColor Yellow
Write-Host "Checking $SampleCount random files..." -ForegroundColor Yellow
Write-Host ""

# Sample random files
$sampled = $files | Get-Random -Count ([Math]::Min($SampleCount, $files.Count))

$matches = 0
$mismatches = 0
$missing = 0

foreach ($relativePath in $sampled) {
    $fileInfo = $variant.files.$relativePath
    $fullPath = Join-Path $ReleaseDir $relativePath
    
    Write-Host "Checking: $relativePath" -ForegroundColor Gray
    
    if (-not (Test-Path $fullPath)) {
        Write-Host "  [MISSING] File does not exist on disk" -ForegroundColor Red
        $missing++
        continue
    }
    
    $actualHash = (Get-FileHash -Path $fullPath -Algorithm SHA256).Hash.ToLower()
    $expectedHash = $fileInfo.sha256
    
    if ($actualHash -eq $expectedHash) {
        Write-Host "  [OK] Hash matches" -ForegroundColor Green
        $matches++
    }
    else {
        Write-Host "  [MISMATCH] Hashes do not match!" -ForegroundColor Red
        Write-Host "    Expected: $expectedHash" -ForegroundColor Yellow
        Write-Host "    Actual:   $actualHash" -ForegroundColor Yellow
        $mismatches++
    }
}

Write-Host ""
Write-Host "==================================" -ForegroundColor Cyan
Write-Host " Results" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host "Matches:    $matches" -ForegroundColor Green
Write-Host "Mismatches: $mismatches" -ForegroundColor $(if ($mismatches -eq 0) { "Green" } else { "Red" })
Write-Host "Missing:    $missing" -ForegroundColor $(if ($missing -eq 0) { "Green" } else { "Red" })
Write-Host ""

if ($mismatches -gt 0 -or $missing -gt 0) {
    Write-Host "DIAGNOSIS: Manifest does not match Release directory" -ForegroundColor Red
    Write-Host "SOLUTION: Re-run quick_generate.ps1 to regenerate the manifest" -ForegroundColor Yellow
}
else {
    Write-Host "DIAGNOSIS: All sampled files match!" -ForegroundColor Green
    Write-Host "The issue is likely in the updater's file tracking logic" -ForegroundColor Yellow
}
