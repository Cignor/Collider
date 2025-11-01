#=============================================================
#       MOOFY VIDEO NODE: Architecture & Integration Guide
#       This script archives the files related to the
#       Video File Loader node and its integration points.
#       Send the generated text file to another expert to
#       communicate how the node works end-to-end.
#=============================================================

#--- Configuration ---
$projectRoot = $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_VideoNode.txt"

#--- Related Files (Video File Loader & Integration) ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: CORE VIDEO NODE IMPLEMENTATION
    #=============================================================
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",

    #=============================================================
    # SECTION 2: SHARED VIDEO FRAME MANAGER
    # Thread-safe singleton for publishing/consuming frames
    #=============================================================
    "juce/Source/video/VideoFrameManager.h",

    #=============================================================
    # SECTION 3: MODULE FACTORY REGISTRATION
    # Where the module type key is registered to the factory
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",

    #=============================================================
    # SECTION 4: PIN DATABASE & NODE EDITOR INTEGRATION
    # Pin definitions, UI buttons/menus, descriptions, and search
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 4b: COMPUTER VISION MODULES (Nodes)
    # Core CV source/processing nodes that use VideoFrameManager
    #=============================================================
    "juce/Source/audio/modules/WebcamLoaderModule.h",
    "juce/Source/audio/modules/WebcamLoaderModule.cpp",
    "juce/Source/audio/modules/MovementDetectorModule.h",
    "juce/Source/audio/modules/MovementDetectorModule.cpp",
    "juce/Source/audio/modules/HumanDetectorModule.h",
    "juce/Source/audio/modules/HumanDetectorModule.cpp",
    "juce/Source/audio/modules/PoseEstimatorModule.h",
    "juce/Source/audio/modules/PoseEstimatorModule.cpp",
    "juce/Source/audio/modules/HandTrackerModule.h",
    "juce/Source/audio/modules/HandTrackerModule.cpp",
    "juce/Source/audio/modules/FaceTrackerModule.h",
    "juce/Source/audio/modules/FaceTrackerModule.cpp",
    "juce/Source/audio/modules/ObjectDetectorModule.h",
    "juce/Source/audio/modules/ObjectDetectorModule.cpp",
    "juce/Source/audio/modules/ColorTrackerModule.h",
    "juce/Source/audio/modules/ColorTrackerModule.cpp",
    "juce/Source/audio/modules/ContourDetectorModule.h",
    "juce/Source/audio/modules/ContourDetectorModule.cpp",
    "juce/Source/audio/modules/SemanticSegmentationModule.h",
    "juce/Source/audio/modules/SemanticSegmentationModule.cpp",

    #=============================================================
    # SECTION 5: BUILD SYSTEM (OpenCV/JUCE linkage reference)
    # Useful to understand dependencies for the video pipeline
    #=============================================================
    "juce/CMakeLists.txt"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Video Node script..." -ForegroundColor Cyan
Write-Host "Archiving Video Node related files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    # Detect section headers in comments
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue  # Skip comment lines
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


