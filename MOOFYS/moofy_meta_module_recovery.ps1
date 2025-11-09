#=============================================================
#  Moofy: Meta Module / Inlet / Outlet Recovery Essentials
#=============================================================

$projectRoot = Split-Path -Parent $PSScriptRoot
$moofyDir    = Join-Path $projectRoot "MOOFYS"
$outputFile  = Join-Path $moofyDir "moofy_MetaModule_Recovery.txt"

$sourceFiles = @(
    # Architecture & node-editor context
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.cpp",

    # Meta / Inlet / Outlet implementations
    "juce/Source/audio/modules/MetaModuleProcessor.h",
    "juce/Source/audio/modules/MetaModuleProcessor.cpp",
    "juce/Source/audio/modules/InletModuleProcessor.h",
    "juce/Source/audio/modules/InletModuleProcessor.cpp",
    "juce/Source/audio/modules/OutletModuleProcessor.h",
    "juce/Source/audio/modules/OutletModuleProcessor.cpp",

    # Guides & specs
    "guides/META_MODULE_STATUS_GUIDE.md",
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "guides/UNDO_REDO_SYSTEM_GUIDE.md"
)

if (Test-Path $outputFile) {
    Remove-Item $outputFile -Force
}

Write-Host "Generating Meta Module Recovery Moofy..." -ForegroundColor Cyan

foreach ($file in $sourceFiles) {
    $normalized = $file.Replace('/', '\')
    $fullPath   = Join-Path $projectRoot $normalized

    if (Test-Path $fullPath) {
        $header  = "`n" + ('=' * 80) + "`n"
        $header += "FILE: $normalized`n"
        $header += ('=' * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        Write-Host " -> Archived: $normalized" -ForegroundColor Green
    }
    else {
        $warning  = "`n" + ('=' * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalized`n"
        $warning += ('=' * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: File not found: $normalized" -ForegroundColor Yellow
    }
}

Write-Host "Moofy generation complete -> $outputFile" -ForegroundColor Cyan

