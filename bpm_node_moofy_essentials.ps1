#=============================================================
#       BPM NODE ESSENTIALS: Context for External Expert Review
#       Focused only on BPM Node implementation and related systems
#=============================================================

$projectRoot = $PSScriptRoot
$outputFile = Join-Path $projectRoot "bpm_node_Essentials.txt"

#--- BPM Node Specific Files ---
$sourceFiles = @(
    # BPM Monitor Node Implementation
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.h",
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.cpp",
    
    # Beat Detection Engine
    "juce/Source/audio/modules/TapTempo.h",
    "juce/Source/audio/modules/TapTempo.cpp",
    
    # Rhythm Reporting System Interface
    "juce/Source/audio/modules/ModuleProcessor.h",
    
    # Example Rhythm Reporting Implementations
    "juce/Source/audio/modules/StepSequencerModuleProcessor.h",
    "juce/Source/audio/modules/StepSequencerModuleProcessor.cpp",
    "juce/Source/audio/modules/AnimationModuleProcessor.h",
    "juce/Source/audio/modules/AnimationModuleProcessor.cpp",
    
    # BPM Node Registration & Graph Integration
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting BPM Node Essentials script..." -ForegroundColor Cyan
Write-Host "Archiving BPM-specific files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile
    
    if (Test-Path $fullPath) {
        $header = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"
        
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        
        Write-Host " -> Archived: $normalizedFile" -ForegroundColor Green
    } else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: File not found: $normalizedFile" -ForegroundColor Yellow
    }
}

Write-Host "`nBPM Node Essentials archive complete!" -ForegroundColor Cyan

