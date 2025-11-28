# Distribution Guide - Collider Audio Engine

This guide explains how to package and distribute your Collider Audio Engine application with all required dependencies.

## 📦 Required Dependencies

Your application requires these runtime dependencies on end-user systems:

### 1. **NVIDIA CUDA Runtime v13.0** ⚠️ CRITICAL
- **DLL Name**: `cudart64_130.dll` (this is the "13" DLL you mentioned)
- **Required for**: GPU-accelerated OpenCV operations
- **Size**: ~350 KB

### 2. **Microsoft Visual C++ Redistributable 2022 (x64)**
- **Required DLLs**: `vcruntime140.dll`, `msvcp140.dll`, etc.
- **Required for**: C++ runtime support
- **Size**: ~25 MB installer

### 3. **FFmpeg DLLs**
- **Required DLLs**: 
  - `avformat-*.dll`
  - `avcodec-*.dll`
  - `avutil-*.dll`
  - `swresample-*.dll`
  - `swscale-*.dll`
- **Required for**: Video/audio file processing
- **Size**: ~50 MB total

### 4. **OpenCV DLLs**
- **Required DLLs**:
  - `opencv_world4130.dll` (Release) or `opencv_world4130d.dll` (Debug)
  - `opencv_videoio_ffmpeg4130_64.dll`
- **Required for**: Computer vision and video processing
- **Size**: ~100 MB

### 5. **ONNX Runtime DLLs**
- **Location**: `vendor/onnxruntime/lib/`
- **Required for**: TTS (Text-to-Speech) features
- **Size**: ~20 MB

---

## 🎯 Distribution Strategies

### **Option 1: Portable Distribution (Recommended for Testing)**

Bundle ALL DLLs with your executable in a single folder.

#### Folder Structure:
```
ColliderAudioEngine/
├── ColliderApp.exe                    # Your main executable
├── cudart64_130.dll                   # CUDA runtime
├── cublas64_13.dll                    # CUDA BLAS library
├── cublasLt64_13.dll                  # CUDA BLAS LT library
├── cudnn64_9.dll                      # cuDNN (if using)
├── opencv_world4130.dll               # OpenCV
├── opencv_videoio_ffmpeg4130_64.dll   # OpenCV FFmpeg plugin
├── avformat-*.dll                     # FFmpeg libraries
├── avcodec-*.dll
├── avutil-*.dll
├── swresample-*.dll
├── swscale-*.dll
├── onnxruntime.dll                    # ONNX Runtime
├── soundtouch.dll                     # SoundTouch (if separate)
└── assets/                            # Your application assets
    ├── samples/
    ├── presets/
    └── models/
```

#### How to Create:

1. **Build your application** in Release mode
2. **Copy all DLLs** to the output directory:

```powershell
# Run this script from your project root
$OutputDir = ".\juce\build\Release"
$DistDir = ".\Distribution\ColliderAudioEngine"

# Create distribution directory
New-Item -ItemType Directory -Force -Path $DistDir

# Copy executable
Copy-Item "$OutputDir\ColliderApp.exe" -Destination $DistDir

# Copy CUDA DLLs (from CUDA Toolkit installation)
$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin"
Copy-Item "$CudaPath\cudart64_130.dll" -Destination $DistDir
Copy-Item "$CudaPath\cublas64_13.dll" -Destination $DistDir
Copy-Item "$CudaPath\cublasLt64_13.dll" -Destination $DistDir

# Copy cuDNN DLL (if standalone installation)
# Copy-Item "C:\Program Files\NVIDIA\CUDNN\v9.14\bin\cudnn64_9.dll" -Destination $DistDir

# Copy OpenCV DLLs
$OpenCVPath = "..\opencv_cuda_install\bin"
Copy-Item "$OpenCVPath\opencv_world4130.dll" -Destination $DistDir
Copy-Item "$OpenCVPath\opencv_videoio_ffmpeg4130_64.dll" -Destination $DistDir

# Copy FFmpeg DLLs
$FFmpegPath = "..\vendor\ffmpeg\bin"
Copy-Item "$FFmpegPath\*.dll" -Destination $DistDir

# Copy ONNX Runtime DLLs
$OnnxPath = "..\vendor\onnxruntime\lib"
Copy-Item "$OnnxPath\*.dll" -Destination $DistDir

Write-Host "✓ Distribution package created at: $DistDir"
```

---

### **Option 2: Installer with Redistributables (Recommended for Release)**

Create a proper installer that installs dependencies automatically.

#### Using Inno Setup (Free, Windows)

1. **Download Inno Setup**: https://jrsoftware.org/isinfo.php

2. **Create installer script** (`installer.iss`):

