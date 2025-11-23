# Build OpenCV with CUDA - SHORT PATHS - VERIFIED
$ErrorActionPreference = "Stop"

$OPENCV_SRC = "C:\ocv_src"
$OPENCV_BUILD = "C:\ocv_b" 
$OPENCV_INSTALL = "H:\0000_CODE\01_collider_pyo\juce\opencv_cuda_install"

Write-Host "========================================"
Write-Host "  OpenCV CUDA Builder - SHORT PATHS"
Write-Host "========================================"
Write-Host ""

# Step 1: Load VS environment
Write-Host "[1/6] Loading VS environment..." -ForegroundColor Yellow
$vsPath = "C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vsPath`" && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        Set-Item -Force -Path "ENV:\$($matches[1])" -Value $matches[2]
    }
}
Write-Host "OK" -ForegroundColor Green

# Step 2: Clean
Write-Host ""
Write-Host "[2/6] Cleaning old builds..." -ForegroundColor Yellow
Remove-Item -Recurse -Force $OPENCV_SRC -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $OPENCV_BUILD -ErrorAction SilentlyContinue  
Remove-Item -Recurse -Force $OPENCV_INSTALL -ErrorAction SilentlyContinue
Write-Host "OK" -ForegroundColor Green

# Step 3: Clone
Write-Host ""
Write-Host "[3/6] Cloning source (5 min)..." -ForegroundColor Yellow
git clone --depth 1 --branch 4.x https://github.com/opencv/opencv.git $OPENCV_SRC
if ($LASTEXITCODE -ne 0) { throw "opencv clone failed" }
git clone --depth 1 --branch 4.x https://github.com/opencv/opencv_contrib.git "$OPENCV_SRC\contrib"
if ($LASTEXITCODE -ne 0) { throw "opencv_contrib clone failed" }
Write-Host "OK" -ForegroundColor Green

# Step 4: Configure
Write-Host ""
Write-Host "[4/6] Configuring (2 min)..." -ForegroundColor Yellow
cmake -B $OPENCV_BUILD -S $OPENCV_SRC -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_INSTALL_PREFIX="$OPENCV_INSTALL" `
    -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_opencv_world=ON `
    -DBUILD_TESTS=OFF `
    -DBUILD_PERF_TESTS=OFF `
    -DBUILD_EXAMPLES=OFF `
    -DBUILD_opencv_apps=OFF `
    -DENABLE_PRECOMPILED_HEADERS=OFF `
    -DBUILD_WITH_STATIC_CRT=OFF `
    -DWITH_IPP=OFF `
    -DWITH_CUDA=ON `
    -DWITH_NVIDIA_NPP=ON `
    -DWITH_CUDNN=ON `
    -DOPENCV_DNN_CUDA=ON `
    -DCUDA_ARCH_BIN="8.6;8.9;12.0" `
    -DCUDA_ARCH_PTX="12.0" `
    -DOPENCV_EXTRA_MODULES_PATH="$OPENCV_SRC\contrib\modules" `
    -DBUILD_opencv_core=ON `
    -DBUILD_opencv_dnn=ON `
    -DBUILD_opencv_features2d=ON `
    -DBUILD_opencv_highgui=ON `
    -DBUILD_opencv_imgcodecs=ON `
    -DBUILD_opencv_imgproc=ON `
    -DBUILD_opencv_objdetect=ON `
    -DBUILD_opencv_tracking=ON `
    -DBUILD_opencv_video=ON `
    -DBUILD_opencv_videoio=ON `
    -DBUILD_opencv_calib3d=ON `
    -DOPENCV_DNN_CAFFE=ON
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
Write-Host "OK" -ForegroundColor Green

# Step 5: Build (THE LONG ONE)
Write-Host ""
Write-Host "[5/6] Building (30-45 min - GO GET COFFEE)..." -ForegroundColor Yellow
cmake --build $OPENCV_BUILD --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Write-Host "OK" -ForegroundColor Green

# Step 6: Install
Write-Host ""
Write-Host "[6/6] Installing..." -ForegroundColor Yellow
cmake --install $OPENCV_BUILD --config Release
if ($LASTEXITCODE -ne 0) { throw "install failed" }
Write-Host "OK" -ForegroundColor Green

# VERIFY EVERYTHING
Write-Host ""
Write-Host "========================================"
Write-Host "  VERIFYING INSTALLATION"
Write-Host "========================================"

$lib = Get-ChildItem "$OPENCV_INSTALL\lib" -Filter "opencv_world*.lib" | Select-Object -First 1
if (!$lib) { throw "NO LIBRARY FOUND IN $OPENCV_INSTALL\lib" }
Write-Host "Library: $($lib.Name)" -ForegroundColor Green

$header = "$OPENCV_INSTALL\include\opencv2\opencv.hpp"
if (!(Test-Path $header)) { throw "HEADERS NOT FOUND" }
Write-Host "Headers: OK" -ForegroundColor Green

$cmake = "$OPENCV_INSTALL\lib\cmake\OpenCV\OpenCVConfig.cmake"
if (!(Test-Path $cmake)) { throw "CMAKE CONFIG NOT FOUND" }
Write-Host "CMake config: OK" -ForegroundColor Green

# Update CMakeLists.txt
Write-Host ""
Write-Host "Updating CMakeLists.txt..." -ForegroundColor Yellow
$cmakeFile = "H:\0000_CODE\01_collider_pyo\juce\CMakeLists.txt"
$content = Get-Content $cmakeFile -Raw
$content = $content -replace 'opencv_world4130\.lib', $lib.Name
$content = $content -replace 'opencv_world4130d\.lib', ($lib.Name -replace '\.lib$', 'd.lib')
Set-Content $cmakeFile $content
Write-Host "OK" -ForegroundColor Green

# Cleanup
Write-Host ""
Write-Host "Cleaning up temp files (saves 10GB)..." -ForegroundColor Yellow
Remove-Item -Recurse -Force $OPENCV_SRC -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $OPENCV_BUILD -ErrorAction SilentlyContinue
Write-Host "OK" -ForegroundColor Green

# DONE
Write-Host ""
Write-Host "========================================"
Write-Host "  SUCCESS - READY TO BUILD"
Write-Host "========================================"
Write-Host ""
Write-Host "Installed: $($lib.Name)" -ForegroundColor Cyan
Write-Host "Location: $OPENCV_INSTALL" -ForegroundColor Cyan
Write-Host ""
Write-Host "NOW RUN:" -ForegroundColor Yellow
Write-Host "cd H:/0000_CODE/01_collider_pyo" -ForegroundColor White
Write-Host 'cmd /c ""C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"" && cmake -B juce\build-ninja-release -S juce -G Ninja -DCMAKE_BUILD_TYPE=Release' -ForegroundColor White
Write-Host 'cmd /c ""C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build juce\build-ninja-release --target PresetCreatorApp' -ForegroundColor White
