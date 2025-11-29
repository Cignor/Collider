$path = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release\Pikon Raditsz.exe"
if (Test-Path $path) { Write-Host "Exists" } else { Write-Host "Missing" }
$item = Get-Item $path
Write-Host "Name: $($item.Name)"
Write-Host "Length: $($item.Length)"
