cmake_minimum_required(VERSION 3.22)
project(ColliderAudioEngine VERSION 0.1.0)

# --- TTS Integration (Piper) ---

# 1. Set paths to pre-built Piper and ONNX Runtime
set(PIPER_DIR "${CMAKE_SOURCE_DIR}/../vendor/piper/piper" CACHE PATH "Path to Piper TTS")
set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/../vendor/onnxruntime" CACHE PATH "Path to ONNX Runtime")

if(NOT EXISTS "${PIPER_DIR}")
    message(WARNING "Piper TTS not found at ${PIPER_DIR}. TTS features will be limited.")
endif()

if(NOT EXISTS "${ONNXRUNTIME_DIR}")
    message(WARNING "ONNX Runtime not found at ${ONNXRUNTIME_DIR}. TTS features will be limited.")
endif()

# 2. Add SoundTouch library
set(SOUNDTOUCH_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../soundtouch/source")
add_library(soundtouch STATIC
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/AAFilter.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/BPMDetect.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/FIFOSampleBuffer.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/FIRFilter.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/InterpolateCubic.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/InterpolateLinear.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/InterpolateShannon.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/PeakFinder.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/RateTransposer.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/SoundTouch.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/TDStretch.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/cpu_detect_x86.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/mmx_optimized.cpp
    ${SOUNDTOUCH_SOURCE_DIR}/SoundTouch/sse_optimized.cpp
)
target_include_directories(soundtouch PUBLIC 
    ${CMAKE_SOURCE_DIR}/../soundtouch/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/source/SoundTouch
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ==============================================================================
# Third-Party Dependencies (Fetch Only)
# ==============================================================================

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.9
)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(imgui_fc
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.0
)
FetchContent_MakeAvailable(imgui_fc)

# imnodes uses find_package(imgui) in its CMake; build as sources instead
FetchContent_Declare(imnodes_fc
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG b2ec254ce576ac3d42dfb7aef61deadbff8e7211
)
FetchContent_GetProperties(imnodes_fc)
if(NOT imnodes_fc_POPULATED)
  FetchContent_Populate(imnodes_fc)
endif()

# imgui_juce backend (dedicated JUCE bridge for ImGui)
FetchContent_Declare(imgui_juce_fc
    GIT_REPOSITORY https://github.com/Krasjet/imgui_juce.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imgui_juce_fc)

# --------------------------------------------------------------
# Box2D (2D physics engine for physics-based audio module)
# --------------------------------------------------------------
FetchContent_Declare(box2d_fc
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v2.4.1
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d_fc)

# --------------------------------------------------------------
# GLM (OpenGL Mathematics library for 3D vector/matrix math)
# --------------------------------------------------------------
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1 # Latest stable version
)
FetchContent_MakeAvailable(glm)

# --------------------------------------------------------------
# nlohmann/json (required by tinygltf)
# --------------------------------------------------------------
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3 # A recent stable version
)
FetchContent_MakeAvailable(nlohmann_json)

# --------------------------------------------------------------
# tinygltf (glTF 2.0 file loader for animation system)
# --------------------------------------------------------------
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG        v2.8.20 # A recent stable version
)
FetchContent_MakeAvailable(tinygltf)

# --------------------------------------------------------------
# ufbx (FBX file loader for animation system)
# --------------------------------------------------------------
FetchContent_Declare(
  ufbx
  GIT_REPOSITORY https://github.com/ufbx/ufbx.git
  GIT_TAG        v0.10.0 # A recent stable version of ufbx
)
FetchContent_MakeAvailable(ufbx)

# Get the path to the downloaded ufbx source code
FetchContent_GetProperties(ufbx SOURCE_DIR ufbx_SOURCE_DIR)

# Create a dedicated static library target for ufbx (single-header library needs separate compilation unit)
add_library(ufbx_static STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx_static PUBLIC ${ufbx_SOURCE_DIR})

# ==============================================================================
# CUDA & cuDNN Configuration (MUST be done before OpenCV)
# ==============================================================================
# Find the CUDA Toolkit. This will use the default installation path or environment
# variables. This is essential for OpenCV's configuration to detect CUDA correctly.
find_package(CUDAToolkit REQUIRED)

# Find CUDA runtime library for direct linking if CUDAToolkit::cudart target is not available
if(MSVC)
    find_library(CUDA_CUDART_LIBRARY
        NAMES cudart_static.lib cudart.lib
        PATHS "${CUDAToolkit_LIBRARY_DIR}"
        NO_DEFAULT_PATH
    )
else()
    find_library(CUDA_CUDART_LIBRARY
        NAMES cudart
        PATHS "${CUDAToolkit_LIBRARY_DIR}"
        NO_DEFAULT_PATH
    )
endif()

# --- NPP (NVIDIA Performance Primitives) Configuration ---
# Explicitly set CUDA and NPP paths for OpenCV to find them
set(CUDA_TOOLKIT_ROOT_DIR "${CUDAToolkit_TARGET_DIR}" CACHE PATH "CUDA Toolkit root directory")
set(CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES "${CUDAToolkit_INCLUDE_DIRS}" CACHE PATH "CUDA Toolkit include directories")

# Set NPP include and library directories explicitly
# NPP headers are in the CUDA Toolkit include directory
set(CUDA_INCLUDE_DIRS "${CUDAToolkit_INCLUDE_DIRS}" CACHE PATH "CUDA include directories")
set(CUDA_NPP_LIBRARY_ROOT_DIR "${CUDAToolkit_LIBRARY_DIR}" CACHE PATH "NPP library root directory")

# Provide hints for cuDNN path.
# First check if cuDNN is integrated into CUDA Toolkit (recommended approach)
# Then fall back to standalone cuDNN installation
set(CUDNN_FOUND FALSE)

# CRITICAL FIX: CUDAToolkit_INCLUDE_DIRS is a LIST, extract the first element
list(GET CUDAToolkit_INCLUDE_DIRS 0 CUDA_MAIN_INCLUDE_DIR)

# Check in CUDA Toolkit directory first
if(EXISTS "${CUDA_MAIN_INCLUDE_DIR}/cudnn.h")
    set(CUDNN_INCLUDE_DIR "${CUDA_MAIN_INCLUDE_DIR}" CACHE PATH "Path to cuDNN include directory")
    set(CUDNN_LIBRARY "${CUDAToolkit_LIBRARY_DIR}/cudnn.lib" CACHE FILEPATH "Path to cuDNN library")
    set(CUDNN_FOUND TRUE)
    message(STATUS "Found cuDNN integrated into CUDA Toolkit at ${CUDA_MAIN_INCLUDE_DIR}")
# Fall back to standalone cuDNN installation
elseif(EXISTS "C:/Program Files/NVIDIA/CUDNN/v9.14/include/cudnn.h")
    set(CUDNN_INCLUDE_DIR "C:/Program Files/NVIDIA/CUDNN/v9.14/include" CACHE PATH "Path to cuDNN include directory")
    set(CUDNN_LIBRARY "C:/Program Files/NVIDIA/CUDNN/v9.14/lib/x64/cudnn.lib" CACHE FILEPATH "Path to cuDNN library")
    set(CUDNN_FOUND TRUE)
    message(STATUS "Found standalone cuDNN at C:/Program Files/NVIDIA/CUDNN/v9.14/")
endif()

# Configure OpenCV based on cuDNN availability
if(CUDNN_FOUND)
    set(WITH_CUDNN ON CACHE BOOL "" FORCE)
    set(OPENCV_DNN_CUDA ON CACHE BOOL "" FORCE)
