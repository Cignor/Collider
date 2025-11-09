#=============================================================
#   MOOFY: Drag-Based Node Suggestion Context Bundle
#   Aggregates every critical file for external collaborators
#   working on the cable-drag suggestion workflow.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_DragNodeSuggestions.txt"

#--- Context Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PROGRAM BRIEFING
    #=============================================================
    "MOOFYS/moofys.md",                        # High-level mission summary & open questions

    #=============================================================
    # SECTION 2: NODE EDITOR IMPLEMENTATION
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 3: PIN & MODULE METADATA
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",

    #=============================================================
    # SECTION 4: GRAPH OPERATIONS
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",

    #=============================================================
    # SECTION 5: REFERENCE GUIDES
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Drag Node Suggestion Moofy..." -ForegroundColor Cyan
Write-Host "Compiling context bundle -> $outputFile"

foreach ($file in $sourceFiles) {
    if ([string]::IsNullOrWhiteSpace($file) -or $file.Trim().StartsWith("#")) {
        continue
    }

    $normalizedFile = $file.Replace('/', '\')
    $fullPath       = Join-Path $projectRoot $normalizedFile

    if (Test-Path $fullPath) {
        $header  = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile

        Write-Host " -> Added: $normalizedFile" -ForegroundColor Green
    }
    else {
        $warning  = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: Missing $normalizedFile" -ForegroundColor Yellow
    }
}

Write-Host "Done. Share moofy_DragNodeSuggestions.txt with the expert." -ForegroundColor Cyan

