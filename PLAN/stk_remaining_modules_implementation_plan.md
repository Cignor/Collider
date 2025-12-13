# STK Remaining Modules Implementation Plan

**Date**: 2025-01-XX  
**Feature**: Implementation of 3 Remaining STK Modules (stk_wind, stk_percussion, stk_plucked)  
**Status**: Planning Phase

---

## Executive Summary

This plan details the implementation of the three remaining STK (Synthesis Toolkit) modules for the Pikon Raditsz modular synthesizer:

1. **`stk_wind`** - Physical modeling wind instruments (Flute, Clarinet, Saxophone, Brass)
2. **`stk_percussion`** - Modal synthesis percussion (ModalBar, BandedWG, Shakers)
3. **`stk_plucked`** - Simplified Karplus-Strong plucked string synthesis

**Total New Modules**: 3  
**Foundation**: `stk_string` module already implemented and working as reference

---

## Risk Rating: **LOW** ✅

**Overall Risk Assessment:**
- **Technical Risk**: Low - STK library is mature, well-documented, and we have a working reference implementation (`stk_string`)
- **Performance Risk**: Low - STK instruments are optimized for real-time audio, similar CPU usage to `stk_string`
- **Compatibility Risk**: Low - All STK instruments follow the same API pattern (`Instrmnt` base class)
- **Integration Risk**: Low - Follows exact same pattern as `stk_string` (factory registration, pin database, UI menus)
- **Maintenance Risk**: Low - STK library is stable, minimal external dependencies

### Difficulty Levels

#### **Level 1: Easy (Foundation & Setup)** - 1 day
- Module class structure creation (copy from `stk_string` template)
- Parameter layout definition
- Basic instrument instantiation
- Factory registration

#### **Level 2: Medium (Core Implementation)** - 2-3 days per module
- Instrument-specific parameter mapping
- CV modulation integration
- Gate/trigger handling
- Audio processing loop
- Parameter update logic

#### **Level 3: Advanced (UI & Polish)** - 1-2 days per module
- `drawParametersInNode()` implementation
- Visualization data structures
- Scroll-edit support
- Node visualization (waveform display)
- Pin labels and descriptions

**Total Estimated Duration**: 8-12 days (1.5-2.5 weeks)

### Confidence Rating: **VERY HIGH** ✅ (95%)

**Strong Points:**
- ✅ **Proven Foundation**: `stk_string` module is fully working and provides a complete reference implementation
- ✅ **Consistent API**: All STK instruments inherit from `Instrmnt` and follow the same interface pattern (`noteOn()`, `noteOff()`, `tick()`, `controlChange()`)
- ✅ **Clear Documentation**: STK headers include detailed control change numbers and parameter descriptions
- ✅ **Established Patterns**: Module registration, pin database, UI integration patterns are already established
- ✅ **No New Dependencies**: STK library is already integrated and building successfully
- ✅ **Similar Complexity**: All three modules are similar in complexity to `stk_string`
- ✅ **Visualization Pattern**: Node visualization pattern already established in `stk_string` and `VCOModuleProcessor`

**Weak Points:**
- ⚠️ **Instrument-Specific APIs**: Each instrument type has unique control change numbers and parameter ranges (mitigated by STK documentation)
- ⚠️ **Wind Instrument Breath Control**: Wind instruments use `startBlowing()`/`stopBlowing()` instead of simple `noteOn()`/`noteOff()` (requires careful gate handling)
- ⚠️ **Percussion Instrument Variety**: `Shakers` has 23 different instrument types with different parameter behaviors (requires careful parameter mapping)
- ⚠️ **ModalBar Presets**: `ModalBar` uses preset selection (0-8) rather than continuous parameters (requires dropdown UI)
- ⚠️ **Testing Required**: Each instrument needs testing to ensure proper parameter ranges and audio output levels

---

## Detailed Risk Analysis

### 1. Technical Risks

#### **Risk: Wind Instrument Breath Control Complexity**
- **Severity**: Low-Medium
- **Probability**: Medium
- **Impact**: Wind instruments require continuous breath pressure control via `startBlowing()`/`stopBlowing()` rather than simple note triggers
- **Mitigation**: 
  - Use gate signal to control breath pressure envelope
  - Map gate high → `startBlowing(amplitude, rate)` with CV-modulated amplitude
  - Map gate low → `stopBlowing(rate)` with configurable release rate
  - Implement breath pressure smoothing to avoid clicks
  - Reference: `stk_string` gate handling pattern

#### **Risk: Percussion Instrument Parameter Mapping**
- **Severity**: Low
- **Probability**: Medium
- **Impact**: Different percussion instruments (ModalBar, BandedWG, Shakers) have different parameter sets and control change numbers
- **Mitigation**:
  - Create instrument-specific parameter mapping functions
  - Use `controlChange()` with documented control numbers
  - Implement conditional parameter updates based on instrument type
  - Test each instrument type individually

