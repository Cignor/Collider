# Modules Requiring `getRhythmInfo()` Implementation

This document lists all modules that should implement `getRhythmInfo()` to enable BPM Monitor introspection, organized by priority and implementation complexity.

---

## ✅ Currently Implemented

These modules already implement `getRhythmInfo()` correctly:

1. **StepSequencerModuleProcessor** ✓
   - **Location:** `juce/Source/audio/modules/StepSequencerModuleProcessor.cpp:942`
   - **Status:** Working correctly
   - **Notes:** Handles sync and free-running modes, reads live transport state

2. **MultiSequencerModuleProcessor** ✓
   - **Location:** `juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp:742`
   - **Status:** Working correctly
   - **Notes:** Same pattern as StepSequencer, handles multiple tracks

3. **AnimationModuleProcessor** ✓
   - **Location:** `juce/Source/audio/modules/AnimationModuleProcessor.cpp:1239`
   - **Status:** Working correctly
   - **Notes:** Free-running, calculates BPM from animation clip duration

---

## 🔴 High Priority - Critical Rhythm Sources

These modules are **essential rhythm sources** and should be implemented first:

### 1. TempoClockModuleProcessor
- **File:** `juce/Source/audio/modules/TempoClockModuleProcessor.h/cpp`
- **Why:** This is **THE master tempo source** for the entire system. It should absolutely report its BPM.
- **Complexity:** Low - it already has BPM parameter and transport state
- **Implementation Notes:**
  - Display name: `"Tempo Clock #" + juce::String(getLogicalId())`
  - Source type: `"tempo_clock"`
  - BPM: Read from `bpmParam` (already exists)
  - Active: Check if transport is playing (from `m_currentTransport` or live state)
  - Synced: Check `syncToHostParam` or `syncToTimelineParam`
  - **Special consideration:** This is the source of truth for tempo, so it should always report accurately

### 2. StrokeSequencerModuleProcessor
- **File:** `juce/Source/audio/modules/StrokeSequencerModuleProcessor.h/cpp`
- **Why:** It's a sequencer that produces rhythmic patterns from stroke data
- **Complexity:** Medium - needs to calculate BPM from stroke playback rate
- **Implementation Notes:**
  - Display name: `"Stroke Seq #" + juce::String(getLogicalId()))`
  - Source type: `"stroke_sequencer"`
  - BPM: Calculate from stroke playback rate and pattern length
  - Active: Check if strokes are being played
  - Synced: Check if synced to transport (if it has sync parameter)

### 3. SnapshotSequencerModuleProcessor
- **File:** `juce/Source/audio/modules/SnapshotSequencerModuleProcessor.h/cpp`
- **Why:** It's a sequencer that cycles through snapshots at a rate
- **Complexity:** Medium - needs to calculate BPM from step rate
- **Implementation Notes:**
  - Display name: `"Snapshot Seq #" + juce::String(getLogicalId())`
  - Source type: `"snapshot_sequencer"`
  - BPM: Calculate from step rate (similar to StepSequencer pattern)
  - Active: Check if sequencer is running
  - Synced: Check if synced to transport (if it has sync parameter)

### 4. TimelineModuleProcessor
- **File:** `juce/Source/audio/modules/TimelineModuleProcessor.h/cpp`
- **Why:** Time-based automation that plays back at a rate
- **Complexity:** Medium - needs to calculate effective BPM from timeline length and playback rate
- **Implementation Notes:**
  - Display name: `"Timeline #" + juce::String(getLogicalId())`
  - Source type: `"timeline"`
  - BPM: Calculate from timeline length and playback rate (if it loops)
  - Active: Check `playParam` state
  - Synced: Likely synced to transport (check implementation)

### 5. ClockDividerModuleProcessor
- **File:** `juce/Source/audio/modules/ClockDividerModuleProcessor.h/cpp`
- **Why:** Divides incoming clock, producing rhythmic outputs at different rates
- **Complexity:** Medium - needs to calculate effective BPM from input clock rate and division ratios
- **Implementation Notes:**
  - Display name: `"Clock Divider #" + juce::String(getLogicalId())`
  - Source type: `"clock_divider"`
  - BPM: Calculate from input clock BPM divided by division ratios (e.g., input 120 BPM /2 = 60 BPM)
  - Active: Check if input clock is active
  - Synced: Inherits sync status from input clock
  - **Special consideration:** This module depends on input clock, so it might need to query the input source's BPM or analyze the input signal

---

## 🟡 Medium Priority - Oscillators and Generators

These modules produce periodic signals that could be reported as BPM:

### 6. LFOModuleProcessor
- **File:** `juce/Source/audio/modules/LFOModuleProcessor.h/cpp`
- **Why:** Low-frequency oscillator produces periodic waveforms at a rate
- **Complexity:** Low - already has rate parameter and sync support
- **Implementation Notes:**
  - Display name: `"LFO #" + juce::String(getLogicalId())`
  - Source type: `"lfo"`
  - BPM: Convert rate (Hz) to BPM: `rate * 60.0f` (if rate is in Hz)
  - Active: Always active when running
  - Synced: Check `syncParam` state
  - **Special consideration:** Only report if rate is in the BPM range (e.g., 0.5-10 Hz = 30-600 BPM). If rate is too high, it's not really "BPM" anymore.

