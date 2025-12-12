# Essentia & STK Integration Plan for Pikon Raditsz

## Overview

This plan details the integration of two high-priority audio libraries into Pikon Raditsz:
- **Essentia** - Audio analysis and music information retrieval (C++ library)
- **STK (Synthesis Toolkit)** - Physical modeling synthesis (C++ library)

**Target Modules:**
- **Essentia (5 modules)**: beat_tracker, key_detector, onset_detector, pitch_tracker, audio_to_cv
- **STK (4 modules)**: stk_string, stk_wind, stk_percussion, stk_plucked

**Total New Modules**: 9

---

## Executive Summary

### Risk Rating: **MEDIUM** ⚠️

**Overall Risk Assessment:**
- **Technical Risk**: Medium - Both libraries are mature and well-documented, but integration complexity varies
- **Performance Risk**: Low-Medium - Essentia analysis may require careful optimization for real-time
- **Compatibility Risk**: Low - Both are C++ libraries compatible with JUCE
- **Maintenance Risk**: Low - Both libraries are actively maintained

### Difficulty Levels

#### **Level 1: Easy (Foundation Setup)** - 2-3 days
- Library acquisition and build system integration
- Basic wrapper classes
- Module skeleton creation

#### **Level 2: Medium (Core Integration)** - 1-2 weeks per library
- STK module implementation (4 modules)
- Essentia module implementation (5 modules)
- CV modulation integration
- Parameter system integration

#### **Level 3: Advanced (Optimization & Polish)** - 1 week
- Real-time performance optimization
- UI parameter drawing
- Menu system integration
- Documentation and testing

### Confidence Rating: **HIGH** ✅ (85%)

**Strong Points:**
- ✅ Both libraries are mature, well-documented C++ libraries
- ✅ Direct C++ integration (no wrappers needed)
- ✅ JUCE-compatible architecture
- ✅ Existing module system is well-established
- ✅ Clear integration path following existing patterns
- ✅ STK is specifically designed for real-time audio
- ✅ Essentia has real-time capable algorithms

**Weak Points:**
- ⚠️ Essentia may require careful buffer management for real-time analysis
- ⚠️ STK requires understanding of physical modeling parameters
- ⚠️ Both libraries add external dependencies
- ⚠️ Essentia's analysis algorithms may need optimization for low-latency
- ⚠️ STK modules may have higher CPU usage than simple oscillators

---

## Detailed Risk Analysis

### 1. Technical Risks

#### **Risk: Essentia Real-Time Performance**
- **Severity**: Medium
- **Probability**: Medium
- **Impact**: Analysis modules may introduce latency or CPU spikes
- **Mitigation**: 
  - Use Essentia's streaming algorithms designed for real-time
  - Implement buffering strategies (analysis on background thread)
  - Provide buffer size parameters for user control
  - Profile and optimize hot paths
  - Consider decoupled analysis (ANIRA-style approach)

#### **Risk: STK CPU Usage**
- **Severity**: Low-Medium
- **Probability**: Medium
- **Impact**: Physical modeling may be CPU-intensive with multiple instances
- **Mitigation**:
  - STK is optimized for real-time, but monitor CPU usage
  - Provide voice limiting options
  - Document CPU usage expectations
  - Consider polyphonic voice management

#### **Risk: Library Build System Integration**
- **Severity**: Low
- **Probability**: Low
- **Impact**: CMake/build system complications
- **Mitigation**:
  - Both libraries support CMake
  - Use vcpkg or manual integration
  - Test on all target platforms early
  - Document build requirements

#### **Risk: Parameter Mapping Complexity**
- **Severity**: Low
- **Probability**: Medium
- **Impact**: STK physical parameters may not map intuitively to CV ranges
- **Mitigation**:
  - Create sensible CV-to-parameter mappings
  - Provide parameter range documentation
  - Use logarithmic scaling where appropriate
  - Test with various CV sources

### 2. Integration Risks