#### **Risk: Shakers Instrument Selection**
- **Severity**: Low
- **Probability**: Low
- **Impact**: `Shakers` uses `noteOn(instrument, amplitude)` where `instrument` is a type selector (0-22), not a frequency
- **Mitigation**:
  - Use frequency parameter as instrument selector (map 0-2000 Hz to 0-22)
  - Or add separate "Instrument Type" parameter (like `stk_string`)
  - Document the mapping clearly in UI

#### **Risk: ModalBar Preset System**
- **Severity**: Low
- **Probability**: Low
- **Impact**: `ModalBar` uses discrete presets (0-8) rather than continuous parameters
- **Mitigation**:
  - Use `AudioParameterChoice` for preset selection (like Instrument Type in `stk_string`)
  - Map presets to user-friendly names (Marimba, Vibraphone, Agogo, etc.)
  - Allow CV modulation of preset via quantized mapping

### 2. Integration Risks

#### **Risk: Factory Registration**
- **Severity**: Very Low
- **Probability**: Very Low
- **Impact**: Module not appearing in menus or factory
- **Mitigation**: Follow exact pattern from `stk_string` registration in `ModularSynthProcessor.cpp`

#### **Risk: Pin Database Configuration**
- **Severity**: Very Low
- **Probability**: Very Low
- **Impact**: Incorrect I/O pin labels or descriptions
- **Mitigation**: Copy structure from `stk_string` in `PinDatabase.cpp` and adapt for each module's specific inputs/outputs

#### **Risk: UI Menu Integration**
- **Severity**: Very Low
- **Probability**: Very Low
- **Impact**: Module not appearing in left panel, context menu, or fuzzy search
- **Mitigation**: Follow exact pattern from `stk_string` in `ImGuiNodeEditorComponent.cpp`

### 3. Performance Risks

#### **Risk: CPU Usage**
- **Severity**: Low
- **Probability**: Low
- **Impact**: Multiple STK modules causing CPU spikes
- **Mitigation**:
  - STK instruments are optimized for real-time (similar to `stk_string`)
  - Monitor CPU usage during testing
  - Document expected CPU usage per module instance
  - Consider voice limiting if needed (future optimization)

#### **Risk: Audio Buffer Underruns**
- **Severity**: Very Low
- **Probability**: Very Low
- **Impact**: Audio dropouts with multiple modules
- **Mitigation**:
  - STK `tick()` is sample-accurate and efficient
  - Follow same buffer processing pattern as `stk_string`
  - Test with multiple instances in parallel

### 4. User Experience Risks

#### **Risk: Parameter Confusion**
- **Severity**: Low
- **Probability**: Medium
- **Impact**: Users confused by instrument-specific parameters
- **Mitigation**:
  - Clear parameter labels and tooltips
  - Group related parameters in UI
  - Provide sensible default values
  - Document parameter ranges in module descriptions

#### **Risk: Inconsistent UI Patterns**
- **Severity**: Low
- **Probability**: Low
- **Impact**: Different UI layout/behavior compared to `stk_string`
- **Mitigation**:
  - Follow exact UI pattern from `stk_string`
  - Use same visualization approach (waveform display, parameter sliders)
  - Maintain consistent scroll-edit behavior

---

## Implementation Plan

### Phase 1: Module Structure & Foundation (Difficulty: Easy, Duration: 1 day)

#### 1.1 Create Module Files

**For each module (`stk_wind`, `stk_percussion`, `stk_plucked`):**

**Files to Create:**
```
juce/Source/audio/modules/stk/
├── StkWindModuleProcessor.h
├── StkWindModuleProcessor.cpp
├── StkPercussionModuleProcessor.h
├── StkPercussionModuleProcessor.cpp
├── StkPluckedModuleProcessor.h
└── StkPluckedModuleProcessor.cpp
```

**Template Source:** Copy structure from `StkStringModuleProcessor.h/cpp`

**Key Changes:**
- Rename class and module name
- Update parameter IDs to match instrument-specific parameters
- Update includes for specific STK instruments
- Adjust bus configuration for module-specific inputs

#### 1.2 Parameter Layout Definition

