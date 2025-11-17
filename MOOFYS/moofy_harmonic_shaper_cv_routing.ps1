#=============================================================
#       MOOFY: Harmonic Shaper CV Routing Issue
#       This script archives files needed to understand
#       the JUCE buffer aliasing and CV modulation routing problem
#       in HarmonicShaperModuleProcessor.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_HarmonicShaper_CV_Routing.txt"

#--- Essential Files for This Issue ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: THE PROBLEM - Current Broken Implementation
    #=============================================================
    "juce/Source/audio/modules/HarmonicShaperModuleProcessor.h",
    "juce/Source/audio/modules/HarmonicShaperModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 2: WORKING REFERENCE - Granulator (Same Pattern, Works!)
    #=============================================================
    "juce/Source/audio/modules/GranulatorModuleProcessor.h",
    "juce/Source/audio/modules/GranulatorModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 3: CRITICAL DEBUG GUIDE - Buffer Aliasing
    #=============================================================
    "guides/DEBUG_INPUT_IMPORTANT.md",
    
    #=============================================================
    # SECTION 4: BEST PRACTICES - Modulation Input Pattern
    #=============================================================
    "juce/Source/audio/modules/BestPracticeNodeProcessor.md",
    
    #=============================================================
    # SECTION 5: BASE MODULE ARCHITECTURE
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 6: ROUTING SYSTEM - How CV Signals Are Routed
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 7: OTHER WORKING EXAMPLES - For Comparison
    #=============================================================
    "juce/Source/audio/modules/BitCrusherModuleProcessor.h",
    "juce/Source/audio/modules/BitCrusherModuleProcessor.cpp"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy: Harmonic Shaper CV Routing Issue..." -ForegroundColor Cyan
Write-Host "Archiving essential files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile
    
    # Detect section headers in comments
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue  # Skip comment lines
    }
    
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

Write-Host "`nMoofy complete! Output: $outputFile" -ForegroundColor Cyan