#### **Risk: Module Registration Errors**
- **Severity**: Low
- **Probability**: Low
- **Impact**: Modules don't appear in menus or fail to instantiate
- **Mitigation**:
  - Follow existing module registration pattern exactly
  - Test module factory registration
  - Verify pin database entries
  - Check menu system integration

#### **Risk: State Serialization Issues**
- **Severity**: Low
- **Probability**: Low
- **Impact**: Module state not saved/loaded correctly
- **Mitigation**:
  - Use existing APVTS pattern
  - Test preset save/load
  - Verify parameter persistence

#### **Risk: Thread Safety**
- **Severity**: Medium
- **Probability**: Low
- **Impact**: Race conditions in audio thread
- **Mitigation**:
  - Follow JUCE audio thread guidelines
  - Use lock-free data structures where possible
  - Ensure parameter access is thread-safe
  - Test with multiple instances

### 3. User Experience Risks

#### **Risk: Complex Parameter Sets**
- **Severity**: Low
- **Probability**: High
- **Impact**: Users overwhelmed by physical modeling parameters
- **Mitigation**:
  - Provide sensible defaults
  - Group related parameters
  - Add helpful tooltips
  - Create example patches

#### **Risk: Analysis Module Latency**
- **Severity**: Medium
- **Probability**: Medium
- **Impact**: Audio analysis introduces noticeable delay
- **Mitigation**:
  - Use low-latency analysis algorithms
  - Provide lookahead controls
  - Document expected latency
  - Consider offline analysis mode

---

## Implementation Phases

### Phase 1: Foundation Setup (Difficulty: Easy, Duration: 2-3 days)

#### 1.1 Library Acquisition & Build Integration

**STK Integration:**
```cpp
// Option A: Git Submodule
git submodule add https://github.com/thestk/stk.git juce/Source/vendor/stk

// Option B: vcpkg
vcpkg install stk

// Option C: Manual Download
// Download STK source, place in juce/Source/vendor/stk
```

**Essentia Integration:**
```cpp
// Option A: Git Submodule
git submodule add https://github.com/MTG/essentia.git juce/Source/vendor/essentia

// Option B: Manual Download
// Download Essentia source, place in juce/Source/vendor/essentia
```

**CMake Integration:**
- Add STK and Essentia to CMakeLists.txt
- Configure include paths
- Link libraries
- Handle platform-specific builds (Windows, macOS, Linux)

**Files to Modify:**
- `juce/CMakeLists.txt` - Add subdirectories and link libraries
- `juce/Source/vendor/` - Add library sources

**Risks:**
- Build system complexity
- Platform-specific compilation issues
- Dependency conflicts

**Mitigation:**
- Test on all platforms early
- Use CMake find_package or manual paths
- Document build requirements

---

#### 1.2 Create Wrapper/Utility Classes

**STK Wrapper:**
```cpp
// juce/Source/audio/modules/stk/StkWrapper.h
#pragma once
#include "ModuleProcessor.h"
#include <stk/Stk.h>
#include <stk/Instrmnt.h>

class StkWrapper
{
public:
    static void initializeStk(double sampleRate);
    static void shutdownStk();
    // Helper functions for STK instrument management
};
```

**Essentia Wrapper:**
```cpp
// juce/Source/audio/modules/essentia/EssentiaWrapper.h
#pragma once
#include "ModuleProcessor.h"
#include <essentia/algorithmfactory.h>
#include <essentia/streaming/algorithms/poolstorage.h>

class EssentiaWrapper
{
public:
    static void initializeEssentia();
    static void shutdownEssentia();
    // Helper functions for Essentia algorithm management
};
```

**Purpose:**
- Centralize library initialization
- Handle sample rate changes
- Provide common utilities
- Manage library lifecycle

---

### Phase 2: STK Module Implementation (Difficulty: Medium, Duration: 1-2 weeks)

#### 2.1 STK String Module (`stk_string`)