**`stk_wind` Parameters:**
```cpp
static constexpr auto paramIdFrequency = "frequency";
static constexpr auto paramIdInstrumentType = "instrument_type"; // Flute, Clarinet, Saxophone, Brass
static constexpr auto paramIdBreathPressure = "breath_pressure"; // 0.0-1.0
static constexpr auto paramIdVibratoRate = "vibrato_rate"; // 0.0-20.0 Hz
static constexpr auto paramIdVibratoDepth = "vibrato_depth"; // 0.0-1.0
static constexpr auto paramIdReedStiffness = "reed_stiffness"; // 0.0-1.0 (Clarinet/Saxophone)
static constexpr auto paramIdJetDelay = "jet_delay"; // 0.0-1.0 (Flute)
static constexpr auto paramIdLipTension = "lip_tension"; // 0.0-1.0 (Brass)

// CV modulation inputs
static constexpr auto paramIdFreqMod = "freq_mod";
static constexpr auto paramIdBreathMod = "breath_mod";
static constexpr auto paramIdVibratoMod = "vibrato_mod";
static constexpr auto paramIdGateMod = "gate_mod";
```

**`stk_percussion` Parameters:**
```cpp
static constexpr auto paramIdFrequency = "frequency";
static constexpr auto paramIdInstrumentType = "instrument_type"; // ModalBar, BandedWG, Shakers
static constexpr auto paramIdStrikeVelocity = "strike_velocity"; // 0.0-1.0
static constexpr auto paramIdStrikePosition = "strike_position"; // 0.0-1.0
static constexpr auto paramIdStickHardness = "stick_hardness"; // 0.0-1.0 (ModalBar)
static constexpr auto paramIdPreset = "preset"; // 0-8 (ModalBar), 0-3 (BandedWG), 0-22 (Shakers)
static constexpr auto paramIdDecay = "decay"; // 0.0-1.0
static constexpr auto paramIdResonance = "resonance"; // 0.0-1.0

// CV modulation inputs
static constexpr auto paramIdFreqMod = "freq_mod";
static constexpr auto paramIdVelocityMod = "velocity_mod";
static constexpr auto paramIdGateMod = "gate_mod";
```

**`stk_plucked` Parameters:**
```cpp
static constexpr auto paramIdFrequency = "frequency";
static constexpr auto paramIdDamping = "damping"; // 0.0-1.0 (loop gain)
static constexpr auto paramIdPickPosition = "pick_position"; // 0.0-1.0 (internal, not directly exposed in STK)
static constexpr auto paramIdPluckVelocity = "pluck_velocity"; // 0.0-1.0

// CV modulation inputs
static constexpr auto paramIdFreqMod = "freq_mod";
static constexpr auto paramIdDampingMod = "damping_mod";
static constexpr auto paramIdGateMod = "gate_mod";
```

#### 1.3 Bus Configuration

**`stk_wind`:**
```cpp
BusesProperties()
    .withInput("Inputs", juce::AudioChannelSet::discreteChannels(4), true)
    // ch0: Freq Mod, ch1: Gate, ch2: Breath Pressure, ch3: Vibrato
    .withOutput("Output", juce::AudioChannelSet::mono(), true)
```

**`stk_percussion`:**
```cpp
BusesProperties()
    .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true)
    // ch0: Freq Mod, ch1: Gate/Strike, ch2: Velocity
    .withOutput("Output", juce::AudioChannelSet::mono(), true)
```

**`stk_plucked`:**
```cpp
BusesProperties()
    .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true)
    // ch0: Freq Mod, ch1: Gate, ch2: Damping Mod
    .withOutput("Output", juce::AudioChannelSet::mono(), true)
```

---

### Phase 2: Core Implementation (Difficulty: Medium, Duration: 2-3 days per module)

#### 2.1 STK Wind Module (`stk_wind`)

**Instrument Selection:**
```cpp
void StkWindModuleProcessor::updateInstrument()
{
    auto type = (int)instrumentTypeParam->load();
    
#ifdef STK_FOUND
    switch (type)
    {
        case 0: // Flute
            instrument = std::make_unique<stk::Flute>(20.0); // lowestFrequency
            break;
        case 1: // Clarinet
            instrument = std::make_unique<stk::Clarinet>(20.0);
            break;
        case 2: // Saxophone
            instrument = std::make_unique<stk::Saxofony>(20.0);
            break;
        case 3: // Brass
            instrument = std::make_unique<stk::Brass>(20.0);
            break;
    }
    
    if (instrument)
    {
        instrument->setSampleRate(currentSampleRate);
        // Set initial frequency
        instrument->setFrequency(frequencyParam->load());
    }
#endif
}
```

