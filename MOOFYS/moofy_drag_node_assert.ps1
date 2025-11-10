#=============================================================
#   MOOFY: Drag Insert Assertion Crash Context Bundle
#   Collects all files relevant to the ImNodes scope assertion
#   for external helpers.
#=============================================================

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_DragInsertAssertion.txt"

$sourceFiles = @(
    #=== Core context and planning ===
    "MOOFYS/moofys.md",
    "MOOFYS/moofys_drag_assertion.md",
    ".cursor/commands/communicate.md",

    #=== Source files under investigation ===
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/audio/modules/VCOModuleProcessor.h",
    "juce/Source/audio/modules/VCOModuleProcessor.cpp",
    "juce/Source/audio/modules/MIDIJogWheelModuleProcessor.h",
    "juce/Source/audio/modules/MIDIJogWheelModuleProcessor.cpp",
    "juce/Source/audio/modules/CommentModuleProcessor.h",
    "juce/Source/audio/modules/CommentModuleProcessor.cpp",

    #=== Additional helpers recently touched ===
    "MOOFYS/moofy_unbalanced_scope_audit.ps1",
    "MOOFYS/moofy_shortcut_manager.ps1",

    #=== Guides & specs to understand UI conventions ===
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/SHORTCUT_SYSTEM_SCAN.md"
)

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Add-Content -Path $outputFile -Value ("="*80 + "`nDRAG INSERT ASSERTION CONTEXT`n" + "="*80 + "`n")

foreach ($file in $sourceFiles) {
    if ([string]::IsNullOrWhiteSpace($file) -or $file.Trim().StartsWith("#")) { continue }

    $normalized = $file.Replace('/', '\')
    $fullPath   = Join-Path $projectRoot $normalized

    if (Test-Path $fullPath) {
        Add-Content -Path $outputFile -Value ("`n" + ("="*80) + "`nFILE: $normalized`n" + ("="*80) + "`n`n")
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
    } else {
        Add-Content -Path $outputFile -Value ("`n" + ("="*80) + "`nFILE NOT FOUND: $normalized`n" + ("="*80) + "`n")
    }
}

Write-Host "Drag insert assertion context exported to $outputFile" -ForegroundColor Cyan

