#=============================================================
#       MOOFY HELP MANAGER: Help System Implementation Pack
#       Generates a context bundle for implementing the new
#       centralized Help Manager system with tabbed interface
#       for shortcuts, documentation, tutorials, and app info.
#
#       KEY IMPROVEMENT: All documentation tabs should be
#       data-driven (markdown files) rather than hard-coded,
#       allowing updates without recompilation.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_HelpManager.txt"

#--- Curated Source List ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: DESIGN DOCUMENTS & REQUIREMENTS
    # Read these first to understand the system design
    #=============================================================
    "guides/HELP_MANAGER_DESIGN.md",
    "guides/HELP_MANAGER_CONCEPTUALIZATION.md",
    "MOOFYS/moofy_help_manager.md",
    "MOOFYS/communicate.md",
    
    #=============================================================
    # SECTION 2: REFERENCE IMPLEMENTATION - ThemeEditorComponent
    # Study this pattern for tabbed UI components
    #=============================================================
    "juce/Source/preset_creator/theme/ThemeEditorComponent.h",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",
    "juce/Source/preset_creator/theme/ThemeManager.h",
    "juce/Source/preset_creator/theme/ThemeManager.cpp",
    "juce/Source/preset_creator/theme/Theme.h",
    
    #=============================================================
    # SECTION 9: HELP MANAGER COMPONENT (NEW)
    # The new tabbed help window component
    #=============================================================
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    
    #=============================================================
    # SECTION 3: SHORTCUT MANAGER - Integration Target
    # The shortcuts tab will integrate this existing system
    #=============================================================
    "juce/Source/preset_creator/ShortcutManager.h",
    "juce/Source/preset_creator/ShortcutManager.cpp",
    "guides/SHORTCUT_MANAGER_GUIDE.md",
    
    #=============================================================
    # SECTION 4: MAIN EDITOR COMPONENT - Integration Point
    # Where Help Manager will be integrated (menu, shortcuts)
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 5: MARKDOWN CONTENT - Documentation Files
    # These markdown files will be rendered in tabs (data-driven)
    #=============================================================
    "USER_MANUAL/Nodes_Dictionary.md",
    # Note: Getting_Started.md and FAQ.md will be created during implementation
    
    #=============================================================
    # SECTION 6: UI DESIGN PATTERNS & GUIDES
    # Best practices for ImGui implementation
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "imgui_demo.cpp",
    
    #=============================================================
    # SECTION 7: NOTIFICATION SYSTEM - Example Component
    # Reference for component pattern and rendering
    #=============================================================
    "juce/Source/preset_creator/NotificationManager.h",
    "juce/Source/preset_creator/NotificationManager.cpp",
    
    #=============================================================
    # SECTION 8: APPLICATION STRUCTURE
    # Understanding the overall application architecture
    #=============================================================
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",
    
    #=============================================================
    # SECTION 9: FILE I/O & PERSISTENCE
    # For markdown file loading and potential caching
    # Examples of JUCE file operations, string handling
    #=============================================================
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/SampleManager.h",
    "juce/Source/preset_creator/SampleManager.cpp",
    
    #=============================================================
    # SECTION 10: CLIPBOARD & TEXT HANDLING
    # For copy-to-clipboard functionality in code blocks
    #=============================================================
    # JUCE clipboard usage examples can be found in existing code
    # Look for juce::SystemClipboard usage patterns
    
    #=============================================================
    # SECTION 10: BUILD SYSTEM
    # Understanding dependencies and where to add new files
    #=============================================================
    "juce/CMakeLists.txt"
    
    #=============================================================
    # SECTION 11: IMPLEMENTATION NOTES
    # Key decisions and patterns to follow
    #=============================================================
    # - All documentation tabs should be DATA-DRIVEN (markdown files)
    #   NOT hard-coded in C++. This allows updates without recompilation.
    # - Reuse the markdown parser for Node Dictionary, Getting Started, FAQ
    # - Follow ThemeEditorComponent pattern for tabbed UI
    # - Phase 1: Foundation (HelpManagerComponent class, menu integration)
    # - Phase 2: Shortcuts & About tabs (quick wins)
    # - Phase 3: Getting Started tab (data-driven markdown)
    # - Phase 4: Node Dictionary tab (core feature with search)
    # - Phase 5: Polish (context menu, scroll-to, etc.)
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Help Manager script..." -ForegroundColor Cyan
Write-Host "Aggregating context bundle at: $outputFile"

$fileCount = 0
$missingCount = 0

foreach ($file in $sourceFiles) {
    # Skip comment lines
    if ($file.StartsWith("#")) { continue }

    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    if (Test-Path $fullPath) {
        $header = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"
        
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        
        $fileCount++
        Write-Host " -> Archived: $normalizedFile" -ForegroundColor Green
    } else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        $missingCount++
        Write-Host " -> WARNING: File not found: $normalizedFile" -ForegroundColor Yellow
    }
}

Write-Host "`n" -NoNewline
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Moofy Help Manager script completed!" -ForegroundColor Cyan
Write-Host "Files archived: $fileCount" -ForegroundColor Green
if ($missingCount -gt 0) {
    Write-Host "Files missing: $missingCount" -ForegroundColor Yellow
}
Write-Host "Output file: $outputFile" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

