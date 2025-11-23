# OpenCV CUDA Builder - FULL OUTPUT VERSION
$ErrorActionPreference = "Stop"

$SRC = "C:\ocv_src"
$BUILD = "C:\ocv_b" 
$INSTALL = "H:\0000_CODE\01_collider_pyo\juce\opencv_cuda_install"

Write-Host "Starting OpenCV build..." -ForegroundColor Cyan

# Load VS
cmd /c """C:\VS\Studio2022\VC\Auxiliary\Build\vcvars64.bat"" && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        Set-Item -Force -Path "ENV:\$($matches[1])" -Value $matches[2]
    }
}

# Clean
Remove-Item -Recurse -Force $SRC,$BUILD,$INSTALL -ErrorAction SilentlyContinue

# Clone
Write-Host "Cloning..." -ForegroundColor Yellow
git clone --depth 1 --branch 4.x https://github.com/opencv/opencv.git $SRC
git clone --depth 1 --branch 4.x https://github.com/opencv/opencv_contrib.git "$SRC\contrib"

# Configure
Write-Host "Configuring..." -ForegroundColor Yellow
cmake -B $BUILD -S $SRC -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_INSTALL_PREFIX="$INSTALL" `
    -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_opencv_world=ON `
    -DBUILD_TESTS=OFF `
    -DBUILD_PERF_TESTS=OFF `
    -DBUILD_EXAMPLES=OFF `
    -DBUILD_opencv_apps=OFF `
    -DWITH_CUDA=ON `
    -DWITH_CUDNN=ON `
    -DOPENCV_DNN_CUDA=ON `
    -DCUDA_ARCH_BIN="8.6;8.9;12.0" `
    -DCUDA_ARCH_PTX="12.0" `
    -DOPENCV_EXTRA_MODULES_PATH="$SRC\contrib\modules"

if ($LASTEXITCODE -ne 0) {
    Write-Host "CONFIGURE FAILED!" -ForegroundColor Red
    exit 1
}

# Build
Write-Host "Building (30-45 min)..." -ForegroundColor Yellow
cmake --build $BUILD --config Release -- -j8

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
}

# Check if library was built
$builtLib = Get-ChildItem "$BUILD\lib\Release" -Filter "opencv_world*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
if (!$builtLib) {
    Write-Host "BUILD SUCCEEDED BUT NO LIBRARY FOUND!" -ForegroundColor Red
    Write-Host "Checking alternate locations..." -ForegroundColor Yellow
    Get-ChildItem $BUILD -Recurse -Filter "*.lib" | Select-Object FullName -First 10
    exit 1
}
Write-Host "Built: $($builtLib.Name)" -ForegroundColor Green

# Install
Write-Host "Installing..." -ForegroundColor Yellow
cmake --install $BUILD --config Release

# Verify
$installedLib = Get-ChildItem "$INSTALL" -Recurse -Filter "opencv_world*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
if (!$installedLib) {
    Write-Host "INSTALL FAILED - NO LIBRARY IN $INSTALL" -ForegroundColor Red
    Write-Host "What was installed:" -ForegroundColor Yellow
    Get-ChildItem $INSTALL -Recurse | Select-Object FullName -First 20
    exit 1
}

Write-Host ""
Write-Host "SUCCESS!" -ForegroundColor Green
Write-Host "Library: $($installedLib.FullName)" -ForegroundColor Cyan
Write-Host ""
Write-Host "CMakeLists.txt is already configured for opencv_world4130.lib" -ForegroundColor Green
Write-Host ""
Write-Host "Cleaning temp files..." -ForegroundColor Yellow
Remove-Item -Recurse -Force $SRC,$BUILD -ErrorAction SilentlyContinue
Write-Host "DONE" -ForegroundColor Green