**Breath Control Implementation:**
```cpp
void StkWindModuleProcessor::processBlock(...)
{
    // ... get modulation buffers ...
    
    const float* gateBuffer = getModulationBuffer(paramIdGateMod);
    const float* breathModBuffer = getModulationBuffer(paramIdBreathMod);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Get gate level
        float gateLevel = gateBuffer ? gateBuffer[i] : 0.0f;
        smoothedGate = smoothedGate * 0.95f + gateLevel * 0.05f;
        
        // Calculate breath pressure (gate + CV modulation)
        float breathPressure = breathPressureParam->load();
        if (breathModBuffer)
            breathPressure = juce::jlimit(0.0f, 1.0f, breathPressure + breathModBuffer[i]);
        breathPressure *= smoothedGate; // Gate controls breath on/off
        
        // Update frequency with modulation
        float freq = frequencyParam->load();
        if (freqModBuffer)
            freq = juce::jlimit(20.0f, 2000.0f, freq + freqModBuffer[i] * 200.0f);
        
        if (instrument)
        {
            instrument->setFrequency(freq);
            
            // Wind instruments use startBlowing/stopBlowing
            bool isGateHigh = smoothedGate > 0.1f;
            if (isGateHigh && !wasGateHigh)
            {
                // Gate just went high - start blowing
                instrument->startBlowing(breathPressure, 0.5f); // amplitude, rate
            }
            else if (!isGateHigh && wasGateHigh)
            {
                // Gate just went low - stop blowing
                instrument->stopBlowing(0.1f); // release rate
            }
            else if (isGateHigh)
            {
                // Continuous blowing - update breath pressure via controlChange
                // Control 128 = Breath Pressure (for most wind instruments)
                instrument->controlChange(128, breathPressure * 128.0f);
            }
            
            wasGateHigh = isGateHigh;
            
            // Update vibrato
            float vibratoRate = vibratoRateParam->load();
            float vibratoDepth = vibratoDepthParam->load();
            if (vibratoModBuffer)
                vibratoDepth = juce::jlimit(0.0f, 1.0f, vibratoDepth + vibratoModBuffer[i]);
            
            instrument->controlChange(11, vibratoRate * 128.0f / 20.0f); // Vibrato Frequency
            instrument->controlChange(1, vibratoDepth * 128.0f); // Vibrato Gain
            
            // Instrument-specific parameters
            if (auto* clarinet = dynamic_cast<stk::Clarinet*>(instrument.get()))
            {
                float reedStiffness = reedStiffnessParam->load();
                clarinet->controlChange(2, reedStiffness * 128.0f); // Reed Stiffness
            }
            else if (auto* sax = dynamic_cast<stk::Saxofony*>(instrument.get()))
            {
                float reedStiffness = reedStiffnessParam->load();
                sax->controlChange(2, reedStiffness * 128.0f); // Reed Stiffness
            }
            else if (auto* flute = dynamic_cast<stk::Flute*>(instrument.get()))
            {
                float jetDelay = jetDelayParam->load();
                flute->controlChange(2, jetDelay * 128.0f); // Jet Delay
            }
            else if (auto* brass = dynamic_cast<stk::Brass*>(instrument.get()))
            {
                float lipTension = lipTensionParam->load();
                brass->controlChange(2, lipTension * 128.0f); // Lip Tension
            }
            
            // Generate audio sample
            float sample = instrument->tick();
            output[i] = sample * 10.0f; // Gain boost (similar to stk_string)
        }
        else
        {
            output[i] = 0.0f;
        }
    }
}
```

**Key Implementation Notes:**
- Wind instruments require continuous breath pressure control
- Use `startBlowing()`/`stopBlowing()` for gate transitions
- Use `controlChange(128, value)` for continuous breath pressure updates
- Vibrato is common to all wind instruments (control 11 = rate, control 1 = depth)
- Each instrument type has unique parameters (reed stiffness, jet delay, lip tension)

#### 2.2 STK Percussion Module (`stk_percussion`)

**Instrument Selection:**
```cpp
void StkPercussionModuleProcessor::updateInstrument()
{
    auto type = (int)instrumentTypeParam->load();
    
#ifdef STK_FOUND
    switch (type)
    {
        case 0: // ModalBar (Marimba, Vibraphone, etc.)
            instrument = std::make_unique<stk::ModalBar>();
            break;
        case 1: // BandedWG (Cymbal, Glass, Bowl)
            instrument = std::make_unique<stk::BandedWG>();
            break;
        case 2: // Shakers (Maraca, Tambourine, etc.)
            instrument = std::make_unique<stk::Shakers>(0); // Default to Maraca
            break;
    }
    
    if (instrument)
    {
        instrument->setSampleRate(currentSampleRate);
        // Set initial frequency
        instrument->setFrequency(frequencyParam->load());
    }
#endif
}
```

