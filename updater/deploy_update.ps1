# Deploy Update to OVH Server
# This script uploads a release to your OVH website via FTP/SFTP

param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    
    [Parameter(Mandatory = $true)]
    [string]$Version,
    
    [Parameter(Mandatory = $true)]
    [ValidateSet("standard", "cuda")]
    [string]$Variant,
    
    [Parameter(Mandatory = $true)]
    [string]$FtpServer,
    
    [Parameter(Mandatory = $true)]
    [string]$FtpUser,
    
    [Parameter(Mandatory = $false)]
    [string]$FtpPassword,
    
    [Parameter(Mandatory = $false)]
    [switch]$UseSecure  # Use SFTP instead of FTP
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Pikon Raditsz - Deploy to OVH" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Validate build directory
if (-not (Test-Path $BuildDir)) {
    Write-Host "ERROR: Build directory not found: $BuildDir" -ForegroundColor Red
    exit 1
}

# Get password if not provided
if (-not $FtpPassword) {
    $securePassword = Read-Host "Enter FTP password" -AsSecureString
    $FtpPassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
    )
}

Write-Host "Server: $FtpServer" -ForegroundColor Green
Write-Host "User: $FtpUser" -ForegroundColor Green
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "Variant: $Variant" -ForegroundColor Green
Write-Host "Protocol: $(if ($UseSecure) { 'SFTP' } else { 'FTP' })" -ForegroundColor Green
Write-Host ""

# Remote paths
$remoteBasePath = "/public_html/updates/versions/$Version/$Variant"
$remotePresetsPath = "/public_html/updates/versions/$Version/presets"
$remoteDocsPath = "/public_html/updates/versions/$Version/docs"

Write-Host "Remote path: $remoteBasePath" -ForegroundColor Yellow
Write-Host ""

# Check if WinSCP is available (recommended for SFTP)
$winscpPath = "C:\Program Files (x86)\WinSCP\WinSCP.com"
$useWinSCP = (Test-Path $winscpPath) -and $UseSecure

if ($UseSecure -and -not $useWinSCP) {
    Write-Host "WARNING: WinSCP not found. Install WinSCP for SFTP support." -ForegroundColor Yellow
    Write-Host "Download from: https://winscp.net/eng/download.php" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Falling back to FTP..." -ForegroundColor Yellow
    $UseSecure = $false
}

# Function to upload via WinSCP (SFTP)
function Upload-ViaWinSCP {
    param(
        [string]$LocalPath,
        [string]$RemotePath
    )
    
    Write-Host "Uploading via WinSCP (SFTP)..." -ForegroundColor Yellow
    
    # Create WinSCP script
    $scriptPath = Join-Path $env:TEMP "winscp_upload.txt"
    $scriptContent = @"
option batch abort
option confirm off
open sftp://${FtpUser}:${FtpPassword}@${FtpServer}/
option transfer binary
mkdir $RemotePath
put $LocalPath/* $RemotePath/
close
exit
"@
    
    $scriptContent | Out-File -FilePath $scriptPath -Encoding ASCII
    
    # Execute WinSCP
    try {
        & $winscpPath /script=$scriptPath
        if ($LASTEXITCODE -ne 0) {
            throw "WinSCP upload failed with exit code $LASTEXITCODE"
        }
        Write-Host "Upload complete!" -ForegroundColor Green
    }
    finally {
        Remove-Item $scriptPath -ErrorAction SilentlyContinue
    }
}

# Function to upload via FTP (fallback)
function Upload-ViaFTP {
    param(
        [string]$LocalPath,
        [string]$RemotePath
    )
    
    Write-Host "Uploading via FTP..." -ForegroundColor Yellow
    Write-Host "NOTE: This is a basic implementation. For production, use WinSCP or FileZilla." -ForegroundColor Yellow
    Write-Host ""
    
    # Get all files
    $files = Get-ChildItem -Path $LocalPath -Recurse -File
    $fileCount = $files.Count
    $progress = 0
    
    foreach ($file in $files) {
        $progress++
        $percentComplete = [math]::Round(($progress / $fileCount) * 100, 1)
        
        # Get relative path
        $relativePath = $file.FullName.Substring($LocalPath.Length + 1)
        $remoteFile = "$RemotePath/$($relativePath.Replace('\', '/'))"
        
        Write-Host "[$percentComplete%] Uploading: $relativePath" -ForegroundColor Gray
        
        # Create FTP request
        $ftpUri = "ftp://$FtpServer$remoteFile"
        $request = [System.Net.FtpWebRequest]::Create($ftpUri)
        $request.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
        $request.Credentials = New-Object System.Net.NetworkCredential($FtpUser, $FtpPassword)
        $request.UseBinary = $true
        $request.UsePassive = $true
        
        # Upload file
        try {
            $fileContent = [System.IO.File]::ReadAllBytes($file.FullName)
            $request.ContentLength = $fileContent.Length
            
            $requestStream = $request.GetRequestStream()
            $requestStream.Write($fileContent, 0, $fileContent.Length)
            $requestStream.Close()
            
            $response = $request.GetResponse()
            $response.Close()
        }
        catch {
            Write-Host "  ERROR: Failed to upload $relativePath" -ForegroundColor Red
            Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    
    Write-Host "Upload complete!" -ForegroundColor Green
}

# Perform upload
try {
    if ($useWinSCP) {
        Upload-ViaWinSCP -LocalPath $BuildDir -RemotePath $remoteBasePath
    }
    else {
        Upload-ViaFTP -LocalPath $BuildDir -RemotePath $remoteBasePath
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host " Deployment Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Files uploaded to:" -ForegroundColor Yellow
    Write-Host "  https://$FtpServer/updates/versions/$Version/$Variant/" -ForegroundColor White
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Yellow
    Write-Host "  1. Verify files are accessible via browser" -ForegroundColor White
    Write-Host "  2. Update the main manifest.json to point to this version" -ForegroundColor White
    Write-Host "  3. Test the updater with a client" -ForegroundColor White
    Write-Host ""
}
catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host " Deployment Failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "  - Verify FTP credentials are correct" -ForegroundColor White
    Write-Host "  - Check firewall settings" -ForegroundColor White
    Write-Host "  - Ensure remote directory exists" -ForegroundColor White
    Write-Host "  - For SFTP, install WinSCP: https://winscp.net/" -ForegroundColor White
    Write-Host ""
    exit 1
}
