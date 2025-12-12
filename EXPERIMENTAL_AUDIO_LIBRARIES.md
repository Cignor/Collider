# Experimental Audio Libraries - JUCE-Compatible & Real-Time

**Filtered list**: Only libraries that can integrate with JUCE and are real-time capable.

## ✅ DSP & Synthesis Libraries (C/C++)

### **Synthesis Toolkit (STK)**
- **Language**: C++
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ Direct C++ integration
- **Focus**: Physical modeling synthesis
- **Features**: 
  - Low-level synthesis and signal processing classes
  - Higher-level instrument classes (strings, winds, percussion)
  - Physical modeling algorithms (Karplus-Strong, modal synthesis, etc.)
- **Website**: https://ccrma.stanford.edu/software/stk/

### **Soundpipe**
- **Language**: C
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ Easy C wrapper → C++
- **Focus**: Lightweight music DSP library
- **Features**:
  - Over 100 DSP modules
  - Many modules ported from Csound
  - Minimal dependencies
  - Suitable for embedded systems
- **Website**: https://github.com/PaulBatchelor/soundpipe

### **CREATE Signal Library (CSL)**
- **Language**: C++
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ C++ framework, compatible
- **Focus**: Portable sound synthesis framework
- **Features**:
  - Unit generator architecture
  - Sound synthesis and signal processing functions
  - Can be used as standalone server or integrated
- **Website**: https://github.com/csl-ucsb/CSL

### **Gamma DSP Library**
- **Language**: C++
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ Header-only or library, C++17 compatible
- **Focus**: Experimental DSP and synthesis
- **Features**:
  - Modern C++ design
  - Granular synthesis
  - Physical modeling
  - Spectral processing
- **Website**: https://github.com/LancePutnam/Gamma

## ✅ Functional/Declarative Audio Languages

### **Faust (Functional Audio Stream)**
- **Language**: Faust (compiles to C++)
- **Real-Time**: ✅ Yes (compiles to optimized C++)
- **JUCE Integration**: ✅ Can generate JUCE plugin code directly
- **Focus**: Functional programming for audio DSP
- **Features**:
  - High-level functional language
  - Compiles to optimized C++ code
  - Can generate JUCE plugins directly
  - Extensive DSP library
  - Real-time compilation possible
- **Website**: https://faust.grame.fr/

### **libpd (Pure Data Embedded)**
- **Language**: C wrapper around Pure Data
- **Real-Time**: ✅ Yes
- **JUCE Integration**: ✅ C library, can be wrapped for C++/JUCE
- **Focus**: Embedding Pure Data patches in applications
- **Features**:
  - Run Pure Data patches without GUI
  - Extensive community patch library
  - Real-time audio processing
  - MIDI support
- **Website**: https://github.com/libpd/libpd

### **Csound**
- **Language**: Csound (C API available)
- **Real-Time**: ✅ Yes (can be real-time)
- **JUCE Integration**: ✅ C API available, can be integrated
- **Focus**: Audio programming language and system
- **Features**:
  - Extensive synthesis capabilities
  - Can be embedded as library
  - C API for integration
  - Decades of development and community
- **Note**: Heavier than other options, but very powerful
- **Website**: https://csound.com/

## ✅ Audio Analysis & Feature Extraction

### **Essentia**
- **Language**: C++ (also has Python bindings)
- **Real-Time**: ✅ Yes (real-time capable)
- **JUCE Integration**: ✅ Native C++ library, perfect for JUCE
- **Focus**: Audio analysis and music information retrieval
- **Features**:
  - Real-time audio feature extraction
  - Music analysis algorithms
  - Beat tracking, key detection, etc.
- **Website**: https://essentia.upf.edu/

### **aubio**
- **Language**: C (C++ bindings available)
- **Real-Time**: ✅ Yes (real-time capable)
- **JUCE Integration**: ✅ C library, easy C++ integration
- **Focus**: Audio analysis and tempo tracking
- **Features**:
  - Onset detection
  - Beat tracking
  - Pitch detection
  - Tempo estimation
- **Website**: https://aubio.org/

## ❌ ELIMINATED (Not Real-Time or Not Directly Integratable)

### Neural Network Libraries (Eliminated)
- **NSynth** - Requires inference engine, not real-time
- **Open-Amp** - Neural models require inference, not real-time
- **Asteroid** - PyTorch inference, not real-time
- **MRCV** - Python/neural, not real-time

### Separate Process/Communication (Eliminated)
- **SuperCollider** - Separate process via OSC, not direct integration
- **ChucK** - Separate runtime, not direct integration

### Standards/Frameworks (Eliminated)
- **CLAP** - Plugin standard, not a synthesis library

### GPU/Inference (Eliminated)
- **TorchFX** - Python/PyTorch, requires inference, not real-time

## Integration Priority for JUCE Projects

### **Tier 1: Easiest Integration (Direct C++)**
1. **STK** - Direct C++ integration, physical modeling
2. **Essentia** - Native C++ audio analysis
3. **Gamma** - Header-only or library, modern C++17
4. **CSL** - C++ framework

### **Tier 2: Requires Wrapping (C → C++)**
1. **Soundpipe** - Lightweight C library, minimal wrapping needed
2. **libpd** - C library, needs C++ wrapper
3. **aubio** - C library with good C++ bindings
4. **Csound** - C API available (heavier option)

### **Tier 3: Code Generation**
1. **Faust** - Generates JUCE plugin code directly (very powerful)

## Notes

- All listed libraries are **real-time capable** and can be integrated into JUCE projects
- C libraries can be easily wrapped in C++ classes for JUCE integration
- Faust is unique in that it can generate JUCE plugin code directly
- Consider licensing compatibility (most listed are open-source)
- Csound is powerful but heavier than other options

