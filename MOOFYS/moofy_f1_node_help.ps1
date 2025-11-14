# PowerShell script to aggregate context for F1 Node Help Feature
# Generated for external expert consultation

$outputFile = "MOOFYS/moofy_f1_node_help.txt"
$sourceFiles = @(
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",
    "USER_MANUAL/Nodes_Dictionary.md"
)

$content = @"
================================================================================
CONTEXT BUNDLE: F1 Key Node Help Feature - Tab Selection Issue
================================================================================
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Purpose: External expert consultation on programmatic tab selection in ImGui

================================================================================
PROBLEM STATEMENT
================================================================================

CURRENT TASK:
We need to implement a feature where:
1. User selects a node in the node editor
2. User presses F1 key
3. Help Manager opens to the "Node Dictionary" tab (index 1)
4. Help Manager scrolls to and displays the help entry for that specific node

CURRENT PROBLEM:
The tab selection mechanism is not working correctly. When F1 is pressed with a 
node selected:
- The Help Manager window opens correctly
- The code sets m_currentTab = 1 and m_shouldSetTab = true
- The scroll anchor is set correctly
- BUT the window always shows the "Shortcuts" tab (index 0) instead of 
  "Node Dictionary" tab (index 1)

ROOT CAUSE:
ImGui's BeginTabItem() with ImGuiTabItemFlags_SetSelected doesn't work when:
- The target tab is not the first tab rendered
- The tab bar already exists (not the first frame)
- ImGui defaults to selecting the first tab rendered, ignoring SetSelected on later tabs

REQUIREMENTS:
1. F1 with no node selected → Open Help Manager to Shortcuts tab (index 0) ✓ (works)
2. F1 with node selected → Open Help Manager to Node Dictionary tab (index 1) ✗ (broken)
3. Users must be able to click tabs normally at all times ✓ (works after revert)
4. Tab selection must work reliably on first window open ✗ (broken)

================================================================================
CURRENT IMPLEMENTATION FLOW
================================================================================

1. USER ACTION: Selects a node and presses F1

2. SHORTCUT HANDLER (ImGuiNodeEditorComponent.cpp:510-534):
   - Checks if a node is selected using ImNodes::NumSelectedNodes()
   - Gets the selected node's logical ID
   - Calls synth->getModuleTypeForLogical(logicalId) to get module type (e.g., "vco", "track_mixer")
   - Calls m_helpManager.openToNode(moduleType)

3. openToNode() METHOD (HelpManagerComponent.cpp:123-146):
   - Calls open() to set m_isOpen = true
   - Calls setActiveTab(1) which sets m_currentTab = 1 and m_shouldSetTab = true
   - Converts module type to anchor format (e.g., "track_mixer" → "track-mixer")
   - Sets scrollToAnchor for later scrolling
   - Loads the Node Dictionary markdown file if not already loaded

4. RENDER LOOP (HelpManagerComponent.cpp:161-214):
   - Creates tab bar with ImGui::BeginTabBar("HelpTabs")
   - Renders "Shortcuts" tab first (index 0) - NO SetSelected flag
   - Renders "Node Dictionary" tab second (index 1) - WITH SetSelected flag if m_shouldSetTab && m_currentTab == 1
   - PROBLEM: ImGui selects the first tab (Shortcuts) by default, ignoring SetSelected on second tab

================================================================================
KEY CODE SECTIONS
================================================================================

"@

foreach ($file in $sourceFiles) {
    if (Test-Path $file) {
        $content += "`n`n================================================================================`n"
        $content += "FILE: $file`n"
        $content += "================================================================================`n`n"
        
        # For large files, only include relevant sections
        if ($file -like "*HelpManagerComponent.cpp") {
            $content += Get-Content $file -Raw | Select-String -Pattern "(openToNode|setActiveTab|render\(\)|BeginTabBar|BeginTabItem)" -Context 0,20 | ForEach-Object { $_.Line + "`n" + $_.Context.PostContext -join "`n" }
        }
        elseif ($file -like "*ImGuiNodeEditorComponent.cpp") {
            $content += Get-Content $file -Raw | Select-String -Pattern "(viewToggleShortcutsWindow|openToNode|F1|NumSelectedNodes)" -Context 0,20 | ForEach-Object { $_.Line + "`n" + $_.Context.PostContext -join "`n" }
        }
        elseif ($file -like "*HelpManagerComponent.h") {
            $content += Get-Content $file -Raw
        }
        else {
            $content += Get-Content $file -Raw
        }
        $content += "`n"
    } else {
        $content += "`n`nWARNING: File not found: $file`n"
    }
}

$content += @"

================================================================================
SPECIFIC QUESTIONS FOR EXPERT
================================================================================

1. TAB SELECTION MECHANISM:
   - How does ImGui::BeginTabItem() with ImGuiTabItemFlags_SetSelected work?
   - Why doesn't it work when applied to the second tab instead of the first?
   - Is there a way to force a specific tab to be selected after the tab bar is created?

2. CORRECT PATTERN:
   - What is the correct pattern for programmatic tab selection in ImGui?
   - Should we render the target tab FIRST when we want it selected?
   - Is there an alternative API or approach we should use?
   - Are there any ImGui examples or demos showing this pattern?

3. WORKAROUNDS:
   - Can we conditionally render tabs in a different order?
   - Should we use ImGui::SetNextItemOpen() or similar APIs?
   - Is there a way to "reset" the tab bar state?

4. ALTERNATIVE APPROACHES:
   - Should we close and reopen the window to force tab selection?
   - Should we use a different UI pattern (e.g., separate windows)?
   - Is there a way to query/set the active tab index directly?

================================================================================
TECHNICAL CONTEXT
================================================================================

- Framework: ImGui (dear imgui) - latest version
- Language: C++17
- Platform: Windows
- Tab Bar API: ImGui::BeginTabBar(), ImGui::BeginTabItem()
- Current approach: Using m_shouldSetTab flag + ImGuiTabItemFlags_SetSelected
- Window state: Persistent across frames (not recreated each frame)

================================================================================
SUCCESS CRITERIA
================================================================================

A solution is successful if:
1. ✅ F1 with no node → Opens to Shortcuts tab (currently works)
2. ❌ F1 with node selected → Opens to Node Dictionary tab (currently broken)
3. ✅ Users can click tabs normally (works after revert)
4. ❌ Tab selection works on first window open (currently broken)
5. ✅ Help Manager scrolls to correct node entry (anchor scrolling works)

================================================================================
END OF CONTEXT BUNDLE
================================================================================
"@

$content | Out-File -FilePath $outputFile -Encoding UTF8
Write-Host "Context bundle created: $outputFile"

