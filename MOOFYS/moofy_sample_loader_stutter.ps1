#=============================================================
#   MOOFY: Sample Loader Sync Stutter Investigation Pack
#   Mirrors the Moofy Essentials style but focuses on
#   the files tied to synced transport playback glitches.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_sample_loader_stutter.txt"

#--- Context Files To Archive ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: TRANSPORT BROADCAST (GLOBAL SOURCE OF COMMANDS)
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",

    #=============================================================
    # SECTION 2: SAMPLE LOADER IMPLEMENTATION (PRIMARY BUG SITE)
    #=============================================================
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.h",
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp",

    #=============================================================
    # SECTION 3: RELATED SAMPLE MODULES (REFERENCE BEHAVIOUR)
    #=============================================================
    "juce/Source/audio/modules/SampleSfxModuleProcessor.h",
    "juce/Source/audio/modules/SampleSfxModuleProcessor.cpp",

    #=============================================================
    # SECTION 4: TRANSPORT CONTROL UI
    #=============================================================
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 5: ACTIVE PLANS / NOTES
    #=============================================================
    "PLAN/scan-play-pause-stop.md",
    "PLAN/sample_pause_rollout.md"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Building Sample Loader stutter Moofy..." -ForegroundColor Cyan
Write-Host "Output: $outputFile"

foreach ($file in $sourceFiles) {
    if ($file.StartsWith("#")) { continue }

    $normalized = $file.Replace('/', '\')
    $fullPath   = Join-Path $projectRoot $normalized

    if (Test-Path $fullPath) {
        $header  = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalized`n"
        $header += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        Write-Host " -> Archived: $normalized" -ForegroundColor Green
    } else {
        $warning  = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalized`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: Missing: $normalized" -ForegroundColor Yellow
    }
}

Write-Host "Moofy bundle complete." -ForegroundColor Cyan