### 7. FunctionGeneratorModuleProcessor
- **File:** `juce/Source/audio/modules/FunctionGeneratorModuleProcessor.h/cpp`
- **Why:** Generates functions at a rate, similar to LFO
- **Complexity:** Low - already has rate parameter
- **Implementation Notes:**
  - Display name: `"Function Gen #" + juce::String(getLogicalId())`
  - Source type: `"function_generator"`
  - BPM: Convert rate (Hz) to BPM: `rate * 60.0f`
  - Active: Always active when running
  - Synced: Check if it has sync parameter

---

## 🟢 Low Priority - Conditional Rhythm Sources

These modules might produce rhythm in specific configurations:

### 8. PhysicsModuleProcessor
- **File:** `juce/Source/audio/modules/PhysicsModuleProcessor.h/cpp`
- **Why:** Physics simulations can produce rhythmic motion patterns
- **Complexity:** High - needs to analyze motion patterns to detect rhythm
- **Implementation Notes:**
  - Display name: `"Physics #" + juce::String(getLogicalId())`
  - Source type: `"physics"`
  - BPM: Calculate from periodic motion patterns (if detectable)
  - Active: Check if simulation is running and producing motion
  - Synced: Likely free-running
  - **Special consideration:** Only implement if physics simulation produces clearly rhythmic patterns. This might require analyzing motion frequency or collision patterns.

---

## Implementation Priority Summary

| Priority | Module | Complexity | Estimated Effort |
|----------|--------|------------|-----------------|
| 🔴 Critical | TempoClockModuleProcessor | Low | 30 min |
| 🔴 Critical | StrokeSequencerModuleProcessor | Medium | 1-2 hours |
| 🔴 Critical | SnapshotSequencerModuleProcessor | Medium | 1-2 hours |
| 🔴 Critical | TimelineModuleProcessor | Medium | 1-2 hours |
| 🔴 Critical | ClockDividerModuleProcessor | Medium | 2-3 hours |
| 🟡 Medium | LFOModuleProcessor | Low | 30 min |
| 🟡 Medium | FunctionGeneratorModuleProcessor | Low | 30 min |
| 🟢 Low | PhysicsModuleProcessor | High | 4+ hours |

---

## Implementation Checklist

For each module, follow this checklist:

1. **Add method declaration** in header:
   ```cpp
   std::optional<RhythmInfo> getRhythmInfo() const override;
   ```

2. **Implement method** following the pattern from `INTROSPECTED_SOURCES_IMPLEMENTATION_GUIDE.md`:
   - [ ] Read live transport state (not cached copy)
   - [ ] Set display name with logical ID
   - [ ] Set source type
   - [ ] Calculate BPM correctly (validate with `std::isfinite()`)
   - [ ] Determine active state
   - [ ] Determine sync status
   - [ ] Return `std::nullopt` if module is not rhythm-capable

3. **Test implementation**:
   - [ ] Module appears in BPM Monitor's "Introspected Sources" list
   - [ ] Display name shows correctly (no "?????????")
   - [ ] BPM value is accurate
   - [ ] Active state reflects actual state
   - [ ] Sync status is correct
   - [ ] Dynamic output pins are generated correctly

4. **Verify edge cases**:
   - [ ] Handles missing parent gracefully
   - [ ] Handles transport stopped state
   - [ ] Returns safe defaults for invalid parameters
   - [ ] No crashes or NaN values

---

## Quick Reference: Implementation Template

```cpp
std::optional<RhythmInfo> YourModuleProcessor::getRhythmInfo() const
{
    // Check if rhythm-capable
    if (!isRhythmProducing())
        return std::nullopt;
    
    RhythmInfo info;
    info.displayName = "YourModule #" + juce::String(getLogicalId());
    info.sourceType = "your_type";
    
    // Read live transport state
    TransportState transport;
    bool hasTransport = false;
    if (getParent())
    {
        transport = getParent()->getTransportState();
        hasTransport = true;
    }
    
    // Determine sync and active state
    const bool syncEnabled = /* check sync parameter */;
    info.isSynced = syncEnabled;
    info.isActive = /* determine active state */;
    
    // Calculate BPM
    if (syncEnabled && info.isActive && hasTransport)
    {
        // Synced mode calculation
        info.bpm = /* calculate from transport + parameters */;
    }
    else if (!syncEnabled)
    {
        // Free-running mode calculation
        info.bpm = /* calculate from internal rate */;
    }
    else
    {
        info.bpm = 0.0f;
    }
    
    // Validate
    if (!std::isfinite(info.bpm))
        info.bpm = 0.0f;
    
    return info;
}
```

---

## Notes

- **TempoClockModuleProcessor** should be implemented first as it's the master tempo source
- **Sequencer modules** (Stroke, Snapshot) should follow the StepSequencer pattern
- **Oscillator modules** (LFO, FunctionGenerator) are straightforward - just convert Hz to BPM
- **ClockDividerModuleProcessor** is more complex as it depends on input clock rate
- **PhysicsModuleProcessor** should only be implemented if it clearly produces rhythmic patterns

Refer to `INTROSPECTED_SOURCES_IMPLEMENTATION_GUIDE.md` for detailed implementation patterns and common pitfalls.

