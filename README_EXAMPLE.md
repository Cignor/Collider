# Collider Modular Synthesizer

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey.svg)]()
[![JUCE](https://img.shields.io/badge/JUCE-7.0.9-orange.svg)](https://juce.com)

**A powerful, node-based modular synthesizer with computer vision integration, physics simulation, and 100+ synthesis modules.**

Collider is a real-time modular synthesizer built on the JUCE framework, featuring an intuitive visual node editor, extensive modulation capabilities, and unique integrations with computer vision, physics simulation, and animation systems.

---

## ✨ Key Features

- **100+ Synthesis Nodes** across 11 categories (Sources, Effects, Modulators, Utilities, Sequencers, MIDI, Analysis, TTS, Special, Computer Vision, System)
- **Computer Vision Integration** - Real-time video processing with pose estimation, object detection, hand/face tracking, and more
- **Physics-Based Audio** - 2D physics simulation (Box2D) driving synthesis parameters
- **Animation-Driven Synthesis** - Load 3D animations (glTF/FBX) and extract joint data for CV modulation
- **VST Plugin Hosting** - Load and use VST2/VST3 plugins within your patches
- **Text-to-Speech** - Advanced TTS engine with word-level sequencing and formant filtering
- **Visual Node Editor** - Intuitive ImGui-based interface with drag-and-drop patching
- **Comprehensive Modulation** - CV, Gate, Trigger, and Raw signal types with extensive routing options
- **Preset System** - Save and load complete patch configurations with XML persistence
- **Real-Time Processing** - Sample-accurate timing with thread-safe architecture
- **CUDA Acceleration** - Optional GPU acceleration for computer vision processing

---

## 🎯 Quick Start

### Prerequisites

- **CMake** 3.22 or higher
- **C++20** compatible compiler (MSVC 2019+, GCC 10+, Clang 12+)
- **CUDA Toolkit** (optional, for GPU-accelerated computer vision)
- **FFmpeg** (for video file support)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/collider-pyo.git
cd collider-pyo/juce

# Configure with CMake
cmake -B build -S .

# Build
cmake --build build --config Release

# Run
./build/Release/ColliderApp.exe  # Windows
./build/ColliderApp              # macOS/Linux
```

### First Patch

1. Launch **ColliderApp** or **PresetCreatorApp**
2. Add a **VCO** node (Source → VCO)
3. Add a **VCF** node (Effect → VCF)
4. Add a **VCA** node (Utility → VCA)
5. Connect: VCO Out → VCF In → VCA In → Output
6. Add an **ADSR** envelope and connect to VCA Gain Mod
7. Add **MIDI CV** and connect Pitch → VCO Frequency, Gate → ADSR Gate In

---

## 📦 Dependencies

### Core Framework
- **[JUCE](https://juce.com)** 7.0.9 - Cross-platform audio framework
- **[ImGui](https://github.com/ocornut/imgui)** (docking branch) - Immediate mode GUI
- **[imnodes](https://github.com/Nelarius/imnodes)** - Node editor for ImGui

### Audio Processing
- **[SoundTouch](https://www.surina.net/soundtouch/)** - Time-stretching and pitch-shifting
- **[RubberBand](https://breakfastquay.com/rubberband/)** - High-quality time/pitch manipulation

### Computer Vision
- **[OpenCV](https://opencv.org/)** 4.13 - Computer vision library (with CUDA support)
- **[FFmpeg](https://ffmpeg.org/)** - Video/audio codec support

### Physics & 3D
- **[Box2D](https://box2d.org/)** - 2D physics engine
- **[GLM](https://github.com/g-truc/glm)** - OpenGL Mathematics
- **[tinygltf](https://github.com/syoyo/tinygltf)** - glTF 2.0 loader
- **[ufbx](https://github.com/ufbx/ufbx)** - FBX file loader

### Text-to-Speech
- **[Piper TTS](https://github.com/rhasspy/piper)** - Neural TTS engine
- **[ONNX Runtime](https://onnxruntime.ai/)** - Machine learning inference

### Utilities
- **[nlohmann/json](https://github.com/nlohmann/json)** - JSON parsing

---

## 🎛️ Node Categories

### Source Nodes (6)
Generate or input signals: VCO, PolyVCO, Noise, Audio Input, Sample Loader, Value

### Effect Nodes (17)
Process audio: VCF, Delay, Reverb, Chorus, Phaser, Compressor, Limiter, Gate, Drive, Graphic EQ, Waveshaper, 8-Band Shaper, Granulator, Harmonic Shaper, Time/Pitch, De-Crackle, Vocal Tract Filter

### Modulator Nodes (6)
Generate control voltages: LFO, ADSR, Random, Sample & Hold, Function Generator, Shaping Oscillator

### Utility & Logic Nodes (13)
Signal processing and routing: VCA, Mixer, CV Mixer, Track Mixer, PanVol, Attenuverter, Lag Processor, Math, Map Range, Quantizer, Rate, Comparator, Logic, Clock Divider, Sequential Switch

### Sequencer Nodes (6)
Pattern generation: Sequencer, Multi Sequencer, Snapshot Sequencer, Stroke Sequencer, Tempo Clock, Timeline

### MIDI Nodes (6)
MIDI integration: MIDI CV, MIDI Player, MIDI Faders, MIDI Knobs, MIDI Buttons, MIDI Jog Wheel

### Analysis Nodes (4)
Signal visualization: Scope, Debug, Input Debug, Frequency Graph

### TTS Nodes (2)
Text-to-speech: TTS Performer, Vocal Tract Filter

### Special Nodes (2)
Unique features: Physics (2D simulation), Animation (3D animation playback)

### Computer Vision Nodes (11)
Video processing: Webcam Loader, Video File Loader, Movement Detector, Human Detector, Object Detector (YOLOv3), Pose Estimator (OpenPose), Hand Tracker, Face Tracker, Color Tracker, Contour Detector, Semantic Segmentation, Video FX, Crop Video

### System Nodes (7)
Patch organization: Meta, Inlet, Outlet, Comment, Recorder, VST Host, BPM Monitor

**📖 [Complete Nodes Dictionary →](USER_MANUAL/Nodes_Dictionary.md)**

---

## 🎨 Usage Examples

### Basic Subtractive Synthesis
```
MIDI CV → VCO → VCF → VCA → Output
         ↑      ↑     ↑
      ADSR ─────┘     └─── ADSR (gain)
```

### Computer Vision-Controlled Patch
```
Webcam → Pose Estimator → Keypoint X/Y → VCO Frequency/Cutoff
```

### Polyphonic Setup
```
Multi Sequencer → PolyVCO (32 voices) → Track Mixer → Effects → Output
```

---

## 📚 Documentation

- **[Complete Nodes Dictionary](USER_MANUAL/Nodes_Dictionary.md)** - Detailed documentation for all 100+ nodes
- **[User Manual](USER_MANUAL/)** - Comprehensive user guide
- **[Build Guide](guides/)** - Detailed build and setup instructions

---

## 🛠️ Development

### Project Structure
```
collider-pyo/
├── juce/              # Main JUCE application
│   ├── Source/        # Source code
│   └── CMakeLists.txt # Build configuration
├── USER_MANUAL/       # User documentation
├── guides/            # Technical guides
└── vendor/            # Third-party dependencies
```

### Building from Source

See [CMakeLists.txt](juce/CMakeLists.txt) for complete dependency configuration.

**Note:** OpenCV with CUDA support requires a one-time build (~30-45 minutes). See `build_opencv_cuda_once.ps1` for automated setup.

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## 📄 License

[Specify your license here - MIT, GPL, etc.]

---

## 🙏 Acknowledgments

- **[JUCE Framework](https://juce.com)** - Cross-platform audio development
- **[OpenCV](https://opencv.org/)** - Computer vision library
- All open-source contributors and library maintainers

---

## 🔮 Roadmap

- [ ] Additional effect modules
- [ ] Enhanced computer vision models
- [ ] Plugin format support (AU, AAX)
- [ ] Multi-threaded audio processing
- [ ] Cloud preset sharing

---

**Made with ❤️ for the modular synthesis community**