**File Structure:**
```
juce/Source/audio/modules/stk/
├── StkStringModuleProcessor.h
├── StkStringModuleProcessor.cpp
└── StkWrapper.h (shared)
```

**Implementation Steps:**

1. **Create Module Class:**
```cpp
class StkStringModuleProcessor : public ModuleProcessor
{
public:
    static constexpr auto paramIdFrequency = "frequency";
    static constexpr auto paramIdInstrumentType = "instrument_type";
    static constexpr auto paramIdExcitationType = "excitation_type";
    static constexpr auto paramIdDamping = "damping";
    static constexpr auto paramIdPickupPos = "pickup_pos";
    static constexpr auto paramIdBrightness = "brightness";
    static constexpr auto paramIdBodySize = "body_size";
    
    // CV modulation inputs
    static constexpr auto paramIdFreqMod = "freq_mod";
    static constexpr auto paramIdDampingMod = "damping_mod";
    static constexpr auto paramIdPickupMod = "pickup_mod";
    
    const juce::String getName() const override { return "stk_string"; }
    
private:
    std::unique_ptr<stk::Instrmnt> instrument;
    double currentSampleRate = 44100.0;
    juce::AudioProcessorValueTreeState apvts;
};
```

2. **STK Instrument Selection:**
```cpp
void StkStringModuleProcessor::updateInstrument()
{
    auto type = apvts.getRawParameterValue(paramIdInstrumentType)->load();
    auto excitation = apvts.getRawParameterValue(paramIdExcitationType)->load();
    
    // Map to STK instruments:
    // Guitar: stk::Plucked, stk::Mandolin
    // Violin: stk::Bowed
    // Cello: stk::Bowed (with different parameters)
    // Sitar: stk::Sitar
    // Banjo: stk::Plucked (with different parameters)
    
    switch ((int)type)
    {
        case 0: // Guitar
            instrument = std::make_unique<stk::Plucked>();
            break;
        case 1: // Violin
            instrument = std::make_unique<stk::Bowed>();
            break;
        // ... etc
    }
    
    if (instrument)
        instrument->setSampleRate(currentSampleRate);
}
```

3. **Process Block:**
```cpp
void StkStringModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    if (!instrument) return;
    
    const int numSamples = buffer.getNumSamples();
    const float* freqMod = getModulationBuffer(paramIdFreqMod);
    const float* dampingMod = getModulationBuffer(paramIdDampingMod);
    
    float* output = buffer.getWritePointer(0);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Get base frequency
        float freq = apvts.getRawParameterValue(paramIdFrequency)->load();
        
        // Apply CV modulation
        if (freqMod)
            freq *= juce::jmap(freqMod[i], 0.0f, 1.0f, 0.5f, 2.0f); // ±1 octave
        
        // Set frequency
        instrument->setFrequency(freq);
        
        // Apply damping modulation
        if (dampingMod)
        {
            float damping = apvts.getRawParameterValue(paramIdDamping)->load();
            damping = juce::jmap(dampingMod[i], 0.0f, 1.0f, 0.0f, damping);
            instrument->controlChange(2, damping * 128.0f); // CC 2 = damping
        }
        
        // Generate sample
        output[i] = instrument->tick();
    }
}
```

4. **Gate Input Handling:**
```cpp
void StkStringModuleProcessor::processBlock(...)
{
    // Check for gate input
    const float* gateInput = getModulationBuffer("gate");
    
    for (int i = 0; i < numSamples; ++i)
    {
        if (gateInput && gateInput[i] > 0.5f && !wasGateHigh)
        {
            // Trigger note
            float velocity = gateInput[i];
            instrument->noteOn(freq, velocity);
            wasGateHigh = true;
        }
        else if (gateInput && gateInput[i] <= 0.5f && wasGateHigh)
        {
            instrument->noteOff(0.5f);
            wasGateHigh = false;
        }
        
        // ... rest of processing
    }
}
```