**Strike/Trigger Implementation:**
```cpp
void StkPercussionModuleProcessor::processBlock(...)
{
    // ... get modulation buffers ...
    
    const float* gateBuffer = getModulationBuffer(paramIdGateMod);
    const float* velocityModBuffer = getModulationBuffer(paramIdVelocityMod);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Get gate/strike level
        float gateLevel = gateBuffer ? gateBuffer[i] : 0.0f;
        smoothedGate = smoothedGate * 0.95f + gateLevel * 0.05f;
        
        // Update frequency with modulation
        float freq = frequencyParam->load();
        if (freqModBuffer)
            freq = juce::jlimit(20.0f, 2000.0f, freq + freqModBuffer[i] * 200.0f);
        
        // Calculate strike velocity
        float velocity = strikeVelocityParam->load();
        if (velocityModBuffer)
            velocity = juce::jlimit(0.0f, 1.0f, velocity + velocityModBuffer[i]);
        
        if (instrument)
        {
            instrument->setFrequency(freq);
            
            // Detect gate edge for strike trigger
            bool isGateHigh = smoothedGate > 0.1f;
            if (isGateHigh && !wasGateHigh)
            {
                // Gate edge - trigger strike
                if (auto* modalBar = dynamic_cast<stk::ModalBar*>(instrument.get()))
                {
                    modalBar->noteOn(freq, velocity);
                }
                else if (auto* bandedWG = dynamic_cast<stk::BandedWG*>(instrument.get()))
                {
                    bandedWG->pluck(velocity); // BandedWG uses pluck() for strikes
                }
                else if (auto* shakers = dynamic_cast<stk::Shakers*>(instrument.get()))
                {
                    // Shakers uses noteOn(instrument, amplitude) where instrument is type selector
                    int shakerType = (int)presetParam->load();
                    shakers->noteOn(shakerType, velocity);
                }
            }
            
            wasGateHigh = isGateHigh;
            
            // Update instrument-specific parameters
            if (auto* modalBar = dynamic_cast<stk::ModalBar*>(instrument.get()))
            {
                int preset = (int)presetParam->load();
                modalBar->setPreset(preset);
                
                float stickHardness = stickHardnessParam->load();
                modalBar->setStickHardness(stickHardness);
                
                float strikePos = strikePositionParam->load();
                modalBar->setStrikePosition(strikePos);
            }
            else if (auto* bandedWG = dynamic_cast<stk::BandedWG*>(instrument.get()))
            {
                int preset = (int)presetParam->load();
                bandedWG->setPreset(preset);
                
                float strikePos = strikePositionParam->load();
                bandedWG->setStrikePosition(strikePos);
            }
            else if (auto* shakers = dynamic_cast<stk::Shakers*>(instrument.get()))
            {
                // Shakers parameters
                float decay = decayParam->load();
                shakers->controlChange(4, decay * 128.0f); // System Decay
                
                float resonance = resonanceParam->load();
                shakers->controlChange(1, resonance * 128.0f); // Resonance Frequency
            }
            
            // Generate audio sample
            float sample = instrument->tick();
            output[i] = sample * 8.0f; // Gain boost (percussion may need less than strings)
        }
        else
        {
            output[i] = 0.0f;
        }
    }
}
```

**Key Implementation Notes:**
- Percussion instruments are triggered on gate edge (not continuous like wind)
- `ModalBar` and `BandedWG` use `noteOn()` or `pluck()` for strikes
- `Shakers` uses `noteOn(instrumentType, amplitude)` where instrumentType selects the shaker type
- Each instrument type has different parameter sets
- Preset selection is important for `ModalBar` (9 presets) and `BandedWG` (4 presets)

#### 2.3 STK Plucked Module (`stk_plucked`)

**Implementation (Simplified Karplus-Strong):**
```cpp
void StkPluckedModuleProcessor::processBlock(...)
{
    // ... get modulation buffers ...
    
    const float* gateBuffer = getModulationBuffer(paramIdGateMod);
    const float* dampingModBuffer = getModulationBuffer(paramIdDampingMod);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Get gate level
        float gateLevel = gateBuffer ? gateBuffer[i] : 0.0f;
        smoothedGate = smoothedGate * 0.95f + gateLevel * 0.05f;
        
        // Update frequency with modulation
        float freq = frequencyParam->load();
        if (freqModBuffer)
            freq = juce::jlimit(20.0f, 2000.0f, freq + freqModBuffer[i] * 200.0f);
        
        // Calculate damping (loop gain)
        float damping = dampingParam->load();
        if (dampingModBuffer)
            damping = juce::jlimit(0.0f, 1.0f, damping + dampingModBuffer[i]);
        
        // Calculate pluck velocity
        float velocity = pluckVelocityParam->load();
        
        if (instrument)
        {
            instrument->setFrequency(freq);
            
            // Detect gate edge for pluck trigger
            bool isGateHigh = smoothedGate > 0.1f;
            if (isGateHigh && !wasGateHigh)
            {
                // Gate edge - trigger pluck
                instrument->pluck(velocity);
            }
            
            wasGateHigh = isGateHigh;
            
            // Note: Plucked doesn't expose damping directly via controlChange
            // Damping is controlled by loop gain, which is internal
            // We can't directly control it, but we can re-pluck with different velocities
            // to simulate damping changes
            
            // Generate audio sample
            float sample = instrument->tick();
            output[i] = sample * 10.0f; // Gain boost
        }
        else
        {
            output[i] = 0.0f;
        }
    }
}
```

