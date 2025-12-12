# Neural Network Audio Generation - Integration Guide for JUCE

This document covers neural network libraries and strategies for integrating neural audio generation into your JUCE-based modular synthesizer.

## Overview: Neural Audio Generation Approaches

Neural audio generation can be integrated in several ways:
1. **Real-time inference** - Generate audio on-the-fly during playback
2. **Offline generation** - Generate audio buffers that are then played back
3. **Hybrid approach** - Pre-generate with real-time parameter control

## 🎯 Real-Time Neural Inference Libraries (C++)

### **RTNeural**
- **Language**: C++ (header-only)
- **Real-Time**: ✅ Yes, optimized for real-time audio
- **JUCE Integration**: ✅ Direct C++ integration
- **Focus**: Lightweight real-time neural network inference
- **Features**:
  - Extremely fast inference optimized for audio
  - Supports multiple layer types (dense, LSTM, GRU, Conv1D)
  - Can convert PyTorch/TensorFlow models to C++ headers
  - Minimal dependencies
  - Designed specifically for audio applications
- **Model Conversion**: Tools to convert trained models to C++ headers
- **Use Cases**: Real-time audio effects, neural synthesis, timbre transfer
- **Website**: https://github.com/jatinchowdhury18/RTNeural
- **Example JUCE Project**: https://github.com/jatinchowdhury18/RTNeural-example

### **ANIRA (Audio Neural Inference Runtime Architecture)**
- **Language**: C++
- **Real-Time**: ✅ Yes, with decoupled inference
- **JUCE Integration**: ✅ Cross-platform C++ library
- **Focus**: Real-time neural inference with multiple backends
- **Features**:
  - Supports ONNX Runtime, LibTorch, TensorFlow Lite
  - Decouples inference from audio callback (prevents dropouts)
  - Thread-safe design
  - Multiple backend support for flexibility
- **Backends**:
  - ONNX Runtime (recommended for cross-platform)
  - LibTorch (PyTorch C++ API)
  - TensorFlow Lite (lightweight)
- **Website**: https://github.com/anira-ai/anira
- **Paper**: https://arxiv.org/abs/2506.12665

### **Neutone SDK**
- **Language**: C++ (Python for model development)
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ Model-agnostic interface
- **Focus**: Deploy PyTorch-based neural audio models
- **Features**:
  - Handles variable buffer sizes automatically
  - Sample rate conversion
  - Control parameter handling
  - Model-agnostic interface
  - Can work entirely in Python for model development
- **Workflow**: Develop models in Python → Export → Use in C++/JUCE
- **Website**: https://github.com/QosmoInc/neutone_sdk
- **Paper**: https://arxiv.org/abs/2508.09126

## 🧠 Neural Audio Synthesis Models

### **NSynth (Neural Audio Synthesis)**
- **Developer**: Google Brain / Magenta
- **Model Type**: WaveNet-based autoencoder
- **Capabilities**:
  - Generate sounds by interpolating between instruments
  - Blend characteristics of different sounds
  - High-quality audio generation
- **Integration**:
  - Export trained models to TensorFlow Lite or ONNX
  - Use RTNeural or ANIRA for inference
  - Can be used for real-time synthesis with optimization
- **Website**: https://magenta.tensorflow.org/nsynth
- **GitHub**: https://github.com/magenta/magenta/tree/main/magenta/models/nsynth

### **RAVE (Realtime Audio Variational autoEncoder)**
- **Developer**: ACIDS / IRCAM
- **Model Type**: Variational autoencoder
- **Capabilities**:
  - Real-time timbre transfer
  - High-quality audio synthesis
  - Latent space manipulation
- **Integration**:
  - Export to ONNX format
  - Use ONNX Runtime or ANIRA for inference
  - Can achieve real-time performance
- **Website**: https://github.com/acids-ircam/rave
- **Paper**: https://arxiv.org/abs/2111.05011

### **DDSP (Differentiable Digital Signal Processing)**
- **Developer**: Google Magenta
- **Model Type**: Neural synthesis with interpretable controls
- **Capabilities**:
  - MIDI-to-audio synthesis
  - Timbre transfer
  - Interpretable parameters (pitch, loudness, etc.)
  - More controllable than pure neural synthesis
- **Integration**:
  - Export models to TensorFlow Lite or ONNX
  - Use RTNeural or ANIRA for inference
  - Good for MIDI-driven synthesis
- **Website**: https://github.com/magenta/ddsp
- **Paper**: https://arxiv.org/abs/2001.04643

### **Open-Amp**
- **Developer**: Research project
- **Model Type**: Neural amplifier/effect emulation
- **Capabilities**:
  - Guitar amplifier emulation
  - Audio effects modeling
  - High-quality neural emulation
