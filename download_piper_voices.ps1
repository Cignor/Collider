# Piper TTS Voice Downloader Script
# Downloads multiple voices from the official Piper TTS repository
# Stores them in the specified models directory

param(
    [string]$ModelsDir = "H:\0000_CODE\01_collider_pyo\juce\build\PresetCreatorApp_artefacts\Release\models"
)

# Ensure models directory exists
if (!(Test-Path $ModelsDir)) {
    New-Item -ItemType Directory -Path $ModelsDir -Force
    Write-Host "Created models directory: $ModelsDir" -ForegroundColor Green
}

# Base URL for Piper TTS models
$BaseUrl = "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0"

# Comprehensive list of available voices with their characteristics
$Voices = @(
    # English (US) - Various accents and styles
    @{ Name = "en_US-lessac-medium"; Language = "English (US)"; Accent = "General American"; Gender = "Female"; Quality = "Medium" },
    @{ Name = "en_US-lessac-high"; Language = "English (US)"; Accent = "General American"; Gender = "Female"; Quality = "High" },
    @{ Name = "en_US-lessac-low"; Language = "English (US)"; Accent = "General American"; Gender = "Female"; Quality = "Low" },
    @{ Name = "en_US-libritts-high"; Language = "English (US)"; Accent = "General American"; Gender = "Male"; Quality = "High" },
    @{ Name = "en_US-libritts-medium"; Language = "English (US)"; Accent = "General American"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "en_US-libritts-low"; Language = "English (US)"; Accent = "General American"; Gender = "Male"; Quality = "Low" },
    @{ Name = "en_US-vctk-medium"; Language = "English (US)"; Accent = "Various"; Gender = "Mixed"; Quality = "Medium" },
    
    # English (UK) - British accents
    @{ Name = "en_GB-alan-medium"; Language = "English (UK)"; Accent = "British"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "en_GB-alan-high"; Language = "English (UK)"; Accent = "British"; Gender = "Male"; Quality = "High" },
    @{ Name = "en_GB-southern_english_female-medium"; Language = "English (UK)"; Accent = "Southern British"; Gender = "Female"; Quality = "Medium" },
    
    # English (AU) - Australian accent
    @{ Name = "en_AU-shmale-medium"; Language = "English (AU)"; Accent = "Australian"; Gender = "Male"; Quality = "Medium" },
    
    # German voices
    @{ Name = "de_DE-thorsten-medium"; Language = "German"; Accent = "Standard German"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "de_DE-thorsten-high"; Language = "German"; Accent = "Standard German"; Gender = "Male"; Quality = "High" },
    @{ Name = "de_DE-thorsten-low"; Language = "German"; Accent = "Standard German"; Gender = "Male"; Quality = "Low" },
    @{ Name = "de_DE-ramona-medium"; Language = "German"; Accent = "Standard German"; Gender = "Female"; Quality = "Medium" },
    
    # Spanish voices
    @{ Name = "es_ES-davefx-medium"; Language = "Spanish (Spain)"; Accent = "Castilian"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "es_ES-davefx-high"; Language = "Spanish (Spain)"; Accent = "Castilian"; Gender = "Male"; Quality = "High" },
    @{ Name = "es_MX-claudio-medium"; Language = "Spanish (Mexico)"; Accent = "Mexican"; Gender = "Male"; Quality = "Medium" },
    
    # French voices
    @{ Name = "fr_FR-siwis-medium"; Language = "French"; Accent = "Standard French"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "fr_FR-siwis-high"; Language = "French"; Accent = "Standard French"; Gender = "Male"; Quality = "High" },
    @{ Name = "fr_FR-siwis-low"; Language = "French"; Accent = "Standard French"; Gender = "Male"; Quality = "Low" },
    @{ Name = "fr_FR-siwis_female-medium"; Language = "French"; Accent = "Standard French"; Gender = "Female"; Quality = "Medium" },
    
    # Italian voices
    @{ Name = "it_IT-riccardo-medium"; Language = "Italian"; Accent = "Standard Italian"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "it_IT-riccardo-high"; Language = "Italian"; Accent = "Standard Italian"; Gender = "Male"; Quality = "High" },
    
    # Portuguese voices
    @{ Name = "pt_BR-faber-medium"; Language = "Portuguese (Brazil)"; Accent = "Brazilian"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "pt_BR-faber-high"; Language = "Portuguese (Brazil)"; Accent = "Brazilian"; Gender = "Male"; Quality = "High" },
    
    # Dutch voices
    @{ Name = "nl_NL-mls-medium"; Language = "Dutch"; Accent = "Standard Dutch"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "nl_NL-mls-high"; Language = "Dutch"; Accent = "Standard Dutch"; Gender = "Male"; Quality = "High" },
    
    # Russian voices
    @{ Name = "ru_RU-dmitri-medium"; Language = "Russian"; Accent = "Standard Russian"; Gender = "Male"; Quality = "Medium" },
    @{ Name = "ru_RU-dmitri-high"; Language = "Russian"; Accent = "Standard Russian"; Gender = "Male"; Quality = "High" },
    
    # Chinese voices
    @{ Name = "zh_CN-huayan-medium"; Language = "Chinese (Mandarin)"; Accent = "Standard Mandarin"; Gender = "Female"; Quality = "Medium" },
    @{ Name = "zh_CN-huayan-high"; Language = "Chinese (Mandarin)"; Accent = "Standard Mandarin"; Gender = "Female"; Quality = "High" },
    
    # Japanese voices
    @{ Name = "ja_JP-ljspeech-medium"; Language = "Japanese"; Accent = "Standard Japanese"; Gender = "Female"; Quality = "Medium" },
    @{ Name = "ja_JP-ljspeech-high"; Language = "Japanese"; Accent = "Standard Japanese"; Gender = "Female"; Quality = "High" },
    
    # Korean voices
    @{ Name = "ko_KR-kss-medium"; Language = "Korean"; Accent = "Standard Korean"; Gender = "Female"; Quality = "Medium" },
    
    # Polish voices
    @{ Name = "pl_PL-darkman-medium"; Language = "Polish"; Accent = "Standard Polish"; Gender = "Male"; Quality = "Medium" },
    
    # Czech voices
    @{ Name = "cs_CZ-jirka-medium"; Language = "Czech"; Accent = "Standard Czech"; Gender = "Male"; Quality = "Medium" },
    
    # Greek voices
    @{ Name = "el_GR-rapunzelina-medium"; Language = "Greek"; Accent = "Standard Greek"; Gender = "Female"; Quality = "Medium" },
    
    # Finnish voices
    @{ Name = "fi_FI-harri-medium"; Language = "Finnish"; Accent = "Standard Finnish"; Gender = "Male"; Quality = "Medium" },
    
    # Swedish voices
    @{ Name = "sv_SE-nst-medium"; Language = "Swedish"; Accent = "Standard Swedish"; Gender = "Male"; Quality = "Medium" },
    
    # Norwegian voices
    @{ Name = "nb_NO-talesyntese-medium"; Language = "Norwegian"; Accent = "Standard Norwegian"; Gender = "Male"; Quality = "Medium" },
    
    # Danish voices
    @{ Name = "da_DK-talesyntese-medium"; Language = "Danish"; Accent = "Standard Danish"; Gender = "Male"; Quality = "Medium" }
)