**Key Implementation Notes:**
- `Plucked` is the simplest STK instrument (pure Karplus-Strong)
- Uses `pluck(amplitude)` for triggering (not `noteOn()`)
- Damping is controlled internally via loop gain (not directly accessible)
- Very lightweight - good for polyphonic use
- May need continuous re-triggering like `stk_string` Plucked instruments if gate is held

---

### Phase 3: UI Implementation (Difficulty: Advanced, Duration: 1-2 days per module)

#### 3.1 Visualization Data Structure

**Add to each module header (similar to `stk_string`):**
```cpp
#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> outputWaveform;
        std::atomic<float> currentFrequency { 440.0f };
        std::atomic<int> currentInstrumentType { 0 };
        std::atomic<float> gateLevel { 0.0f };
        std::atomic<float> outputLevel { 0.0f };
        // Instrument-specific visualization
        std::atomic<float> breathPressure { 0.0f }; // Wind
        std::atomic<float> strikeVelocity { 0.0f }; // Percussion
        
        VizData()
        {
            for (auto& v : outputWaveform) v.store(0.0f);
        }
    };
    VizData vizData;
    
    juce::AudioBuffer<float> vizOutputBuffer;
    int vizWritePos { 0 };
    static constexpr int vizBufferSize = 2048;
#endif
```

#### 3.2 Waveform Capture in `processBlock()`

**Add to each module's `processBlock()` (after generating sample):**
```cpp
#if defined(PRESET_CREATOR_UI)
    // Capture waveform for visualization
    if (vizOutputBuffer.getNumSamples() > 0)
    {
        vizOutputBuffer.setSample(0, vizWritePos, sample);
        vizWritePos = (vizWritePos + 1) % vizBufferSize;
        
        // Update visualization data periodically (every ~8ms at 48kHz = ~384 samples)
        static int vizUpdateCounter = 0;
        if (++vizUpdateCounter >= 384)
        {
            vizUpdateCounter = 0;
            
            // Copy recent waveform to visualization buffer
            const int copyStart = (vizWritePos - VizData::waveformPoints + vizBufferSize) % vizBufferSize;
            for (int i = 0; i < VizData::waveformPoints; ++i)
            {
                const int srcIdx = (copyStart + i) % vizBufferSize;
                vizData.outputWaveform[i].store(vizOutputBuffer.getSample(0, srcIdx));
            }
            
            // Update other visualization data
            vizData.currentFrequency.store(freq);
            vizData.currentInstrumentType.store((int)instrumentTypeParam->load());
            vizData.gateLevel.store(smoothedGate);
            vizData.outputLevel.store(sample);
            
            // Instrument-specific
            if (/* wind module */)
                vizData.breathPressure.store(breathPressure);
            else if (/* percussion module */)
                vizData.strikeVelocity.store(velocity);
        }
    }
#endif
```

#### 3.3 `drawParametersInNode()` Implementation

**Follow pattern from `stk_string`:**

1. **Waveform Visualization** - Child window with oscilloscope display
2. **Parameter Sliders** - Frequency, instrument-specific parameters
3. **Instrument Type Combo** - Dropdown with scroll-edit support
4. **CV Modulation Indicators** - Visual feedback for modulated parameters
5. **Gate Level Indicator** - Visual gate state display

**Key UI Elements:**
- Use `ImGui::SliderFloat()` for continuous parameters
- Use `ImGui::Combo()` for instrument type selection (with scroll-edit)
- Use `adjustParamOnWheel()` for scroll-edit support on all sliders
- Follow theme colors from `ThemeManager`
- Display waveform in child window (similar to `VCOModuleProcessor`)

---

### Phase 4: Integration (Difficulty: Easy, Duration: 1 day)

#### 4.1 Factory Registration

**File:** `juce/Source/audio/graph/ModularSynthProcessor.cpp`

**Add to `getModuleFactory()`:**
```cpp
reg("stk_wind", []{ return std::make_unique<StkWindModuleProcessor>(); });
reg("stk_percussion", []{ return std::make_unique<StkPercussionModuleProcessor>(); });
reg("stk_plucked", []{ return std::make_unique<StkPluckedModuleProcessor>(); });
```

#### 4.2 Pin Database Registration

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