**Risks:**
- STK instrument initialization timing
- Sample rate changes
- Parameter mapping to STK controls

**Mitigation:**
- Initialize instruments in prepareToPlay()
- Handle sample rate changes properly
- Test all instrument types
- Document parameter mappings

---

#### 2.2 STK Wind Module (`stk_wind`)

**Similar structure to stk_string, but:**
- Uses wind instruments: Flute, Clarinet, Saxophone, Trumpet
- Breath pressure CV input
- Vibrato CV input
- Tongue articulation gate

**Key Differences:**
```cpp
// Wind-specific parameters
static constexpr auto paramIdBreathPressure = "breath_pressure";
static constexpr auto paramIdVibratoRate = "vibrato_rate";
static constexpr auto paramIdVibratoDepth = "vibrato_depth";
static constexpr auto paramIdReedStiffness = "reed_stiffness";

// Wind instruments
case 0: instrument = std::make_unique<stk::Flute>(); break;
case 1: instrument = std::make_unique<stk::Clarinet>(); break;
case 2: instrument = std::make_unique<stk::Saxofony>(); break;
case 3: instrument = std::make_unique<stk::Brass>(); break;
```

---

#### 2.3 STK Percussion Module (`stk_percussion`)

**Uses modal synthesis:**
- Kick, Snare, Tom, Hi-Hat, Cymbal
- Strike gate input
- Velocity CV input
- Pitch CV for pitched drums

**Key Implementation:**
```cpp
case 0: instrument = std::make_unique<stk::ModalBar>(); break; // Marimba
case 1: instrument = std::make_unique<stk::BandedWG>(); break; // Cymbal
case 2: instrument = std::make_unique<stk::Shakers>(); break; // Shaker
// ... etc
```

---

#### 2.4 STK Plucked Module (`stk_plucked`)

**Simplified Karplus-Strong:**
- Lightweight plucked string
- Good for polyphonic use
- Simple parameters: frequency, damping, pick position

---

### Phase 3: Essentia Module Implementation (Difficulty: Medium-Hard, Duration: 1-2 weeks)

#### 3.1 Essentia Beat Tracker (`essentia_beat_tracker`)

**File Structure:**
```
juce/Source/audio/modules/essentia/
├── EssentiaBeatTrackerModuleProcessor.h
├── EssentiaBeatTrackerModuleProcessor.cpp
└── EssentiaWrapper.h (shared)
```

**Implementation Challenges:**
- Real-time beat detection requires buffering
- Analysis may lag behind audio
- Need to balance accuracy vs latency

**Implementation Strategy:**

1. **Use Essentia Streaming Algorithms:**
```cpp
class EssentiaBeatTrackerModuleProcessor : public ModuleProcessor
{
private:
    essentia::streaming::Algorithm* audioLoader;
    essentia::streaming::Algorithm* beatTracker;
    essentia::streaming::Algorithm* vectorInput;
    essentia::streaming::Algorithm* poolOutput;
    
    essentia::Pool pool;
    essentia::scheduler::Network network;
    
    // Buffer for analysis
    std::vector<float> analysisBuffer;
    int bufferWritePos = 0;
    static constexpr int ANALYSIS_BUFFER_SIZE = 2048; // ~46ms at 44.1kHz
    
    // Beat detection state
    std::atomic<float> detectedTempo{120.0f};
    std::atomic<float> confidence{0.0f};
    juce::AbstractFifo beatFifo{64}; // Beat trigger FIFO
    std::vector<float> beatTriggers;
};
```

