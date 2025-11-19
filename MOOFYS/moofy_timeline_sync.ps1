#=============================================================
#       MOOFY TIMELINE SYNC: Feature Implementation Context
#       This script archives the ESSENTIAL files needed
#       to understand and implement the Timeline Sync feature
#       for TempoClockModuleProcessor.
#       Perfect for external experts reviewing the architecture.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_TimelineSync.txt"

#--- Essential Files for Timeline Sync Feature ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: FEATURE PLAN & SPECIFICATION
    # The complete implementation plan with architecture, risks, and phases
    #=============================================================
    "PLAN/timeline_sync_feature.md",
    
    #=============================================================
    # SECTION 2: CORE ARCHITECTURE - MODULE SYSTEM
    # Understanding how modules work and communicate
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 3: TRANSPORT & TIMING SYSTEM
    # How global transport state works and is broadcast
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    
    #=============================================================
    # SECTION 4: TEMPO CLOCK MODULE (TARGET)
    # The module that will consume timeline sync
    #=============================================================
    "juce/Source/audio/modules/TempoClockModuleProcessor.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 5: TIMELINE SOURCE MODULES
    # Modules that will report their timeline state
    #=============================================================
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.h",
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp",
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",
    
    #=============================================================
    # SECTION 6: EXISTING PRIORITY SYSTEM EXAMPLES
    # How BPM priority currently works (CV > Host > Manual)
    #=============================================================
    "juce/Source/audio/modules/BPM_CONTROL_FLICKER_FIX.md",
    
    #=============================================================
    # SECTION 7: THREAD SAFETY PATTERNS
    # Examples of thread-safe communication in the codebase
    #=============================================================
    "juce/Source/audio/modules/MIDIPlayerModuleProcessor.h",
    "juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 8: UI INTEGRATION
    # How modules draw parameters in the node editor
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 9: STATE PERSISTENCE
    # How module state is saved/loaded in presets
    #=============================================================
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    
    #=============================================================
    # SECTION 10: RELATED GUIDES
    # Documentation on similar features and patterns
    #=============================================================
    "guides/TEMPO_CLOCK_INTEGRATION_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Timeline Sync script..." -ForegroundColor Cyan
Write-Host "Archiving essential files for Timeline Sync feature to: $outputFile"

# Add header with context
$header = @"
=============================================================
MOOFY TIMELINE SYNC FEATURE - IMPLEMENTATION CONTEXT
=============================================================

This archive contains all essential files needed to understand
and implement the Timeline Sync feature for TempoClockModuleProcessor.

FEATURE OVERVIEW:
- Allows TempoClock to sync global transport to SampleLoader/VideoLoader timelines
- Multiple timeline sources with dropdown selection
- Thread-safe registry system for timeline source reporting
- Priority system: Timeline Sync > BPM CV > Host Sync > Manual

KEY IMPLEMENTATION AREAS:
1. Timeline Source Registry in ModularSynthProcessor
2. Module reporting interface (canProvideTimeline, reportTimelineState)
3. TempoClock parameter additions (sync toggle, source selection)
4. UI dropdown for source selection
5. Transport position synchronization logic
6. Thread safety (audio thread updates, UI thread reads)

CRITICAL CONCERNS:
- Thread safety (audio thread updates vs UI thread reads)
- Position synchronization accuracy
- Module lifecycle management (deletion while syncing)
- Priority system integration with existing BPM sources

=============================================================

"@

Add-Content -Path $outputFile -Value $header

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile
    
    # Detect section headers in comments
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue  # Skip comment lines
    }
    
    if (Test-Path $fullPath) {
        $fileHeader = "`n" + ("=" * 80) + "`n"
        $fileHeader += "FILE: $normalizedFile`n"
        $fileHeader += ("=" * 80) + "`n`n"
        
        Add-Content -Path $outputFile -Value $fileHeader
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

Write-Host "`nMoofy Timeline Sync archive complete!" -ForegroundColor Cyan
Write-Host "Output file: $outputFile" -ForegroundColor Green

