#=============================================================
#       MOOFY INSERT-ON-LINK DEEP DIVE
#       Curated context for debugging and extending the
#       "Insert Node on Cable" workflow inside the Preset Creator.
#       Share this with any external helper focusing on the feature.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_InsertOnLink.txt"

#--- Insert-On-Link Knowledge Pack ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PRIMARY IMPLEMENTATION
    # Core component driving the popup, link bookkeeping, and wiring.
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 2: SUPPORTING SYSTEMS
    # Shortcuts, pin metadata, and synth graph helpers used by insert flow.
    #=============================================================
    "juce/Source/preset_creator/ShortcutManager.h",
    "juce/Source/preset_creator/ShortcutManager.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",

    #=============================================================
    # SECTION 3: DESIGN REFERENCES
    # Guides detailing drag/insert UX and registration requirements.
    #=============================================================
    "guides/DRAG_INSERT_NODE_GUIDE.md",
    "guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Insert-On-Link script..." -ForegroundColor Cyan
Write-Host "Gathering insert-on-link resources at: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

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


