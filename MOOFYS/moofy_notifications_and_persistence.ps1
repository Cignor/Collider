#=============================================================
#   MOOFY NOTIFICATIONS & PERSISTENCE: Architecture Extractor
#   This script archives the key files needed to understand the
#   Notification system and XML Saving/Loading (Preset) system.
#   Share the generated .txt with external experts for onboarding.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Notifications_And_Persistence.txt"

#--- Focused Architecture Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: INTEGRATION GUIDES (READ FIRST)
    #=============================================================
    "guides/NOTIFICATION_SYSTEM_INTEGRATION_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "USER_MANUAL/Nodes_Dictionary.md",

    #=============================================================
    # SECTION 2: NOTIFICATION SYSTEM CORE
    #=============================================================
    "juce/Source/preset_creator/NotificationManager.h",
    "juce/Source/preset_creator/NotificationManager.cpp",

    # Related UI integration points that dispatch/handle notifications
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    # Pin and Node metadata database (requested)
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/ui/MainComponent.cpp",
    "juce/Source/ui/TestHarnessComponent.cpp",

    #=============================================================
    # SECTION 3: PRESET / PERSISTENCE (XML Saving & Loading)
    #=============================================================
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/ControllerPresetManager.h",
    "juce/Source/preset_creator/ControllerPresetManager.cpp",
    "juce/Source/preset_creator/SavePresetJob.h",
    "juce/Source/preset_creator/SavePresetJob.cpp",

    # Touch points used by Preset Manager / Save-Load flows
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",
    "juce/Source/preset_creator/SampleManager.h",
    "juce/Source/preset_creator/SampleManager.cpp",

    #=============================================================
    # SECTION 4: AUDIO GRAPH INTEGRATION (STATE/PARAM SNAPSHOTS)
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/graph/VoiceProcessor.h",
    "juce/Source/audio/graph/VoiceProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",

    # Timing/transport and snapshot-related modules that interact with state
    "juce/Source/audio/modules/TempoClockModuleProcessor.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.cpp",
    "juce/Source/audio/modules/SnapshotSequencerModuleProcessor.h",
    "juce/Source/audio/modules/SnapshotSequencerModuleProcessor.cpp",

    #=============================================================
    # SECTION 5: IPC/COMMAND INFRA (if presets/notifications cross boundaries)
    #=============================================================
    "juce/Source/ipc/CommandBus.h",
    "juce/Source/ipc/IpcServer.h",

    #=============================================================
    # SECTION 6: BUILD SYSTEM (for dependencies and paths)
    #=============================================================
    "juce/CMakeLists.txt"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Notifications & Persistence script..." -ForegroundColor Cyan
Write-Host "Archiving focused files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    # Skip comment entries beginning with '#'
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


