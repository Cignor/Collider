#=============================================================
#       MOOFY ESSENTIALS: Architecture & Integration Guide
#       This script archives the CORE architecture files needed
#       to understand how to implement complex nodes and systems.
#       Perfect for AI assistants or developers discovering the project.
#=============================================================

#--- Configuration ---
$projectRoot = $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Essentials.txt"

#--- Essential Architecture Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: ARCHITECTURE DOCUMENTATION
    # Read these first to understand the system design
    #=============================================================
    "architecture/00_PROJECT_OVERVIEW.md",
    "architecture/01_CORE_GAME_ENGINE.md",
    "architecture/02_AUDIO_SYSTEM.md",
    "architecture/README.md",
    
    #=============================================================
    # SECTION 2: CORE AUDIO GRAPH & ENGINE
    # The heart of the modular synthesis system
    #=============================================================
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/graph/VoiceProcessor.h",
    "juce/Source/audio/graph/VoiceProcessor.cpp",
    "juce/Source/audio/AudioEngine.h",
    "juce/Source/audio/AudioEngine.cpp",
    
    #=============================================================
    # SECTION 3: BASE MODULE ARCHITECTURE
    # The foundation all nodes inherit from
    #=============================================================
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 4: EXAMPLE NODES - SIMPLE TO COMPLEX
    # Study these to understand node implementation patterns
    #=============================================================
    
    # Example 1: Best Practice Template (START HERE!)
    "juce/Source/audio/modules/BestPracticeNodeProcessor.h",
    "juce/Source/audio/modules/BestPracticeNodeProcessor.cpp",
    
    # Example 2: Simple Utility Node (demonstrates basic I/O)
    "juce/Source/audio/modules/ValueModuleProcessor.h",
    "juce/Source/audio/modules/ValueModuleProcessor.cpp",
    
    # Example 3: Complex Processing Node (demonstrates state management)
    "juce/Source/audio/modules/LFOModuleProcessor.h",
    "juce/Source/audio/modules/LFOModuleProcessor.cpp",
    
    # Example 4: Advanced Multi-Pin Node (demonstrates complex routing)
    "juce/Source/audio/modules/MixerModuleProcessor.h",
    "juce/Source/audio/modules/MixerModuleProcessor.cpp",
    
    # Example 5: External Library Integration (SoundTouch example)
    "juce/Source/audio/modules/TimePitchModuleProcessor.h",
    "juce/Source/audio/modules/TimePitchModuleProcessor.cpp",
    
    # Example 6: Complex State & Timing (shows transport integration)
    "juce/Source/audio/modules/StepSequencerModuleProcessor.h",
    "juce/Source/audio/modules/StepSequencerModuleProcessor.cpp",
    
    # Example 7: Animation System Integration (STUDY THIS for OpenCV!)
    # This shows how to integrate a complex external system (GLTF/FBX animation)
    "juce/Source/audio/modules/AnimationModuleProcessor.h",
    "juce/Source/audio/modules/AnimationModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 5: ANIMATION SYSTEM (Complete External System Example)
    # This is a COMPLETE example of integrating a complex system
    # Study this pattern for OpenCV integration!
    #=============================================================
    "juce/Source/animation/AnimationData.h",
    "juce/Source/animation/AnimationRenderer.h",
    "juce/Source/animation/AnimationRenderer.cpp",
    "juce/Source/animation/Animator.h",
    "juce/Source/animation/Animator.cpp",
    "juce/Source/animation/GltfLoader.h",
    "juce/Source/animation/GltfLoader.cpp",
    "juce/Source/animation/FbxLoader.h",
    "juce/Source/animation/FbxLoader.cpp",
    "juce/Source/animation/RawAnimationData.h",
    "juce/Source/animation/AnimationBinder.h",
    "juce/Source/animation/AnimationBinder.cpp",
    
    #=============================================================
    # SECTION 6: TIMING & TRANSPORT SYSTEM
    # Essential for synchronized processing
    #=============================================================
    "juce/Source/audio/modules/TimingData.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.h",
    "juce/Source/audio/modules/TempoClockModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 7: NODE EDITOR UI & PIN SYSTEM
    # How nodes are created, displayed, and connected
    #=============================================================
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/SampleManager.h",
    "juce/Source/preset_creator/SampleManager.cpp",
    
    #=============================================================
    # SECTION 8: APPLICATION STRUCTURE
    # How everything is initialized and connected
    #=============================================================
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",
    
    #=============================================================
    # SECTION 9: UI COMPONENTS
    # Visualization and testing infrastructure
    #=============================================================
    "juce/Source/ui/MainComponent.h",
    "juce/Source/ui/MainComponent.cpp",
    "juce/Source/ui/VisualiserComponent.h",
    "juce/Source/ui/VisualiserComponent.cpp",
    "juce/Source/ui/TestHarnessComponent.h",
    "juce/Source/ui/TestHarnessComponent.cpp",
    
    #=============================================================
    # SECTION 10: BUILD SYSTEM
    # Understanding dependencies and linking
    #=============================================================
    "juce/CMakeLists.txt",
    
    #=============================================================
    # SECTION 11: KEY INTEGRATION GUIDES
    # Step-by-step guides for complex integrations
    #=============================================================
    "guides/IMGUI_NODE_DESIGN_GUIDE.md",
    "guides/XML_SAVING_AND_LOADING_HOW_TO.md",
    "guides/UNDO_REDO_SYSTEM_GUIDE.md",
    "guides/TEMPO_CLOCK_INTEGRATION_GUIDE.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Essentials script..." -ForegroundColor Cyan
Write-Host "Archiving essential architecture files to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
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

