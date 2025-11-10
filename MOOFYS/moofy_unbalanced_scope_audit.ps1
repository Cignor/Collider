#=============================================================
#    MOOFY: Unbalanced ImGui Scope Audit
#    Goal: Package every suspected module UI implementation
#          so an external helper can locate missing End*/Pop*
#          calls that crash the node editor.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_unbalanced_scope_audit.txt"

#--- Situation Overview ---
$overview = @'
================================================================================
CRASH CONTEXT
================================================================================
• Regression: Dragging nodes intermittently crashes due to an ImGui assertion:
      "ImGui::End: Mismatched Begin/End calls" (unbalanced scope).
• Analysis points to module UIs that open ImGui tables/groups without closing
  them (missing EndTable/EndGroup/PopID/etc.).
• Highest-risk modules (from IMGUI_NODE_DESIGN_GUIDE notes & recent edits):
    1. PolyVCO – multiple historical fixes around collapsing headers and tables.
    2. Meta Module – brand-new UI; still being stabilized.
    3. MIDI Faders – complex table layout with learn buttons.
• Need to inspect both `drawParametersInNode()` and `drawIoPins()` for each.
• Also grab the main render loop (`ImGuiNodeEditorComponent`) to see how
  modules are invoked and wrapped with stack guards.
'@

#--- Files to Archive ---
$files = @(
    "juce/Source/audio/modules/PolyVCOModuleProcessor.h",
    "juce/Source/audio/modules/PolyVCOModuleProcessor.cpp",
    "juce/Source/audio/modules/MetaModuleProcessor.h",
    "juce/Source/audio/modules/MetaModuleProcessor.cpp",
    "juce/Source/audio/modules/MIDIFadersModuleProcessor.h",
    "juce/Source/audio/modules/MIDIFadersModuleProcessor.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "guides/IMGUI_NODE_DESIGN_GUIDE.md"  # contains prior UI bug notes referenced in analysis
)

#--- Questions / Guidance for Helper ---
$questions = @'
================================================================================
WHAT WE NEED FROM YOU
================================================================================
1. Audit each module's `drawParametersInNode()` and `drawIoPins()` for:
      • Missing `ImGui::End*`, `ImGui::Pop*`, `ImNodes::End*`, or `ImGui::EndGroup()`
      • Early returns that skip cleanup
      • ImGui table usage (ensure `BeginTable` ⇒ `EndTable` in every path)
2. Verify `MetaModuleProcessor`'s UI path: does any popup or flag leave the
   stack unbalanced when `editRequested` toggles?
3. Double-check `MIDIFadersModuleProcessor` table view: ensure the custom
   table flags and `PushID`/`PopID` calls are matched even when there are
   zero faders or when learn mode exits early.
4. Confirm `PolyVCOModuleProcessor`'s grouped pin drawing uses balanced
   `BeginGroup`/`EndGroup`, especially inside loops.
5. Ensure the main Render loop (`ImGuiNodeEditorComponent`) isn’t skipping
   `parameterStackGuard.validate()` due to exceptions or early breaks.

Please annotate any suspicious sections and suggest fixes (e.g., RAII helpers,
scope guards). Call out if more files are needed.
'@

#--- Execute Packaging ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Add-Content -Path $outputFile -Value $overview

foreach ($file in $files) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    if (Test-Path $fullPath) {
        $header = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
    } else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
    }
}

Add-Content -Path $outputFile -Value $questions

Write-Host "Moofy scope-audit package built at $outputFile" -ForegroundColor Cyan
