# Debug script to find why Pikon Raditsz.exe is missing
param(
    [string]$BuildDir = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release"
)

$exeName = "Pikon Raditsz.exe"
$fullPath = Join-Path $BuildDir $exeName

Write-Host "Checking for: $fullPath"

if (Test-Path $fullPath) {
    Write-Host "File EXISTS on disk." -ForegroundColor Green
}
else {
    Write-Host "File NOT FOUND on disk." -ForegroundColor Red
    exit
}

# Simulate the exclusion logic
$excludeFolders = @("VST", "models", "Samples", "video", "record", "TTSPERFORMER", "juce\logs", "logs", "USER_MANUAL")
$excludePatterns = @("*.log", "*.tmp", "*.pdb", "*.ilk", "*.exp", "*.lib", "*\logs\*")

$fileItem = Get-Item $fullPath
$relativePath = $fileItem.FullName.Substring($BuildDir.Length + 1)

Write-Host "Relative Path: '$relativePath'"

# Check folder exclusions
foreach ($folder in $excludeFolders) {
    if ($relativePath -like "$folder\*" -or $relativePath -like "$folder/*") {
        Write-Host "EXCLUDED by folder: $folder" -ForegroundColor Red
    }
}

# Check pattern exclusions
foreach ($pattern in $excludePatterns) {
    if ($relativePath -like $pattern) {
        Write-Host "EXCLUDED by pattern: $pattern" -ForegroundColor Red
    }
}

Write-Host "Done checking."