2. **Decoupled Analysis (Background Thread):**
```cpp
class EssentiaBeatTrackerModuleProcessor : public ModuleProcessor
{
private:
    std::thread analysisThread;
    std::atomic<bool> shouldStopAnalysis{false};
    juce::AbstractFifo audioFifo{8192}; // Audio buffer for analysis
    
    void analysisThreadFunction()
    {
        while (!shouldStopAnalysis)
        {
            // Read audio from FIFO
            float audio[ANALYSIS_BUFFER_SIZE];
            int numRead = audioFifo.read(audio, ANALYSIS_BUFFER_SIZE);
            
            if (numRead == ANALYSIS_BUFFER_SIZE)
            {
                // Run Essentia analysis
                processAnalysisBuffer(audio, ANALYSIS_BUFFER_SIZE);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    void processAnalysisBuffer(const float* audio, int numSamples)
    {
        // Feed to Essentia streaming network
        // Extract beat times and tempo
        // Update atomic variables
        // Push beat triggers to FIFO
    }
};
```

3. **Audio Thread Processing:**
```cpp
void EssentiaBeatTrackerModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Write audio to analysis FIFO (non-blocking)
    const float* input = buffer.getReadPointer(0);
    int numWritten = audioFifo.write(input, buffer.getNumSamples());
    
    // Read beat triggers from FIFO
    float triggers[64];
    int numTriggers = beatFifo.read(triggers, 64);
    
    // Output beat gate
    float* beatOut = buffer.getWritePointer(0); // Reuse buffer
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        beatOut[i] = 0.0f;
    }
    
    // Place beat triggers
    for (int i = 0; i < numTriggers; ++i)
    {
        int samplePos = (int)(triggers[i] * getSampleRate());
        if (samplePos < buffer.getNumSamples())
            beatOut[samplePos] = 1.0f;
    }
    
    // Output tempo CV (smoothed)
    float* tempoCV = buffer.getWritePointer(1); // Second channel
    float tempo = detectedTempo.load();
    float tempoNormalized = juce::jmap(tempo, 60.0f, 200.0f, 0.0f, 1.0f);
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        tempoCV[i] = tempoNormalized;
    }
}
```

**Risks:**
- Analysis latency
- CPU usage
- Thread synchronization

**Mitigation:**
- Use efficient Essentia algorithms
- Implement proper FIFO management
- Profile and optimize
- Provide buffer size controls

---

#### 3.2 Essentia Key Detector (`essentia_key_detector`)

**Simpler than beat tracker:**
- Can run less frequently (every 1-2 seconds)
- Lower CPU usage
- Outputs key CV and mode CV

**Implementation:**
```cpp
class EssentiaKeyDetectorModuleProcessor : public ModuleProcessor
{
private:
    essentia::standard::Algorithm* keyDetector;
    essentia::Pool pool;
    
    std::vector<float> analysisBuffer;
    int samplesCollected = 0;
    static constexpr int ANALYSIS_WINDOW = 44100 * 2; // 2 seconds
    
    std::atomic<int> detectedKey{0}; // 0-11 (C to B)
    std::atomic<bool> detectedMode{false}; // false=major, true=minor
    std::atomic<float> confidence{0.0f};
    
    void processAnalysis()
    {
        if (samplesCollected >= ANALYSIS_WINDOW)
        {
            // Run key detection
            std::vector<essentia::Real> key, scale, strength;
            keyDetector->input("pcp").set(pool.value<std::vector<essentia::Real>>("tonal.key_profiles"));
            keyDetector->output("key").set(key);
            keyDetector->output("scale").set(scale);
            keyDetector->output("strength").set(strength);
            keyDetector->compute();
            
            // Update atomics
            detectedKey.store(key[0]);
            detectedMode.store(scale[0] == "minor");
            confidence.store(strength[0]);
            
            samplesCollected = 0;
        }
    }
};
```

---

#### 3.3 Essentia Onset Detector (`essentia_onset_detector`)

**Real-time capable:**
- Lower latency than beat tracker
- Can run more frequently
- Outputs onset gate triggers

---

#### 3.4 Essentia Pitch Tracker (`essentia_pitch_tracker`)

**Real-time capable:**
- YIN algorithm is efficient
- Low latency
- Outputs pitch CV

---

#### 3.5 Essentia Audio-to-CV (`essentia_audio_to_cv`)

