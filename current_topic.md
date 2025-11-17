# Harmonic Shaper CV Modulation Routing Issue - JUCE Buffer Aliasing

## 🐛 Problem Summary

The `HarmonicShaperModuleProcessor` has 6 CV modulation inputs (Freq, Drive, Gain, Mix, Character, Smoothness) that should be routed to channels 2-7 on a unified input bus (bus 0). However, **all channels 2-7 are reading the same value** because JUCE is aliasing them to the same memory address.

## 🔍 Key Evidence from Debug Logs

```
[HarmonicShaper][ROUTING REQ] paramId=masterFrequency_mod
[HarmonicShaper][ROUTING REQ] paramId=masterDrive_mod
[HarmonicShaper][ROUTING REQ] paramId=outputGain_mod
[HarmonicShaper][ROUTING REQ] paramId=mix_mod
[HarmonicShaper][ROUTING REQ] paramId=character_mod
[HarmonicShaper][ROUTING REQ] paramId=smoothness_mod

[HarmonicShaper][POINTERS] freqPtr=00000288F47ACD90 drivePtr=00000288F47ACD90 gainPtr=00000288F47ACD90 mixPtr=00000288F47ACD90 charPtr=00000288F47ACD90 smoothPtr=00000288F47ACD90

[HarmonicShaper][RAW CH] ch2:ptr=00000288F47ACD90:val=0.645 ch3:ptr=00000288F47ACD90:val=0.645 ch4:ptr=00000288F47ACD90:val=0.645 ch5:ptr=00000288F47ACD90:val=0.645 ch6:ptr=00000288F47ACD90:val=0.645 ch7:ptr=00000288F47ACD90:val=0.645

[HarmonicShaper][COPIED VALUES] freq=0.645 drive=0.645 gain=0.645 mix=0.645 char=0.645 smooth=0.645
```

**Critical Observation:**
- All 6 CV channels (2-7) point to the **same memory address** (00000288F47ACD90)
- All channels have the **same value** (0.645)
- The routing system **IS** calling `getParamRouting()` for all parameter IDs correctly
- But JUCE is **aliasing** all channels to the same memory

## ✅ What Works (Granulator Reference)

The `GranulatorModuleProcessor` uses the **exact same pattern** and works perfectly:
- Same unified input bus (bus 0, 8 channels)
- Same routing pattern (channels 2-7 for CV inputs)
- Same `getParamRouting()` implementation
- **Different channels have different memory addresses and values**

## 🏗️ Architecture Context

### Bus Configuration
```cpp
// HarmonicShaperModuleProcessor constructor:
BusesProperties()
    .withInput("Inputs", juce::AudioChannelSet::discreteChannels(8), true)
    // Channels: 0-1 = Audio L/R, 2-7 = CV modulation inputs
    .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(2), true)
```

### Parameter Routing
```cpp
bool HarmonicShaperModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdMasterFreqMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdMasterDriveMod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdOutputGainMod) { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdMixMod) { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdCharacterMod) { outChannelIndexInBus = 6; return true; }
    if (paramId == paramIdSmoothnessMod) { outChannelIndexInBus = 7; return true; }
    return false;
}
```

### Current CV Reading Pattern (Following DEBUG_INPUT_IMPORTANT.md)
```cpp
// 1. Get connection status
const bool isFreqMod = isParamInputConnected(paramIdMasterFreqMod);
const bool isDriveMod = isParamInputConnected(paramIdMasterDriveMod);
// ... etc

// 2. Get CV pointers BEFORE any output operations
const float* freqCVPtr = (isFreqMod && inBus.getNumChannels() > 2) ? inBus.getReadPointer(2) : nullptr;
const float* driveCVPtr = (isDriveMod && inBus.getNumChannels() > 3) ? inBus.getReadPointer(3) : nullptr;
// ... etc

// 3. Copy CV data to HeapBlock buffers (aliasing-safe)
juce::HeapBlock<float> freqCV, driveCV, gainCV, mixCV, characterCV, smoothnessCV;
if (freqCVPtr) { freqCV.malloc(numSamples); std::memcpy(freqCV.get(), freqCVPtr, sizeof(float) * (size_t)numSamples); }
// ... etc

// 4. Get output pointers AFTER CV copying
auto* outL = outBus.getWritePointer(0);
auto* outR = outBus.getWritePointer(1);
```

## 🤔 The Mystery

**Why does Granulator work but HarmonicShaper doesn't?**

Both modules:
- Use the same bus configuration pattern
- Use the same `getParamRouting()` pattern
- Use the same CV reading pattern (following DEBUG_INPUT_IMPORTANT.md)
- Are called by the same routing system

**Key Difference:**
- Granulator: Different channels have different memory addresses ✅
- HarmonicShaper: All channels share the same memory address ❌

## 💡 Hypotheses

1. **Routing System Not Writing to Channels 4-7**
   - The routing system recognizes the parameter IDs (we see routing requests)
   - But maybe it's not actually writing CV signals to channels 4-7?
   - JUCE aliases unused channels to save memory

2. **Connection Detection Issue**
   - Maybe `isParamInputConnected()` returns true for all, but connections aren't actually established?
   - The routing system might only write to channels that have "active" connections

3. **Timing Issue**
   - Maybe the routing system writes to channels AFTER we read them?
   - But we're reading in `processBlock()`, which should be after routing...

4. **JUCE Buffer Allocation**
   - Maybe JUCE only allocates separate memory for channels that are written to
   - If channels 4-7 are never written to, they alias to channel 2

## 🎯 What We Need Help With

1. **Why is JUCE aliasing channels 4-7 to channel 2?**
   - Is this expected behavior when channels aren't written to?
   - How does the routing system write CV signals to input bus channels?

2. **How does Granulator avoid this aliasing?**
   - What's different about how Granulator's channels are used?
   - Is there something in the routing system that treats Granulator differently?

3. **What's the correct way to force channel separation?**
   - Can we force JUCE to allocate separate memory for each channel?
   - Should we modify the routing system to write zeros to unused channels?
   - Is there a way to detect aliasing and work around it?

4. **Is there a bug in the routing system?**
   - Should the routing system be writing to channels 4-7 but isn't?
   - How can we verify that the routing system is actually writing to the correct channels?

## 📋 Files to Review

- `HarmonicShaperModuleProcessor.cpp` - Current broken implementation
- `GranulatorModuleProcessor.cpp` - Working reference implementation
- `DEBUG_INPUT_IMPORTANT.md` - Buffer aliasing guide (we're following this)
- `BestPracticeNodeProcessor.md` - Modulation input pattern guide
- `ImGuiNodeEditorComponent.cpp` - Routing system implementation (lines ~2770-2785)

## 🔧 What We've Tried

1. ✅ Following DEBUG_INPUT_IMPORTANT.md pattern (read CVs before output operations)
2. ✅ Copying CV data to HeapBlock buffers (aliasing-safe)
3. ✅ Verifying routing requests are being made (they are)
4. ✅ Comparing with working Granulator implementation (identical pattern)
5. ❌ Attempted to write zeros to input channels (can't write to input buses)
6. ❌ Checked if parameter IDs match (they do)

## 🚨 Critical Question

**How does the routing system write CV signals to input bus channels, and why might it not be writing to channels 4-7 in HarmonicShaper when it works for Granulator?**