# Function to download a file with progress
function Download-FileWithProgress {
    param(
        [string]$Url,
        [string]$OutputPath,
        [string]$Description
    )
    
    Write-Host "Downloading $Description..." -ForegroundColor Yellow
    Write-Host "URL: $Url" -ForegroundColor Gray
    Write-Host "Output: $OutputPath" -ForegroundColor Gray
    
    try {
        # Use Invoke-WebRequest with progress
        $ProgressPreference = 'Continue'
        Invoke-WebRequest -Uri $Url -OutFile $OutputPath -UseBasicParsing
        Write-Host "✓ Downloaded $Description" -ForegroundColor Green
        return $true
    }
    catch {
        Write-Host "✗ Failed to download $Description" -ForegroundColor Red
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Function to get file size in MB
function Get-FileSizeMB {
    param([string]$Path)
    if (Test-Path $Path) {
        return [math]::Round((Get-Item $Path).Length / 1MB, 2)
    }
    return 0
}

# Main download loop
$TotalVoices = $Voices.Count
$DownloadedCount = 0
$SkippedCount = 0
$FailedCount = 0
$TotalSizeMB = 0

Write-Host "=== Piper TTS Voice Downloader ===" -ForegroundColor Cyan
Write-Host "Target Directory: $ModelsDir" -ForegroundColor Cyan
Write-Host "Total Voices: $TotalVoices" -ForegroundColor Cyan
Write-Host ""

# Create a summary file
$SummaryPath = Join-Path $ModelsDir "voice_summary.txt"
$SummaryContent = @"
Piper TTS Voice Collection Summary
Generated: $(Get-Date)
Total Voices: $TotalVoices

"@

foreach ($Voice in $Voices) {
    $VoiceName = $Voice.Name
    $OnnxUrl = "$BaseUrl/$VoiceName.onnx"
    $JsonUrl = "$BaseUrl/$VoiceName.onnx.json"
    
    $OnnxPath = Join-Path $ModelsDir "$VoiceName.onnx"
    $JsonPath = Join-Path $ModelsDir "$VoiceName.onnx.json"
    
    Write-Host "[$($DownloadedCount + $SkippedCount + $FailedCount + 1)/$TotalVoices] Processing: $VoiceName" -ForegroundColor White
    
    $OnnxExists = Test-Path $OnnxPath
    $JsonExists = Test-Path $JsonPath
    
    if ($OnnxExists -and $JsonExists) {
        Write-Host "  ✓ Already exists, skipping..." -ForegroundColor Green
        $SkippedCount++
        $TotalSizeMB += Get-FileSizeMB $OnnxPath
        $TotalSizeMB += Get-FileSizeMB $JsonPath
    }
    else {
        $OnnxSuccess = $true
        $JsonSuccess = $true
        
        # Download ONNX file
        if (!$OnnxExists) {
            $OnnxSuccess = Download-FileWithProgress -Url $OnnxUrl -OutputPath $OnnxPath -Description "$VoiceName.onnx"
        }
        
        # Download JSON config file
        if (!$JsonExists) {
            $JsonSuccess = Download-FileWithProgress -Url $JsonUrl -OutputPath $JsonPath -Description "$VoiceName.onnx.json"
        }
        
        if ($OnnxSuccess -and $JsonSuccess) {
            $DownloadedCount++
            $TotalSizeMB += Get-FileSizeMB $OnnxPath
            $TotalSizeMB += Get-FileSizeMB $JsonPath
        }
        else {
            $FailedCount++
            # Clean up partial downloads
            if (Test-Path $OnnxPath) { Remove-Item $OnnxPath -Force }
            if (Test-Path $JsonPath) { Remove-Item $JsonPath -Force }
        }
    }
    
    # Add to summary
    $SummaryContent += "`n$VoiceName`n"
    $SummaryContent += "  Language: $($Voice.Language)`n"
    $SummaryContent += "  Accent: $($Voice.Accent)`n"
    $SummaryContent += "  Gender: $($Voice.Gender)`n"
    $SummaryContent += "  Quality: $($Voice.Quality)`n"
    $SummaryContent += "  Files: $([System.IO.Path]::GetFileName($OnnxPath)), $([System.IO.Path]::GetFileName($JsonPath))`n"
    
    Write-Host ""
}

# Write summary file
$SummaryContent += "`n`nDownload Statistics:`n"
$SummaryContent += "  Downloaded: $DownloadedCount`n"
$SummaryContent += "  Skipped: $SkippedCount`n"
$SummaryContent += "  Failed: $FailedCount`n"
$SummaryContent += "  Total Size: $([math]::Round($TotalSizeMB, 2)) MB`n"

Set-Content -Path $SummaryPath -Value $SummaryContent -Encoding UTF8

# Final summary
Write-Host "=== Download Complete ===" -ForegroundColor Cyan
Write-Host "Downloaded: $DownloadedCount voices" -ForegroundColor Green
Write-Host "Skipped: $SkippedCount voices (already exist)" -ForegroundColor Yellow
Write-Host "Failed: $FailedCount voices" -ForegroundColor Red
Write-Host "Total Size: $([math]::Round($TotalSizeMB, 2)) MB" -ForegroundColor Cyan
Write-Host "Summary saved to: $SummaryPath" -ForegroundColor Cyan

if ($FailedCount -gt 0) {
    Write-Host ""
    Write-Host "Some downloads failed. This could be due to:" -ForegroundColor Yellow
    Write-Host "  - Network connectivity issues" -ForegroundColor Gray
    Write-Host "  - Voice names that are no longer available" -ForegroundColor Gray
    Write-Host "  - Server-side issues" -ForegroundColor Gray
    Write-Host ""
    Write-Host "You can run this script again to retry failed downloads." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "All voices are now ready for use in your TTS Performer module!" -ForegroundColor Green
