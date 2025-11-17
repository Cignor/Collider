# Sample & Hold Implementation Comparison: HISE vs Collider PYO

## Executive Summary

This document provides a detailed technical comparison between **HISE's `fx.sampleandhold`** node implementation and **Collider PYO's `SAndHModuleProcessor`**. The two implementations represent fundamentally different architectural approaches to sample & hold functionality.

**Key Finding**: HISE uses a **counter-based periodic sampling** approach, while Collider PYO uses a **gate-triggered event-driven** approach. These are complementary but distinct implementations serving different use cases.

---

## Table of Contents

1. [Architectural Overview](#architectural-overview)
2. [HISE Implementation Analysis](#hise-implementation-analysis)
3. [Collider PYO Implementation Analysis](#collider-pyo-implementation-analysis)
4. [Detailed Code Comparison](#detailed-code-comparison)
5. [Parameter Comparison](#parameter-comparison)
6. [Signal Flow Analysis](#signal-flow-analysis)
7. [Edge Cases & Potential Issues](#edge-cases--potential-issues)
8. [Recommendations](#recommendations)

---

## Architectural Overview

### HISE's Approach: Counter-Based Periodic Sampling

**Concept**: Samples the input signal at **regular, fixed intervals** determined by a `Counter` parameter.

```cpp
// Pseudo-code representation of HISE's approach
int counter = 4;  // Sample every 4 samples
int sampleIndex = 0;
float heldValue = 0.0f;

for (int i = 0; i < numSamples; ++i)
{
    if (sampleIndex % counter == 0)
    {
        heldValue = input[i];  // Sample at regular intervals
    }
    output[i] = heldValue;     // Always output held value
    sampleIndex++;
}
```

**Characteristics**:
- ✅ Predictable, deterministic timing
- ✅ No external trigger required
- ✅ Simple implementation
- ❌ Cannot respond to external events
- ❌ Fixed sampling rate (not tempo-synced)

**Documentation Reference**: [HISE Scriptnode fx.sampleandhold](https://docs.hise.dev/scriptnode/list/fx/sampleandhold.html)

**Key Quote from Documentation**:
> "This node samples the input at intervals determined by the Counter parameter, holding each sampled value until the next interval. For instance, setting the Counter to 4 samples the input every 4 samples."

---

### Collider PYO's Approach: Gate-Triggered Event-Driven Sampling

**Concept**: Samples the input signal when a **gate/trigger signal crosses a threshold** (rising edge, falling edge, or both).

```cpp
// Simplified representation of Collider PYO's approach
float threshold = 0.5f;
float lastGate = 0.0f;
float heldValue = 0.0f;
int edgeMode = 0;  // 0=rising, 1=falling, 2=both

for (int i = 0; i < numSamples; ++i)
{
    float gate = gateInput[i];
    
    // Detect edge crossing
    bool risingEdge = (gate > threshold && lastGate <= threshold);
    bool fallingEdge = (gate < threshold && lastGate >= threshold);
    
    // Sample on detected edge
    if ((edgeMode == 0 && risingEdge) || 
        (edgeMode == 1 && fallingEdge) || 
        (edgeMode == 2 && (risingEdge || fallingEdge)))
    {
        heldValue = signalInput[i];  // Sample on trigger
    }
    
    // Apply slew limiting (smoothing)
    output[i] = smoothTowards(heldValue, output[i], slewCoeff);
    lastGate = gate;
}
```

**Characteristics**:
- ✅ Responds to external triggers/events
- ✅ Musically useful (can sync to MIDI, sequencers, etc.)
- ✅ Flexible edge detection (rising/falling/both)
- ✅ Includes slew limiting for smooth transitions
- ❌ More complex implementation
- ❌ Requires gate input signal
- ❌ Edge detection can be sensitive to noise

---

## HISE Implementation Analysis

### Parameter Structure

Based on HISE documentation, the `fx.sampleandhold` node has:

**Single Parameter**:
- `Counter` (Integer, 1-128): Number of samples between each sampling event
  - Counter = 1: Samples every sample (effectively bypass)
  - Counter = 4: Samples every 4 samples
  - Counter = 128: Samples every 128 samples (~2.9ms at 44.1kHz)

### Processing Algorithm (Inferred from Documentation)

```cpp
// HISE's fx.sampleandhold processing loop (conceptual)
class SampleAndHoldNode
{
    int counter = 4;
    int currentIndex = 0;
    float heldValue = 0.0f;
    
    void processBlock(AudioBuffer<float>& buffer)
    {
        const float* input = buffer.getReadPointer(0);
        float* output = buffer.getWritePointer(0);
        const int numSamples = buffer.getNumSamples();
        
        for (int i = 0; i < numSamples; ++i)
        {
            // Check if it's time to sample
            if (currentIndex % counter == 0)
            {
                heldValue = input[i];
            }
            
            output[i] = heldValue;
            currentIndex++;
        }
    }
};
```

### Key Implementation Details

1. **No Gate Input**: HISE's implementation doesn't use external triggers
2. **No Threshold**: No edge detection logic required
3. **No Slew Limiting**: Direct value assignment (instantaneous)
4. **Counter Reset**: Counter wraps around automatically via modulo operation
5. **Mono Processing**: Typically processes single channel (can be duplicated for stereo)

### Use Cases

- **Lo-fi Effects**: Downsampling audio for bitcrushing effects
- **Glitch Effects**: Stepped modulation at regular intervals
- **Noise Reduction**: Holding values to reduce high-frequency content
- **Simple Modulation**: Creating stepped LFOs or control signals

---

## Collider PYO Implementation Analysis

### Parameter Structure

```cpp
// From SAndHModuleProcessor.cpp lines 21-32
juce::AudioProcessorValueTreeState::ParameterLayout SAndHModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Core parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold", 
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "edge", "Edge", 
        juce::StringArray{"Rising", "Falling", "Both"}, 0));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slewMs", "Slew (ms)", 
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.01f, 0.35f), 0.0f));
    
    // Modulation parameters (CV inputs)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold_mod", "Threshold Mod", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slewMs_mod", "Slew Mod", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "edge_mod", "Edge Mod", 0.0f, 1.0f, 0.0f));
    
    return {params.begin(), params.end()};
}
```

### Input/Output Structure

```cpp
// From SAndHModuleProcessor.cpp lines 4-6
SAndHModuleProcessor::SAndHModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(7), true)
        // Channel mapping:
        // 0-1: Signal input (L/R)
        // 2-3: Gate input (L/R)
        // 4:   Threshold modulation CV
        // 5:   Edge mode modulation CV
        // 6:   Slew modulation CV
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
```

### Core Processing Loop

```cpp
// From SAndHModuleProcessor.cpp lines 123-278
void SAndHModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // ... setup code ...
    
    for (int i = 0; i < numSamples; ++i)
    {
        // 1. PER-SAMPLE PARAMETER MODULATION
        // Calculate modulated parameters for this sample
        float thr = baseThreshold;
        if (isThresholdMod && thresholdCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, thresholdCV[i]);
            const float thresholdRange = 0.6f;
            const float offset = (cv - 0.5f) * thresholdRange;
            thr = juce::jlimit(0.0f, 1.0f, baseThreshold + offset);
        }
        
        float slewMs = baseSlewMs;
        if (isSlewMod && slewCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, slewCV[i]);
            const float slewRange = 500.0f;
            const float slewOffset = (cv - 0.5f) * slewRange;
            slewMs = baseSlewMs + slewOffset;
            slewMs = juce::jlimit(0.0f, 2000.0f, slewMs);
        }
        
        int edge = baseEdge;
        if (isEdgeMod && edgeCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, edgeCV[i]);
            const int edgeOffset = static_cast<int>((cv - 0.5f) * 3.0f);
            edge = (baseEdge + edgeOffset + 3) % 3; // Wrap around
        }
        
        // 2. SLEW COEFFICIENT CALCULATION
        const float effectiveSlewMs = std::max(slewMs, 0.1f);
        const float slewCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * effectiveSlewMs * sr)));
        
        // 3. GATE NORMALIZATION
        auto normalizeGate = [](float gate) -> float {
            // Handle bipolar (-1 to 1) or unipolar (0 to 1) gates
            if (gate < 0.0f) return (gate + 1.0f) * 0.5f;
            return juce::jlimit(0.0f, 1.0f, gate);
        };
        
        const float gL = gateL != nullptr ? normalizeGate(gateL[i]) : 0.0f;
        const float gR = gateR != nullptr ? normalizeGate(gateR[i]) : 0.0f;
        
        // 4. EDGE DETECTION
        const bool riseL = (gL > thr && lastGateL <= thr);
        const bool fallL = (gL < thr && lastGateL >= thr);
        const bool riseR = (gR > thr && lastGateR <= thr);
        const bool fallR = (gR < thr && lastGateR >= thr);
        
        // 5. TRIGGER DECISION
        const bool doL = (edge == 0 && riseL) || 
                        (edge == 1 && fallL) || 
                        (edge == 2 && (riseL || fallL));
        const bool doR = (edge == 0 && riseR) || 
                        (edge == 1 && fallR) || 
                        (edge == 2 && (riseR || fallR));
        
        // 6. SAMPLE ON TRIGGER
        if (doL) {
            heldL = sigL[i];
        }
        if (doR) {
            heldR = sigR[i];
        }
        
        // 7. UPDATE GATE STATE
        lastGateL = gL;
        lastGateR = gR;
        
        // 8. APPLY SLEW LIMITING
        outL = outL + slewCoeff * (heldL - outL);
        outR = outR + slewCoeff * (heldR - outR);
        
        // 9. WRITE OUTPUT
        outLw[i] = outL;
        outRw[i] = outR;
    }
}
```

### Key Implementation Details

#### 1. Gate Normalization Logic

```cpp
// Lines 164-169
auto normalizeGate = [](float gate) -> float {
    // If gate is negative, assume bipolar and normalize to 0-1
    if (gate < 0.0f) return (gate + 1.0f) * 0.5f;
    // Otherwise assume unipolar 0-1, clamp to be safe
    return juce::jlimit(0.0f, 1.0f, gate);
};
```

**Analysis**: This handles both unipolar (0-1) and bipolar (-1 to 1) gate signals. However, there's a potential issue:
- **Problem**: If a gate signal is exactly 0.0f, it's treated as unipolar. But a bipolar signal at -1.0f would also normalize to 0.0f.
- **Impact**: Ambiguous normalization could cause missed triggers.

#### 2. Edge Detection Logic

```cpp
// Lines 188-194
const bool riseL = (gL > thr && lastGateL <= thr);
const bool fallL = (gL < thr && lastGateL >= thr);
const bool riseR = (gR > thr && lastGateR <= thr);
const bool fallR = (gR < thr && lastGateR >= thr);

const bool doL = (edge == 0 && riseL) || (edge == 1 && fallL) || (edge == 2 && (riseL || fallL));
const bool doR = (edge == 0 && riseR) || (edge == 1 && fallR) || (edge == 2 && (riseR || fallR));
```

**Analysis**: 
- ✅ Correct edge detection logic
- ✅ Handles all three edge modes
- ⚠️ **Potential Issue**: No hysteresis, which could cause multiple triggers on noisy signals near threshold

#### 3. Slew Limiting

```cpp
// Lines 157-160, 235-236
const float effectiveSlewMs = std::max(slewMs, 0.1f);
const float slewCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * effectiveSlewMs * sr)));

outL = outL + slewCoeff * (heldL - outL);
outR = outR + slewCoeff * (heldR - outR);
```

**Analysis**:
- ✅ Exponential smoothing (one-pole lowpass filter)
- ✅ Minimum slew time (0.1ms) prevents clicks
- ✅ Formula: `α = 1 - exp(-1 / (τ * fs))` where τ = slew time in seconds
- ✅ This is **NOT present in HISE's implementation**

#### 4. Per-Sample Parameter Modulation

```cpp
// Lines 125-150
// Parameters are recalculated for EVERY sample
float thr = baseThreshold;
if (isThresholdMod && thresholdCV != nullptr) {
    // ... modulation calculation ...
}
```

**Analysis**:
- ✅ Allows audio-rate modulation of parameters
- ✅ More flexible than HISE's static parameters
- ⚠️ **Performance Cost**: Recalculating parameters per-sample adds CPU overhead

---

## Detailed Code Comparison

### Sampling Mechanism

| Aspect | HISE | Collider PYO |
|--------|------|--------------|
| **Trigger Source** | Internal counter | External gate signal |
| **Timing** | Regular intervals | Event-driven (edge crossings) |
| **Determinism** | Fully deterministic | Depends on gate signal |
| **Code Complexity** | ~10 lines | ~150 lines |

### HISE's Sampling (Conceptual)

```cpp
// Simple counter-based approach
int counter = 4;  // Parameter
int index = 0;
float held = 0.0f;

for (int i = 0; i < numSamples; ++i)
{
    if (index % counter == 0)
        held = input[i];
    output[i] = held;
    index++;
}
```

### Collider PYO's Sampling

```cpp
// Complex edge-detection approach
float threshold = 0.5f;  // Parameter
float lastGate = 0.0f;
float held = 0.0f;
int edgeMode = 0;  // Parameter

for (int i = 0; i < numSamples; ++i)
{
    float gate = gateInput[i];
    float normalizedGate = normalizeGate(gate);
    
    bool rising = (normalizedGate > threshold && lastGate <= threshold);
    bool falling = (normalizedGate < threshold && lastGate >= threshold);
    
    bool trigger = (edgeMode == 0 && rising) ||
                   (edgeMode == 1 && falling) ||
                   (edgeMode == 2 && (rising || falling));
    
    if (trigger)
        held = signalInput[i];
    
    // Apply slew limiting
    output[i] = smooth(held, output[i], slewCoeff);
    lastGate = normalizedGate;
}
```

### State Management

#### HISE
```cpp
// Minimal state
int currentIndex = 0;  // Counter position
float heldValue = 0.0f;  // Current held value
```

#### Collider PYO
```cpp
// Extensive state (from SAndHModuleProcessor.h lines 82-89)
float lastGateL { 0.0f };      // Previous gate value (L)
float lastGateR { 0.0f };     // Previous gate value (R)
float heldL { 0.0f };          // Held value (L)
float heldR { 0.0f };          // Held value (R)
float outL { 0.0f };            // Current output (L, after slew)
float outR { 0.0f };            // Current output (R, after slew)
double sr { 44100.0 };          // Sample rate
```

---

## Parameter Comparison

### HISE Parameters

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `Counter` | Integer | 1-128 | 4 | Samples between each sampling event |

**Total**: 1 parameter

### Collider PYO Parameters

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `threshold` | Float | 0.0-1.0 | 0.5 | Gate threshold for edge detection |
| `edge` | Choice | Rising/Falling/Both | Rising | Edge type to trigger on |
| `slewMs` | Float | 0.0-2000.0 ms | 0.0 | Slew limiting time |
| `threshold_mod` | Float | 0.0-1.0 | 0.0 | CV modulation for threshold |
| `slewMs_mod` | Float | 0.0-1.0 | 0.0 | CV modulation for slew |
| `edge_mod` | Float | 0.0-1.0 | 0.0 | CV modulation for edge mode |

**Total**: 6 parameters (3 core + 3 modulation)

---

## Signal Flow Analysis

### HISE Signal Flow

```
Input Signal
    ↓
[Counter Check] → (every N samples)
    ↓
[Sample Value] → heldValue
    ↓
Output (heldValue)
```

**Characteristics**:
- Single input signal
- No external dependencies
- Predictable output timing

### Collider PYO Signal Flow

```
Signal Input ──┐
               ├─→ [Edge Detection] ──→ [Sample on Trigger] ──→ [Slew Limiter] ──→ Output
Gate Input ────┘         ↑
                         │
                    [Threshold]
                    [Edge Mode]
```

**Characteristics**:
- Two input signals (signal + gate)
- External trigger dependency
- Additional processing (slew limiting)
- More flexible but more complex

---

## Edge Cases & Potential Issues

### Issue 1: Gate Normalization Ambiguity

**Location**: `SAndHModuleProcessor.cpp` lines 164-169

```cpp
auto normalizeGate = [](float gate) -> float {
    if (gate < 0.0f) return (gate + 1.0f) * 0.5f;
    return juce::jlimit(0.0f, 1.0f, gate);
};
```

**Problem**: 
- A bipolar gate at -1.0f normalizes to 0.0f
- A unipolar gate at 0.0f also equals 0.0f
- Cannot distinguish between "low gate" and "negative gate"

**Impact**: 
- May miss triggers if gate signal is exactly at -1.0f
- Ambiguous behavior at boundary conditions

**Recommendation**:
```cpp
auto normalizeGate = [](float gate) -> float {
    // Assume all gates are unipolar 0-1 by default
    // Only normalize if we detect clear bipolar signal
    if (gate < -0.5f) {
        // Definitely bipolar, normalize
        return juce::jlimit(0.0f, 1.0f, (gate + 1.0f) * 0.5f);
    }
    // Assume unipolar, just clamp
    return juce::jlimit(0.0f, 1.0f, gate);
};
```

### Issue 2: No Hysteresis in Edge Detection

**Location**: `SAndHModuleProcessor.cpp` lines 188-194

**Problem**: 
- If gate signal hovers exactly at threshold, multiple triggers can occur
- No hysteresis band to prevent chatter

**Example**:
```
Gate: 0.49 → 0.51 → 0.49 → 0.51 (oscillating at threshold)
Result: Multiple triggers on each crossing
```

**Recommendation**: Add hysteresis
```cpp
const float hysteresis = 0.01f;  // Small hysteresis band
const float upperThr = threshold + hysteresis;
const float lowerThr = threshold - hysteresis;

bool rising = (gL > upperThr && lastGateL <= lowerThr);
bool falling = (gL < lowerThr && lastGateL >= upperThr);
```

### Issue 3: Per-Sample Parameter Recalculation

**Location**: `SAndHModuleProcessor.cpp` lines 125-150

**Problem**: 
- Parameters recalculated for every sample
- Adds CPU overhead even when not modulated

**Optimization**:
```cpp
// Only recalculate if modulation is connected
if (isThresholdMod && thresholdCV != nullptr) {
    // Recalculate per sample
} else {
    // Use cached base value
    thr = baseThreshold;
}
```

### Issue 4: Debug Logging in Production Code

**Location**: Throughout `SAndHModuleProcessor.cpp` (lines 92-277)

**Problem**: 
- Extensive debug logging in audio processing loop
- String concatenation is expensive
- Should be conditionally compiled or removed

**Recommendation**: 
```cpp
#ifdef DEBUG_SANDH
    // Debug logging code
#endif
```

### Issue 5: Slew Minimum Time

**Location**: `SAndHModuleProcessor.cpp` line 159

```cpp
const float effectiveSlewMs = std::max(slewMs, 0.1f);
```

**Analysis**: 
- ✅ Good: Prevents clicks/pops
- ⚠️ **Issue**: Even with `slewMs = 0.0f`, there's still 0.1ms slew
- **Impact**: Cannot achieve truly instant transitions even when desired

**Recommendation**: Make minimum slew optional or configurable
```cpp
const float effectiveSlewMs = (slewMs < 0.001f) ? 0.0f : std::max(slewMs, 0.1f);
if (effectiveSlewMs > 0.0f) {
    // Apply slew
} else {
    // Instant transition
    outL = heldL;
}
```

---

## Recommendations

### 1. Add Counter-Based Mode (HISE Compatibility)

Implement a dual-mode S&H that supports both approaches:

```cpp
// Add to parameter layout
params.push_back(std::make_unique<juce::AudioParameterChoice>(
    "mode", "Mode",
    juce::StringArray{"Gate Trigger", "Counter Periodic"},
    0));
params.push_back(std::make_unique<juce::AudioParameterInt>(
    "counter", "Counter",
    1, 128, 4));

// In processBlock
int mode = modeParam->getIndex();
if (mode == 0) {
    // Current gate-triggered mode
} else {
    // HISE-style counter mode
    static int sampleIndex = 0;
    int counter = counterParam->get();
    for (int i = 0; i < numSamples; ++i) {
        if (sampleIndex % counter == 0) {
            heldL = sigL[i];
            heldR = sigR[i];
        }
        // Apply slew if enabled
        outL = outL + slewCoeff * (heldL - outL);
        outR = outR + slewCoeff * (heldR - outR);
        outLw[i] = outL;
        outRw[i] = outR;
        sampleIndex++;
    }
}
```

### 2. Fix Gate Normalization

Improve the normalization logic to handle edge cases better:

```cpp
auto normalizeGate = [](float gate) -> float {
    // Use a threshold to detect bipolar vs unipolar
    // If signal goes below -0.5, assume bipolar
    static float lastGate = 0.0f;
    bool isBipolar = (gate < -0.5f) || (lastGate < -0.5f);
    lastGate = gate;
    
    if (isBipolar) {
        return juce::jlimit(0.0f, 1.0f, (gate + 1.0f) * 0.5f);
    }
    return juce::jlimit(0.0f, 1.0f, gate);
};
```

### 3. Add Hysteresis to Edge Detection

Prevent multiple triggers on noisy signals:

```cpp
// Add hysteresis parameter
params.push_back(std::make_unique<juce::AudioParameterFloat>(
    "hysteresis", "Hysteresis",
    juce::NormalisableRange<float>(0.0f, 0.1f), 0.01f));

// In processing loop
float hyst = hysteresisParam->load();
float upperThr = thr + hyst;
float lowerThr = thr - hyst;

bool riseL = (gL > upperThr && lastGateL <= lowerThr);
bool fallL = (gL < lowerThr && lastGateL >= upperThr);
```

### 4. Optimize Parameter Modulation

Cache parameter values when modulation is not connected:

```cpp
// Calculate once per block if not modulated
if (!isThresholdMod) {
    const float thr = baseThreshold;
    // Use thr for entire block
} else {
    // Recalculate per sample
    for (int i = 0; i < numSamples; ++i) {
        float thr = calculateModulatedThreshold(i);
        // ...
    }
}
```

### 5. Remove/Guard Debug Logging

```cpp
#ifdef DEBUG_SANDH_VERBOSE
    // Extensive debug logging
    juce::Logger::writeToLog("[S&H] ...");
#endif
```

### 6. Make Slew Truly Optional

Allow instant transitions when slew = 0:

```cpp
if (effectiveSlewMs < 0.001f) {
    // Instant transition, no slew
    outL = heldL;
    outR = heldR;
} else {
    // Apply slew limiting
    const float slewCoeff = calculateSlewCoeff(effectiveSlewMs);
    outL = outL + slewCoeff * (heldL - outL);
    outR = outR + slewCoeff * (heldR - outR);
}
```

---

## Conclusion

### Summary of Differences

| Feature | HISE | Collider PYO |
|---------|------|--------------|
| **Trigger Method** | Counter-based | Gate-triggered |
| **Complexity** | Simple | Complex |
| **Flexibility** | Limited | High |
| **Musical Use** | Lo-fi effects | Modulation/sequencing |
| **Parameters** | 1 | 6 |
| **Slew Limiting** | No | Yes |
| **CV Modulation** | No | Yes |
| **Stereo Support** | Limited | Full |

### When to Use Each Approach

**Use HISE's approach when**:
- Creating lo-fi/bitcrush effects
- Need predictable, regular sampling
- Simple downsampling is sufficient
- No external triggers available

**Use Collider PYO's approach when**:
- Need to respond to external events (MIDI, sequencers)
- Want smooth transitions (slew limiting)
- Need CV modulation of parameters
- Working in a modular/patched environment

### Final Recommendation

**Hybrid Approach**: Implement both modes in a single module:
1. **Mode 1**: Counter-based (HISE-style) for lo-fi effects
2. **Mode 2**: Gate-triggered (current) for musical modulation

This provides maximum flexibility while maintaining compatibility with both use cases.

---

## References

- [HISE Scriptnode Documentation - fx.sampleandhold](https://docs.hise.dev/scriptnode/list/fx/sampleandhold.html)
- [HISE GitHub Repository](https://github.com/christophhart/HISE)
- Collider PYO Source: `juce/Source/audio/modules/SAndHModuleProcessor.cpp`
- Collider PYO Header: `juce/Source/audio/modules/SAndHModuleProcessor.h`

---

**Document Version**: 1.0  
**Last Updated**: 2025-01-27  
**Author**: Technical Analysis for Collider PYO Development Team

