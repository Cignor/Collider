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
)

target_link_libraries(ColliderApp PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_osc
    soundtouch
    box2d
    glm::glm
    tinygltf
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_TARGET}>
    # Piper TTS libraries (pre-built)
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)

# Add include directories for ColliderApp
target_include_directories(ColliderApp PRIVATE
    ${PIPER_DIR}
    ${ONNXRUNTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/../vendor/openvino_toolkit_windows_2025.3.0.19807.44526285f24_x86_64/samples/cpp/thirdparty/nlohmann_json/single_include
    ${CMAKE_SOURCE_DIR}/../soundtouch/include
    ${CMAKE_SOURCE_DIR}/../soundtouch/source/SoundTouch
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
    $<$<BOOL:${USE_RUBBERBAND}>:${RUBBERBAND_TARGET}>
    # Piper TTS libraries (pre-built)
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)

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
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PIPER_DIR}/espeak-ng-data"
                "$<TARGET_FILE_DIR:ColliderApp>/espeak-ng-data"
        COMMENT "Copying Piper TTS runtime files to ColliderApp output directory"
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
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PIPER_DIR}/espeak-ng-data"
                "$<TARGET_FILE_DIR:PresetCreatorApp>/espeak-ng-data"
        COMMENT "Copying Piper TTS runtime files to PresetCreatorApp output directory"
    )
endif()
