# PowerShell script to generate update manifest
# Usage: .\generate_manifest.ps1 -BuildDir "path/to/build" -Version "1.0.0" -Variant "cuda"

param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    
    [Parameter(Mandatory = $false)]
    [string]$Version,
    
    [Parameter(Mandatory = $false)]
    [string]$Variant = "standard",
    
    [Parameter(Mandatory = $false)]
    [string]$BaseUrl = "https://pimpant.club/pikon-raditsz",
    
    [Parameter(Mandatory = $false)]
    [string]$OutputFile = "manifest.json",
    
    [Parameter(Mandatory = $false)]
    [string[]]$ExcludeFolders = @(),
    
    [Parameter(Mandatory = $false)]
    [string[]]$ExcludePatterns = @()
)

$ErrorActionPreference = "Stop"

# ============================================
# STEP 1: Auto-detect version from VersionInfo.h
# ============================================
$versionInfoPath = Join-Path $PSScriptRoot "..\juce\Source\utils\VersionInfo.h"
if (-not $Version -and (Test-Path $versionInfoPath)) {
    $content = Get-Content $versionInfoPath -Raw
    if ($content -match 'VERSION_FULL\s*=\s*"([^"]+)"') {
        $Version = $Matches[1]
        Write-Host "Auto-detected version: $Version" -ForegroundColor Cyan
    }
}

if (-not $Version) {
    Write-Error "Version not specified and could not be detected from VersionInfo.h"
    exit 1
}

# Parse version components
if ($Version -match '^(\d+)\.(\d+)\.(\d+)') {
    $VersionMajor = [int]$Matches[1]
    $VersionMinor = [int]$Matches[2]
    $VersionPatch = [int]$Matches[3]
}

Write-Host "  Major: $VersionMajor" -ForegroundColor Gray
Write-Host "  Minor: $VersionMinor" -ForegroundColor Gray
Write-Host "  Patch: $VersionPatch" -ForegroundColor Gray
Write-Host ""

# ============================================
# STEP 2: Validate build directory
# ============================================
if (-not (Test-Path $BuildDir)) {
    Write-Host "ERROR: Build directory not found: $BuildDir" -ForegroundColor Red
    exit 1
}

Write-Host "Build Directory: $BuildDir" -ForegroundColor Green
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "Variant: $Variant" -ForegroundColor Green
Write-Host ""

# ============================================
# STEP 3: Define file processing functions
# ============================================

# Critical files that require restart (executables and DLLs)
$criticalExtensions = @(".exe", ".dll")

# Function to calculate SHA256 hash
function Get-FileSHA256 {
    param([string]$FilePath)
    $hash = Get-FileHash -Path $FilePath -Algorithm SHA256
    return $hash.Hash.ToLower()
}

# Function to determine if file is critical
function Test-IsCritical {
    param([string]$FilePath)
    $ext = [System.IO.Path]::GetExtension($FilePath).ToLower()
    return $criticalExtensions -contains $ext
}

# Function to check if file should be excluded
function Test-ShouldExclude {
    param(
        [string]$RelativePath,
        [string[]]$ExcludeFolders,
        [string[]]$ExcludePatterns
    )
    
    # Check folder exclusions
    foreach ($folder in $ExcludeFolders) {
        if ($RelativePath -like "$folder\*" -or $RelativePath -like "$folder/*") {
            return $true
        }
    }
    
    # Check pattern exclusions
    foreach ($pattern in $ExcludePatterns) {
        if ($RelativePath -like $pattern) {
            return $true
        }
    }
    
    return $false
}

# ============================================
# STEP 4: Scan and process files
# ============================================

Write-Host "Scanning files..." -ForegroundColor Yellow
# Use -Force to find hidden files just in case
$allFiles = Get-ChildItem -Path $BuildDir -Recurse -File -Force
Write-Host "Found $($allFiles.Count) total files" -ForegroundColor Gray

