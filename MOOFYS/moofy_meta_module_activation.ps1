#=============================================================
#   MOOFY: Meta Module Activation & Recovery Context
#   Collect every file an external helper needs to fix the
#   Meta / Inlet / Outlet pipeline and understand current gaps.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_MetaModule_Activation.txt"

#--- Target Files ---
$sourceFiles = @(
    #=============================================================
    # 1. Status & Guides
    #=============================================================
    "guides/META_MODULE_STATUS_GUIDE.md",
    "guides/DRAG_INSERT_NODE_GUIDE.md",
    
    #=============================================================
    # 2. Core Audio Graph & Module Infrastructure
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    
    #=============================================================
    # 3. Meta / Inlet / Outlet Modules
    #=============================================================
    "juce/Source/audio/modules/MetaModuleProcessor.h",
    "juce/Source/audio/modules/MetaModuleProcessor.cpp",
    "juce/Source/audio/modules/InletModuleProcessor.h",
    "juce/Source/audio/modules/InletModuleProcessor.cpp",
    "juce/Source/audio/modules/OutletModuleProcessor.h",
    "juce/Source/audio/modules/OutletModuleProcessor.cpp",
    
    #=============================================================
    # 4. Node Editor UI & Collapse/Expand Workflows
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    
    #=============================================================
    # 5. Supporting Infrastructure / Docs
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/UNDO_REDO_SYSTEM_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md"
)

#--- Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Generating Meta Module activation Moofy..."
Write-Host "Output   : $outputFile"

foreach ($file in $sourceFiles) {
    $normalized = $file.Replace('/', '\')
    $fullPath   = Join-Path $projectRoot $normalized
    
    if (Test-Path $fullPath) {
        $header  = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalized`n"
        $header += ("=" * 80) + "`n`n"
        
        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        
        Write-Host " -> Archived: $normalized" -ForegroundColor Green
    }
    else {
        $warning  = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalized`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        
        Write-Host " -> WARNING: Missing file: $normalized" -ForegroundColor Yellow
    }
}

Write-Host "Meta Module activation Moofy generation complete." -ForegroundColor Cyan

