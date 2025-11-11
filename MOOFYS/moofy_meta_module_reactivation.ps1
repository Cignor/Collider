$ErrorActionPreference = "Stop"

#=============================================================
#       MOOFY: Meta Module Reactivation Context Bundle
#       Purpose-built package for external helpers reviving the
#       Meta / Inlet / Outlet toolchain & typed drag-insert flow.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_MetaModule.txt"

#--- Situation Brief (prepended to report) ---
$problemSummary = @"
================================================================================
CONTEXT: Meta Module MVP Reactivation
--------------------------------------------------------------------------------
Goal:
    Finalise the Meta module feature set (nested editing, boundary metadata,
    typed wiring, modulation/MIDI bridging) so Meta, Inlet, Outlet nodes are
    production-ready.

Critical Pain Points:
    1. Nested ImGui/ImNodes editor shell exists but lacks palettes, undo/redo,
       node search, and position persistence.
    2. Boundary metadata (logical IDs, channels, mod/MIDI routes) now persists
       but collapse/expand paths still ignore it.
    3. Dynamic bus layouts and buffer management must propagate channel edits
       safely between UI and audio threads.
    4. Typed pin propagation (Audio/CV/Gate/Trigger/Raw) is incomplete across
       drag-insert and meta boundaries.
    5. Modulation/MIDI bridging and parameter forwarding need implementation.

What We Need From External Helper:
    - Audit current editor shell and propose UX/undo architecture.
    - Wire persisted metadata back into expand/collapse routines without
      breaking legacy presets.
    - Validate MetaModuleProcessor bus rebuild & buffer reuse strategy.
    - Extend typed pin routing, modulation & MIDI bridging logic end-to-end.
    - Outline test coverage strategy (automation or scripted Moofy validation).
================================================================================
"@

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Add-Content -Path $outputFile -Value $problemSummary

#--- Source File Manifest ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: Product & Architecture References
    #=============================================================
    "architecture/00_PROJECT_OVERVIEW.md",
    "architecture/02_AUDIO_SYSTEM.md",
    "guides/META_MODULE_STATUS_GUIDE.md",
    "guides/DRAG_INSERT_NODE_GUIDE.md",
    "guides/TIMELINE_NODE_REMEDIATION_GUIDE.md",

    #=============================================================
    # SECTION 2: Node Editor Frontend (ImGui/ImNodes)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/theme/ThemeManager.h",
    "juce/Source/preset_creator/theme/ThemeManager.cpp",

    #=============================================================
    # SECTION 3: Meta Module Audio Processors
    #=============================================================
    "juce/Source/audio/modules/MetaModuleProcessor.h",
    "juce/Source/audio/modules/MetaModuleProcessor.cpp",
    "juce/Source/audio/modules/InletModuleProcessor.h",
    "juce/Source/audio/modules/InletModuleProcessor.cpp",
    "juce/Source/audio/modules/OutletModuleProcessor.h",
    "juce/Source/audio/modules/OutletModuleProcessor.cpp",
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",

    #=============================================================
    # SECTION 4: Collapse / Expand Infrastructure
    #=============================================================
    "juce/Source/preset_creator/graph/MetaModuleHelpers.h",
    "juce/Source/preset_creator/graph/MetaModuleHelpers.cpp",
    "juce/Source/preset_creator/graph/GraphCommands.h",
    "juce/Source/preset_creator/graph/GraphCommands.cpp",

    #=============================================================
    # SECTION 5: State Persistence & APVTS
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",

    #=============================================================
    # SECTION 6: Existing Moofy Bundles / Scripts
    #=============================================================
    "MOOFYS/moofy_meta_module_activation.ps1",
    "MOOFYS/moofy_cable_inspector.ps1",
    "MOOFYS/moofy_Essentials.txt"
)

Write-Host "Starting Meta Module Moofy..." -ForegroundColor Cyan
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

Write-Host "Meta Module Moofy complete." -ForegroundColor Cyan

