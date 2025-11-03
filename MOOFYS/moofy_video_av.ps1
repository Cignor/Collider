#=============================================================
#       MOOFY: Video Loader + Audio Sync + FX (JUCE/OpenCV)
#       Archives all relevant source files for external helpers
#       to get full context on video playback, A/V sync, and FX.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Video_AV.txt"

#--- Relevant Files ---
$sourceFiles = @(
    # Focus modules
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",
    "juce/Source/audio/modules/VideoFXModule.h",
    "juce/Source/audio/modules/VideoFXModule.cpp",

    # Frame pub/sub
    "juce/Source/video/VideoFrameManager.h",
    "juce/Source/video/VideoFrameManager.cpp",

    # Base module infra
    "juce/Source/audio/modules/ModuleProcessor.h",
    "juce/Source/audio/modules/ModuleProcessor.cpp",

    # Time/pitch and engines (include candidates; script will warn if missing)
    "juce/Source/audio/modules/TimePitchModuleProcessor.h",
    "juce/Source/audio/modules/TimePitchModuleProcessor.cpp",
    "juce/Source/audio/time_pitch/TimePitchProcessor.h",
    "juce/Source/audio/time_pitch/TimePitchProcessor.cpp",

    # FFmpeg audio reader (multiple likely locations)
    "juce/Source/audio/ffmpeg/FFmpegAudioReader.h",
    "juce/Source/audio/ffmpeg/FFmpegAudioReader.cpp",
    "juce/Source/audio/codecs/FFmpegAudioReader.h",
    "juce/Source/audio/codecs/FFmpegAudioReader.cpp",

    # Build system for linkage context
    "juce/CMakeLists.txt"
)

#--- Script Execution ---
if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Video/AV script..." -ForegroundColor Cyan
Write-Host "Archiving video + audio sync + FX context to: $outputFile"

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile

    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) { continue }

    if (Test-Path $fullPath) {
        $header = "`n" + ("=" * 80) + "`n"
        $header += "FILE: $normalizedFile`n"
        $header += ("=" * 80) + "`n`n"

        Add-Content -Path $outputFile -Value $header
        Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile

        Write-Host " -> Archived: $normalizedFile" -ForegroundColor Green
    }
    else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: File not found: $normalizedFile" -ForegroundColor Yellow
    }
}


