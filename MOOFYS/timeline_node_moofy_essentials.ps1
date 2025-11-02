#=============================================================
#       TIMELINE NODE ESSENTIALS: Architecture & Integration
#       Specialized context for the Timeline Node feature
#       For external experts implementing automation recording
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "timeline_node_Essentials.txt"

#--- Essential Architecture Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PROJECT OVERVIEW
    # Understand the system architecture first
    #=============================================================
    "architecture/00_PROJECT_OVERVIEW.md",
    "architecture/01_CORE_GAME_ENGINE.md",
    "architecture/02_AUDIO_SYSTEM.md",
    
    #=============================================================
    # SECTION 2: TRANSPORT SYSTEM & TEMPO SYNCHRONIZATION
    # CRITICAL: Timeline Node MUST sync with Tempo Clock
    #=============================================================
    "juce/Source/audio/modules/TimingData.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.cpp",
    "guides/TEMPO_CLOCK_INTEGRATION_GUIDE.md",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    
    #=============================================================
    # SECTION 3: BASE MODULE ARCHITECTURE
    # The foundation all nodes inherit from
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    "juce/Source/audio/modules/BestPracticeNodeProcessor.h",
    "juce/Source/audio/modules/BestPracticeNodeProcessor.cpp",
    
    #=============================================================
    # SECTION 4: DYNAMIC INPUTS/OUTPUTS PATTERN
    # Timeline Node needs dynamic I/O for pass-through automation
    #=============================================================
    "juce/Source/audio/modules/ColorTrackerModule.h",
    "juce/Source/audio/modules/ColorTrackerModule.cpp",
    "juce/Source/audio/modules/AnimationModuleProcessor.h",
    "juce/Source/audio/modules/AnimationModuleProcessor.cpp",
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.h",
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 5: MIDI LOGGING & PLAYBACK EXAMPLE
    # Reference for recording and playing back timed events
    #=============================================================
    "juce/Source/audio/modules/MIDIPlayerModuleProcessor.h",
    "juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 6: XML SAVE/LOAD SYSTEM
    # CRITICAL: Timeline Node MUST save/load keyframes as XML
    #=============================================================
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",
    
    #=============================================================
    # SECTION 7: EXAMPLE COMPLEX NODES
    # Study these for transport-aware, time-sensitive processing
    #=============================================================
    "juce/Source/audio/modules/StepSequencerModuleProcessor.h",
    "juce/Source/audio/modules/StepSequencerModuleProcessor.cpp",
    "juce/Source/audio/modules/MultiSequencerModuleProcessor.h",
    "juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 8: ANIMATION SYSTEM (Time-based playback example)
    # Shows how to integrate time-based data with transport
    #=============================================================
    "juce/Source/animation/AnimationData.h",
    "juce/Source/animation/AnimationRenderer.h",
    "juce/Source/animation/AnimationRenderer.cpp",
    "juce/Source/animation/Animator.h",
    "juce/Source/animation/Animator.cpp",
    
    #=============================================================
    # SECTION 9: NODE EDITOR UI & PIN SYSTEM
    # How dynamic pins are displayed and connected
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    
    #=============================================================
    # SECTION 10: COMPLETE NODE IMPLEMENTATION GUIDE
    # Step-by-step guide for creating new nodes
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Timeline Node Essentials script..." -ForegroundColor Cyan
Write-Host "Archiving essential architecture files to: $outputFile"

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

Write-Host "`nTimeline Node Essentials archive complete!" -ForegroundColor Green
Write-Host "Output file: $outputFile"

