# PowerShell script to aggregate context for Tab Selection Fix Implementation
# Generated to document the solution and implementation

$outputFile = "MOOFYS/moofy_tab_selection_fix.txt"
$sourceFiles = @(
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp"
)

$content = @"
================================================================================
CONTEXT BUNDLE: Tab Selection Fix Implementation
================================================================================
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Purpose: Document the solution and implementation for programmatic tab selection

================================================================================
PROBLEM SUMMARY
================================================================================

ISSUE:
When F1 was pressed with a node selected, the Help Manager would open but always
default to the "Shortcuts" tab (index 0) instead of the "Node Dictionary" tab (index 1).

ROOT CAUSE:
1. On the first frame a tab bar is created, ImGui defaults to selecting the first tab
2. The code was updating m_currentTab = 0 based on ImGui's default selection
3. This prematurely cleared the m_shouldSetTab flag
4. The SetSelected flag on the second tab was never seen on frame 2 where it would work

SOLUTION:
Only update m_currentTab when a user physically clicks a tab (using ImGui::IsItemClicked()).
Keep the m_shouldSetTab flag persistent until the target tab is actually selected.

================================================================================
IMPLEMENTATION DETAILS
================================================================================

KEY CHANGES:
1. Added ImGui::IsItemClicked() check before updating m_currentTab
2. Only update m_currentTab on actual user clicks, not on default/programmatic selection
3. Keep m_shouldSetTab flag until the target tab's BeginTabItem block executes
4. Apply ImGuiTabItemFlags_SetSelected to all tabs (not just Node Dictionary)

HOW IT WORKS:
- Frame 1: ImGui selects first tab by default, but m_currentTab stays at 1, m_shouldSetTab stays true
- Frame 2: SetSelected flag on Node Dictionary tab is now respected, tab switches correctly
- Frame 3+: Tab remains selected, flag is cleared, normal operation continues

================================================================================
CODE FILES
================================================================================

"@

foreach ($file in $sourceFiles) {
    if (Test-Path $file) {
        $content += "`n`n================================================================================`n"
        $content += "FILE: $file`n"
        $content += "================================================================================`n`n"
        
        if ($file -like "*HelpManagerComponent.cpp") {
            # Include the render() function and tab selection logic
            $content += Get-Content $file -Raw | Select-String -Pattern "(render\(\)|BeginTabBar|BeginTabItem|IsItemClicked|m_shouldSetTab|m_currentTab)" -Context 0,5 | ForEach-Object { $_.Line + "`n" + $_.Context.PostContext -join "`n" }
        }
        elseif ($file -like "*HelpManagerComponent.h") {
            $content += Get-Content $file -Raw
        }
        elseif ($file -like "*ImGuiNodeEditorComponent.cpp") {
            $content += Get-Content $file -Raw | Select-String -Pattern "(openToNode|viewToggleShortcutsWindow|F1|NumSelectedNodes)" -Context 0,10 | ForEach-Object { $_.Line + "`n" + $_.Context.PostContext -join "`n" }
        }
        $content += "`n"
    } else {
        $content += "`n`nWARNING: File not found: $file`n"
    }
}

$content += @"

================================================================================
EXPERT SOLUTION SUMMARY
================================================================================

The expert provided a solution based on the following principles:

1. STATE ISOLATION:
   - Only update m_currentTab when user physically clicks (ImGui::IsItemClicked())
   - Don't let ImGui's default selection clobber intended state

2. PERSISTENT FLAG:
   - Keep m_shouldSetTab true until target tab is actually selected
   - Clear flag only when target tab's BeginTabItem block executes

3. TWO-FRAME APPROACH:
   - Frame 1: ImGui defaults to first tab, but state remains correct
   - Frame 2: SetSelected flag works because tab bar already exists
   - Frame 3+: Normal operation, flag cleared

4. USER INTERACTION:
   - User clicks always override programmatic selection
   - Normal tab clicking works perfectly

================================================================================
TESTING CHECKLIST
================================================================================

✅ F1 with no node selected → Opens to Shortcuts tab (index 0)
✅ F1 with node selected → Opens to Node Dictionary tab (index 1)
✅ Users can click tabs normally at all times
✅ Tab selection works reliably on first window open
✅ Help Manager scrolls to correct node entry

================================================================================
END OF CONTEXT BUNDLE
================================================================================
"@

$content | Out-File -FilePath $outputFile -Encoding UTF8
Write-Host "Context bundle created: $outputFile"

