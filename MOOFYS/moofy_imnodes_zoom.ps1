#=============================================================
#       MOOFY: ImNodes Zoom Integration Context Pack
#       This script aggregates ONLY the files and notes needed
#       to help an external expert implement robust zooming
#       in our ImGui/ImNodes-based node editor.
#       Reference commit analyzed:
#       https://github.com/Nelarius/imnodes/pull/192/commits/0cda3b818e9bc4ede43b094796ec95792bf5fbb7
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_ImNodes_Zoom.txt"

#--- Context Notes (prepended to output) ---
$contextNotes = @'
================================================================================
TOPIC: ImNodes Zoom Integration (Dual ImGui Context, IO Mirroring, Copy-Back)

Goal:
- Implement smooth zooming for the node editor using a second ImGui context to
  render at scale, mirroring IO to ensure correct hover/selection, and copying
  transformed draw data back into the primary context.

Key APIs expected:
- EditorContextGetZoom()
- EditorContextSetZoom(float zoom_scale, ImVec2 zoom_center)
- IsEditorHovered()

Critical invariants:
- Stroke thickness divided by ZoomScale for visual consistency
- Zoom centered at mouse: adjust panning by (p/zoom_new - p/zoom_old)
- IO mirroring: DisplaySize, DeltaTime, MousePos, MouseWheel, KeyMods, flags
- AppendDrawData: translate + scale positions when copying draw lists back

Primary reference (must-read):
- Add zoom functionality — 0cda3b8
  https://github.com/Nelarius/imnodes/pull/192/commits/0cda3b818e9bc4ede43b094796ec95792bf5fbb7

Testing checklist:
- Hover/select/drag works at fractional zooms
- Link thickness stable across zoom
- Panning unaffected by zoom level
- Overlays/tooltips align with canvas at all zoom levels
================================================================================
'@

#--- Target Files (minimal, high-signal) ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: Guides & Analysis (Read these first)
    #=============================================================
    "guides/IMNODES_ZOOM_INTEGRATION_GUIDE.md",
    "guides/THEME_MANAGER_INTEGRATION_GUIDE.md",

    #=============================================================
    # SECTION 2: Node Editor (Core Rendering & Input)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 3: App Bootstrap (ImGui/ImNodes init & context)
    #=============================================================
    "juce/Source/app/MainApplication.h",
    "juce/Source/app/MainApplication.cpp",

    #=============================================================
    # SECTION 4: Theming (for grid/link/pin thickness & colors)
    #=============================================================
    "guides/THEME_EDITOR_DESIGN.md",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",

    #=============================================================
    # SECTION 5: Upstream imnodes (optional - if fetched locally)
    # Tries common FetchContent and vendor locations; will mark missing.
    #=============================================================
    "_deps/imnodes_fc-src/imnodes.cpp",
    "_deps/imnodes_fc-src/imnodes.h",
    "_deps/imnodes_fc-src/imnodes_internal.h",
    "../build/_deps/imnodes_fc-src/imnodes.cpp",
    "../build/_deps/imnodes_fc-src/imnodes.h",
    "../build/_deps/imnodes_fc-src/imnodes_internal.h",
    "third_party/imnodes/imnodes.cpp",
    "third_party/imnodes/imnodes.h",
    "third_party/imnodes/imnodes_internal.h",
    "vendor/imnodes/imnodes.cpp",
    "vendor/imnodes/imnodes.h",
    "vendor/imnodes/imnodes_internal.h",

    # Also include CMake FetchContent default under juce/build
    "juce/build/_deps/imnodes_fc-src/imnodes.cpp",
    "juce/build/_deps/imnodes_fc-src/imnodes.h",
    "juce/build/_deps/imnodes_fc-src/imnodes_internal.h",

    #=============================================================
    # SECTION 6: Local examples for context (if present)
    #=============================================================
    "imnode_examples/hello.cpp",
    "imnode_examples/main.cpp",
    "imnode_examples/save_load.cpp",
    "imnode_examples/multi_editor.cpp",
    "imnode_examples/color_node_editor.cpp"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy: ImNodes Zoom" -ForegroundColor Cyan
Write-Host "Writing context pack to: $outputFile"

# Prepend context notes
Add-Content -Path $outputFile -Value $contextNotes

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    if ($file.StartsWith('#') -or $normalizedFile.StartsWith('#')) { continue }

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


