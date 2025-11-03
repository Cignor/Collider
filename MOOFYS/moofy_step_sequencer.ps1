#=============================================================
#       MOOFY: Step Sequencer Focused Context Bundle
#       Purpose: Provide all essential context for an external
#       helper to understand, modify, or debug the Step Sequencer
#       module and its integration points.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_StepSequencer_Context.txt"

#--- Focused Context Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PRIMARY MODULE - STEP SEQUENCER
    # Core implementation and its base class
    #=============================================================
    "juce/Source/audio/modules/StepSequencerModuleProcessor.h",
    "juce/Source/audio/modules/StepSequencerModuleProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",

    #=============================================================
    # SECTION 2: TIMING, TRANSPORT, AND CLOCK INTEGRATION
    # How sequencing syncs to transport and clock
    #=============================================================
    "juce/Source/audio/modules/TimingData.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.cpp",

    #=============================================================
    # SECTION 3: RHYTHM REPORTING / MONITORING INTEGRATION
    # How Step Sequencer exposes rhythm info and who consumes it
    #=============================================================
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.h",
    "juce/Source/audio/modules/BPMMonitorModuleProcessor.cpp",

    #=============================================================
    # SECTION 4: AUDIO GRAPH REGISTRATION & WIRING
    # Where the module gets constructed/registered in the graph
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/AudioEngine.h",
    "juce/Source/audio/AudioEngine.cpp",

    #=============================================================
    # SECTION 5: NODE EDITOR UI & PIN SYSTEM
    # Pins, parameter drawing, and node UI integration
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",

    #=============================================================
    # SECTION 6: PRESET / STATE MANAGEMENT
    # Save/load including extra state tree handling
    #=============================================================
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/SavePresetJob.cpp",

    #=============================================================
    # SECTION 7: KEY REFERENCE GUIDES
    # Reading these provides system-level context
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "guides/TEMPO_CLOCK_INTEGRATION_GUIDE.md",
    "guides/PIN_DATABASE_SYSTEM_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy: Step Sequencer context export..." -ForegroundColor Cyan
Write-Host "Archiving focused files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    # Skip comment markers accidentally added to list
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue
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


