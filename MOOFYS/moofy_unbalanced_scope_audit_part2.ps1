#=============================================================
#    MOOFY: Unbalanced ImGui Scope Audit (Part 2)
#    Packages additional suspect module UIs for scope auditing.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_unbalanced_scope_audit_part2.txt"

#--- Context Recap ---
$overview = @'
================================================================================
FOLLOW-UP TARGETS
================================================================================
The initial audit cleared PolyVCO, Meta Module, and MIDI Faders. The next
high-risk modules (called out in IMGUI_NODE_DESIGN_GUIDE and undo docs) are:
  • VCO Module – custom PlotLines UI + collapsible sections
  • MIDI Jog Wheel – advanced ImDrawList rendering & table widgets
  • Comment Module – resizable text UI that manipulates ImGui state
These files may still contain unmatched Begin/End or Push/Pop calls.
'@

#--- Files to Archive ---
$files = @(
    "juce/Source/audio/modules/VCOModuleProcessor.h",
    "juce/Source/audio/modules/VCOModuleProcessor.cpp",
    "juce/Source/audio/modules/MIDIJogWheelModuleProcessor.h",
    "juce/Source/audio/modules/MIDIJogWheelModuleProcessor.cpp",
    "juce/Source/audio/modules/CommentModuleProcessor.h",
    "juce/Source/audio/modules/CommentModuleProcessor.cpp"
)

#--- Guidance ---
$questions = @'
================================================================================
AUDIT CHECKLIST
================================================================================
1. Ensure every `ImGui::Begin*`/`ImGui::End*`, `ImGui::Push*`/`ImGui::Pop*`,
   `ImNodes::Begin*`/`ImNodes::End*`, and `ImGui::BeginGroup`/`EndGroup` pair
   is perfectly matched across success and early-exit paths.
2. Watch for hidden early returns (e.g., when a module has zero channels or
   a feature flag disables a section) that might skip cleanup.
3. For custom drawing (PlotLines, ImDrawList, drag handles), confirm that
   every helper preserving ImGui state restores it afterwards.
4. Verify no module leaves the cursor in an indented state (`ImGui::Indent`
   without `Unindent`).
5. If anything looks suspect, propose RAII wrappers or guard utilities that
   match the patterns in IMGUI_NODE_DESIGN_GUIDE.
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

Write-Host "Moofy scope-audit Part 2 package built at $outputFile" -ForegroundColor Cyan
