#=============================================================
#       MOOFY: TimeStretch Diagnostics Pack
#       Collects all artifacts needed to debug TimePitch issues.
#       Use when helpers need the full timestretch context.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile  = Join-Path $projectRoot "moofy_timestretch_diagnostics.txt"

#--- Target Files ---
$sourceFiles = @(
    #=========================================================
    # Core DSP plumbing
    #=========================================================
    "juce/Source/audio/dsp/TimePitchProcessor.h",
    "juce/Source/audio/modules/TimePitchModuleProcessor.h",
    "juce/Source/audio/modules/TimePitchModuleProcessor.cpp",

    #=========================================================
    # Reference implementation (video loader)
    #=========================================================
    "juce/Source/audio/modules/VideoFileLoaderModule.h",
    "juce/Source/audio/modules/VideoFileLoaderModule.cpp",

    #=========================================================
    # Audio extraction utilities
    #=========================================================
    "juce/Source/audio/modules/FFmpegAudioReader.h",
    "juce/Source/audio/modules/FFmpegAudioReader.cpp",

    #=========================================================
    # Helpful guides / notes
    #=========================================================
    "guides/SHORTCUT_SYSTEM_SCAN.md",          # project level scan info
    "architecture/02_AUDIO_SYSTEM.md"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting TimeStretch Moofy script..." -ForegroundColor Cyan
Write-Host "Exporting diagnostics to: $outputFile"

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
        Write-Host " -> WARNING: Missing: $normalizedFile" -ForegroundColor Yellow
    }
}