else()
    message(WARNING "cuDNN not found. OpenCV will build without cuDNN support.")
    message(WARNING "Disabling OPENCV_DNN_CUDA since cuDNN is required for CUDA DNN backend.")
    set(WITH_CUDNN OFF CACHE BOOL "" FORCE)
    set(OPENCV_DNN_CUDA OFF CACHE BOOL "" FORCE)
endif()

# --------------------------------------------------------------
# OpenCV (Computer Vision Library - used for Preset Creator UI)
# --------------------------------------------------------------

# Add /FS flag for MSVC BEFORE declaring OpenCV to fix parallel compilation PDB access issues
if(MSVC)
    # Save current flags
    set(SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    
    # Add /FS flag to init variables (used by subprojects)
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} /FS" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} /FS" CACHE STRING "" FORCE)
    
    # Also add to current flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /FS")
endif()

# CRITICAL FIX: Add CUDA include directories using CMake's proper mechanism
# This works for both C++ and CUDA compilation (unlike compiler flags which break on spaces)
include_directories(SYSTEM ${CUDAToolkit_INCLUDE_DIRS})

# UPGRADED: Using OpenCV 4.x development branch with CUDA 12.9+ fix
# This includes PR #27288 which fixes nppGetStreamContext() issue for CUDA 12.9+
FetchContent_Declare(
  opencv
  GIT_REPOSITORY https://github.com/opencv/opencv.git
  GIT_TAG        4.x # Latest development branch with CUDA 13.0 support
)

# ADDED: opencv_contrib is required for many CUDA modules and advanced features (especially cudev).
FetchContent_Declare(
  opencv_contrib
  GIT_REPOSITORY https://github.com/opencv/opencv_contrib.git
  GIT_TAG        4.x # Must match the main OpenCV branch
)

# Fetch and populate opencv_contrib FIRST so we can get its source directory
FetchContent_GetProperties(opencv_contrib)
if(NOT opencv_contrib_POPULATED)
    FetchContent_Populate(opencv_contrib)
endif()

# Get the opencv_contrib source directory BEFORE configuring OpenCV
FetchContent_GetProperties(opencv_contrib SOURCE_DIR opencv_contrib_SOURCE_DIR)

# CRITICAL: Set OPENCV_EXTRA_MODULES_PATH BEFORE FetchContent_MakeAvailable(opencv)
# This ensures OpenCV can find the cudev module during configuration
set(OPENCV_EXTRA_MODULES_PATH "${opencv_contrib_SOURCE_DIR}/modules" CACHE PATH "" FORCE)

# --- OpenCV Build Configuration ---
set(BUILD_SHARED_LIBS OFF)
set(BUILD_opencv_world ON)
set(BUILD_TESTS OFF)
set(BUILD_PERF_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_opencv_apps OFF)
set(ENABLE_PRECOMPILED_HEADERS OFF)
set(BUILD_WITH_STATIC_CRT OFF) # Correctly matches JUCE's dynamic runtime
set(WITH_IPP OFF) # Disable Intel IPP to avoid header path issues

# --- CUDA and cuDNN Configuration ---
set(WITH_CUDA ON CACHE BOOL "" FORCE)
# OPENCV_DNN_CUDA and WITH_CUDNN are set above based on cuDNN availability
set(WITH_NVIDIA_NPP ON CACHE BOOL "" FORCE)
set(WITH_NVCUVID OFF) # Video decoding, disable if not needed.
set(WITH_NVCUVENC OFF) # Video encoding, disable if not needed.

# Explicitly provide CUDA paths to OpenCV (critical for finding NPP headers)
set(CUDA_TOOLKIT_ROOT_DIR "${CUDAToolkit_TARGET_DIR}" CACHE PATH "" FORCE)
set(CUDA_INCLUDE_DIRS "${CUDAToolkit_INCLUDE_DIRS}" CACHE PATH "" FORCE)
# DO NOT set CUDA_LIBRARIES to a directory - causes "x64.lib" linker error
# OpenCV will find CUDA libraries automatically via CUDA_TOOLKIT_ROOT_DIR

# --- CUDA Architecture Targeting ---
# Target NVIDIA Blackwell (RTX 5090), Ada Lovelace (40-series), and Ampere (30-series)
set(CUDA_ARCH_BIN "8.6;8.9;12.0")
# Provide forward compatibility for future architectures
set(CUDA_ARCH_PTX "12.0")

# Explicitly enable required modules.
set(BUILD_opencv_core ON)
set(BUILD_opencv_dnn ON)
set(BUILD_opencv_features2d ON)
set(BUILD_opencv_highgui ON)
set(BUILD_opencv_imgcodecs ON)
set(BUILD_opencv_imgproc ON)
set(BUILD_opencv_objdetect ON)
set(BUILD_opencv_tracking ON)
set(BUILD_opencv_video ON)
set(BUILD_opencv_videoio ON)
set(BUILD_opencv_calib3d ON)
set(OPENCV_DNN_CAFFE ON)

# --- Finalize and Build OpenCV ---
FetchContent_MakeAvailable(opencv)

# Get the OpenCV build directory for generated headers
FetchContent_GetProperties(opencv)
if(NOT opencv_POPULATED)
    FetchContent_Populate(opencv)