# Apply exclusions
if ($ExcludeFolders.Count -gt 0 -or $ExcludePatterns.Count -gt 0) {
    Write-Host ""
    Write-Host "Applying exclusions:" -ForegroundColor Yellow
    if ($ExcludeFolders.Count -gt 0) {
        Write-Host "  Excluded folders: $($ExcludeFolders -join ', ')" -ForegroundColor Gray
    }
    if ($ExcludePatterns.Count -gt 0) {
        Write-Host "  Excluded patterns: $($ExcludePatterns -join ', ')" -ForegroundColor Gray
    }
}

$files = @()
$excludedCount = 0
foreach ($file in $allFiles) {
    $relativePath = $file.FullName.Substring($BuildDir.Length + 1)
    if (Test-ShouldExclude -RelativePath $relativePath -ExcludeFolders $ExcludeFolders -ExcludePatterns $ExcludePatterns) {
        $excludedCount++
    }
    else {
        $files += $file
    }
}

$fileCount = $files.Count
Write-Host ""
Write-Host "After exclusions: $fileCount files (excluded: $excludedCount)" -ForegroundColor Green
Write-Host ""

# Build file manifest
$fileManifest = @{}
$totalSize = 0
$criticalCount = 0
$nonCriticalCount = 0

Write-Host "Processing files..." -ForegroundColor Yellow
$progress = 0

foreach ($file in $files) {
    $progress++
    $percentComplete = [math]::Round(($progress / $fileCount) * 100, 1)
    
    # Get relative path from build directory
    $relativePath = $file.FullName.Substring($BuildDir.Length + 1)
    
    # Calculate hash
    Write-Host "[$percentComplete%] $relativePath" -ForegroundColor Gray
    $hash = Get-FileSHA256 -FilePath $file.FullName
    $size = $file.Length
    $isCritical = Test-IsCritical -FilePath $file.Name
    
    if ($isCritical) { $criticalCount++ } else { $nonCriticalCount++ }
    $totalSize += $size
    
    # Add to manifest
    $fileManifest[$relativePath] = @{
        version  = $Version
        size     = $size
        sha256   = $hash
        critical = $isCritical
    }
}

Write-Host ""
Write-Host "File processing complete!" -ForegroundColor Green
Write-Host "  - Critical files (exe/dll): $criticalCount" -ForegroundColor Gray
Write-Host "  - Non-critical files: $nonCriticalCount" -ForegroundColor Gray
Write-Host "  - Total size: $([math]::Round($totalSize / 1MB, 2)) MB" -ForegroundColor Gray
Write-Host ""

# ============================================
# STEP 5: Generate JSON
# ============================================

Write-Host "Generating JSON..." -ForegroundColor Yellow

$releaseDate = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

$manifest = @{
    latestVersion  = $Version
    minimumVersion = "0.6.0" # Hardcoded for now, could be dynamic
    releaseDate    = $releaseDate
    appName        = "Pikon Raditsz"
    updateUrl      = $BaseUrl
    changelog      = @{
        summary = "Version $Version release"
        url     = "$BaseUrl/../changelog.html#v$Version"
    }
    variants       = @{
        $Variant = @{
            displayName = if ($Variant -eq "cuda") { "CUDA-Enabled (Full Features)" } else { "Standard (Lightweight)" }
            files       = $fileManifest
        }
    }
}

$json = $manifest | ConvertTo-Json -Depth 10
$json | Set-Content -Path $OutputFile -Encoding UTF8

Write-Host "Manifest saved to: $OutputFile" -ForegroundColor Green
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Manifest Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Version: $Version (from VersionInfo.h)"
Write-Host "Variant: $Variant"
Write-Host "Files: $fileCount"
Write-Host "Total Size: $([math]::Round($totalSize / 1MB, 2)) MB"
Write-Host "Critical Files: $criticalCount"
Write-Host "Non-Critical Files: $nonCriticalCount"
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Review the generated $OutputFile" -ForegroundColor Gray
Write-Host "  2. Update changelog summary if needed" -ForegroundColor Gray
Write-Host "  3. Run deploy_update.ps1 to upload to OVH" -ForegroundColor Gray
Write-Host ""