- **Integration**:
  - Export models to ONNX or TensorFlow Lite
  - Use inference engines for real-time processing
- **Paper**: https://arxiv.org/abs/2411.14972

## 🔧 Inference Engines & Backends

### **ONNX Runtime**
- **Language**: C++ API available
- **Real-Time**: ✅ Yes (with optimization)
- **JUCE Integration**: ✅ C++ API
- **Features**:
  - Cross-platform (Windows, macOS, Linux)
  - Supports CPU and GPU inference
  - Model quantization support
  - Optimized for production
- **Use Case**: General-purpose neural inference
- **Website**: https://onnxruntime.ai/

### **TensorFlow Lite**
- **Language**: C++ API available
- **Real-Time**: ✅ Yes (designed for mobile/embedded)
- **JUCE Integration**: ✅ C++ API
- **Features**:
  - Lightweight (smaller than full TensorFlow)
  - Quantization support
  - Good for resource-constrained environments
- **Use Case**: Lightweight models, mobile/embedded
- **Website**: https://www.tensorflow.org/lite

### **LibTorch (PyTorch C++)**
- **Language**: C++ API
- **Real-Time**: ✅ Yes (with optimization)
- **JUCE Integration**: ✅ C++ API
- **Features**:
  - Direct PyTorch model loading
  - Full PyTorch functionality
  - Larger binary size than alternatives
- **Use Case**: PyTorch models without conversion
- **Website**: https://pytorch.org/cppdocs/

## 📋 Integration Strategies for JUCE

### **Strategy 1: Real-Time Inference (RTNeural)**
**Best for**: Real-time audio effects, neural synthesis with low latency

```cpp
// Pseudo-code example
class NeuralSynthProcessor : public juce::AudioProcessor
{
    RTNeural::Model<float> model;
    
    void processBlock(juce::AudioBuffer<float>& buffer, ...) override
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            // Get input from buffer
            float input = buffer.getSample(0, sample);
            
            // Run inference
            float output = model.forward(input);
            
            // Write to buffer
            buffer.setSample(0, sample, output);
        }
    }
};
```

**Workflow**:
1. Train model in Python (PyTorch/TensorFlow)
2. Convert to RTNeural format using conversion tools
3. Include generated header in JUCE project
4. Use in audio processing callback

### **Strategy 2: Decoupled Inference (ANIRA)**
**Best for**: Complex models that might violate real-time constraints

```cpp
// Pseudo-code example
class NeuralSynthProcessor : public juce::AudioProcessor
{
    anira::InferenceBackend backend;
    juce::AbstractFifo fifo;
    
    void processBlock(juce::AudioBuffer<float>& buffer, ...) override
    {
        // Read from FIFO (inference runs on separate thread)
        // Process audio from pre-computed results
    }
    
    void inferenceThread()
    {
        // Run inference on separate thread
        // Write results to FIFO
    }
};
```

**Workflow**:
1. Load model using ANIRA (supports ONNX, LibTorch, TFLite)
2. ANIRA handles thread decoupling automatically
3. Inference runs on background thread
4. Results buffered for audio thread

### **Strategy 3: Offline Generation with Real-Time Playback**
**Best for**: Complex generation that can't run in real-time

```cpp
// Pseudo-code example
class NeuralSynthProcessor : public juce::AudioProcessor
{
    juce::AudioBuffer<float> generatedBuffer;
    bool isGenerating = false;
    
    void generateAudio()
    {
        // Run inference offline (can take time)
        // Generate audio buffer
        // Store in generatedBuffer
    }
    
    void processBlock(juce::AudioBuffer<float>& buffer, ...) override
    {
        // Playback pre-generated buffer
        // Apply real-time effects/processing
    }
};
```

**Workflow**:
1. User triggers generation (button press, MIDI note)
2. Run inference on background thread (can take seconds)
3. Store generated audio in buffer
4. Playback generated buffer with real-time effects

### **Strategy 4: Hybrid Approach (Neutone SDK)**
**Best for**: PyTorch models with automatic buffer handling

**Workflow**:
1. Develop model in Python using Neutone SDK
2. Export model with Neutone wrapper
3. Load in JUCE using Neutone C++ interface
4. SDK handles buffer size conversion, sample rate, etc.

## 🔄 Model Conversion Workflow

### **PyTorch → RTNeural**
```python
# 1. Train model in PyTorch
import torch
model = YourNeuralModel()
# ... training ...

# 2. Export using RTNeural converter
from rtneural import export_model
export_model(model, "model.json")

# 3. Convert to C++ header
# Use RTNeural's conversion tool
# Generates model.h for inclusion in JUCE project
```

