# Moofy: Markdown Rendering Improvements for Help Manager
# Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
# Purpose: Context bundle for improving markdown rendering in HelpManagerComponent

$outputFile = "MOOFYS/moofy_markdown_rendering.txt"
$sourceFiles = @(
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    "USER_MANUAL/Nodes_Dictionary.md",
    "USER_MANUAL/Getting_Started.md",
    "USER_MANUAL/FAQ.md",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",
    "guides/HELP_MANAGER_DESIGN.md"
)

$content = @"
================================================================================
MOOFY: Markdown Rendering Improvements for Help Manager
================================================================================
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

CONTEXT SUMMARY
================================================================================
The user wants to improve the markdown rendering in the Help Manager component.
Currently, the Node Dictionary (and other markdown tabs) display as plain text
without proper styling, colors, typography, or modern formatting.

KEY REQUIREMENTS:
1. Respect theme colors from the application
2. Proper typography: font weights, sizes, capitalization
3. Clean, modern appearance (not just a text file dump)
4. Better rendering of headers, bold text, code blocks, lists
5. Update Getting Started and FAQ markdown files with proper structure

TECHNICAL CONTEXT:
- Using ImGui for rendering
- Theme system uses ImGuiCol_* colors
- Markdown files are parsed into hierarchical MarkdownSection structures
- Current rendering is very basic (plain TextWrapped calls)

FILES TO REVIEW:
================================================================================
"@

foreach ($file in $sourceFiles) {
    if (Test-Path $file) {
        $content += "`n`n=== $file ===`n"
        $content += Get-Content $file -Raw
    } else {
        $content += "`n`n=== $file (NOT FOUND) ===`n"
    }
}

$content += @"

================================================================================
IMPLEMENTATION NOTES
================================================================================

CURRENT STATE:
- renderMarkdownText() is very basic - just handles lists and simple formatting
- Headers use minimal styling (SetWindowFontScale only)
- No theme color integration
- Code blocks not properly styled
- Bold text uses simple font scaling
- Inline code not properly styled

DESIRED IMPROVEMENTS:
1. Headers should use theme colors (ImGuiCol_HeaderHovered for accent)
2. Different font sizes for different header levels (## = 1.25x, ### = 1.1x)
3. Bold text should use accent color + slight font increase
4. Inline code should have background (FrameBg) and rounded corners
5. Code blocks should have proper background and padding
6. Lists should have proper indentation
7. Better spacing between sections

TECHNICAL APPROACH:
- Use ImGui::PushStyleColor() with theme colors
- Use ImGui::SetWindowFontScale() for typography
- Use ImGui::PushStyleVar() for padding/rounding
- Parse markdown inline formatting (bold, code) into segments
- Render segments with appropriate styling

QUESTIONS FOR EXPERT:
1. Best way to render inline formatting (bold/code) within wrapped text?
2. Should we use ImGui::TextColored() or PushStyleColor()?
3. How to handle code blocks with proper background?
4. Best practices for markdown rendering in ImGui?

================================================================================
END OF MOOFY
================================================================================
"@

$content | Out-File -FilePath $outputFile -Encoding UTF8
Write-Host "Moofy created: $outputFile"