**Multiple feature extraction:**
- RMS, Peak, Spectral Centroid, etc.
- Can run in real-time
- Multiple CV outputs

---

### Phase 4: Module Registration & UI Integration (Difficulty: Easy-Medium, Duration: 3-5 days)

#### 4.1 Module Factory Registration

**File:** `juce/Source/audio/graph/ModularSynthProcessor.cpp`

**Add to `getModuleFactory()`:**
```cpp
// STK Modules
reg("stk_string", []{ return std::make_unique<StkStringModuleProcessor>(); });
reg("stk_wind", []{ return std::make_unique<StkWindModuleProcessor>(); });
reg("stk_percussion", []{ return std::make_unique<StkPercussionModuleProcessor>(); });
reg("stk_plucked", []{ return std::make_unique<StkPluckedModuleProcessor>(); });

// Essentia Modules
reg("essentia_beat_tracker", []{ return std::make_unique<EssentiaBeatTrackerModuleProcessor>(); });
reg("essentia_key_detector", []{ return std::make_unique<EssentiaKeyDetectorModuleProcessor>(); });
reg("essentia_onset_detector", []{ return std::make_unique<EssentiaOnsetDetectorModuleProcessor>(); });
reg("essentia_pitch_tracker", []{ return std::make_unique<EssentiaPitchTrackerModuleProcessor>(); });
reg("essentia_audio_to_cv", []{ return std::make_unique<EssentiaAudioToCVModuleProcessor>(); });
```

---

#### 4.2 Pin Database Registration

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

**Example for stk_string:**
```cpp
db["stk_string"] = ModulePinInfo(
    NodeWidth::Medium,
    {  // Inputs
        AudioPin("Frequency", 0, PinDataType::CV),
        AudioPin("Pluck/Bow", 1, PinDataType::Gate),
        AudioPin("Velocity", 2, PinDataType::CV),
        AudioPin("Damping", 3, PinDataType::CV),
        AudioPin("Pickup Pos", 4, PinDataType::CV)
    },
    {  // Outputs
        AudioPin("Out", 0, PinDataType::Audio)
    },
    {}  // Mod outputs
);
```

**Example for essentia_beat_tracker:**
```cpp
db["essentia_beat_tracker"] = ModulePinInfo(
    NodeWidth::Medium,
    { AudioPin("Audio In", 0, PinDataType::Audio) },
    {  // Outputs
        AudioPin("Beat", 0, PinDataType::Gate),
        AudioPin("Tempo CV", 1, PinDataType::CV),
        AudioPin("Confidence", 2, PinDataType::CV),
        AudioPin("Phase", 3, PinDataType::CV)
    },
    {}
);
```

---

#### 4.3 Menu System Integration

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Add to appropriate categories:**

**STK Modules → Source category:**
```cpp
if (ImGui::MenuItem("STK String", nullptr, false, true))
    addNodeAtMouse("stk_string");
if (ImGui::MenuItem("STK Wind", nullptr, false, true))
    addNodeAtMouse("stk_wind");
if (ImGui::MenuItem("STK Percussion", nullptr, false, true))
    addNodeAtMouse("stk_percussion");
if (ImGui::MenuItem("STK Plucked", nullptr, false, true))
    addNodeAtMouse("stk_plucked");
```

**Essentia Modules → Analysis category:**
```cpp
if (ImGui::MenuItem("Beat Tracker", nullptr, false, true))
    addNodeAtMouse("essentia_beat_tracker");
if (ImGui::MenuItem("Key Detector", nullptr, false, true))
    addNodeAtMouse("essentia_key_detector");
if (ImGui::MenuItem("Onset Detector", nullptr, false, true))
    addNodeAtMouse("essentia_onset_detector");
if (ImGui::MenuItem("Pitch Tracker", nullptr, false, true))
    addNodeAtMouse("essentia_pitch_tracker");
if (ImGui::MenuItem("Audio to CV", nullptr, false, true))
    addNodeAtMouse("essentia_audio_to_cv");
```