**Add for each module:**
```cpp
db["stk_wind"] = ModulePinInfo(
    NodeWidth::Medium,
    {  // Inputs
        { 0, "Freq Mod", PinType::CV },
        { 1, "Gate", PinType::Gate },
        { 2, "Breath", PinType::CV },
        { 3, "Vibrato", PinType::CV }
    },
    {  // Outputs
        { 0, "Out", PinType::Audio }
    }
);

db["stk_percussion"] = ModulePinInfo(
    NodeWidth::Medium,
    {  // Inputs
        { 0, "Freq Mod", PinType::CV },
        { 1, "Strike", PinType::Gate },
        { 2, "Velocity", PinType::CV }
    },
    {  // Outputs
        { 0, "Out", PinType::Audio }
    }
);

db["stk_plucked"] = ModulePinInfo(
    NodeWidth::Medium,
    {  // Inputs
        { 0, "Freq Mod", PinType::CV },
        { 1, "Gate", PinType::Gate },
        { 2, "Damping", PinType::CV }
    },
    {  // Outputs
        { 0, "Out", PinType::Audio }
    }
);
```

#### 4.3 UI Menu Integration

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Add to left panel menu (Sources category):**
```cpp
if (ImGui::MenuItem("STK Wind", nullptr, false, true))
    addNodeAtMouse("stk_wind");
if (ImGui::MenuItem("STK Percussion", nullptr, false, true))
    addNodeAtMouse("stk_percussion");
if (ImGui::MenuItem("STK Plucked", nullptr, false, true))
    addNodeAtMouse("stk_plucked");
```

**Add to right-click context menu:**
```cpp
if (ImGui::MenuItem("STK Wind", nullptr, false, true))
    addNodeAtMouse("stk_wind");
if (ImGui::MenuItem("STK Percussion", nullptr, false, true))
    addNodeAtMouse("stk_percussion");
if (ImGui::MenuItem("STK Plucked", nullptr, false, true))
    addNodeAtMouse("stk_plucked");
```

**Add to "Insert After Selection" menu:**
```cpp
if (ImGui::MenuItem("STK Wind", nullptr, false, true))
    addNodeAfterSelection("stk_wind");
if (ImGui::MenuItem("STK Percussion", nullptr, false, true))
    addNodeAfterSelection("stk_percussion");
if (ImGui::MenuItem("STK Plucked", nullptr, false, true))
    addNodeAfterSelection("stk_plucked");
```

**Add to fuzzy search system:**
```cpp
// In fuzzy search keyword mapping
searchKeywords["wind"] = { "stk_wind" };
searchKeywords["flute"] = { "stk_wind" };
searchKeywords["clarinet"] = { "stk_wind" };
searchKeywords["saxophone"] = { "stk_wind" };
searchKeywords["brass"] = { "stk_wind" };

searchKeywords["percussion"] = { "stk_percussion" };
searchKeywords["drum"] = { "stk_percussion" };
searchKeywords["marimba"] = { "stk_percussion" };
searchKeywords["cymbal"] = { "stk_percussion" };
searchKeywords["shaker"] = { "stk_percussion" };

searchKeywords["plucked"] = { "stk_plucked" };
searchKeywords["karplus"] = { "stk_plucked" };
searchKeywords["string"] = { "stk_plucked", "stk_string" }; // Both match
```

**Add module descriptions:**
```cpp
descriptions["stk_wind"] = "Physical modeling wind instruments (flute, clarinet, saxophone, brass)";
descriptions["stk_percussion"] = "Modal synthesis percussion (marimba, cymbal, shakers, etc.)";
descriptions["stk_plucked"] = "Karplus-Strong plucked string synthesis";
```

---

## Testing Strategy

### Unit Testing
1. **Instrument Creation**: Verify each instrument type instantiates correctly
2. **Parameter Updates**: Test parameter changes update instrument state
3. **Gate Handling**: Test gate edge detection and breath/strike triggering
4. **CV Modulation**: Test CV inputs modulate parameters correctly
5. **Audio Output**: Verify non-zero audio output when gate is high

### Integration Testing
1. **Factory Registration**: Verify modules appear in factory and can be created
2. **Pin Database**: Verify I/O pins are correctly labeled and routed
3. **UI Menus**: Verify modules appear in all menus (left panel, context menu, fuzzy search)
4. **Parameter Persistence**: Verify parameters save/load correctly in presets
5. **Multiple Instances**: Test multiple instances of each module in parallel

### Performance Testing
1. **CPU Usage**: Measure CPU usage per module instance
2. **Audio Latency**: Verify no audio dropouts or latency issues
3. **Memory Usage**: Check for memory leaks with multiple instances
4. **Real-time Performance**: Test with low buffer sizes (64-128 samples)

### User Experience Testing
1. **Parameter Ranges**: Verify all parameters have sensible ranges and defaults
2. **UI Responsiveness**: Test scroll-edit, slider dragging, combo selection
3. **Visualization**: Verify waveform display updates correctly
4. **Tooltips**: Verify parameter tooltips are helpful and accurate

---

## Potential Problems & Mitigations

### Problem 1: Wind Instruments Not Producing Sound
**Symptoms**: Gate high but no audio output  
**Causes**: 
- Breath pressure not being applied correctly
- `startBlowing()` not called on gate edge
- Breath pressure too low

