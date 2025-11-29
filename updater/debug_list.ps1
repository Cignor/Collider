$path = "H:\0000_CODE\01_collider_pyo\juce\build-ninja-release\PresetCreatorApp_artefacts\Release"
$files = Get-ChildItem -Path $path -File -Force
$exe = $files | Where-Object { $_.Name -eq "Pikon Raditsz.exe" }
if ($exe) {
    Write-Host "Found EXE in listing!"
    Write-Host "Name: $($exe.Name)"
}
else {
    Write-Host "EXE NOT FOUND in listing!"
    Write-Host "Total files found: $($files.Count)"
    Write-Host "First 5 files:"
    $files | Select-Object -First 5 | ForEach-Object { Write-Host " - $($_.Name)" }
}
