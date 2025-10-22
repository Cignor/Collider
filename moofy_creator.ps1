#=============================================================
 #       MOOFY SCRIPT: JUCE Core Audio Debug (rev 2)
 #Archives the complete C++ audio pipeline and test harness,
 #including the new modular architecture components, for a
 #definitive code review and bug fix.
#=============================================================

#--- Configuration ---
 $projectRoot = $PSScriptRoot
 $outputFile = Join-Path $projectRoot "moofy_Preset_creator.txt"

#--- List of files relevant to the JUCE audio engine and test harness ---
 $sourceFiles = @(
  # --- Core Audio Graph & Engine ---
    "juce/Source/audio/graph/ModularSynthProcessor.h",
    "juce/Source/audio/graph/ModularSynthProcessor.cpp",
    "juce/Source/audio/graph/VoiceProcessor.cpp",
    "juce/Source/audio/graph/VoiceProcessor.h",
    "juce/Source/audio/AudioEngine.h",
    "juce/Source/audio/AudioEngine.cpp",
    
    # --- Audio Modules ---
"juce/Source/audio/modules/ADSRModuleProcessor.cpp",
"juce/Source/audio/modules/ADSRModuleProcessor.h",
"juce/Source/audio/modules/AttenuverterModuleProcessor.cpp",
"juce/Source/audio/modules/AttenuverterModuleProcessor.h",
"juce/Source/audio/modules/AudioInputModuleProcessor.cpp",
"juce/Source/audio/modules/AudioInputModuleProcessor.h",
"juce/Source/audio/modules/BestPracticeNodeProcessor.cpp",
"juce/Source/audio/modules/BestPracticeNodeProcessor.h",
"juce/Source/audio/modules/BestPracticeNodeProcessor.md",
"juce/Source/audio/modules/ChorusModuleProcessor.cpp",
"juce/Source/audio/modules/ChorusModuleProcessor.h",
"juce/Source/audio/modules/ClockDividerModuleProcessor.cpp",
"juce/Source/audio/modules/ClockDividerModuleProcessor.h",
"juce/Source/audio/modules/CommentModuleProcessor.cpp",
"juce/Source/audio/modules/CommentModuleProcessor.h",
"juce/Source/audio/modules/ComparatorModuleProcessor.cpp",
"juce/Source/audio/modules/ComparatorModuleProcessor.h",
"juce/Source/audio/modules/CompressorModuleProcessor.cpp",
"juce/Source/audio/modules/CompressorModuleProcessor.h",
"juce/Source/audio/modules/CVMixerModuleProcessor.cpp",
"juce/Source/audio/modules/CVMixerModuleProcessor.h",
"juce/Source/audio/modules/DebugModuleProcessor.cpp",
"juce/Source/audio/modules/DebugModuleProcessor.h",
"juce/Source/audio/modules/DeCrackleModuleProcessor.cpp",
"juce/Source/audio/modules/DeCrackleModuleProcessor.h",
"juce/Source/audio/modules/DelayModuleProcessor.cpp",
"juce/Source/audio/modules/DelayModuleProcessor.h",
"juce/Source/audio/modules/DriveModuleProcessor.cpp",
"juce/Source/audio/modules/DriveModuleProcessor.h",
"juce/Source/audio/modules/FunctionGeneratorModuleProcessor.cpp",
"juce/Source/audio/modules/FunctionGeneratorModuleProcessor.h",
"juce/Source/audio/modules/GateModuleProcessor.cpp",
"juce/Source/audio/modules/GateModuleProcessor.h",
"juce/Source/audio/modules/GranulatorModuleProcessor.cpp",
"juce/Source/audio/modules/GranulatorModuleProcessor.h",
"juce/Source/audio/modules/GraphicEQModuleProcessor.cpp",
"juce/Source/audio/modules/GraphicEQModuleProcessor.h",
"juce/Source/audio/modules/FrequencyGraphModuleProcessor.cpp",
"juce/Source/audio/modules/FrequencyGraphModuleProcessor.h",
"juce/Source/audio/modules/HarmonicShaperModuleProcessor.cpp",
"juce/Source/audio/modules/HarmonicShaperModuleProcessor.h",
"juce/Source/audio/modules/InputDebugModuleProcessor.cpp",
"juce/Source/audio/modules/InputDebugModuleProcessor.h",
"juce/Source/audio/modules/LagProcessorModuleProcessor.cpp",
"juce/Source/audio/modules/LagProcessorModuleProcessor.h",
"juce/Source/audio/modules/LFOModuleProcessor.cpp",
"juce/Source/audio/modules/LFOModuleProcessor.h",
"juce/Source/audio/modules/LimiterModuleProcessor.cpp",
"juce/Source/audio/modules/LimiterModuleProcessor.h",
"juce/Source/audio/modules/LogicModuleProcessor.cpp",
"juce/Source/audio/modules/LogicModuleProcessor.h",
"juce/Source/audio/modules/MapRangeModuleProcessor.cpp",
"juce/Source/audio/modules/MapRangeModuleProcessor.h",
"juce/Source/audio/modules/MathModuleProcessor.cpp",
"juce/Source/audio/modules/MathModuleProcessor.h",
"juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp",
"juce/Source/audio/modules/MIDIPlayerModuleProcessor.h",
"juce/Source/audio/modules/MIDICVModuleProcessor.cpp",
"juce/Source/audio/modules/MIDICVModuleProcessor.h",
"juce/Source/audio/modules/MixerModuleProcessor.cpp",
"juce/Source/audio/modules/MixerModuleProcessor.h",
"juce/Source/audio/modules/ModuleProcessor.cpp",
"juce/Source/audio/modules/ModuleProcessor.h",
"juce/Source/audio/modules/MultiBandShaperModuleProcessor.cpp",
"juce/Source/audio/modules/MultiBandShaperModuleProcessor.h",
"juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp",
"juce/Source/audio/modules/MultiSequencerModuleProcessor.h",
"juce/Source/audio/modules/NoiseModuleProcessor.cpp",
"juce/Source/audio/modules/NoiseModuleProcessor.h",
"juce/Source/audio/modules/PerformanceMonitor.h",
"juce/Source/audio/modules/PhaserModuleProcessor.cpp",
"juce/Source/audio/modules/PhaserModuleProcessor.h",
"juce/Source/audio/modules/PolyVCOModuleProcessor.cpp",
"juce/Source/audio/modules/PolyVCOModuleProcessor.h",
"juce/Source/audio/modules/QuantizerModuleProcessor.cpp",
"juce/Source/audio/modules/QuantizerModuleProcessor.h",
"juce/Source/audio/modules/RandomModuleProcessor.cpp",
"juce/Source/audio/modules/RandomModuleProcessor.h",
"juce/Source/audio/modules/RateModuleProcessor.cpp",
"juce/Source/audio/modules/RateModuleProcessor.h",
"juce/Source/audio/modules/RecordModuleProcessor.cpp",
"juce/Source/audio/modules/RecordModuleProcessor.h",
"juce/Source/audio/modules/ReverbModuleProcessor.cpp",
"juce/Source/audio/modules/ReverbModuleProcessor.h",
"juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp",
"juce/Source/audio/modules/SampleLoaderModuleProcessor.h",
"juce/Source/audio/modules/SAndHModuleProcessor.cpp",
"juce/Source/audio/modules/SAndHModuleProcessor.h",
"juce/Source/audio/modules/ScopeModuleProcessor.cpp",
"juce/Source/audio/modules/ScopeModuleProcessor.h",
"juce/Source/audio/modules/SequentialSwitchModuleProcessor.cpp",
"juce/Source/audio/modules/SequentialSwitchModuleProcessor.h",
"juce/Source/audio/modules/ShapingOscillatorModuleProcessor.cpp",
"juce/Source/audio/modules/ShapingOscillatorModuleProcessor.h",
"juce/Source/audio/modules/StepSequencerModuleProcessor.cpp",
"juce/Source/audio/modules/StepSequencerModuleProcessor.h",
"juce/Source/audio/modules/TimePitchModuleProcessor.cpp",
"juce/Source/audio/modules/TimePitchModuleProcessor.h",
"juce/Source/audio/modules/TimingData.h",
"juce/Source/audio/modules/TrackMixerModuleProcessor.cpp",
"juce/Source/audio/modules/TrackMixerModuleProcessor.h",
"juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp",
"juce/Source/audio/modules/TTSPerformerModuleProcessor.h",
"juce/Source/audio/modules/ValueModuleProcessor.cpp",
"juce/Source/audio/modules/ValueModuleProcessor.h",
"juce/Source/audio/modules/VCAModuleProcessor.cpp",
"juce/Source/audio/modules/VCAModuleProcessor.h",
"juce/Source/audio/modules/VCFModuleProcessor.cpp",
"juce/Source/audio/modules/VCFModuleProcessor.h",
"juce/Source/audio/modules/VCOModuleProcessor.cpp",
"juce/Source/audio/modules/VCOModuleProcessor.h",
"juce/Source/audio/modules/VocalTractFilterModuleProcessor.cpp",
"juce/Source/audio/modules/VocalTractFilterModuleProcessor.h",
"juce/Source/audio/modules/VstHostModuleProcessor.cpp",
"juce/Source/audio/modules/VstHostModuleProcessor.h",
"juce/Source/audio/modules/WaveshaperModuleProcessor.cpp",
"juce/Source/audio/modules/WaveshaperModuleProcessor.h",





"juce/Source/audio/voices/ModularVoice.h",
"juce/Source/audio/voices/NoiseVoiceProcessor.h",
"juce/Source/audio/voices/NoiseVoiceProcessor.cpp",
"juce/Source/audio/voices/SampleVoiceProcessor.cpp",
"juce/Source/audio/voices/SampleVoiceProcessor.h",
"juce/Source/audio/voices/SynthVoiceProcessor.h",
"juce/Source/audio/voices/SynthVoiceProcessor.cpp",
"juce/Source/audio/voices/TTSVoice.cpp",
"juce/Source/audio/voices/TTSVoice.h",

"juce/Source/audio/dsp/TimePitchProcessor.h",





    # --- Preset Creator UI ---
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.h",
    "juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp",
    "juce/Source/preset_creator/PinDatabase.h",
    "juce/Source/preset_creator/PinDatabase.cpp",
    "juce/Source/preset_creator/PresetManager.h",
    "juce/Source/preset_creator/PresetManager.cpp",
    "juce/Source/preset_creator/SampleManager.h",
    "juce/Source/preset_creator/SampleManager.cpp",
    "juce/Source/preset_creator/PresetCreatorApplication.h",
    "juce/Source/preset_creator/PresetCreatorComponent.h",
    "juce/Source/preset_creator/PresetCreatorComponent.cpp",    
	"juce/CMakeLists.txt",
    "juce/Source/preset_creator/PresetCreatorMain.cpp",
    "juce/Source/ui/TestHarnessComponent.cpp",
    "juce/Source/ui/MainComponent.cpp",
    "juce/Source/ui/MainComponent.h",
    "juce/Source/ui/VisualiserComponent.h",
    "juce/Source/ui/VisualiserComponent.cpp",
    "juce/Source/ui/TestHarnessComponent.h"
	
	


 )

#--- Script Execution ---

 if (Test-Path $outputFile) {
     Remove-Item $outputFile
 }

 Write-Host "Starting Moofy script for JUCE core debug..." -ForegroundColor Cyan
 Write-Host "Archiving specified source files to: $outputFile"

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

 Write-Host "Moofy script complete." -ForegroundColor Cyan