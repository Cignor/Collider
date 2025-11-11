#=============================================================
#       MOOFY SHORTCUT MANAGER OVERHAUL: Action Registry Pack
#       Generates a context bundle for finishing the new
#       data-driven shortcut system in Collider's preset editor.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_ShortcutManagerOverhaul.txt"

#--- Curated Source List ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: REQUIREMENTS & CURRENT LIMITATIONS
    #=============================================================
    "guides/SHORTCUT_SYSTEM_SCAN.md",
    "ninja_sccache_BUILD_FIX_IMPLEMENTATION_SUMMARY.md",
    "guides/ninja-build-release-or-debug.md",

    #=============================================================
    # SECTION 2: MANAGER CORE & DATA STRUCTURES
    #=============================================================
    "juce/Source/preset_creator/ShortcutManager.h",
    "juce/Source/preset_creator/ShortcutManager.cpp",

    #=============================================================
    # SECTION 3: LEGACY KEY HANDLING TO REFACTOR
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 4: ACTION CALLBACK TARGETS
    #=============================================================
    "juce/Source/preset_creator/SavePresetJob.h",
    "juce/Source/preset_creator/SavePresetJob.cpp",
    "juce/Source/preset_creator/NotificationManager.h",
    "juce/Source/preset_creator/NotificationManager.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",

    #=============================================================
    # SECTION 5: STATE MANAGEMENT & PERSISTENCE
    #=============================================================
    "guides/UNDO_REDO_SYSTEM_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",

    #=============================================================
    # SECTION 6: BUILD & CONFIGURATION TOUCHPOINTS
    #=============================================================
    "juce/CMakeLists.txt",
    ".cursor/commands/debug.md",
    ".cursor/commands/release.md"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Shortcut Manager Overhaul script..." -ForegroundColor Cyan
Write-Host "Aggregating helper bundle at: $outputFile"

foreach ($file in $sourceFiles) {
    if ($file.StartsWith("#")) { continue }

    $normalizedFile = $file.Replace('/', '\\')
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
