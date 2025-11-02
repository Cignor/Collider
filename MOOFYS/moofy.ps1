# =============================================================
#         MOOFY SCRIPT: JUCE Core Audio Debug (rev 2)
#  Archives the complete C++ audio pipeline and test harness,
#  including the new modular architecture components, for a
#  definitive code review and bug fix.
# =============================================================

# --- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "moofy_Collider.txt"

# --- List of files relevant to the JUCE audio engine and test harness ---
$sourceFiles = @(
    # --- Core Engine ---
    "juce\Source\audio\AudioEngine.h",
    "juce\Source\audio\AudioEngine.cpp",

    # --- New Modular Synth Container ---
    "juce\Source\audio\graph\ModularSynthProcessor.h",
    "juce\Source\audio\graph\ModularSynthProcessor.cpp",
	"juce\Source\audio\graph\VoiceProcessor.cpp",
    "juce\Source\audio\graph\VoiceProcessor.cpp",


    # --- New Modular Synth Modules ---
    "juce\Source\audio\modules\ModuleProcessor.h",
    "juce\Source\audio\modules\VCOModuleProcessor.h",
    "juce\Source\audio\modules\VCOModuleProcessor.cpp",
    "juce\Source\audio\modules\VCFModuleProcessor.h",
    "juce\Source\audio\modules\VCFModuleProcessor.cpp",
    "juce\Source\audio\modules\VCAModuleProcessor.h",
    "juce\Source\audio\modules\VCAModuleProcessor.cpp",

    # --- New Modular Voice Adapter ---
    "juce\Source\audio\voices\ModularVoice.h",

    # --- Original Voices and Processors ---
	
    "juce\Source\audio\graph\ModularVoice.h",
    "juce\Source\audio\voices\NoiseVoiceProcessor.h",
    "juce\Source\audio\voices\NoiseVoiceProcessor.cpp",
    "juce\Source\audio\voices\SampleVoiceProcessor.h",
    "juce\Source\audio\voices\SampleVoiceProcessor.cpp",
    "juce\Source\audio\voices\SynthVoiceProcessor.h",
    "juce\Source\audio\voices\SynthVoiceProcessor.cpp",
    
    # --- Assets and FX ---
    "juce\Source\audio\assets\SampleBank.h",
    "juce\Source\audio\assets\SampleBank.cpp",
    "juce\Source\audio\fx\FXChain.h",
    "juce\Source\audio\fx\FXChain.cpp",
    "juce\Source\audio\fx\GainProcessor.h",
    "juce\Source\audio\fx\GainProcessor.cpp",
    "juce\Source\audio\utils\VoiceDeletionUtils.h",
    
    # --- Test Harness UI ---
    "juce\Source\ui\TestHarnessComponent.h",
    "juce\Source\ui\TestHarnessComponent.cpp"
)

# --- Script Execution ---

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