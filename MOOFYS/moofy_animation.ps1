#=============================================================
#       MOOFY ANIMATION SCRIPT: Complete Animation System Archive
#Archives all animation-related files including the Animation
#Module, loaders, renderers, preset files, and supporting 
#infrastructure for comprehensive code review and debugging.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Animation.txt"

#--- List of files relevant to the Animation system ---
$sourceFiles = @(
    # --- Animation Module (Audio Integration) ---
    "juce/Source/audio/modules/AnimationModuleProcessor.cpp",
    "juce/Source/audio/modules/AnimationModuleProcessor.h",
    
    # --- Core Animation System ---
    "juce/Source/animation/AnimationData.h",
    "juce/Source/animation/RawAnimationData.h",
    "juce/Source/animation/RawAnimationData.cpp",
    "juce/Source/animation/Animator.h",
    "juce/Source/animation/Animator.cpp",
    "juce/Source/animation/AnimationBinder.h",
    "juce/Source/animation/AnimationBinder.cpp",
    "juce/Source/animation/AnimationFileLoader.h",
    "juce/Source/animation/AnimationFileLoader.cpp",
    
    # --- Animation Loaders ---
    "juce/Source/animation/GltfLoader.h",
    "juce/Source/animation/GltfLoader.cpp",
    "juce/Source/animation/FbxLoader.h",
    "juce/Source/animation/FbxLoader.cpp",
    
    # --- Animation Rendering ---
    "juce/Source/animation/AnimationRenderer.h",
    "juce/Source/animation/AnimationRenderer.cpp",
    
    # --- Build Configuration ---
    "juce/CMakeLists.txt",
    
    # --- Animation Documentation & Guides ---

    
    # --- Core Engine (Animation Context) ---
    "juce/Source/audio/AudioEngine.h",
    "juce/Source/audio/AudioEngine.cpp",
    
    # --- UI Components (Animation Integration) ---
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    
    # --- Animation Preset Files ---
    "Synth_presets/animation.xml",
    "Synth_presets/animation_load.xml",
    "Synth_presets/animation_load2.xml",
    "Synth_presets/animation_glb_working.xml"
)

#--- Auto-discover additional animation-related files ---
$animationRoot = Join-Path $projectRoot "juce\Source\animation"
if (Test-Path $animationRoot) {
    $patterns = @('*.h','*.cpp','*.md')
    $discovered = Get-ChildItem -Path $animationRoot -Recurse -File -Include $patterns
    foreach ($item in $discovered) {
        $relativePath = $item.FullName.Substring($projectRoot.Length + 1).Replace('\','/')
        if (-not ($sourceFiles -contains $relativePath)) {
            $sourceFiles += $relativePath
        }
    }
}

#--- Include animation test/example files if they exist ---
$animationGltfDir = Join-Path $projectRoot "animation_gltf"
if (Test-Path $animationGltfDir) {
    # Include FBX and GLTF files metadata (list, don't read binary)
    $animFiles = Get-ChildItem -Path $animationGltfDir -File -Include @('*.fbx','*.glb','*.gltf') -Recurse
    if ($animFiles.Count -gt 0) {
        $animListContent = "`n" + ("=" * 80) + "`n"
        $animListContent += "ANIMATION FILES INVENTORY (animation_gltf/)`n"
        $animListContent += ("=" * 80) + "`n`n"
        foreach ($animFile in $animFiles) {
            $animListContent += "$($animFile.Name) ($($animFile.Length) bytes)`n"
        }
        Add-Content -Path $outputFile -Value $animListContent
    }
}

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Animation script..." -ForegroundColor Cyan
Write-Host "Archiving animation system files to: $outputFile"

# Add header to output file
$scriptHeader = @"
================================================================================
                    MOOFY ANIMATION SYSTEM ARCHIVE
================================================================================
This archive contains all files related to the Animation system including:
  - AnimationModuleProcessor (audio module integration)
  - Animation core system (Animator, AnimationBinder, etc.)
  - File loaders (GLTF, FBX)
  - Animation rendering
  - Animation preset files (.xml)
  - Related documentation and guides

Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
================================================================================


"@

Add-Content -Path $outputFile -Value $scriptHeader

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile
   
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

Write-Host "`nMoofy Animation script complete." -ForegroundColor Cyan
Write-Host "Total files processed: $($sourceFiles.Count)" -ForegroundColor Cyan
Write-Host "Output: $outputFile" -ForegroundColor Green