**Mitigation**:
- Add logging to verify `startBlowing()` is called
- Ensure breath pressure is scaled correctly (0-1 → 0-128 for controlChange)
- Add minimum breath pressure threshold
- Test with high breath pressure values first

### Problem 2: Percussion Instruments Not Triggering
**Symptoms**: Gate edge detected but no strike sound  
**Causes**:
- Wrong trigger method (`noteOn()` vs `pluck()`)
- Velocity too low
- Frequency out of range for instrument

**Mitigation**:
- Verify correct trigger method for each instrument type
- Add minimum velocity threshold
- Clamp frequency to instrument-specific ranges
- Add logging for trigger events

### Problem 3: Shakers Instrument Selection Confusion
**Symptoms**: Frequency parameter doesn't match expected instrument  
**Causes**:
- Using frequency as instrument selector instead of separate parameter
- Instrument type not mapped correctly

**Mitigation**:
- Use separate "Instrument Type" parameter (like `stk_string`)
- Map instrument types to user-friendly names
- Document the mapping in UI tooltips

### Problem 4: ModalBar Preset Not Updating
**Symptoms**: Preset parameter changes but sound doesn't change  
**Causes**:
- `setPreset()` not called when preset parameter changes
- Preset value out of range (0-8)

**Mitigation**:
- Call `setPreset()` in `updateInstrument()` and when preset parameter changes
- Clamp preset value to valid range
- Add logging to verify preset changes

### Problem 5: Visualization Not Updating
**Symptoms**: Waveform display shows no signal  
**Causes**:
- Visualization buffer not being filled
- Update counter not triggering
- Thread safety issues with atomic updates

**Mitigation**:
- Verify `vizOutputBuffer` is allocated in `prepareToPlay()`
- Check update counter logic
- Ensure atomic operations are used correctly
- Test with known-good audio signal first

### Problem 6: Scroll-Edit Not Working
**Symptoms**: Mouse wheel doesn't adjust parameters  
**Causes**:
- `adjustParamOnWheel()` not called
- Parameter not hovered correctly
- ImGui focus issues

**Mitigation**:
- Follow exact pattern from `stk_string` scroll-edit implementation
- Verify `ImGui::IsItemHovered()` returns true
- Test with different ImGui window focus states

---

## Success Criteria

### Functional Requirements
- ✅ All three modules produce audio output when gate is high
- ✅ All instrument types are selectable and produce distinct sounds
- ✅ CV modulation works for all modulatable parameters
- ✅ Gate/trigger handling works correctly for each module type
- ✅ Parameters save/load correctly in presets
- ✅ Modules appear in all UI menus and fuzzy search

### Performance Requirements
- ✅ CPU usage per module instance < 5% (on modern CPU)
- ✅ No audio dropouts with 10+ instances
- ✅ Real-time performance at 64-128 sample buffer sizes

### User Experience Requirements
- ✅ All parameters have sensible ranges and defaults
- ✅ UI is responsive and intuitive
- ✅ Waveform visualization updates in real-time
- ✅ Scroll-edit works on all parameters
- ✅ Parameter tooltips are helpful

---

## Dependencies

### Existing Dependencies (Already Integrated)
- ✅ STK library (already building and working)
- ✅ JUCE framework
- ✅ ModuleProcessor base class
- ✅ AudioProcessorValueTreeState (APVTS)
- ✅ ThemeManager (for UI theming)
- ✅ ImGui/ImNodes (for UI)

### No New Dependencies Required
- All necessary libraries and frameworks are already integrated
- STK instruments are header-only (implementation already compiled)

---

## Timeline

### Week 1: Foundation & Core Implementation
- **Day 1**: Module structure creation, parameter layouts, factory registration
- **Day 2-3**: `stk_wind` core implementation
- **Day 4-5**: `stk_percussion` core implementation
- **Day 6-7**: `stk_plucked` core implementation

### Week 2: UI & Integration
- **Day 8-9**: UI implementation for all three modules
- **Day 10**: Integration (pin database, menus, fuzzy search)
- **Day 11-12**: Testing, bug fixes, polish

**Total Duration**: 12 days (2.5 weeks)

---

## Conclusion

This implementation plan provides a comprehensive roadmap for implementing the three remaining STK modules. With a **LOW risk rating** and **VERY HIGH confidence (95%)**, the implementation should proceed smoothly by following the established patterns from the `stk_string` module.

The main challenges will be:
1. Understanding instrument-specific parameter mappings (mitigated by STK documentation)
2. Implementing breath control for wind instruments (mitigated by gate handling pattern from `stk_string`)
3. Handling different trigger methods for percussion (mitigated by instrument type detection)

All risks have been identified and mitigation strategies are in place. The implementation can begin immediately with high confidence of success.

