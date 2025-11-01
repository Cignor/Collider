# 📦 FFmpeg Setup Guide - Vendor Approach

## 🎯 Quick Setup (5 minutes)

### **1. Download Pre-Built FFmpeg**

**Windows (64-bit):**
- Go to: https://github.com/BtbN/FFmpeg-Builds/releases
- Download: `ffmpeg-master-latest-win64-gpl-shared.zip` (~150 MB)
- Alternative: https://www.gyan.dev/ffmpeg/builds/ → Download "release builds"

**What you need:**
- Shared libraries (`.dll` files)
- Header files (`.h` files)
- Import libraries (`.lib` files for linking)

---

### **2. Extract to Vendor Folder**

```
H:\0000_CODE\01_collider_pyo\
├── vendor/
│   ├── ffmpeg/                  ← Create this folder
│   │   ├── bin/                 ← DLL files go here
│   │   │   ├── avcodec-61.dll
│   │   │   ├── avformat-61.dll
│   │   │   ├── avutil-59.dll
│   │   │   ├── swresample-5.dll
│   │   │   └── swscale-8.dll
│   │   ├── include/             ← Header files go here
│   │   │   ├── libavcodec/
│   │   │   ├── libavformat/
│   │   │   ├── libavutil/
│   │   │   ├── libswresample/
│   │   │   └── libswscale/
│   │   └── lib/                 ← .lib files go here
│   │       ├── avcodec.lib
│   │       ├── avformat.lib
│   │       ├── avutil.lib
│   │       ├── swresample.lib
│   │       └── swscale.lib
│   ├── piper/                   ← Already exists
│   ├── onnxruntime/             ← Already exists
│   └── ...
```

---

### **3. Extraction Steps**

#### **From BtbN Builds:**
1. Extract the zip file
2. Inside, you'll find `bin/`, `include/`, and `lib/` folders
3. Copy these **entire folders** to `vendor/ffmpeg/`

#### **From Gyan.dev Builds:**
1. Extract the zip file
2. Navigate to the extracted folder
3. Copy `bin/`, `include/`, and `lib/` folders to `vendor/ffmpeg/`

---

## 🔧 CMake Configuration (Simplified)

Your new CMakeLists.txt FFmpeg section will be **~15 lines** instead of **~250 lines**:

```cmake
# --------------------------------------------------------------
# FFmpeg (Pre-built vendor binaries)
# --------------------------------------------------------------
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/../vendor/ffmpeg" CACHE PATH "Path to FFmpeg")

if(EXISTS "${FFMPEG_DIR}/include/libavformat/avformat.h")
    message(STATUS "✓ FFmpeg found at ${FFMPEG_DIR}")
    
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
    set(FFMPEG_LIB_DIR "${FFMPEG_DIR}/lib")
    
    # Find libraries
    find_library(AVFORMAT_LIBRARY avformat PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(AVCODEC_LIBRARY avcodec PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(AVUTIL_LIBRARY avutil PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(SWRESAMPLE_LIBRARY swresample PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(SWSCALE_LIBRARY swscale PATHS "${FFMPEG_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
    
    set(FFMPEG_LIBRARIES 
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${SWSCALE_LIBRARY}
    )
else()
    message(FATAL_ERROR 
        "FFmpeg not found!\n"
        "Please download FFmpeg and extract to: ${FFMPEG_DIR}\n"
        "See FFMPEG_SETUP_GUIDE.md for instructions."
    )
endif()
```

---

## 📋 Version Compatibility

### **Recommended Versions:**
- **FFmpeg 6.1** or **7.0** (latest stable)
- **Architecture**: x64 (64-bit)
- **Build Type**: Shared (DLL) - smaller and easier to update

### **Why Shared over Static?**
- ✅ Smaller executable size
- ✅ Easier to update FFmpeg independently
- ✅ Faster linking times
- ✅ Standard practice for FFmpeg

---

## 🚀 Benefits of This Approach

| Aspect | Old (vcpkg) | New (Vendor) |
|--------|-------------|--------------|
| **First Build Time** | 30-60 min | < 1 min |
| **Complexity** | 250+ lines | 15 lines |
| **Dependencies** | Git, vcpkg, build tools | None |
| **Reproducibility** | Variable | Identical |
| **Debugging** | Complex | Simple |
| **Offline Builds** | ❌ No | ✅ Yes |

---

## 🔄 Runtime DLL Deployment

Your post-build step already copies DLLs correctly, just update the FFmpeg DLL names:

```cmake
# Copy FFmpeg DLLs to output directory
if(WIN32 AND EXISTS "${FFMPEG_DIR}/bin")
    add_custom_command(TARGET ColliderApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_DIR}/bin"
            "$<TARGET_FILE_DIR:ColliderApp>"
        COMMENT "Copying FFmpeg DLLs to output directory"
    )
endif()
```

---

## ❓ Troubleshooting

### **"FFmpeg not found" error**
- Check that `vendor/ffmpeg/` exists
- Verify the folder structure matches the guide
- Ensure you have `include/`, `lib/`, and `bin/` folders

### **Linker errors (LNK2019)**
- Make sure `.lib` files are in `vendor/ffmpeg/lib/`
- Check that library names match (avcodec.lib, avformat.lib, etc.)

### **Runtime errors (DLL not found)**
- Ensure `.dll` files are copied to the output directory
- Check the post-build copy command runs successfully

---

## 📚 Alternative: Linux/macOS

### **Linux:**
```bash
sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev
```

### **macOS:**
```bash
brew install ffmpeg
```

CMake will automatically find these via pkg-config.

---

## ✅ Verification

After setup, verify FFmpeg is found:

```powershell
cd H:\0000_CODE\01_collider_pyo\juce\build
cmake .. -G "Visual Studio 17 2022"
```

You should see:
```
✓ FFmpeg found at H:/0000_CODE/01_collider_pyo/vendor/ffmpeg
  - Include: H:/0000_CODE/01_collider_pyo/vendor/ffmpeg/include
  - Libraries: avformat, avcodec, avutil, swresample, swscale
```

---

## 🎉 Summary

**Old way:** Download → compile → wait 30 min → hope it works
**New way:** Download → extract → done in 5 min → always works

This matches your existing approach for Piper, ONNX Runtime, and SoundTouch! 🚀