endif()
set(OPENCV_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/opencv-build")

# ==============================================================================
# AUTO-GENERATE opencv_modules.hpp
# ==============================================================================
# OpenCV's build system should generate this file, but sometimes it doesn't
# create it during the initial configuration. This ensures it always exists.
set(OPENCV_MODULES_HPP "${OPENCV_BUILD_DIR}/opencv2/opencv_modules.hpp")
if(NOT EXISTS "${OPENCV_MODULES_HPP}")
    message(STATUS "Generating missing opencv_modules.hpp...")
    file(MAKE_DIRECTORY "${OPENCV_BUILD_DIR}/opencv2")
    file(WRITE "${OPENCV_MODULES_HPP}"
"/*
 * This file is auto-generated. Do not edit!
 */

#ifndef OPENCV_MODULES_HPP
#define OPENCV_MODULES_HPP

#define HAVE_OPENCV_CALIB3D
#define HAVE_OPENCV_CORE
#define HAVE_OPENCV_DNN
#define HAVE_OPENCV_FEATURES2D
#define HAVE_OPENCV_FLANN
#define HAVE_OPENCV_GAPI
#define HAVE_OPENCV_HIGHGUI
#define HAVE_OPENCV_IMGCODECS
#define HAVE_OPENCV_IMGPROC
#define HAVE_OPENCV_ML
#define HAVE_OPENCV_OBJDETECT
#define HAVE_OPENCV_PHOTO
#define HAVE_OPENCV_STITCHING
#define HAVE_OPENCV_VIDEO
#define HAVE_OPENCV_VIDEOIO
#define HAVE_OPENCV_WORLD

#endif
")
    message(STATUS "Generated opencv_modules.hpp at ${OPENCV_MODULES_HPP}")
endif()
# ==============================================================================

# Restore flags after OpenCV if needed
if(MSVC)
    set(CMAKE_CXX_FLAGS "${SAVED_CMAKE_CXX_FLAGS} /FS")
    set(CMAKE_C_FLAGS "${SAVED_CMAKE_C_FLAGS} /FS")
endif()

# ==============================================================================
# Define CUDA Support Preprocessor Macro for Application Code
# ==============================================================================
# This allows C++ code to check #if WITH_CUDA_SUPPORT at compile time
# Only define if both CUDA and cuDNN are available (required for full DNN CUDA support)
if(WITH_CUDA AND CUDNN_FOUND)
    message(STATUS "✓ Enabling WITH_CUDA_SUPPORT preprocessor definition")
    message(STATUS "  - CUDA Version: ${CUDAToolkit_VERSION}")
    message(STATUS "  - cuDNN Found: ${CUDNN_INCLUDE_DIR}")
    # Will be added to targets later via target_compile_definitions
    set(CUDA_SUPPORT_ENABLED TRUE)
else()
    message(STATUS "CUDA support disabled - WITH_CUDA_SUPPORT not defined")
    if(NOT WITH_CUDA)
        message(STATUS "  Reason: WITH_CUDA is OFF")
    elseif(NOT CUDNN_FOUND)
        message(STATUS "  Reason: cuDNN not found")
    endif()
    set(CUDA_SUPPORT_ENABLED FALSE)
endif()

# ==============================================================================
# FFmpeg (Pre-built vendor binaries - SIMPLIFIED!)
# ==============================================================================
# Windows: Download from https://github.com/BtbN/FFmpeg-Builds/releases
# Extract to vendor/ffmpeg/ (see FFMPEG_SETUP_GUIDE.md)
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/../vendor/ffmpeg" CACHE PATH "Path to FFmpeg")
set(FFMPEG_FOUND FALSE)

if(WIN32)
    # Windows: Use pre-built vendor binaries
    if(EXISTS "${FFMPEG_DIR}/include/libavformat/avformat.h")
        message(STATUS "✓ FFmpeg found at ${FFMPEG_DIR}")
        
        set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
        set(FFMPEG_LIB_DIR "${FFMPEG_DIR}/lib")
        set(FFMPEG_BIN_DIR "${FFMPEG_DIR}/bin")
        
        # Find all required libraries
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
        
        message(STATUS "  - Include: ${FFMPEG_INCLUDE_DIR}")
        message(STATUS "  - Libraries: ${FFMPEG_LIBRARIES}")
        set(FFMPEG_FOUND TRUE)
    else()
        message(FATAL_ERROR 
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "❌ FFmpeg NOT FOUND!\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "Expected location: ${FFMPEG_DIR}\n"
            "\n"
            "📦 Quick Setup (5 minutes):\n"
            "  1. Download: https://github.com/BtbN/FFmpeg-Builds/releases\n"
            "     Get: ffmpeg-master-latest-win64-gpl-shared.zip\n"
            "  2. Extract to: ${FFMPEG_DIR}\n"
            "  3. Folder structure should be:\n"
            "     vendor/ffmpeg/\n"
            "       ├── bin/     (DLL files)\n"
            "       ├── include/ (header files)\n"
            "       └── lib/     (.lib files)\n"
            "\n"
            "📖 See FFMPEG_SETUP_GUIDE.md for detailed instructions\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        )
    endif()
else()
    # Linux/Mac: Use system packages
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG
            libavformat>=57.0
            libavcodec>=57.0
            libavutil>=55.0
            libswresample>=3.0
            libswscale>=5.0
        )
        if(FFMPEG_FOUND)
            set(FFMPEG_INCLUDE_DIR ${FFMPEG_INCLUDE_DIRS})
            set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
            message(STATUS "✓ FFmpeg found via pkg-config")
            message(STATUS "  - Include: ${FFMPEG_INCLUDE_DIR}")
        endif()
    endif()
    
    # Fallback: try finding libraries directly
    if(NOT FFMPEG_FOUND)
        find_path(FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h
            PATHS /usr/include /usr/local/include /opt/homebrew/include
        )
        find_library(AVFORMAT_LIBRARY avformat)
        find_library(AVCODEC_LIBRARY avcodec)
        find_library(AVUTIL_LIBRARY avutil)
        find_library(SWRESAMPLE_LIBRARY swresample)
        find_library(SWSCALE_LIBRARY swscale)
        
        if(FFMPEG_INCLUDE_DIR AND AVFORMAT_LIBRARY)
            set(FFMPEG_LIBRARIES 
                ${AVFORMAT_LIBRARY}
                ${AVCODEC_LIBRARY}
                ${AVUTIL_LIBRARY}
                ${SWRESAMPLE_LIBRARY}
                ${SWSCALE_LIBRARY}
            )
            set(FFMPEG_FOUND TRUE)
            message(STATUS "✓ FFmpeg found")
        else()
            message(FATAL_ERROR
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                "❌ FFmpeg NOT FOUND!\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                "📦 Install FFmpeg:\n"
                "  Linux:  sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev\n"
                "  macOS:  brew install ffmpeg\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            )
        endif()
    endif()
endif()

# --------------------------------------------------------------
# Rubber Band (optional, real-time timestretcher/pitch-shifter)
# --------------------------------------------------------------
set(USE_RUBBERBAND ON CACHE BOOL "Enable Rubber Band time/pitch processing")
if (USE_RUBBERBAND)
  FetchContent_Declare(rubberband_fc
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v3.3.0
  )
  FetchContent_MakeAvailable(rubberband_fc)
  # Build from single-file amalgamation to avoid external link issues
  # Use official amalgamated single-file build (works cross-platform)
  add_library(rubberband_single STATIC
    ${rubberband_fc_SOURCE_DIR}/single/RubberBandSingle.cpp
  )
  target_include_directories(rubberband_single PUBLIC
    ${rubberband_fc_SOURCE_DIR}
    ${rubberband_fc_SOURCE_DIR}/single
  )
  target_compile_definitions(rubberband_single PUBLIC
    NOMINMAX
    _USE_MATH_DEFINES
    RUBBERBAND_USE_R3=1
    RUBBERBAND_BUILD_FFT=KISSFFT
    RUBBERBAND_BUILD_RESAMPLER=NONE
  )
  set(RUBBERBAND_TARGET rubberband_single)
  set(RUBBERBAND_INCLUDE_DIR "${rubberband_fc_SOURCE_DIR}")
endif()

# ==============================================================================
# Main Engine App Target (Unchanged)
# ==============================================================================
juce_add_gui_app(ColliderApp
    PRODUCT_NAME "Collider Audio Engine"
    VERSION "0.1.0"
    COMPANY_NAME "Collider"
)

target_sources(ColliderApp PRIVATE
    Source/main.cpp
    Source/app/MainApplication.cpp
    Source/app/MainApplication.h
    Source/ui/MainComponent.cpp
    Source/ui/MainComponent.h
    Source/ui/TestHarnessComponent.cpp
    Source/ui/TestHarnessComponent.h
    Source/ui/VisualiserComponent.cpp
    Source/ui/VisualiserComponent.h
    Source/ui/DebugInfo.h
    Source/audio/AudioEngine.h
    Source/audio/AudioEngine.cpp
    Source/audio/MidiDeviceManager.h
    Source/audio/MidiDeviceManager.cpp
    Source/audio/voices/SampleVoiceProcessor.h
    Source/audio/voices/SampleVoiceProcessor.cpp
    Source/audio/voices/SynthVoiceProcessor.h
    Source/audio/voices/SynthVoiceProcessor.cpp
    Source/audio/voices/NoiseVoiceProcessor.h
    Source/audio/voices/NoiseVoiceProcessor.cpp
    Source/audio/voices/ModularVoice.h
    Source/audio/graph/VoiceProcessor.h
    Source/audio/graph/VoiceProcessor.cpp
    Source/audio/graph/ModularSynthProcessor.h
    Source/audio/graph/ModularSynthProcessor.cpp
    Source/audio/fx/FXChain.h
    Source/audio/fx/GainProcessor.h
    Source/audio/fx/GainProcessor.cpp
    Source/audio/assets/SampleBank.h
    Source/audio/assets/SampleBank.cpp
    Source/audio/modules/ModuleProcessor.h
    Source/audio/modules/ModuleProcessor.cpp
    Source/audio/modules/AudioInputModuleProcessor.h
    Source/audio/modules/AudioInputModuleProcessor.cpp
    Source/audio/modules/VCOModuleProcessor.h
    Source/audio/modules/VCOModuleProcessor.cpp
    Source/audio/modules/VCFModuleProcessor.h
    Source/audio/modules/VCFModuleProcessor.cpp
    Source/audio/modules/VCAModuleProcessor.h
    Source/audio/modules/VCAModuleProcessor.cpp
    Source/audio/modules/NoiseModuleProcessor.h
    Source/audio/modules/NoiseModuleProcessor.cpp
    Source/audio/modules/LFOModuleProcessor.h
    Source/audio/modules/LFOModuleProcessor.cpp
    Source/audio/modules/ADSRModuleProcessor.h
    Source/audio/modules/ADSRModuleProcessor.cpp
    Source/audio/modules/MixerModuleProcessor.h
    Source/audio/modules/MixerModuleProcessor.cpp
    Source/audio/modules/CVMixerModuleProcessor.h
    Source/audio/modules/CVMixerModuleProcessor.cpp
    Source/audio/modules/DelayModuleProcessor.h
    Source/audio/modules/DelayModuleProcessor.cpp
    Source/audio/modules/ReverbModuleProcessor.h
    Source/audio/modules/ReverbModuleProcessor.cpp
    Source/audio/modules/SAndHModuleProcessor.h
    Source/audio/modules/SAndHModuleProcessor.cpp
    Source/audio/modules/AttenuverterModuleProcessor.h
    Source/audio/modules/AttenuverterModuleProcessor.cpp
    Source/audio/modules/ScopeModuleProcessor.h
    Source/audio/modules/ScopeModuleProcessor.cpp
    Source/audio/modules/StepSequencerModuleProcessor.h
    Source/audio/modules/StepSequencerModuleProcessor.cpp
    Source/audio/modules/MultiSequencerModuleProcessor.h
    Source/audio/modules/MultiSequencerModuleProcessor.cpp
    Source/audio/modules/LagProcessorModuleProcessor.h
    Source/audio/modules/LagProcessorModuleProcessor.cpp
    Source/audio/modules/DeCrackleModuleProcessor.h
    Source/audio/modules/DeCrackleModuleProcessor.cpp
    Source/audio/modules/GraphicEQModuleProcessor.h
    Source/audio/modules/GraphicEQModuleProcessor.cpp
    Source/audio/modules/FrequencyGraphModuleProcessor.h
    Source/audio/modules/FrequencyGraphModuleProcessor.cpp
    Source/audio/modules/ChorusModuleProcessor.h
    Source/audio/modules/ChorusModuleProcessor.cpp
    Source/audio/modules/PhaserModuleProcessor.h
    Source/audio/modules/PhaserModuleProcessor.cpp
    Source/audio/modules/CompressorModuleProcessor.h
    Source/audio/modules/CompressorModuleProcessor.cpp
    Source/audio/modules/RecordModuleProcessor.h
    Source/audio/modules/RecordModuleProcessor.cpp
    Source/audio/modules/CommentModuleProcessor.h
    Source/audio/modules/CommentModuleProcessor.cpp
    Source/audio/modules/LimiterModuleProcessor.h
    Source/audio/modules/LimiterModuleProcessor.cpp
    Source/audio/modules/GateModuleProcessor.h
    Source/audio/modules/GateModuleProcessor.cpp
    Source/audio/modules/DriveModuleProcessor.h
    Source/audio/modules/DriveModuleProcessor.cpp
    Source/audio/modules/MathModuleProcessor.h
    Source/audio/modules/MathModuleProcessor.cpp
    Source/audio/modules/MapRangeModuleProcessor.h
    Source/audio/modules/MapRangeModuleProcessor.cpp
    Source/audio/modules/ComparatorModuleProcessor.h
    Source/audio/modules/ComparatorModuleProcessor.cpp
    Source/audio/modules/RandomModuleProcessor.h
    Source/audio/modules/RandomModuleProcessor.cpp
    Source/audio/modules/RateModuleProcessor.h
    Source/audio/modules/RateModuleProcessor.cpp
    Source/audio/modules/QuantizerModuleProcessor.h
    Source/audio/modules/QuantizerModuleProcessor.cpp
    Source/audio/modules/SequentialSwitchModuleProcessor.h
    Source/audio/modules/SequentialSwitchModuleProcessor.cpp
    Source/audio/modules/LogicModuleProcessor.h
    Source/audio/modules/LogicModuleProcessor.cpp
    Source/audio/modules/ClockDividerModuleProcessor.h
    Source/audio/modules/ClockDividerModuleProcessor.cpp
    Source/audio/modules/WaveshaperModuleProcessor.h
    Source/audio/modules/WaveshaperModuleProcessor.cpp
    Source/audio/modules/MultiBandShaperModuleProcessor.h
    Source/audio/modules/MultiBandShaperModuleProcessor.cpp
    Source/audio/modules/GranulatorModuleProcessor.h
    Source/audio/modules/GranulatorModuleProcessor.cpp
    Source/audio/modules/HarmonicShaperModuleProcessor.h
    Source/audio/modules/HarmonicShaperModuleProcessor.cpp
    Source/audio/modules/ValueModuleProcessor.h
    Source/audio/modules/ValueModuleProcessor.cpp
    Source/audio/modules/TimePitchModuleProcessor.h
    Source/audio/modules/TimePitchModuleProcessor.cpp
    Source/audio/modules/MIDIPlayerModuleProcessor.h
    Source/audio/modules/MIDIPlayerModuleProcessor.cpp
        Source/audio/modules/TrackMixerModuleProcessor.h
        Source/audio/modules/TrackMixerModuleProcessor.cpp
    Source/audio/modules/PolyVCOModuleProcessor.h
    Source/audio/modules/PolyVCOModuleProcessor.cpp
        Source/audio/modules/DebugModuleProcessor.h
        Source/audio/modules/DebugModuleProcessor.cpp
        Source/audio/modules/CommentModuleProcessor.h
        Source/audio/modules/CommentModuleProcessor.cpp
        Source/audio/modules/InputDebugModuleProcessor.h
        Source/audio/modules/InputDebugModuleProcessor.cpp
    Source/audio/modules/TTSPerformerModuleProcessor.h
    Source/audio/modules/TTSPerformerModuleProcessor.cpp
    Source/audio/modules/TimingData.h
    Source/audio/modules/SampleLoaderModuleProcessor.h
    Source/audio/modules/SampleLoaderModuleProcessor.cpp
    Source/audio/modules/FunctionGeneratorModuleProcessor.h
    Source/audio/modules/FunctionGeneratorModuleProcessor.cpp
    Source/audio/modules/BestPracticeNodeProcessor.h
    Source/audio/modules/BestPracticeNodeProcessor.cpp
    Source/audio/modules/TimelineModuleProcessor.h
    Source/audio/modules/TimelineModuleProcessor.cpp
    Source/audio/modules/VocalTractFilterModuleProcessor.h
    Source/audio/modules/VocalTractFilterModuleProcessor.cpp
    Source/audio/modules/VstHostModuleProcessor.h
    Source/audio/modules/VstHostModuleProcessor.cpp
    Source/audio/modules/ShapingOscillatorModuleProcessor.h
    Source/audio/modules/ShapingOscillatorModuleProcessor.cpp
    Source/audio/modules/InletModuleProcessor.h
    Source/audio/modules/InletModuleProcessor.cpp
    Source/audio/modules/OutletModuleProcessor.h
    Source/audio/modules/OutletModuleProcessor.cpp
    Source/audio/modules/MetaModuleProcessor.h
    Source/audio/modules/MetaModuleProcessor.cpp
    Source/audio/modules/SnapshotSequencerModuleProcessor.h
    Source/audio/modules/SnapshotSequencerModuleProcessor.cpp
    Source/audio/modules/MIDICVModuleProcessor.h
    Source/audio/modules/MIDICVModuleProcessor.cpp
    Source/audio/modules/MIDIFadersModuleProcessor.h
    Source/audio/modules/MIDIFadersModuleProcessor.cpp
    Source/audio/modules/MIDIKnobsModuleProcessor.h
    Source/audio/modules/MIDIKnobsModuleProcessor.cpp
    Source/audio/modules/MIDIButtonsModuleProcessor.h
    Source/audio/modules/MIDIButtonsModuleProcessor.cpp
    Source/audio/modules/MIDIJogWheelModuleProcessor.h
    Source/audio/modules/MIDIJogWheelModuleProcessor.cpp
    Source/audio/modules/MIDIPadModuleProcessor.h
    Source/audio/modules/MIDIPadModuleProcessor.cpp
    Source/audio/modules/MidiLoggerModuleProcessor.h
    Source/audio/modules/MidiLoggerModuleProcessor.cpp
    Source/audio/modules/TempoClockModuleProcessor.h
    Source/audio/modules/TempoClockModuleProcessor.cpp
    Source/audio/modules/PhysicsModuleProcessor.h
    Source/audio/modules/PhysicsModuleProcessor.cpp
    Source/audio/modules/StrokeSequencerModuleProcessor.h
    Source/audio/modules/StrokeSequencerModuleProcessor.cpp
    Source/audio/modules/TapTempo.h
    Source/audio/modules/TapTempo.cpp
    Source/audio/modules/BPMMonitorModuleProcessor.h
    Source/audio/modules/BPMMonitorModuleProcessor.cpp
    Source/audio/modules/AnimationModuleProcessor.h
    Source/audio/modules/AnimationModuleProcessor.cpp
    Source/video/VideoFrameManager.h
    Source/video/CameraEnumerator.h
    Source/audio/modules/WebcamLoaderModule.h
    Source/audio/modules/WebcamLoaderModule.cpp
    Source/audio/modules/VideoFileLoaderModule.h
    Source/audio/modules/VideoFileLoaderModule.cpp
    Source/audio/modules/FFmpegAudioReader.h
    Source/audio/modules/FFmpegAudioReader.cpp
    Source/audio/modules/TimeStretcherAudioSource.h
    Source/audio/modules/TimeStretcherAudioSource.cpp
    Source/audio/modules/VideoFXModule.h
    Source/audio/modules/VideoFXModule.cpp
    Source/audio/modules/MovementDetectorModule.h
    Source/audio/modules/MovementDetectorModule.cpp
    Source/audio/modules/HumanDetectorModule.h
    Source/audio/modules/HumanDetectorModule.cpp
    Source/audio/modules/PoseEstimatorModule.h
    Source/audio/modules/PoseEstimatorModule.cpp
    Source/audio/modules/HandTrackerModule.h
    Source/audio/modules/HandTrackerModule.cpp
    Source/audio/modules/FaceTrackerModule.h
    Source/audio/modules/FaceTrackerModule.cpp
    Source/audio/modules/ObjectDetectorModule.h
    Source/audio/modules/ObjectDetectorModule.cpp
    Source/audio/modules/ColorTrackerModule.h
    Source/audio/modules/ColorTrackerModule.cpp
    Source/audio/modules/ContourDetectorModule.h
    Source/audio/modules/ContourDetectorModule.cpp
    Source/audio/modules/SemanticSegmentationModule.h
    Source/audio/modules/SemanticSegmentationModule.cpp
    Source/audio/modules/CropVideoModule.h
    Source/audio/modules/CropVideoModule.cpp
    Source/animation/AnimationData.h
    Source/animation/RawAnimationData.h
    Source/animation/RawAnimationData.cpp
    Source/animation/AnimationBinder.h
    Source/animation/AnimationBinder.cpp
    Source/animation/Animator.h
    Source/animation/Animator.cpp
    Source/animation/AnimationFileLoader.h
    Source/animation/AnimationFileLoader.cpp
    Source/animation/GltfLoader.h
    Source/animation/GltfLoader.cpp
    Source/animation/FbxLoader.h
    Source/animation/FbxLoader.cpp
    Source/animation/AnimationRenderer.h
    Source/animation/AnimationRenderer.cpp
    Source/ipc/IpcServer.cpp
    Source/ipc/IpcServer.h
    Source/ipc/OscClient.h
    Source/ipc/CommandBus.cpp
    Source/ipc/CommandBus.h
    Source/audio/utils/VoiceDeletionUtils.h
    Source/audio/dsp/TimePitchProcessor.h
    Source/utils/RtLogger.h
    Source/utils/RtLogger.cpp
)

target_compile_definitions(ColliderApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ColliderApp,PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ColliderApp,VERSION>"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    $<$<BOOL:${USE_RUBBERBAND}>:USE_RUBBERBAND=1>
    $<$<NOT:$<BOOL:${USE_RUBBERBAND}>>:USE_RUBBERBAND=0>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:WITH_CUDA_SUPPORT=1>
)

target_link_libraries(ColliderApp PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_osc
    soundtouch
    box2d
    glm::glm
    tinygltf
    ufbx_static
    opencv_world
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_TARGET}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVFORMAT_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVCODEC_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVUTIL_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${SWRESAMPLE_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${SWSCALE_LIBRARY}>
    # Piper TTS libraries (pre-built)
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)

# Link CUDA runtime for OpenCV CUDA symbols
# Use CUDAToolkit::cudart if available, otherwise link directly
if(TARGET CUDAToolkit::cudart)
    target_link_libraries(ColliderApp PRIVATE CUDAToolkit::cudart)
elseif(CUDA_CUDART_LIBRARY)
    target_link_libraries(ColliderApp PRIVATE ${CUDA_CUDART_LIBRARY})
endif()

# Link Windows libraries for native camera enumeration (DirectShow)
if(WIN32)
    target_link_libraries(ColliderApp PRIVATE ole32 strmiids)
endif()

# Add include directories for ColliderApp
target_include_directories(ColliderApp PRIVATE
    ${PIPER_DIR}
    ${ONNXRUNTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/../vendor/openvino_toolkit_windows_2025.3.0.19807.44526285f24_x86_64/samples/cpp/thirdparty/nlohmann_json/single_include
    ${CMAKE_SOURCE_DIR}/../soundtouch/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/source/SoundTouch
    ${ufbx_SOURCE_DIR}
    $<$<BOOL:${FFMPEG_FOUND}>:${FFMPEG_INCLUDE_DIR}>
    # OpenCV include directories - source and generated headers
    ${opencv_SOURCE_DIR}/include
    ${opencv_SOURCE_DIR}/modules/core/include
    ${opencv_SOURCE_DIR}/modules/imgproc/include
    ${opencv_SOURCE_DIR}/modules/imgcodecs/include
    ${opencv_SOURCE_DIR}/modules/videoio/include
    ${opencv_SOURCE_DIR}/modules/highgui/include
    ${opencv_SOURCE_DIR}/modules/video/include
    ${opencv_SOURCE_DIR}/modules/objdetect/include
    ${opencv_SOURCE_DIR}/modules/features2d/include
    ${opencv_SOURCE_DIR}/modules/calib3d/include
    ${opencv_SOURCE_DIR}/modules/dnn/include              # For DNN headers (dnn.hpp)
    ${OPENCV_BUILD_DIR}
    ${OPENCV_BUILD_DIR}/modules/world
    # OpenCV CUDA modules (from opencv_contrib source)
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaimgproc/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudawarping/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaarithm/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudafilters/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudafeatures2d/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaobjdetect/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaoptflow/include>
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_INCLUDE_DIR}>
)

# ==============================================================================
# Preset Creator App Target (Final Corrected Version)
# ==============================================================================

juce_add_gui_app(PresetCreatorApp
    PRODUCT_NAME "Preset Creator"
    VERSION "0.1.0"
    COMPANY_NAME "Collider"
)

target_sources(PresetCreatorApp PRIVATE
    # Your application sources
    Source/preset_creator/PresetCreatorMain.cpp
    Source/preset_creator/PinDatabase.h
    Source/preset_creator/PinDatabase.cpp
    Source/preset_creator/NotificationManager.h
    Source/preset_creator/NotificationManager.cpp
    Source/preset_creator/SavePresetJob.h
    Source/preset_creator/SavePresetJob.cpp
    Source/preset_creator/ImGuiNodeEditorComponent.h
    Source/preset_creator/ImGuiNodeEditorComponent.cpp
    Source/preset_creator/PresetCreatorComponent.h
    Source/preset_creator/PresetCreatorComponent.cpp
    Source/preset_creator/ControllerPresetManager.h
    Source/preset_creator/ControllerPresetManager.cpp

    # Add ImGui, imnodes, and the backend DIRECTLY as source files
    ${imgui_fc_SOURCE_DIR}/imgui.cpp
    ${imgui_fc_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_fc_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_fc_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_fc_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
    ${imnodes_fc_SOURCE_DIR}/imnodes.cpp

    # Your other reused engine modules
    Source/audio/MidiDeviceManager.h
    Source/audio/MidiDeviceManager.cpp
    Source/audio/graph/ModularSynthProcessor.h
    Source/audio/graph/ModularSynthProcessor.cpp
    Source/audio/modules/ModuleProcessor.h
    Source/audio/modules/ModuleProcessor.cpp
    Source/audio/modules/AudioInputModuleProcessor.h
    Source/audio/modules/AudioInputModuleProcessor.cpp
    Source/audio/modules/VCOModuleProcessor.h
    Source/audio/modules/VCOModuleProcessor.cpp
    Source/audio/modules/VCFModuleProcessor.h
    Source/audio/modules/VCFModuleProcessor.cpp
    Source/audio/modules/VCAModuleProcessor.h
    Source/audio/modules/VCAModuleProcessor.cpp
    Source/audio/modules/NoiseModuleProcessor.h
    Source/audio/modules/NoiseModuleProcessor.cpp
    Source/audio/modules/LFOModuleProcessor.h
    Source/audio/modules/LFOModuleProcessor.cpp
    Source/audio/modules/ADSRModuleProcessor.h
    Source/audio/modules/ADSRModuleProcessor.cpp
    Source/audio/modules/MixerModuleProcessor.h
    Source/audio/modules/MixerModuleProcessor.cpp
    Source/audio/modules/CVMixerModuleProcessor.h
    Source/audio/modules/CVMixerModuleProcessor.cpp
    Source/audio/modules/DelayModuleProcessor.h
    Source/audio/modules/DelayModuleProcessor.cpp
    Source/audio/modules/ReverbModuleProcessor.h
    Source/audio/modules/ReverbModuleProcessor.cpp
    Source/audio/modules/SAndHModuleProcessor.h
    Source/audio/modules/SAndHModuleProcessor.cpp
    Source/audio/modules/AttenuverterModuleProcessor.h
    Source/audio/modules/AttenuverterModuleProcessor.cpp
    Source/audio/modules/ScopeModuleProcessor.h
    Source/audio/modules/ScopeModuleProcessor.cpp
    Source/audio/modules/StepSequencerModuleProcessor.h
    Source/audio/modules/StepSequencerModuleProcessor.cpp
    Source/audio/modules/MathModuleProcessor.h
    Source/audio/modules/MathModuleProcessor.cpp
    Source/audio/modules/MapRangeModuleProcessor.h
    Source/audio/modules/MapRangeModuleProcessor.cpp
    Source/audio/modules/ComparatorModuleProcessor.h
    Source/audio/modules/ComparatorModuleProcessor.cpp
    Source/audio/modules/RandomModuleProcessor.h
    Source/audio/modules/RandomModuleProcessor.cpp
    Source/audio/modules/RateModuleProcessor.h
    Source/audio/modules/RateModuleProcessor.cpp
    Source/audio/modules/QuantizerModuleProcessor.h
    Source/audio/modules/QuantizerModuleProcessor.cpp
    Source/audio/modules/SequentialSwitchModuleProcessor.h
    Source/audio/modules/SequentialSwitchModuleProcessor.cpp
    Source/audio/modules/LogicModuleProcessor.h
    Source/audio/modules/LogicModuleProcessor.cpp
    Source/audio/modules/ClockDividerModuleProcessor.h
    Source/audio/modules/ClockDividerModuleProcessor.cpp
    Source/audio/modules/WaveshaperModuleProcessor.h
    Source/audio/modules/WaveshaperModuleProcessor.cpp
    Source/audio/modules/MultiBandShaperModuleProcessor.h
    Source/audio/modules/MultiBandShaperModuleProcessor.cpp
    Source/audio/modules/GranulatorModuleProcessor.h
    Source/audio/modules/GranulatorModuleProcessor.cpp
    Source/audio/modules/HarmonicShaperModuleProcessor.h
    Source/audio/modules/HarmonicShaperModuleProcessor.cpp
    Source/audio/modules/ValueModuleProcessor.h
    Source/audio/modules/ValueModuleProcessor.cpp
        Source/audio/modules/DebugModuleProcessor.h
        Source/audio/modules/DebugModuleProcessor.cpp
        Source/audio/modules/CommentModuleProcessor.h
        Source/audio/modules/CommentModuleProcessor.cpp
        Source/audio/modules/InputDebugModuleProcessor.h
        Source/audio/modules/InputDebugModuleProcessor.cpp
    Source/audio/modules/TimePitchModuleProcessor.h
    Source/audio/modules/TimePitchModuleProcessor.cpp
    Source/audio/modules/MIDIPlayerModuleProcessor.h
    Source/audio/modules/MIDIPlayerModuleProcessor.cpp
        Source/audio/modules/TrackMixerModuleProcessor.h
        Source/audio/modules/TrackMixerModuleProcessor.cpp
    Source/audio/modules/PolyVCOModuleProcessor.h
    Source/audio/modules/PolyVCOModuleProcessor.cpp
    
    Source/audio/modules/TTSPerformerModuleProcessor.h
    Source/audio/modules/TTSPerformerModuleProcessor.cpp
    Source/audio/modules/TimingData.h
    Source/audio/modules/SampleLoaderModuleProcessor.h
    Source/audio/modules/SampleLoaderModuleProcessor.cpp
    Source/audio/modules/FunctionGeneratorModuleProcessor.h
    Source/audio/modules/FunctionGeneratorModuleProcessor.cpp
    Source/audio/modules/BestPracticeNodeProcessor.h
    Source/audio/modules/BestPracticeNodeProcessor.cpp
    Source/audio/modules/TimelineModuleProcessor.h
    Source/audio/modules/TimelineModuleProcessor.cpp
    Source/audio/modules/ShapingOscillatorModuleProcessor.h
    Source/audio/modules/ShapingOscillatorModuleProcessor.cpp
    Source/audio/voices/SampleVoiceProcessor.h
    Source/audio/voices/SampleVoiceProcessor.cpp
    Source/audio/graph/VoiceProcessor.h
    Source/audio/graph/VoiceProcessor.cpp
    Source/audio/assets/SampleBank.h
    Source/audio/assets/SampleBank.cpp
    Source/utils/RtLogger.h
    Source/utils/RtLogger.cpp
    Source/audio/modules/VocalTractFilterModuleProcessor.h
    Source/audio/modules/VocalTractFilterModuleProcessor.cpp
    Source/audio/modules/VstHostModuleProcessor.h
    Source/audio/modules/VstHostModuleProcessor.cpp
    Source/audio/modules/MultiSequencerModuleProcessor.h
    Source/audio/modules/MultiSequencerModuleProcessor.cpp
    Source/audio/modules/LagProcessorModuleProcessor.h
    Source/audio/modules/LagProcessorModuleProcessor.cpp
    Source/audio/modules/DeCrackleModuleProcessor.h
    Source/audio/modules/DeCrackleModuleProcessor.cpp
    Source/audio/modules/GraphicEQModuleProcessor.h
    Source/audio/modules/GraphicEQModuleProcessor.cpp
    Source/audio/modules/FrequencyGraphModuleProcessor.h
    Source/audio/modules/FrequencyGraphModuleProcessor.cpp
    Source/audio/modules/ChorusModuleProcessor.h
    Source/audio/modules/ChorusModuleProcessor.cpp
    Source/audio/modules/PhaserModuleProcessor.h
    Source/audio/modules/PhaserModuleProcessor.cpp
    Source/audio/modules/CompressorModuleProcessor.h
    Source/audio/modules/CompressorModuleProcessor.cpp
    Source/audio/modules/RecordModuleProcessor.h
    Source/audio/modules/RecordModuleProcessor.cpp
    Source/audio/modules/CommentModuleProcessor.h
    Source/audio/modules/CommentModuleProcessor.cpp
    Source/audio/modules/LimiterModuleProcessor.h
    Source/audio/modules/LimiterModuleProcessor.cpp
    Source/audio/modules/GateModuleProcessor.h
    Source/audio/modules/GateModuleProcessor.cpp
    Source/audio/modules/DriveModuleProcessor.h
    Source/audio/modules/DriveModuleProcessor.cpp
    Source/audio/modules/InletModuleProcessor.h
    Source/audio/modules/InletModuleProcessor.cpp
    Source/audio/modules/OutletModuleProcessor.h
    Source/audio/modules/OutletModuleProcessor.cpp
    Source/audio/modules/MetaModuleProcessor.h
    Source/audio/modules/MetaModuleProcessor.cpp
    Source/audio/modules/SnapshotSequencerModuleProcessor.h
    Source/audio/modules/SnapshotSequencerModuleProcessor.cpp
    Source/audio/modules/MIDICVModuleProcessor.h
    Source/audio/modules/MIDICVModuleProcessor.cpp
    Source/audio/modules/MIDIFadersModuleProcessor.h
    Source/audio/modules/MIDIFadersModuleProcessor.cpp
    Source/audio/modules/MIDIKnobsModuleProcessor.h
    Source/audio/modules/MIDIKnobsModuleProcessor.cpp
    Source/audio/modules/MIDIButtonsModuleProcessor.h
    Source/audio/modules/MIDIButtonsModuleProcessor.cpp
    Source/audio/modules/MIDIJogWheelModuleProcessor.h
    Source/audio/modules/MIDIJogWheelModuleProcessor.cpp
    Source/audio/modules/MIDIPadModuleProcessor.h
    Source/audio/modules/MIDIPadModuleProcessor.cpp
    Source/audio/modules/MidiLoggerModuleProcessor.h
    Source/audio/modules/MidiLoggerModuleProcessor.cpp
    Source/audio/modules/TempoClockModuleProcessor.h
    Source/audio/modules/TempoClockModuleProcessor.cpp
    Source/audio/modules/PhysicsModuleProcessor.h
    Source/audio/modules/PhysicsModuleProcessor.cpp
    Source/audio/modules/StrokeSequencerModuleProcessor.h
    Source/audio/modules/StrokeSequencerModuleProcessor.cpp
    Source/audio/modules/TapTempo.h
    Source/audio/modules/TapTempo.cpp
    Source/audio/modules/BPMMonitorModuleProcessor.h
    Source/audio/modules/BPMMonitorModuleProcessor.cpp
    Source/audio/modules/AnimationModuleProcessor.h
    Source/audio/modules/AnimationModuleProcessor.cpp
    Source/video/VideoFrameManager.h
    Source/video/CameraEnumerator.h
    Source/audio/modules/WebcamLoaderModule.h
    Source/audio/modules/WebcamLoaderModule.cpp
    Source/audio/modules/VideoFileLoaderModule.h
    Source/audio/modules/VideoFileLoaderModule.cpp
    Source/audio/modules/FFmpegAudioReader.h
    Source/audio/modules/FFmpegAudioReader.cpp
    Source/audio/modules/TimeStretcherAudioSource.h
    Source/audio/modules/TimeStretcherAudioSource.cpp
    Source/audio/modules/VideoFXModule.h
    Source/audio/modules/VideoFXModule.cpp
    Source/audio/modules/MovementDetectorModule.h
    Source/audio/modules/MovementDetectorModule.cpp
    Source/audio/modules/HumanDetectorModule.h
    Source/audio/modules/HumanDetectorModule.cpp
    Source/audio/modules/PoseEstimatorModule.h
    Source/audio/modules/PoseEstimatorModule.cpp
    Source/audio/modules/HandTrackerModule.h
    Source/audio/modules/HandTrackerModule.cpp
    Source/audio/modules/FaceTrackerModule.h
    Source/audio/modules/FaceTrackerModule.cpp
    Source/audio/modules/ObjectDetectorModule.h
    Source/audio/modules/ObjectDetectorModule.cpp
    Source/audio/modules/ColorTrackerModule.h
    Source/audio/modules/ColorTrackerModule.cpp
    Source/audio/modules/ContourDetectorModule.h
    Source/audio/modules/ContourDetectorModule.cpp
    Source/audio/modules/SemanticSegmentationModule.h
    Source/audio/modules/SemanticSegmentationModule.cpp
    Source/audio/modules/CropVideoModule.h
    Source/audio/modules/CropVideoModule.cpp
    Source/animation/AnimationData.h
    Source/animation/RawAnimationData.h
    Source/animation/RawAnimationData.cpp
    Source/animation/AnimationBinder.h
    Source/animation/AnimationBinder.cpp
    Source/animation/Animator.h
    Source/animation/Animator.cpp
    Source/animation/AnimationFileLoader.h
    Source/animation/AnimationFileLoader.cpp
    Source/animation/GltfLoader.h
    Source/animation/GltfLoader.cpp
    Source/animation/FbxLoader.h
    Source/animation/FbxLoader.cpp
    Source/animation/AnimationRenderer.h
    Source/animation/AnimationRenderer.cpp
)

# This target now needs to know where to find all the headers
target_include_directories(PresetCreatorApp PRIVATE
    ${imgui_fc_SOURCE_DIR}
    ${imgui_fc_SOURCE_DIR}/backends
    ${imnodes_fc_SOURCE_DIR}
    ${imgui_juce_fc_SOURCE_DIR}
    ${PIPER_DIR}
    ${ONNXRUNTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/../vendor/openvino_toolkit_windows_2025.3.0.19807.44526285f24_x86_64/samples/cpp/thirdparty/nlohmann_json/single_include
    ${CMAKE_SOURCE_DIR}/../soundtouch/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/source/SoundTouch
    ${ufbx_SOURCE_DIR}
    $<$<BOOL:${FFMPEG_FOUND}>:${FFMPEG_INCLUDE_DIR}>
    # OpenCV include directories - source and generated headers
    ${opencv_SOURCE_DIR}/include
    ${opencv_SOURCE_DIR}/modules/core/include
    ${opencv_SOURCE_DIR}/modules/imgproc/include
    ${opencv_SOURCE_DIR}/modules/imgcodecs/include
    ${opencv_SOURCE_DIR}/modules/videoio/include
    ${opencv_SOURCE_DIR}/modules/highgui/include
    ${opencv_SOURCE_DIR}/modules/video/include
    ${opencv_SOURCE_DIR}/modules/objdetect/include
    ${opencv_SOURCE_DIR}/modules/features2d/include
    ${opencv_SOURCE_DIR}/modules/calib3d/include
    ${opencv_SOURCE_DIR}/modules/dnn/include              # For DNN headers (dnn.hpp)
    ${OPENCV_BUILD_DIR}
    ${OPENCV_BUILD_DIR}/modules/world
    # OpenCV CUDA modules (from opencv_contrib source)
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaimgproc/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudawarping/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaarithm/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudafilters/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudafeatures2d/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaobjdetect/include>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:${opencv_contrib_SOURCE_DIR}/modules/cudaoptflow/include>
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_INCLUDE_DIR}>
)

# This target also needs the compile definitions
target_compile_definitions(PresetCreatorApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PresetCreatorApp,PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PresetCreatorApp,VERSION>"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    IMGUI_IMPL_JUCE_BEZEL=0
    IMGUI_DEFINE_MATH_OPERATORS
    IMNODES_NAMESPACE=ImNodes
    IMNODES_STATIC_DEFINE
    PRESET_CREATOR_UI=1
    $<$<BOOL:${USE_RUBBERBAND}>:USE_RUBBERBAND=1>
    $<$<NOT:$<BOOL:${USE_RUBBERBAND}>>:USE_RUBBERBAND=0>
    $<$<BOOL:${CUDA_SUPPORT_ENABLED}>:WITH_CUDA_SUPPORT=1>
)

target_link_libraries(PresetCreatorApp PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    imgui_impl_juce
    soundtouch
    box2d
    glm::glm
    tinygltf
    ufbx_static
    opencv_world
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_TARGET}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVFORMAT_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVCODEC_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${AVUTIL_LIBRARY}>
    $<$<BOOL:${FFMPEG_FOUND}>:${SWRESAMPLE_LIBRARY}>
    # Piper TTS libraries (pre-built)
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)

# Link CUDA runtime for OpenCV CUDA symbols
# Use CUDAToolkit::cudart if available, otherwise link directly
if(TARGET CUDAToolkit::cudart)
    target_link_libraries(PresetCreatorApp PRIVATE CUDAToolkit::cudart)
elseif(CUDA_CUDART_LIBRARY)
    target_link_libraries(PresetCreatorApp PRIVATE ${CUDA_CUDART_LIBRARY})
endif()

# Link Windows libraries for native camera enumeration (DirectShow)
if(WIN32)
    target_link_libraries(PresetCreatorApp PRIVATE ole32 strmiids)
endif()

if(WIN32)
    # Copy Piper executable and DLLs to output directories
    add_custom_command(
        TARGET ColliderApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/piper.exe"
                "$<TARGET_FILE_DIR:ColliderApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/piper_phonemize.dll"
                "$<TARGET_FILE_DIR:ColliderApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/espeak-ng.dll"
                "$<TARGET_FILE_DIR:ColliderApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:ColliderApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:ColliderApp>"
        # Copy OpenCV FFmpeg video backend DLL (relative to build tree)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/opencv_videoio_ffmpeg4130_64.dll"
                "$<TARGET_FILE_DIR:ColliderApp>"
    )
    
    # Copy FFmpeg DLLs separately (if FFmpeg is found)
    # We need to copy each DLL individually to avoid generator expression issues
    if(FFMPEG_FOUND AND EXISTS "${FFMPEG_BIN_DIR}")
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
        if(FFMPEG_DLLS)
            foreach(DLL ${FFMPEG_DLLS})
                get_filename_component(DLL_NAME ${DLL} NAME)
                add_custom_command(
                    TARGET ColliderApp POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DLL}"
                            "$<TARGET_FILE_DIR:ColliderApp>"
                    COMMENT "Copying ${DLL_NAME} to ColliderApp"
                )
                add_custom_command(
                    TARGET PresetCreatorApp POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DLL}"
                            "$<TARGET_FILE_DIR:PresetCreatorApp>"
                    COMMENT "Copying ${DLL_NAME} to PresetCreatorApp"
                )
            endforeach()
        endif()
    endif()
    
    # Continue with other files for ColliderApp
    add_custom_command(
        TARGET ColliderApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PIPER_DIR}/espeak-ng-data"
                "$<TARGET_FILE_DIR:ColliderApp>/espeak-ng-data"
        COMMENT "Copying espeak-ng-data to ColliderApp output directory"
    )
    add_custom_command(
        TARGET ColliderApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${opencv_SOURCE_DIR}/data/haarcascades/haarcascade_frontalface_default.xml"
                "$<TARGET_FILE_DIR:ColliderApp>"
        COMMENT "Copying OpenCV data to ColliderApp output directory"
    )
    add_custom_command(
        TARGET PresetCreatorApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/piper.exe"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/piper_phonemize.dll"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIPER_DIR}/espeak-ng.dll"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        # Copy OpenCV FFmpeg video backend DLL (relative to build tree)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/opencv_videoio_ffmpeg4130_64.dll"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PIPER_DIR}/espeak-ng-data"
                "$<TARGET_FILE_DIR:PresetCreatorApp>/espeak-ng-data"
        # NEW (Corrected): Copy the entire assets folder
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:PresetCreatorApp>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${opencv_SOURCE_DIR}/data/haarcascades/haarcascade_frontalface_default.xml"
                "$<TARGET_FILE_DIR:PresetCreatorApp>"
        COMMENT "Copying Piper TTS, OpenPose, FFmpeg, and other runtime assets to PresetCreatorApp output directory"
    )
endif()