```iss
[Setup]
AppName=Collider Audio Engine
AppVersion=0.1.0
DefaultDirName={autopf}\ColliderAudioEngine
DefaultGroupName=Collider Audio Engine
OutputDir=.\installer_output
OutputBaseFilename=ColliderAudioEngine_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Files]
; Main executable
Source: "juce\build\Release\ColliderApp.exe"; DestDir: "{app}"; Flags: ignoreversion

; CUDA DLLs
Source: "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\cudart64_130.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\cublas64_13.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\cublasLt64_13.dll"; DestDir: "{app}"; Flags: ignoreversion

; OpenCV DLLs
Source: "..\opencv_cuda_install\bin\opencv_world4130.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\opencv_cuda_install\bin\opencv_videoio_ffmpeg4130_64.dll"; DestDir: "{app}"; Flags: ignoreversion

; FFmpeg DLLs
Source: "..\vendor\ffmpeg\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; ONNX Runtime DLLs
Source: "..\vendor\onnxruntime\lib\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Assets
Source: "assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\Collider Audio Engine"; Filename: "{app}\ColliderApp.exe"
Name: "{autodesktop}\Collider Audio Engine"; Filename: "{app}\ColliderApp.exe"

[Run]
; Install VC++ Redistributable
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."; Check: VCRedistNeedsInstall

[Code]
function VCRedistNeedsInstall: Boolean;
begin
  // Check if VC++ 2022 is already installed
  Result := not RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64');
end;
```

3. **Download VC++ Redistributable**:
   - Get from: https://aka.ms/vs/17/release/vc_redist.x64.exe
   - Place in your project root as `VC_redist.x64.exe`

4. **Compile installer**:
   - Right-click `installer.iss` → Compile
   - Output: `installer_output\ColliderAudioEngine_Setup.exe`

---

### **Option 3: CUDA Runtime Redistributable Package**

Instead of bundling CUDA DLLs, you can require users to install CUDA Runtime.

#### Create a dependency checker:

```cpp
// Add to your main.cpp or startup code
#include <windows.h>
#include <iostream>

bool checkCudaRuntime() {
    HMODULE cudaModule = LoadLibraryA("cudart64_130.dll");
    if (cudaModule == nullptr) {
        MessageBoxA(nullptr, 
            "CUDA Runtime 13.0 is required but not found.\n\n"
            "Please install NVIDIA CUDA Toolkit 13.0 or download the runtime from:\n"
            "https://developer.nvidia.com/cuda-downloads",
            "Missing Dependency", MB_ICONERROR);
        return false;
    }
    FreeLibrary(cudaModule);
    return true;
}

int main() {
    if (!checkCudaRuntime()) {
        return 1;
    }
    // Continue with application startup...
}
```

---

## 🔧 CMake Installation Target (Advanced)

Add installation rules to your `CMakeLists.txt`:

```cmake
# Add at the end of juce/CMakeLists.txt

# ==============================================================================
# Installation Configuration
# ==============================================================================

# Install executable
install(TARGETS ColliderApp
    RUNTIME DESTINATION bin
    COMPONENT application
)

# Install CUDA DLLs
if(WIN32 AND CUDAToolkit_FOUND)
    install(FILES
        "${CUDAToolkit_BIN_DIR}/cudart64_130.dll"
        "${CUDAToolkit_BIN_DIR}/cublas64_13.dll"
        "${CUDAToolkit_BIN_DIR}/cublasLt64_13.dll"
        DESTINATION bin
        COMPONENT runtime
    )
endif()

# Install OpenCV DLLs
if(OPENCV_PREBUILT)
    install(FILES
        "${OPENCV_INSTALL_PREFIX}/bin/opencv_world4130.dll"
        "${OPENCV_INSTALL_PREFIX}/bin/opencv_videoio_ffmpeg4130_64.dll"
        DESTINATION bin
        COMPONENT runtime
    )
endif()

# Install FFmpeg DLLs
if(WIN32 AND FFMPEG_FOUND)
    file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
    install(FILES ${FFMPEG_DLLS}
        DESTINATION bin
        COMPONENT runtime
    )
endif()

# Install ONNX Runtime DLLs
file(GLOB ONNX_DLLS "${ONNXRUNTIME_DIR}/lib/*.dll")
install(FILES ${ONNX_DLLS}
    DESTINATION bin
    COMPONENT runtime
)

# Create installation package
set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_PACKAGE_NAME "ColliderAudioEngine")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_VENDOR "Collider")
include(CPack)
```

Then build installation package:
```powershell
cmake --build build --target install
cpack -C Release
```

---

## ⚠️ Important Notes

### GPU Requirements
- **NVIDIA GPU Required**: Your software REQUIRES an NVIDIA GPU to run (for CUDA)
- **Minimum GPU**: CUDA Compute Capability 8.6+ (RTX 30 series or newer)
- **Fallback**: Consider adding CPU-only mode for systems without NVIDIA GPUs

### License Compliance
- **CUDA**: Check NVIDIA's redistribution license
- **FFmpeg**: GPL license - you may need to provide source code
- **OpenCV**: Apache 2.0 - attribution required
- **ONNX Runtime**: MIT license - attribution required

### System Requirements Document
Create a `SYSTEM_REQUIREMENTS.md`:

```markdown
# System Requirements

## Minimum Requirements
- **OS**: Windows 10/11 (64-bit)
- **GPU**: NVIDIA GPU with CUDA Compute Capability 8.6+
  - RTX 3050 or better
  - GTX 1650 or better (with driver update)
- **RAM**: 8 GB
- **Storage**: 500 MB free space
- **NVIDIA Driver**: 522.06 or newer

## Recommended Requirements
- **GPU**: RTX 4060 or better
- **RAM**: 16 GB
- **Storage**: 1 GB free space (for samples/presets)

## Required Software (Auto-installed)
- Microsoft Visual C++ 2022 Redistributable (x64)
- NVIDIA CUDA Runtime 13.0
```

---

## 🚀 Quick Start Script

Save this as `create_distribution.ps1`:

```powershell
# Collider Audio Engine - Distribution Builder
# Run from project root

param(
    [string]$BuildConfig = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Collider Audio Engine - Distribution" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Paths
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "juce\build\$BuildConfig"
$DistDir = Join-Path $ProjectRoot "Distribution\ColliderAudioEngine_v0.1.0"
$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin"
$OpenCVPath = Join-Path $ProjectRoot "..\opencv_cuda_install\bin"
$FFmpegPath = Join-Path $ProjectRoot "..\vendor\ffmpeg\bin"
$OnnxPath = Join-Path $ProjectRoot "..\vendor\onnxruntime\lib"

# Create distribution directory
Write-Host "Creating distribution directory..." -ForegroundColor Yellow
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

# Copy executable
Write-Host "Copying executable..." -ForegroundColor Yellow
Copy-Item (Join-Path $BuildDir "ColliderApp.exe") -Destination $DistDir

# Copy CUDA DLLs
Write-Host "Copying CUDA DLLs..." -ForegroundColor Yellow
$cudaDlls = @(
    "cudart64_130.dll",
    "cublas64_13.dll",
    "cublasLt64_13.dll"
)
foreach ($dll in $cudaDlls) {
    $source = Join-Path $CudaPath $dll
    if (Test-Path $source) {
        Copy-Item $source -Destination $DistDir
        Write-Host "  ✓ $dll" -ForegroundColor Green
    } else {
        Write-Host "  ✗ $dll NOT FOUND" -ForegroundColor Red
    }
}

# Copy OpenCV DLLs
Write-Host "Copying OpenCV DLLs..." -ForegroundColor Yellow
Copy-Item (Join-Path $OpenCVPath "opencv_world4130.dll") -Destination $DistDir
Copy-Item (Join-Path $OpenCVPath "opencv_videoio_ffmpeg4130_64.dll") -Destination $DistDir

# Copy FFmpeg DLLs
Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
Get-ChildItem (Join-Path $FFmpegPath "*.dll") | Copy-Item -Destination $DistDir

# Copy ONNX Runtime DLLs
Write-Host "Copying ONNX Runtime DLLs..." -ForegroundColor Yellow
Get-ChildItem (Join-Path $OnnxPath "*.dll") | Copy-Item -Destination $DistDir

# Create README
Write-Host "Creating README..." -ForegroundColor Yellow
$readme = @"
# Collider Audio Engine v0.1.0

## System Requirements
- Windows 10/11 (64-bit)
- NVIDIA GPU with CUDA support (RTX 30 series or newer recommended)
- NVIDIA Driver 522.06 or newer
- 8 GB RAM minimum (16 GB recommended)

## Installation
1. Extract all files to a folder
2. Run ColliderApp.exe
3. If you get a DLL error, install:
   - Visual C++ Redistributable 2022: https://aka.ms/vs/17/release/vc_redist.x64.exe
   - NVIDIA GPU Drivers: https://www.nvidia.com/Download/index.aspx

## Troubleshooting
- "cudart64_130.dll not found": Update NVIDIA drivers or install CUDA Runtime 13.0
- "VCRUNTIME140.dll not found": Install Visual C++ Redistributable 2022

## Support
For issues, contact: support@collider.audio
"@
Set-Content -Path (Join-Path $DistDir "README.txt") -Value $readme

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host " Distribution created successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "Location: $DistDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Test on a clean Windows machine" -ForegroundColor White
Write-Host "2. Create ZIP archive for distribution" -ForegroundColor White
Write-Host "3. Consider creating an installer with Inno Setup" -ForegroundColor White
```

---

## ✅ Testing Checklist

Before distributing, test on a **clean Windows machine**:

- [ ] Fresh Windows 10/11 installation (VM recommended)
- [ ] No NVIDIA drivers installed → Install latest drivers
- [ ] No Visual C++ Runtime → Should prompt for installation
- [ ] Copy distribution folder → Run executable
- [ ] Verify all features work (GPU acceleration, video, audio)
- [ ] Check for missing DLL errors

---

## 📚 Additional Resources

- **Inno Setup**: https://jrsoftware.org/isinfo.php
- **NSIS**: https://nsis.sourceforge.io/
- **WiX Toolset**: https://wixtoolset.org/
- **CUDA Redistribution Guide**: https://docs.nvidia.com/cuda/eula/index.html#redistribution
- **Dependency Walker**: https://www.dependencywalker.com/ (to check for missing DLLs)

---

**Need help?** Run the `create_distribution.ps1` script to automatically create a portable distribution package!