---

#### 4.4 Module Descriptions

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

```cpp
descriptions["stk_string"] = "Physical modeling string synthesizer (guitar, violin, cello, etc.)";
descriptions["stk_wind"] = "Physical modeling wind instruments (flute, clarinet, saxophone, etc.)";
descriptions["stk_percussion"] = "Modal synthesis percussion (drums, cymbals, etc.)";
descriptions["stk_plucked"] = "Karplus-Strong plucked string synthesis";
descriptions["essentia_beat_tracker"] = "Beat detection and tempo tracking from audio";
descriptions["essentia_key_detector"] = "Musical key detection (C major, A minor, etc.)";
descriptions["essentia_onset_detector"] = "Note onset/attack detection";
descriptions["essentia_pitch_tracker"] = "Pitch detection and tracking";
descriptions["essentia_audio_to_cv"] = "Multi-feature audio-to-CV converter";
```

---

### Phase 5: UI Parameter Drawing (Difficulty: Medium, Duration: 3-5 days)

#### 5.1 Implement `drawParametersInNode()`

**For each module, implement custom parameter UI:**

**Example for stk_string:**
```cpp
void StkStringModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String& paramId)>& isParamModulated,
    const std::function<void()>& onModificationEnded) override
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    // Instrument Type
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdInstrumentType)))
    {
        int value = p->getIndex();
        const char* items[] = {"Guitar", "Violin", "Cello", "Sitar", "Banjo"};
        if (ImGui::Combo("Instrument", &value, items, IM_ARRAYSIZE(items)))
        {
            *p = value;
            updateInstrument(); // Recreate STK instrument
            onModificationEnded();
        }
    }
    
    // Frequency
    float freq = ap.getRawParameterValue(paramIdFrequency)->load();
    bool freqMod = isParamModulated(paramIdFrequency);
    
    if (freqMod) ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 2000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
    {
        if (!freqMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency)))
                *p = freq;
        onModificationEnded();
    }
    if (freqMod) ImGui::PopStyleColor();
    
    // ... more parameters
}
```

---

### Phase 6: Testing & Optimization (Difficulty: Medium, Duration: 1 week)

#### 6.1 Unit Tests

- Test each module in isolation
- Test parameter ranges
- Test CV modulation
- Test gate triggers
- Test state save/load

#### 6.2 Integration Tests

- Test with existing modules
- Test in complex patches
- Test CPU usage
- Test memory usage
- Test thread safety

#### 6.3 Performance Profiling

- Profile STK modules CPU usage
- Profile Essentia analysis latency
- Optimize hot paths
- Test with multiple instances

#### 6.4 User Testing

- Create example patches
- Test with real users
- Gather feedback
- Iterate on UI/UX

---

## Potential Problems & Solutions

### Problem 1: Essentia Analysis Latency

**Symptom:** Beat tracker or other analysis modules introduce noticeable delay

**Solutions:**
- Use Essentia's streaming algorithms with smaller buffers
- Implement lookahead prediction
- Run analysis on background thread
- Provide "low latency" mode with reduced accuracy
- Document expected latency in UI

**Prevention:**
- Profile analysis algorithms early
- Test with various buffer sizes
- Implement adaptive buffer sizing

---

### Problem 2: STK Instrument Initialization Timing

**Symptom:** Instruments don't produce sound or crash on first use

**Solutions:**
- Initialize instruments in `prepareToPlay()`
- Handle sample rate changes properly
- Provide default parameter values
- Add error handling for instrument creation

**Prevention:**
- Test instrument creation in all scenarios
- Handle edge cases (sample rate = 0, etc.)
- Add logging for debugging

---

### Problem 3: Thread Safety Issues

**Symptom:** Crashes or audio glitches with multiple instances

**Solutions:**
- Use lock-free data structures
- Ensure parameter access is thread-safe
- Use atomic variables for shared state
- Test with stress scenarios

