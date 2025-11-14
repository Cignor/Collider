#=============================================================
#       MOOFY BIT CRUSHER: Module Implementation Guide
#       This script archives the essential files needed
#       to implement a new Bit Crusher audio effect module.
#       Perfect for AI assistants or developers implementing
#       the bit crusher node with CV modulation support.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_BitCrusher.txt"

#--- Essential Files for Bit Crusher Implementation ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: COMPREHENSIVE NODE ADDITION GUIDE
    # The definitive guide for adding new modules
    #=============================================================
    "guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md",
    
    #=============================================================
    # SECTION 2: BASE MODULE ARCHITECTURE
    # The foundation all nodes inherit from
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 3: REFERENCE MODULES - STUDY THESE!
    # Drive and Waveshaper are perfect references for effects
    #=============================================================
    
    # Reference 1: Drive Module (Simple Effect with CV Modulation)
    # - Shows basic audio effect pattern
    # - Demonstrates dry/wet mix implementation
    # - Shows parameter layout and UI drawing
    "juce/Source/audio/modules/DriveModuleProcessor.h",
    "juce/Source/audio/modules/DriveModuleProcessor.cpp",
    
    # Reference 2: Waveshaper Module (Advanced Effect with CV Modulation)
    # - Shows per-sample CV modulation (CRITICAL for bit crusher!)
    # - Demonstrates relative/absolute modulation modes
    # - Shows multiple quantization/algorithm modes
    "juce/Source/audio/modules/WaveshaperModuleProcessor.h",
    "juce/Source/audio/modules/WaveshaperModuleProcessor.cpp",
    
    # Reference 3: VCF Module (Complex Processing with State)
    # - Shows stateful audio processing
    # - Demonstrates filter implementation patterns
    "juce/Source/audio/modules/VCFModuleProcessor.h",
    "juce/Source/audio/modules/VCFModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 4: MODULE FACTORY REGISTRATION
    # Where new modules are registered in the system
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    
    #=============================================================
    # SECTION 5: PIN DATABASE & MODULE DESCRIPTIONS
    # Where I/O pins and tooltips are defined
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    
    #=============================================================
    # SECTION 6: UI INTEGRATION - MENU SYSTEMS
    # Where modules appear in all menu systems
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 7: THEME SYSTEM (Optional but Recommended)
    # For consistent UI styling
    #=============================================================
    "juce/Source/preset_creator/theme/ThemeManager.h",
    "juce/Source/preset_creator/theme/ThemeManager.cpp",
    
    #=============================================================
    # SECTION 8: EXAMPLE - SIMPLE UTILITY MODULE
    # For understanding basic module structure
    #=============================================================
    "juce/Source/audio/modules/ValueModuleProcessor.h",
    "juce/Source/audio/modules/ValueModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 9: NODE DICTIONARY (Documentation Reference)
    # To understand how modules are documented
    #=============================================================
    "USER_MANUAL/Nodes_Dictionary.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Bit Crusher script..." -ForegroundColor Cyan
Write-Host "Archiving essential files for Bit Crusher implementation to: $outputFile"

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

Write-Host "`nMoofy Bit Crusher script completed!" -ForegroundColor Cyan
Write-Host "Output file: $outputFile" -ForegroundColor Green

