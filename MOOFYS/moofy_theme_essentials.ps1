#=============================================================
#       MOOFY THEME ESSENTIALS: Theme System Archive Guide
#       Collects all key documentation and source files
#       required to understand and extend the theme pipeline.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_theme_essentials.txt"
$themePresetDir = Join-Path $projectRoot "juce\Source\preset_creator\theme\presets"

#--- Core Theme Files & Docs ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: Theme System Design Docs
    #=============================================================
    "guides/THEME_MANAGER_INTEGRATION_GUIDE.md",
    "guides/THEME_EDITOR_DESIGN.md",
    "guides/MENU_REORGANIZATION_SUMMARY.md",

    #=============================================================
    # SECTION 2: Theme Core Types
    #=============================================================
    "juce/Source/preset_creator/theme/Theme.h",
    "juce/Source/preset_creator/theme/ThemeManager.h",
    "juce/Source/preset_creator/theme/ThemeManager.cpp",

    #=============================================================
    # SECTION 3: Theme Editor UI
    #=============================================================
    "juce/Source/preset_creator/theme/ThemeEditorComponent.h",
    "juce/Source/preset_creator/theme/ThemeEditorComponent.cpp",

    #=============================================================
    # SECTION 4: Integration Points
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Theme Essentials archive..." -ForegroundColor Cyan
Write-Host "Output file: $outputFile"

foreach ($file in $sourceFiles) {
    if ($file.StartsWith("#")) {
        continue
    }

    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    if (Test-Path $fullPath) {
        $header  = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile

        Write-Host " -> Archived: $normalizedFile" -ForegroundColor Green
    } else {
        $warning  = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning

        Write-Host " -> WARNING: File not found: $normalizedFile" -ForegroundColor Yellow
    }
}

#--- Theme Presets (JSON) ---
if (Test-Path $themePresetDir) {
    Write-Host "Archiving theme preset JSON files..." -ForegroundColor Cyan

    Get-ChildItem -Path $themePresetDir -Filter "*.json" | Sort-Object Name | ForEach-Object {
        $relativePath = $_.FullName.Substring($projectRoot.Length + 1)

        $header  = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $relativePath`n"
        $header += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $_.FullName | Add-Content -Path $outputFile

        Write-Host " -> Archived preset: $relativePath" -ForegroundColor Green
    }
} else {
    Write-Host "Theme preset directory not found: $themePresetDir" -ForegroundColor Yellow
}

Write-Host "Moofy Theme Essentials archive complete." -ForegroundColor Cyan


