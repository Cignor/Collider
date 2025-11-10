#=============================================================
#    MOOFY: Shortcut Manager Initiative
#    Goal: Give an external helper everything they need to
#          design & implement a Blender-style shortcut manager
#          for Collider's preset creator UI.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_shortcut_manager.txt"

#--- Context Overview ---
$overview = @'
================================================================================
PROJECT SUMMARY
================================================================================
We need a centralized keyboard shortcut manager that can:
  • Enumerate every shortcut in the system (similar to Blender's keymap editor)
  • Let users rebind shortcuts from a UI list and via right-click on parameters
  • Persist user overrides and resolve conflicts cleanly
  • Expose an API so modules / UI widgets can register actions + default bindings

Today, shortcuts are hard-coded inside ImGui render code with no registry.
This Moofy package gathers the relevant files and design references so an
external expert can dive in without local context.
'@

#--- Key Files to Inspect ---
$files = @(
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",  # all shortcuts hard-coded here
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",    # state flags + handlers invoked by shortcuts
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",    # top-level component (focus + dispatch)
    "guides/SHORTCUT_SYSTEM_SCAN.md",                           # fresh scan of current shortcut landscape
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",                        # explains ImGui integration patterns
    "guides/UNDO_REDO_SYSTEM_GUIDE.md",                         # documents snapshot/undo link used in actions
    "guides/PIN_DATABASE_SYSTEM_GUIDE.md",                      # shows how dynamic pins and type metadata work
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md"                   # reference for persisting config ValueTrees
)

#--- Questions for the External Helper ---
$questions = @'
================================================================================
QUESTIONS WE NEED ANSWERED
================================================================================
1. Action Registry
   - How should we structure a registry of actions (IDs, categories, default chords)?
   - Can we leverage an existing pattern (e.g., JUCE ApplicationCommandManager) or
     should we build an ImGui-centric solution?

2. Binding Representation
   - What data structure best represents key chords (modifiers + key + optional mouse)?
   - Do we need support for multi-step sequences or only single chords?

3. Scope & Context
   - Should shortcuts be global, per-editor, or per-widget? How do we disambiguate
     when multiple contexts want the same binding?
   - How does the system interact with ImGui's `WantCaptureKeyboard` flag to avoid
     stealing focus from text inputs?

4. Right-Click Assignment Flow
   - What hooks do we need to add to sliders/buttons so users can right-click and
     assign a shortcut? Should widgets register action IDs upfront?
   - How do we surface conflicts and confirm overrides during this flow?

5. Persistence
   - Where do we store user keymaps (ValueTree in preset state, separate JSON, etc.)?
   - Do we need profile support or per-project bindings?

6. Runtime Dispatch
   - How should the dispatcher integrate with the existing render loop so it can
     trigger the current handlers (`handleRecordOutput`, `handleNodeChaining`, etc.)?
   - Do we support key-repeat or only edge-triggered events?

7. Migration Plan
   - Strategy to migrate the existing hard-coded checks to the new system without
     breaking behaviour. Can we incrementally move actions?

Please provide architectural recommendations, data structures, and an initial API
sketch we can implement. Call out any missing context you need.
'@

#--- Script Execution ---
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

Write-Host "Moofy Shortcut Manager package built at $outputFile" -ForegroundColor Cyan
