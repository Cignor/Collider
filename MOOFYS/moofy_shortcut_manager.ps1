#=============================================================
#       MOOFY SHORTCUT MANAGER: Input System Modernization
#       This script bundles all files & guides needed to design
#       a centralized shortcut manager for the preset creator.
#       Share this with external helpers for full context.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_ShortcutManager.txt"

#--- Shortcut Manager Knowledge Pack ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: OVERVIEW & CURRENT STATE ANALYSIS
    #=============================================================
    "guides/SHORTCUT_SYSTEM_SCAN.md",
    "guides/DRAG_INSERT_NODE_GUIDE.md",
    "guides/TIMELINE_NODE_REMEDIATION_GUIDE.md",

    #=============================================================
    # SECTION 2: CURRENT SHORTCUT IMPLEMENTATION (NODE EDITOR)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",

    #=============================================================
    # SECTION 3: ACTION HANDLERS & DEPENDENCIES
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/SampleManager.h",
    "juce/Source/preset_creator/SampleManager.cpp",

    #=============================================================
    # SECTION 4: AUDIO ENGINE INTEGRATION REFERENCE
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",

    #=============================================================
    # SECTION 5: PERSISTENCE & UNDO/REDO TOOLING
    #=============================================================
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "guides/UNDO_REDO_SYSTEM_GUIDE.md",

    #=============================================================
    # SECTION 6: CONFIGURATION & ASSET REFERENCES
    #=============================================================
    "juce/assets/module_aliases.json"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Shortcut Manager script..." -ForegroundColor Cyan
Write-Host "Aggregating shortcut manager context to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

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
