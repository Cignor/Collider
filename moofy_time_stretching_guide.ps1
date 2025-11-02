#=============================================================
#       MOOFY TIME-STRETCHING GUIDE: Naive vs RubberBand
#       This script documents how to implement dual-engine
#       time-stretching (Naive & RubberBand) in audio modules.
#       Based on SampleLoaderModuleProcessor pattern.
#=============================================================

#--- Configuration ---
$projectRoot = $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_TimeStretching_Pattern.txt"

#--- Essential Time-Stretching Files ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: CORE TIME-STRETCHING ARCHITECTURE
    # The foundation: TimePitchProcessor with dual engines
    #=============================================================
    "juce/Source/audio/dsp/TimePitchProcessor.h",
    
    #=============================================================
    # SECTION 2: EXAMPLE IMPLEMENTATION - SampleLoaderModuleProcessor
    # Complete working example showing both engines in action
    #=============================================================
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.h",
    "juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 3: VOICE PROCESSOR PATTERN
    # How SampleVoiceProcessor branches between engines
    #=============================================================
    "juce/Source/audio/voices/SampleVoiceProcessor.h",
    "juce/Source/audio/voices/SampleVoiceProcessor.cpp",
    
    #=============================================================
    # SECTION 4: TIME-STRETCHER WRAPPER
    # How to wrap AudioSource with time-stretching
    #=============================================================
    "juce/Source/audio/modules/TimeStretcherAudioSource.h",
    "juce/Source/audio/modules/TimeStretcherAudioSource.cpp",
    
    #=============================================================
    # SECTION 5: ANOTHER EXAMPLE - TimePitchModuleProcessor
    # Shows direct TimePitchProcessor usage (no voice wrapper)
    #=============================================================
    "juce/Source/audio/modules/TimePitchModuleProcessor.h",
    "juce/Source/audio/modules/TimePitchModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 6: VIDEO FILE LOADER MODULE (ISSUE: Buffer/Creaking Audio)
    # VideoFileLoaderModule with FFmpegAudioReader - experiencing
    # buffer management and audio creaking problems that need expert review
    #=============================================================
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",
    "juce/Source/audio/modules/FFmpegAudioReader.h",
    "juce/Source/audio/modules/FFmpegAudioReader.cpp"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Time-Stretching Guide script..." -ForegroundColor Cyan
Write-Host "Archiving time-stretching pattern files to: $outputFile"

# Add header documentation
$header = @"
================================================================================
MOOFY TIME-STRETCHING PATTERN GUIDE: Naive vs RubberBand Engine Selection
================================================================================

This document explains how to implement dual-engine time-stretching in audio
modules, allowing users to choose between:

1. RUBBERBAND: High-quality, phase-aware time-stretching using RubberBand library
   - Best for: Musical content, preserving transients, smooth pitch changes
   - Characteristics: Higher CPU usage, lower latency artifacts, phase-coherent
   
2. NAIVE: Simple linear interpolation-based time-stretching
   - Best for: Low CPU usage, real-time performance, simple use cases
   - Characteristics: Very low CPU, minimal latency, may introduce artifacts

================================================================================
ARCHITECTURE OVERVIEW
================================================================================

The pattern uses a unified TimePitchProcessor facade that internally manages
two separate engines:

  TimePitchProcessor
    ├─ RubberBandEngine (uses RubberBand library)
    └─ FifoEngine (naive linear interpolation)

Both engines share the same API:
  - setTimeStretchRatio(double ratio)
  - setPitchSemitones(double semis)
  - putInterleaved(input, frames)
  - receiveInterleaved(output, framesRequested)

The mode is switched via:
  - timePitch.setMode(TimePitchProcessor::Mode::RubberBand);
  - timePitch.setMode(TimePitchProcessor::Mode::Fifo);

================================================================================
IMPLEMENTATION STEPS
================================================================================

STEP 1: Add Engine Parameter to APVTS
  - Add AudioParameterChoice with values ["RubberBand", "Naive"]
  - Default typically 0 (RubberBand) or 1 (Naive) depending on preference
  - Example:
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "engine", "Engine", juce::StringArray { "RubberBand", "Naive" }, 1));

STEP 2: Create/Update Time-Stretching Component
  - If wrapping AudioSource: Use TimeStretcherAudioSource pattern
  - If direct processing: Use TimePitchProcessor directly
  - Store TimePitchProcessor instance as member

STEP 3: Initialize in prepareToPlay()
  - Call timePitch.prepare(sampleRate, numChannels, blockSize)
  - Set initial mode based on parameter

STEP 4: Process Block - Branch by Engine
  Option A: Direct TimePitchProcessor (like TimePitchModuleProcessor)
    - Read engine parameter
    - Call timePitch.setMode() if changed
    - Use putInterleaved/receiveInterleaved pattern

  Option B: AudioSource Wrapper (like TimeStretcherAudioSource)
    - Add setMode() method to wrapper
    - Delegate mode setting to internal TimePitchProcessor
    - Update wrapper when engine parameter changes

STEP 5: Update UI
  - Add ComboBox/dropdown for engine selection
  - Optionally show RubberBand-specific options when RubberBand selected:
    - rbWindowShort (bool)
    - rbPhaseInd (bool)

STEP 6: Handle Mode Switching
  - Reset TimePitchProcessor when mode changes
  - Re-prime if needed (RubberBand needs initial frames)

================================================================================
KEY DIFFERENCES: RUBBERBAND vs NAIVE
================================================================================

RUBBERBAND (TimePitchProcessor::Mode::RubberBand):
  - Uses RubberBand::RubberBandStretcher library
  - Phase-aware processing
  - Better quality for musical content
  - Requires priming (initial frames) before output
  - Configurable options:
    * Window size (short/standard)
    * Phase independence
  - Higher CPU usage

NAIVE (TimePitchProcessor::Mode::Fifo):
  - Simple linear interpolation
  - Reads from FIFO buffer at variable rate
  - No priming needed
  - Lower CPU usage
  - May introduce artifacts on complex signals
  - Suitable for simple use cases

================================================================================
USAGE EXAMPLES
================================================================================

Example 1: SampleLoaderModuleProcessor Pattern
  - Uses SampleVoiceProcessor wrapper
  - SampleVoiceProcessor has Engine enum
  - Branches in renderBlock() based on engine
  - Naive path: Direct linear interpolation in renderBlock
  - RubberBand path: Uses TimePitchProcessor via interleaved buffers

Example 2: TimeStretcherAudioSource Pattern
  - Wraps PositionableAudioSource
  - Internal TimePitchProcessor handles both modes
  - Currently hardcoded to RubberBand (needs update to support selection)

Example 3: TimePitchModuleProcessor Pattern
  - Direct TimePitchProcessor usage
  - Engine selection via UI parameter
  - Mode switching handled in processBlock()

================================================================================
SECTION 6: VIDEO FILE LOADER MODULE (REQUIRES EXPERT REVIEW)
================================================================================

The VideoFileLoaderModule is experiencing buffer management and audio creaking
issues. This module:

- Uses FFmpegAudioReader to decode audio from video files
- Processes audio through time-stretching engines
- Has reported issues with:
  * Buffer underruns/overruns
  * Audio creaking/clicking artifacts
  * Synchronization problems

Files included for expert review:
  - VideoFileLoaderModule.h/cpp: Main module implementation
  - FFmpegAudioReader.h/cpp: FFmpeg audio decoding wrapper

================================================================================
"@

Add-Content -Path $outputFile -Value $header

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

Write-Host "`nTime-Stretching Guide created successfully!" -ForegroundColor Green
Write-Host "Output: $outputFile" -ForegroundColor Cyan

