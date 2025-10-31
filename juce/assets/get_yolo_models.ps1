# Download CV model assets for Collider to H:\0000_CODE\01_collider_pyo\juce\assets
# - yolov3.weights / yolov3.cfg / coco.names
# - enet-cityscapes-pytorch.onnx / enet-classes.txt

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$assetsRoot = 'H:\0000_CODE\01_collider_pyo\juce\assets'
if (-not (Test-Path $assetsRoot)) { New-Item -ItemType Directory -Path $assetsRoot | Out-Null }

function Invoke-Download {
    param(
        [Parameter(Mandatory=$true)] [string] $Url,
        [Parameter(Mandatory=$true)] [string] $OutPath
    )
    $headers = @{ 'User-Agent' = 'Mozilla/5.0 (Windows NT 10.0)' }
    Invoke-WebRequest -Uri $Url -OutFile $OutPath -Headers $headers -UseBasicParsing -MaximumRedirection 10 -ErrorAction Stop
}

function Download-WithFallback {
    param(
        [Parameter(Mandatory=$true)] [string[]] $Urls,
        [Parameter(Mandatory=$true)] [string]   $OutPath
    )
    foreach ($u in $Urls) {
        try {
            Write-Host "Downloading `"$u`" -> `"$OutPath`" ..." -ForegroundColor Cyan
            # BITS fails if server omits Content-Length; prefer Invoke-WebRequest for all
            Invoke-Download -Url $u -OutPath $OutPath
            if ((Test-Path $OutPath) -and ((Get-Item $OutPath).Length -gt 0)) {
                Write-Host "OK: $OutPath" -ForegroundColor Green
                return
            } else {
                Write-Warning "Downloaded file is empty: $OutPath"
            }
        } catch {
            Write-Warning ("Failed from {0}: {1}" -f $u, $_.Exception.Message)
            Start-Sleep -Seconds 2
        }
    }
    throw ("All URLs failed for {0}" -f $OutPath)
}

# YOLOv3 (weights/cfg/names)
$yoloWeights = Join-Path $assetsRoot 'yolov3.weights'
$yoloCfg     = Join-Path $assetsRoot 'yolov3.cfg'
$cocoNames   = Join-Path $assetsRoot 'coco.names'

Download-WithFallback @(
    # Primary: AlexeyAB release (working)
    'https://github.com/AlexeyAB/darknet/releases/download/darknet_yolo_v3_optimal/yolov3.weights',
    # Mirror: pjreddie (may throttle, but works via iwr)
    'https://pjreddie.com/media/files/yolov3.weights',
    # Mirror: SourceForge (add /download for redirect)
    'https://sourceforge.net/projects/yolov3.mirror/files/v8/yolov3.weights/download'
) $yoloWeights

Download-WithFallback @(
    'https://raw.githubusercontent.com/pjreddie/darknet/master/cfg/yolov3.cfg',
    'https://raw.githubusercontent.com/AlexeyAB/darknet/master/cfg/yolov3.cfg'
) $yoloCfg

Download-WithFallback @(
    'https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names',
    'https://raw.githubusercontent.com/ultralytics/yolov3/master/data/coco.names'
) $cocoNames

# ENet (Cityscapes) from OpenCV Zoo on Hugging Face
$enetOnnx   = Join-Path $assetsRoot 'enet-cityscapes-pytorch.onnx'
$enetLabels = Join-Path $assetsRoot 'enet-classes.txt'

Download-WithFallback @(
    'https://huggingface.co/opencv/opencv_zoo/resolve/main/models/segmentation_enet/enet-cityscapes.onnx',
    'https://huggingface.co/opencv/opencv_zoo/resolve/main/models/segmentation_enet/onnx/enet-cityscapes.onnx'
) $enetOnnx

Download-WithFallback @(
    'https://raw.githubusercontent.com/opencv/opencv_zoo/master/models/segmentation_enet/labels.txt'
) $enetLabels
Write-Host "`nAll downloads completed. Files:" -ForegroundColor Green
Get-ChildItem $assetsRoot -File |
    Where-Object { $_.Name -in @('yolov3.weights','yolov3.cfg','coco.names','enet-cityscapes-pytorch.onnx','enet-classes.txt') } |
    Select-Object Name, Length, LastWriteTime |
    Format-Table -AutoSize