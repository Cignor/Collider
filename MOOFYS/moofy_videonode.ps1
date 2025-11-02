#=============================================================
#       MOOFY COMPUTER VISION: Complete Architecture Guide
#       This script archives ALL files related to computer vision,
#       OpenCV integration, and video processing in the project.
#       Send the generated text file to an external expert to
#       understand the complete CV system architecture.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_VideoNode.txt"

#--- Related Files (Complete Computer Vision System) ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: CORE VIDEO SOURCE MODULES
    # Video input sources (file and webcam)
    #=============================================================
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",
    "juce/Source/audio/modules/WebcamLoaderModule.h",
    "juce/Source/audio/modules/WebcamLoaderModule.cpp",

    #=============================================================
    # SECTION 2: BASE CLASSES & INFRASTRUCTURE
    # Foundation classes for CV modules and video frame management
    #=============================================================
    "juce/Source/audio/modules/OpenCVModuleProcessor.h",
    "juce/Source/video/VideoFrameManager.h",
    "juce/Source/video/CameraEnumerator.h",
    "juce/Source/audio/modules/ModuleProcessor.h",

    #=============================================================
    # SECTION 3: DETECTION & TRACKING MODULES
    # Object, face, hand, pose, and human detection/tracking
    #=============================================================
    "juce/Source/audio/modules/ObjectDetectorModule.h",
    "juce/Source/audio/modules/ObjectDetectorModule.cpp",
    "juce/Source/audio/modules/FaceTrackerModule.h",
    "juce/Source/audio/modules/FaceTrackerModule.cpp",
    "juce/Source/audio/modules/HandTrackerModule.h",
    "juce/Source/audio/modules/HandTrackerModule.cpp",
    "juce/Source/audio/modules/PoseEstimatorModule.h",
    "juce/Source/audio/modules/PoseEstimatorModule.cpp",
    "juce/Source/audio/modules/HumanDetectorModule.h",
    "juce/Source/audio/modules/HumanDetectorModule.cpp",

    #=============================================================
    # SECTION 4: MOVEMENT & OPTICAL FLOW MODULES
    # Motion detection and tracking
    #=============================================================
    "juce/Source/audio/modules/MovementDetectorModule.h",
    "juce/Source/audio/modules/MovementDetectorModule.cpp",
    "juce/Source/audio/modules/MovementDetectorModuleProcessor.h",

    #=============================================================
    # SECTION 5: IMAGE PROCESSING MODULES
    # Color tracking, contour detection, segmentation
    #=============================================================
    "juce/Source/audio/modules/ColorTrackerModule.h",
    "juce/Source/audio/modules/ColorTrackerModule.cpp",
    "juce/Source/audio/modules/ContourDetectorModule.h",
    "juce/Source/audio/modules/ContourDetectorModule.cpp",
    "juce/Source/audio/modules/SemanticSegmentationModule.h",
    "juce/Source/audio/modules/SemanticSegmentationModule.cpp",

    #=============================================================
    # SECTION 6: VIDEO EFFECTS & PROCESSING
    # Video FX and post-processing modules
    #=============================================================
    "juce/Source/audio/modules/VideoFXModule.h",
    "juce/Source/audio/modules/VideoFXModule.cpp",
    "juce/Source/audio/modules/CropVideoModule.h",
    "juce/Source/audio/modules/CropVideoModule.cpp",

    #=============================================================
    # SECTION 7: MODULE FACTORY REGISTRATION
    # Where module types are registered to the factory system
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",

    #=============================================================
    # SECTION 8: PIN DATABASE & NODE EDITOR INTEGRATION
    # Pin definitions, UI buttons/menus, descriptions, and search
    # This is where CV modules are made available in the node editor
    #=============================================================
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",

    #=============================================================
    # SECTION 9: BUILD SYSTEM & DEPENDENCIES
    # OpenCV/JUCE linkage, CUDA support, CMake configuration
    #=============================================================
    "juce/CMakeLists.txt",
    "juce/opencv_cuda13_npp.patch",

    #=============================================================
    # SECTION 10: DOCUMENTATION & GUIDES
    # Architecture documentation and implementation guides
    #=============================================================
    "guides/CUDA_GPU_ACCELERATION_IMPLEMENTATION_GUIDE.md",
    "guides/CUDA_GPU_IMPLEMENTATION_SUMMARY.md",
    "guides/OPENCV_CUDA_BUILD_CACHE_GUIDE.md",
    "guides/OPENCV_CACHE_QUICK_REFERENCE.md",
    "CUDA_OPENCV_INTEGRATION_SUMMARY.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Computer Vision Archive script..." -ForegroundColor Cyan
Write-Host "Archiving ALL Computer Vision related files to: $outputFile"
Write-Host "This includes:" -ForegroundColor Yellow
Write-Host "  - All CV modules (detection, tracking, processing)" -ForegroundColor Yellow
Write-Host "  - Infrastructure (VideoFrameManager, OpenCV base classes)" -ForegroundColor Yellow
Write-Host "  - Build system configuration (OpenCV/CUDA)" -ForegroundColor Yellow
Write-Host "  - Documentation and implementation guides" -ForegroundColor Yellow

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


