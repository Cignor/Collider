# Moofy Script: Node Dictionary Markdown Parser Implementation
# Aggregates all relevant files for implementing markdown parsing in HelpManagerComponent

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "MOOFYS\moofy_node_dictionary.txt"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Moofy Node Dictionary Parser Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Project Root: $projectRoot" -ForegroundColor Gray
Write-Host "Output File: $outputFile" -ForegroundColor Gray
Write-Host ""

# Source files to aggregate
$sourceFiles = @(
    #=============================================================
    # SECTION 1: DESIGN DOCUMENTS
    # Understanding the requirements and architecture
    #=============================================================
    "guides/HELP_MANAGER_DESIGN.md",
    "guides/HELP_MANAGER_CONCEPTUALIZATION.md",
    
    #=============================================================
    # SECTION 2: TARGET MARKDOWN FILE
    # The file we need to parse and render
    #=============================================================
    "USER_MANUAL/Nodes_Dictionary.md",
    
    #=============================================================
    # SECTION 3: HELP MANAGER COMPONENT (CURRENT STATE)
    # The component we're modifying
    #=============================================================
    "juce/Source/preset_creator/HelpManagerComponent.h",
    "juce/Source/preset_creator/HelpManagerComponent.cpp",
    
    #=============================================================
    # SECTION 4: REFERENCE IMPLEMENTATION - ThemeEditorComponent
    # Study this pattern for tabbed UI components
    #=============================================================
    "juce/Source/preset_creator/theme/ThemeEditorComponent.h",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 5: FILE I/O EXAMPLES
    # How to read files in this codebase
    #=============================================================
    "juce/Source/preset_creator/theme/ThemeManager.cpp",
    # Look for loadFileAsString usage
    
    #=============================================================
    # SECTION 6: IMGUI RENDERING PATTERNS
    # How to render UI elements we'll need
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "imgui_demo.cpp",
    
    #=============================================================
    # SECTION 7: JUCE STRING UTILITIES
    # String manipulation functions we'll use
    #=============================================================
    # JUCE String methods: containsIgnoreCase, trim, replace, etc.
    # These are documented in JUCE but usage examples are in existing code
    
    #=============================================================
    # SECTION 8: IMPLEMENTATION NOTES
    # Key decisions and patterns to follow
    #=============================================================
    # - Simple custom markdown parser (no external library)
    # - Parse on first tab open (lazy loading)
    # - Cache parsed structure in memory
    # - Support: headers (##, ###, ####), lists, bold text, inline code
    # - Search functionality: filter sections by title/content match
    # - Render with ImGui: CollapsingHeader for top-level, Text for content
    # - Auto-expand sections that match search
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Aggregating files..." -ForegroundColor Yellow
$fileCount = 0
$missingCount = 0

foreach ($file in $sourceFiles) {
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
Write-Host "Moofy Node Dictionary script completed!" -ForegroundColor Cyan
Write-Host "Files archived: $fileCount" -ForegroundColor Green
if ($missingCount -gt 0) {
    Write-Host "Files missing: $missingCount" -ForegroundColor Yellow
}
Write-Host "Output file: $outputFile" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

