$ProgressPreference = 'SilentlyContinue'
$url = "https://github.com/MTG/essentia/archive/refs/heads/master.zip"
$zip = "essentia.zip"
$dest = "h:\0000_CODE\01_collider_pyo\vendor"
$extractPath = "$dest\essentia-master"
$finalPath = "$dest\essentia"

Write-Host "Downloading Essentia..."
Invoke-WebRequest -Uri $url -OutFile $zip
Write-Host "Extracting..."
Expand-Archive -Path $zip -DestinationPath $dest -Force
Write-Host "Renaming..."
if (Test-Path $finalPath) { Remove-Item $finalPath -Recurse -Force }
Rename-Item $extractPath $finalPath
Remove-Item $zip
Write-Host "Done."
