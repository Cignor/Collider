# Collider Modular Synthesizer

**A real-time modular synthesizer that bridges computer vision, physics simulation, and 3D animation with traditional synthesis.**

Collider is a node-based modular synthesizer built on JUCE, featuring **100+ synthesis modules** and unique integrations that set it apart from traditional modular environments.

---

## What Makes Collider Unique

### 🎥 **Computer Vision Integration**
Control synthesis with your body, hands, or objects in real-time:
- **Pose Estimation** (OpenPose) - 15 body keypoints drive CV parameters
- **Hand Tracking** - 21 hand keypoints per hand for expressive control
- **Face Tracking** - 70 facial landmarks for vocal synthesis
- **Object Detection** (YOLOv3) - Detect and track objects to trigger events
- **Color Tracking** - Multi-color HSV tracking for interactive installations
- **Movement Detection** - Optical flow and background subtraction

### 🎲 **Physics-Based Audio**
2D physics simulation (Box2D) directly drives synthesis parameters. Create interactive soundscapes where collisions, gravity, and forces generate music in real-time.

### 🎬 **Animation-Driven Synthesis**
Load 3D animations (glTF/FBX) and extract joint positions/velocities as CV signals. Drive synthesis from motion capture data, game animations, or custom 3D sequences.

### 🎛️ **100+ Synthesis Nodes**
Comprehensive modular synthesis with sources, effects, modulators, sequencers, utilities, and specialized nodes for TTS, MIDI, analysis, and more.

### 🎤 **Text-to-Speech Synthesis**
Advanced TTS engine with word-level sequencing, formant filtering, and real-time modulation of pitch, speed, and rate.

### 🔌 **VST Plugin Hosting**
Load and use VST2/VST3 plugins within your patches, combining modular synthesis with traditional effects and instruments.

---

## Quick Start

```bash
# Clone and build
git clone https://github.com/yourusername/collider-pyo.git
cd collider-pyo/juce
cmake -B build -S .
cmake --build build --config Release

# Run
./build/Release/ColliderApp.exe  # Windows
./build/ColliderApp              # macOS/Linux
```

**Requirements:** CMake 3.22+, C++20 compiler, CUDA Toolkit (optional), FFmpeg

---

## Example Patches

**Body-Controlled Synthesis:**
```
Webcam → Pose Estimator → Wrist X/Y → VCO Frequency/VCF Cutoff
```

**Physics-Driven Audio:**
```
Physics Node → Collision Events → Sequencer Triggers → PolyVCO
```

**Animation-Based Modulation:**
```
Animation Loader → Joint Positions → LFO Rate/CV Mixer
```

---

## Documentation

- **[Complete Nodes Dictionary](USER_MANUAL/Nodes_Dictionary.md)** - Full documentation for all 100+ nodes
- **[User Manual](USER_MANUAL/)** - Comprehensive guides and tutorials

---

## Libraries Used

### Core Framework
- **[JUCE](https://juce.com)** 7.0.9 - Audio framework
- **[ImGui](https://github.com/ocornut/imgui)** (docking) - GUI
- **[imnodes](https://github.com/Nelarius/imnodes)** - Node editor

### Computer Vision
- **[OpenCV](https://opencv.org/)** 4.13 - Computer vision (CUDA support)
- **[FFmpeg](https://ffmpeg.org/)** - Video/audio codecs

### Audio Processing
- **[SoundTouch](https://www.surina.net/soundtouch/)** - Time-stretching
- **[RubberBand](https://breakfastquay.com/rubberband/)** - Time/pitch manipulation

### Physics & 3D
- **[Box2D](https://box2d.org/)** - 2D physics engine
- **[GLM](https://github.com/g-truc/glm)** - Math library
- **[tinygltf](https://github.com/syoyo/tinygltf)** - glTF loader
- **[ufbx](https://github.com/ufbx/ufbx)** - FBX loader

### Text-to-Speech
- **[Piper TTS](https://github.com/rhasspy/piper)** - Neural TTS
- **[ONNX Runtime](https://onnxruntime.ai/)** - ML inference

### Utilities
- **[nlohmann/json](https://github.com/nlohmann/json)** - JSON parsing

---

## License

[Specify your license]