### **PyTorch → ONNX → ONNX Runtime**
```python
# 1. Train model in PyTorch
import torch
model = YourNeuralModel()
# ... training ...

# 2. Export to ONNX
dummy_input = torch.randn(1, input_size)
torch.onnx.export(model, dummy_input, "model.onnx")

# 3. Use ONNX Runtime in C++
# Load model.onnx in JUCE using ONNX Runtime C++ API
```

### **TensorFlow → TensorFlow Lite**
```python
# 1. Train model in TensorFlow
import tensorflow as tf
model = YourNeuralModel()
# ... training ...

# 2. Convert to TensorFlow Lite
converter = tf.lite.TFLiteConverter.from_saved_model(model_path)
tflite_model = converter.convert()

# 3. Use TensorFlow Lite in C++
# Load .tflite file in JUCE using TFLite C++ API
```

## ⚡ Performance Optimization

### **Model Optimization Techniques**
1. **Quantization**: Reduce precision (FP32 → FP16 → INT8)
   - Reduces model size and inference time
   - May slightly reduce quality
   - Supported by ONNX Runtime, TFLite, RTNeural

2. **Pruning**: Remove unnecessary weights
   - Reduces model complexity
   - Faster inference
   - Maintains quality if done carefully

3. **Model Distillation**: Train smaller model to mimic larger one
   - Smaller, faster model
   - Maintains quality characteristics

4. **Architecture Optimization**: Use efficient architectures
   - Depthwise separable convolutions
   - Efficient activation functions
   - Optimized for audio domain

### **Real-Time Considerations**
- **Latency**: Keep inference time < buffer size
  - 512 sample buffer @ 44.1kHz = ~11.6ms
  - Inference should complete in < 10ms ideally
  
- **Threading**: Never run inference on audio thread
  - Use background threads for inference
  - Buffer results for audio thread
  - ANIRA handles this automatically

- **Memory**: Pre-allocate buffers
  - Avoid allocations in audio callback
  - Pre-allocate model input/output buffers

## 📚 Example Projects & Resources

### **JUCE + Neural Network Examples**
1. **RTNeural Example**
   - https://github.com/jatinchowdhury18/RTNeural-example
   - Shows RTNeural integration with JUCE

2. **Neural Amp Modeler (NAM) JUCE**
   - https://github.com/Tr3m/nam-juce
   - Real-world example of neural audio processing in JUCE

3. **ANIRA Examples**
   - https://github.com/anira-ai/anira
   - Examples showing multiple backend usage

### **Learning Resources**
1. **Build AI-Enhanced Audio Plugins with C++**
   - Book covering AI integration in audio plugins
   - Practical examples with Python, C++, and audio APIs

2. **JUCE Official Documentation**
   - https://juce.com/learn/documentation
   - Audio processing, threading, plugin development

3. **Magenta (Google)**
   - https://magenta.tensorflow.org/
   - Research on music and art generation with ML
   - Pre-trained models available

## 🎯 Recommended Approach for Your Project

### **For Real-Time Neural Synthesis:**
1. **Start with RTNeural** - Easiest integration, best performance
2. **Train/use lightweight models** - Keep inference < 10ms
3. **Consider ANIRA** if models are too complex for RTNeural

### **For Complex Generation:**
1. **Use offline generation** - Generate buffers, then play back
2. **ONNX Runtime** - Most flexible, good performance
3. **Background thread** - Never block audio thread

### **For Model Development:**
1. **Develop in Python** (PyTorch/TensorFlow)
2. **Export to ONNX or RTNeural format**
3. **Integrate in JUCE** using chosen inference engine

## ⚠️ Important Considerations

1. **Licensing**: Check model licenses (many research models are open-source)
2. **Model Size**: Large models may not fit in memory or meet real-time constraints
3. **Quality vs Speed**: Trade-off between model complexity and real-time performance
4. **Platform Support**: Ensure inference engines support your target platforms
5. **Dependencies**: Some inference engines add significant binary size

## 🔗 Quick Reference Links

- **RTNeural**: https://github.com/jatinchowdhury18/RTNeural
- **ANIRA**: https://github.com/anira-ai/anira
- **Neutone SDK**: https://github.com/QosmoInc/neutone_sdk
- **ONNX Runtime**: https://onnxruntime.ai/
- **TensorFlow Lite**: https://www.tensorflow.org/lite
- **LibTorch**: https://pytorch.org/cppdocs/
- **NSynth**: https://magenta.tensorflow.org/nsynth
- **RAVE**: https://github.com/acids-ircam/rave
- **DDSP**: https://github.com/magenta/ddsp

