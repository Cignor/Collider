#=============================================================
#       MOOFY REPORT: Cable Inspector Performance Context
#       Generates a focused knowledge pack for external helpers
#       investigating the Cable Inspector hover performance spike.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_CableInspector.txt"

#--- Problem Summary (written once at the top of the report) ---
$problemSummary = @"
================================================================================
CONTEXT: Cable Inspector Hover Performance Regression
--------------------------------------------------------------------------------
Symptom:
    Hovering cables in the ImGui node editor triggers severe frame drops.

Recent Fix Attempt:
    UI caches the currently probed node/channel and avoids redundant calls into
    ModularSynthProcessor::setProbeConnection(). The synth now memoises the last
    probe and skips graph commits when unchanged.

What We Need From External Helper:
    - Validate that the probe caching fully eliminates redundant graph rebuilds.
    - Analyse remaining hotspots (if any) in tooltip rendering / scope fetch.
    - Suggest further optimizations if hover interactions still stutter.

Meta Module Activation Snapshot:
    The Meta/Meta-Inlet/Meta-Outlet toolchain is mid-MVP. We now have a nested
    ImNodes editor shell, typed drag-insert wiring, and layout rebuild signalling.
    We need focused review on:
        * Typed pin compatibility matrix for Audio/CV/Gate/Trigger/Raw in drag-insert.
        * Metadata persistence for boundary nodes (external logical IDs, channels,
          modulation wiring, MIDI bindings).
        * Dynamic bus layout rebuilds triggered via MetaModuleProcessor::refreshCachedLayout()
          and consumed on the audio thread.
        * Buffer resizing strategy in MetaModuleProcessor::resizeIOBuffers().
        * UX affordances in renderMetaModuleEditor() (search, node palettes, undo/redo).
    Context files are appended below.

Key Files Captured Below:
    * ImGui node editor UI (Cable Inspector logic & memoization state).
    * ModularSynthProcessor probe API (graph mutation + caching).
    * Architecture docs explaining module graph, probe scope, and theme system.
================================================================================
"@

# Write summary header
if (Test-Path $outputFile) { Remove-Item $outputFile }
Add-Content -Path $outputFile -Value $problemSummary

#--- Essential Files for External Review ---
$sourceFiles = @(
    #=============================================================
    # 1. High-Level Architecture References
    #=============================================================
    "architecture/00_PROJECT_OVERVIEW.md",
    "architecture/02_AUDIO_SYSTEM.md",

    #=============================================================
    # 2. Core Graph & Probe Infrastructure
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/modules/ScopeModuleProcessor.h",
    "juce/Source/audio/modules/ScopeModuleProcessor.cpp",

    #=============================================================
    # 3. Node Editor UI (Cable Inspector Logic)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/theme/ThemeManager.h",
    "juce/Source/preset_creator/theme/ThemeManager.cpp",

    #=============================================================
    # 4. Performance & Build Guidance
    #=============================================================
    "guides/ninja-build-release-or-debug.md",
    "ninja_sccache_BUILD_FIX_IMPLEMENTATION_SUMMARY.md",

    #=============================================================
    # 5. Meta Module Activation Context
    #=============================================================
    "guides/META_MODULE_STATUS_GUIDE.md",
    "guides/DRAG_INSERT_NODE_GUIDE.md",
    "juce/Source/audio/modules/MetaModuleProcessor.h",
    "juce/Source/audio/modules/MetaModuleProcessor.cpp",
    "juce/Source/audio/modules/InletModuleProcessor.h",
    "juce/Source/audio/modules/InletModuleProcessor.cpp",
    "juce/Source/audio/modules/OutletModuleProcessor.h",
    "juce/Source/audio/modules/OutletModuleProcessor.cpp",
    "MOOFYS/moofy_meta_module_activation.ps1"
)

#--- Script Execution ---
Write-Host "Starting Cable Inspector Moofy..." -ForegroundColor Cyan
Write-Host "Collecting context into: $outputFile"

foreach ($file in $sourceFiles) {
    if ([string]::IsNullOrWhiteSpace($file) -or $file.Trim().StartsWith("#")) {
        continue
    }

    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    $header = "`n" + ("=" * 80) + "`n"
    $header += "FILE: $normalizedFile`n"
    $header += ("=" * 80) + "`n`n"

    if (Test-Path $fullPath) {
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        Write-Host " -> Captured: $normalizedFile" -ForegroundColor Green
    } else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: Missing file: $normalizedFile" -ForegroundColor Yellow
    }
}

Write-Host "Cable Inspector Moofy complete." -ForegroundColor Cyan


