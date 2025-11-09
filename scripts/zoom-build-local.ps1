# Canvas Zoom (Deprecated)
# This script previously forced CMake to use the local study directory for the
# experimental zoom fork. Zoom work has been shelved, so we simply invoke the
# normal build with upstream imnodes.

param(
    [switch]$Clean,
    [switch]$Run
)

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "  Canvas Zoom - (Disabled)" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Zoom experimentation has been disabled. Building with upstream imnodes..." -ForegroundColor Yellow

$projectDir = "H:\0000_CODE\01_collider_pyo"
$buildDir = "$projectDir\juce\build"

# Navigate to project
Set-Location $projectDir

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning imnodes cache..." -ForegroundColor Yellow
    if (Test-Path "$buildDir\_deps\imnodes_fc-*") {
        Remove-Item -Recurse -Force "$buildDir\_deps\imnodes_fc-*" -ErrorAction SilentlyContinue
        Write-Host "  Deleted imnodes cache" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Configuring CMake (upstream dependencies)..." -ForegroundColor Yellow

$cmakeArgs = @(
    "-B", "juce\build",
    "-S", "juce",
    "-DCMAKE_BUILD_TYPE=Debug"
)
& cmake $cmakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Building PresetCreatorApp..." -ForegroundColor Yellow
cmake --build juce\build --config Debug --target PresetCreatorApp

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "=====================================================" -ForegroundColor Red
    Write-Host "  Build FAILED!" -ForegroundColor Red
    Write-Host "=====================================================" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Green
Write-Host "  Build SUCCESSFUL!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Green

# Run if requested
if ($Run) {
    $exePath = "$buildDir\PresetCreatorApp_artefacts\Debug\Preset Creator.exe"
    
    if (Test-Path $exePath) {
        Write-Host ""
        Write-Host "Launching Preset Creator..." -ForegroundColor Yellow
        Start-Process $exePath
        
        Write-Host ""
        Write-Host "=====================================================" -ForegroundColor Cyan
        Write-Host "  Test Phase 1 (Foundation)" -ForegroundColor Cyan
        Write-Host "=====================================================" -ForegroundColor Cyan
        Write-Host "[ ] App launches without crashing" -ForegroundColor White
        Write-Host "[ ] Can create nodes" -ForegroundColor White
        Write-Host "[ ] Can drag nodes" -ForegroundColor White
        Write-Host "[ ] Can create links" -ForegroundColor White
        Write-Host "[ ] No assertions or errors" -ForegroundColor White
        Write-Host ""
    } else {
        Write-Host ""
        Write-Host "Executable not found at: $exePath" -ForegroundColor Red
    }
}

Write-Host ""