**Prevention:**
- Follow JUCE audio thread guidelines
- Use RAII for resource management
- Test with multiple instances early

---

### Problem 4: Build System Complexity

**Symptom:** Libraries don't compile or link correctly

**Solutions:**
- Use vcpkg for dependency management
- Provide manual build instructions
- Test on all platforms
- Document build requirements

**Prevention:**
- Set up CI/CD for all platforms
- Test builds early and often
- Keep build documentation updated

---

### Problem 5: Parameter Mapping Confusion

**Symptom:** Users don't understand how CV maps to physical parameters

**Solutions:**
- Provide clear tooltips
- Use sensible default ranges
- Add parameter visualization
- Create example patches
- Document CV ranges

**Prevention:**
- Test with users early
- Get feedback on parameter ranges
- Provide helpful defaults

---

### Problem 6: CPU Usage with Multiple Instances

**Symptom:** System becomes sluggish with many STK/Essentia modules

**Solutions:**
- Document CPU usage expectations
- Provide voice limiting
- Optimize hot paths
- Consider polyphonic voice management
- Add CPU usage warnings

**Prevention:**
- Profile early and often
- Test with many instances
- Optimize before release

---

## Success Criteria

### Must Have (MVP):
- ✅ All 9 modules compile and run
- ✅ Modules appear in menus
- ✅ Basic functionality works
- ✅ CV modulation works
- ✅ State save/load works
- ✅ No crashes in normal use

### Should Have:
- ✅ Optimized performance
- ✅ Good UI/UX
- ✅ Example patches
- ✅ Documentation
- ✅ Thread safety verified

### Nice to Have:
- ✅ Advanced features
- ✅ Performance optimizations
- ✅ Additional instrument types
- ✅ More analysis algorithms

---

## Timeline Estimate

### Conservative Estimate:
- **Phase 1 (Foundation)**: 1 week
- **Phase 2 (STK Modules)**: 2-3 weeks
- **Phase 3 (Essentia Modules)**: 2-3 weeks
- **Phase 4 (Registration/UI)**: 1 week
- **Phase 5 (UI Drawing)**: 1 week
- **Phase 6 (Testing)**: 1 week

**Total: 8-10 weeks**

### Optimistic Estimate:
- **Phase 1**: 3 days
- **Phase 2**: 1.5 weeks
- **Phase 3**: 1.5 weeks
- **Phase 4**: 3 days
- **Phase 5**: 3 days
- **Phase 6**: 3 days

**Total: 5-6 weeks**

### Realistic Estimate:
- **Phase 1**: 1 week
- **Phase 2**: 2 weeks
- **Phase 3**: 2 weeks
- **Phase 4**: 1 week
- **Phase 5**: 1 week
- **Phase 6**: 1 week

**Total: 8 weeks**

---

## Dependencies & Requirements

### External Dependencies:
- **STK**: C++ library, requires compilation
- **Essentia**: C++ library, requires compilation, may need FFTW
- **JUCE**: Already integrated
- **CMake**: Build system

### Build Requirements:
- C++17 compiler
- CMake 3.15+
- Platform-specific build tools
- FFTW (for Essentia, optional but recommended)

### Runtime Requirements:
- No additional runtime dependencies (static linking)

---

## Conclusion

This integration plan provides a comprehensive roadmap for adding Essentia and STK libraries to Pikon Raditsz. The plan addresses risks, provides detailed implementation steps, and includes mitigation strategies for potential problems.

**Key Takeaways:**
- **Risk Level**: Medium (manageable with proper planning)
- **Confidence**: High (85%) - Both libraries are mature and well-documented
- **Timeline**: 6-10 weeks (realistic: 8 weeks)
- **Difficulty**: Medium (follows existing patterns)

**Recommendation:** Proceed with integration, starting with Phase 1 (Foundation Setup) to validate build system integration before committing to full implementation.

