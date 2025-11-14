# PowerShell script to aggregate context for ImGui Tab Selection Issue
# Generated for external expert consultation

$outputFile = "MOOFYS/moofy_tab_selection.txt"
$sourceFiles = @(
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.h"
)

$content = @"
================================================================================
CONTEXT BUNDLE: ImGui Tab Selection Issue in HelpManagerComponent
================================================================================
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Purpose: External expert consultation on programmatic tab selection in ImGui

================================================================================
PROBLEM STATEMENT
================================================================================

We have a HelpManagerComponent with a tabbed interface using ImGui::BeginTabBar().
The component needs to support:
1. Normal user tab selection (clicking tabs)
2. Programmatic tab selection (e.g., F1 key opens to specific tab)

CURRENT ISSUE:
- When F1 is pressed with a node selected, we want to open to Node Dictionary tab (index 1)
- However, the window always defaults to Shortcuts tab (index 0)
- Attempts to use ImGuiTabItemFlags_SetSelected have broken normal tab selection
- Users cannot click to change tabs after programmatic selection attempts

REQUIREMENTS:
- F1 with no node selected → Open Help Manager to Shortcuts tab (index 0)
- F1 with node selected → Open Help Manager to Node Dictionary tab (index 1) and scroll to that node
- Users must be able to click tabs normally at all times
- Tab selection must work reliably on first window open

================================================================================
CURRENT IMPLEMENTATION
================================================================================

"@

foreach ($file in $sourceFiles) {
    if (Test-Path $file) {
        $content += "`n`n================================================================================`n"
        $content += "FILE: $file`n"
        $content += "================================================================================`n`n"
        $content += Get-Content $file -Raw
        $content += "`n"
    } else {
        $content += "`n`nWARNING: File not found: $file`n"
    }
}

$content += @"

================================================================================
KEY QUESTIONS FOR EXPERT
================================================================================

1. How does ImGui::BeginTabItem() with ImGuiTabItemFlags_SetSelected work?
   - Does it only work on the first frame the tab bar is created?
   - What happens if multiple tabs have SetSelected flag?
   - What happens if the first tab rendered doesn't have SetSelected but a later one does?

2. What is the correct pattern for programmatic tab selection in ImGui?
   - Should we render the target tab first?
   - Should we use a different approach than SetSelected flag?
   - Is there a way to query/set the active tab after the tab bar is created?

3. How can we ensure normal user tab selection continues to work after programmatic selection?
   - Are there any state flags we need to clear?
   - Should we only use SetSelected on the first frame?

4. Are there any ImGui examples or best practices for this use case?

================================================================================
TECHNICAL CONTEXT
================================================================================

- Framework: ImGui (dear imgui)
- Language: C++17
- Platform: Windows
- Tab Bar API: ImGui::BeginTabBar(), ImGui::BeginTabItem()
- Current approach: Using m_shouldSetTab flag + ImGuiTabItemFlags_SetSelected

================================================================================
END OF CONTEXT BUNDLE
================================================================================
"@

$content | Out-File -FilePath $outputFile -Encoding UTF8
Write-Host "Context bundle created: $outputFile"

