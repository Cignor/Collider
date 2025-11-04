#=============================================================
#       MOOFY: Theme Manager Essentials (Preset Creator UI)
#       Archives all critical files and guides needed by an
#       external expert to help design/implement a Theme Manager
#       without direct repo access.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Theme_Manager_Essentials.txt"

#--- Essential Files for Theme System ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PROBLEM STATEMENT & HIGH-LEVEL CONTEXT
    #=============================================================
    "guides/THEME_MANAGER_INTEGRATION_GUIDE.md",          # Newly created comprehensive plan
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",                  # UI patterns incl. modulation color scheme

    #=============================================================
    # SECTION 2: MAIN UI ENTRYPOINT (ImGui + ImNodes)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/NotificationManager.cpp", # Window alpha & notifications

    #=============================================================
    # SECTION 3: COLOR REGISTRIES & PIN/TYPE SYSTEM
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",

    #=============================================================
    # SECTION 4: MODULE UI EXAMPLES WITH HARDCODED COLORS
    #=============================================================
    "juce/Source/audio/modules/VideoFXModule.cpp",
    "juce/Source/audio/modules/ScopeModuleProcessor.cpp",
    "juce/Source/audio/modules/StrokeSequencerModuleProcessor.cpp",
    "juce/Source/audio/modules/ColorTrackerModule.cpp",
    "juce/Source/audio/modules/CropVideoModule.cpp",

    #=============================================================
    # SECTION 5: APPLICATION & INITIALIZATION (fonts, style)
    #=============================================================
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",

    #=============================================================
    # SECTION 6: SUPPORTING GUIDES (context)
    #=============================================================
    "guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Theme Manager Moofy..." -ForegroundColor Cyan
Write-Host "Archiving essential UI/theme files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    # Skip comment-like lines (defensive, mirrors house style)
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue
    }

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

#--- Final Context Note for External Expert ---
$note = @"

================================================================================
CONTEXT NOTE FOR EXTERNAL EXPERT
================================================================================
Goal: Prepare and implement a Theme Manager to centralize all UI-related styling
(ImGui + ImNodes) for the Preset Creator. The guide 'THEME_MANAGER_INTEGRATION_GUIDE.md'
lists every hardcoded color/size/alpha and proposes a Theme/ThemeManager API.

Priority Integration Points:
1) Replace ImGui::StyleColorsDark with ThemeManager::applyTheme
2) Route node category colors via ThemeManager::getCategoryColor
3) Route pin/link colors via ThemeManager::getPinColor and related APIs
4) Centralize window alpha, spacing, and default widths
5) Migrate module-specific hardcoded colors incrementally

Files above include:
- Core UI component (ImGuiNodeEditorComponent.*) where most colors/styles live
- Representative modules with custom colors (VideoFX, Scope, Stroke Sequencer)
- Notifications (window alpha), fonts (default + Chinese), pin database
- Guides to understand UI patterns and overall system

Deliverables expected from you:
- A concrete Theme struct (as per guide), default theme filled from current values
- ThemeManager singleton with applyTheme(), getters for colors/layout
- Minimal integration PR: swap out direct color usage for theme calls in key places
- Optional: JSON/XML theme serialization + 2-3 preset themes (Dark/Light/High Contrast)

This text archive should be sufficient to review code and produce precise advice/PRs
without repo access. If anything is missing, request additional files by path.
================================================================================
"@

Add-Content -Path $outputFile -Value $note


