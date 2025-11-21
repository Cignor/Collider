# Additional Modules for `getRhythmInfo()` Implementation

This document lists additional modules that could benefit from implementing `getRhythmInfo()` for BPM Monitor introspection, beyond the high and medium priority modules already implemented.

---

## 🟡 Medium-High Priority - Rate-Based Modules

These modules have explicit rate parameters that produce periodic/rhythmic behavior:

### 1. RandomModuleProcessor
- **File:** `juce/Source/audio/modules/RandomModuleProcessor.h/cpp`
- **Why:** Generates random values at a configurable rate (Hz), which can be converted to BPM
- **Complexity:** Low
- **Implementation Notes:**
  - Display name: `"Random #" + juce::String(getLogicalId())`
  - Source type: `"random"`
  - BPM: `rate (Hz) * 60.0f` (one random value per cycle = one "beat")
  - Active: Always active when running
  - Synced: Not synced (free-running)
  - **Note:** Random values aren't truly "rhythmic" but the rate at which they're generated could be useful for tempo matching

### 2. PhaserModuleProcessor
- **File:** `juce/Source/audio/modules/PhaserModuleProcessor.h/cpp`
- **Why:** Has an LFO rate parameter that modulates the phaser effect - this rate could be reported as BPM
- **Complexity:** Low
- **Implementation Notes:**
  - Display name: `"Phaser #" + juce::String(getLogicalId())`
  - Source type: `"phaser"`
  - BPM: `rate (Hz) * 60.0f` (LFO rate converted to BPM)
  - Active: Always active when processing
  - Synced: Not synced (free-running LFO)
  - **Note:** The phaser's LFO rate is typically in the audio range (not BPM range), but could still be useful for tempo matching

### 3. ChorusModuleProcessor
- **File:** `juce/Source/audio/modules/ChorusModuleProcessor.h/cpp`
- **Why:** Has an LFO rate parameter that modulates the chorus effect - similar to Phaser
- **Complexity:** Low
- **Implementation Notes:**
  - Display name: `"Chorus #" + juce::String(getLogicalId())`
  - Source type: `"chorus"`
  - BPM: `rate (Hz) * 60.0f` (LFO rate converted to BPM)
  - Active: Always active when processing
  - Synced: Not synced (free-running LFO)
  - **Note:** Same considerations as Phaser - LFO rate is typically in audio range

---

## 🟢 Medium Priority - Playback Rate Modules

These modules have playback speed/rate parameters that could be reported as effective BPM:

### 4. SampleLoaderModuleProcessor
- **File:** `juce/Source/audio/modules/SampleLoaderModuleProcessor.h/cpp`
- **Why:** Has sync to transport and playback speed/rate - could report effective BPM when looping
- **Complexity:** Medium
- **Implementation Notes:**
  - Display name: `"Sample Loader #" + juce::String(getLogicalId())`
  - Source type: `"sample_loader"`
  - BPM calculation:
    - If synced: `transport.bpm * speed_multiplier` (if looping)
    - If free-running: Calculate from sample length and playback speed
  - Active: When playing and sample is loaded
  - Synced: Check `syncParam` state
  - **Note:** Only makes sense if the sample is looping rhythmically

### 5. TTSPerformerModuleProcessor
- **File:** `juce/Source/audio/modules/TTSPerformerModuleProcessor.h/cpp`
- **Why:** Has playback speed/rate - could report effective BPM for speech rhythm
- **Complexity:** Medium
- **Implementation Notes:**
  - Display name: `"TTS Performer #" + juce::String(getLogicalId())`
  - Source type: `"tts_performer"`
  - BPM: Calculate from clip duration and playback speed (if looping)
  - Active: When playing back a clip
  - Synced: Check if synced to transport
  - **Note:** Speech doesn't have a traditional "BPM" but playback rate could be useful

### 6. TimePitchModuleProcessor
- **File:** `juce/Source/audio/modules/TimePitchModuleProcessor.h/cpp`
- **Why:** Has speed parameter that affects playback rate - could report effective BPM
- **Complexity:** Medium
- **Implementation Notes:**
  - Display name: `"Time Pitch #" + juce::String(getLogicalId())`
  - Source type: `"time_pitch"`
  - BPM: `base_bpm * speed_multiplier` (if input has a known BPM)
  - Active: Always active when processing
  - Synced: Not directly synced (but speed could be modulated)
  - **Note:** Only meaningful if the input signal has a known BPM

---

## 🔵 Low Priority - Conditional/Edge Cases

These modules might produce rhythm in specific configurations but are less clear-cut:

### 7. ADSRModuleProcessor
- **File:** `juce/Source/audio/modules/ADSRModuleProcessor.h/cpp`
- **Why:** Envelope generators have attack/release times, but these aren't really "BPM"
- **Complexity:** Low (but questionable value)
- **Implementation Notes:**
  - Could calculate "BPM" from envelope cycle time (attack + decay + sustain + release)
  - **Note:** Envelopes are typically triggered externally, not self-oscillating, so BPM reporting may not be meaningful

### 8. DelayModuleProcessor
- **File:** `juce/Source/audio/modules/DelayModuleProcessor.h/cpp`
- **Why:** Delay time could be rhythmic if set to musical intervals
- **Complexity:** Medium
- **Implementation Notes:**
  - Could calculate BPM from delay time: `60.0f / delay_time_seconds`
  - **Note:** Only meaningful if delay time is set to a musical interval (e.g., 1/4 note, 1/8 note)

### 9. GranulatorModuleProcessor
- **File:** `juce/Source/audio/modules/GranulatorModuleProcessor.h/cpp`
- **Why:** Grain rate could be reported as BPM
- **Complexity:** Medium
- **Implementation Notes:**
  - BPM: `grain_rate (Hz) * 60.0f`
  - **Note:** Grain rate is typically very high (audio range), not BPM range

### 10. SequentialSwitchModuleProcessor
- **File:** `juce/Source/audio/modules/SequentialSwitchModuleProcessor.h/cpp`
- **Why:** Switches between inputs at a rate - could be rhythmic
- **Complexity:** Medium
- **Implementation Notes:**
  - If it has a rate parameter: `rate (Hz) * 60.0f`
  - If clock-driven: Similar to SnapshotSequencer (BPM = 0.0f, unknown)
  - **Note:** Need to check if it has an internal rate or is clock-driven

---

## 📊 Priority Summary

| Module | Priority | Complexity | Use Case |
|--------|----------|------------|----------|
| RandomModuleProcessor | 🟡 Medium-High | Low | Rate-based random generation |
| PhaserModuleProcessor | 🟡 Medium-High | Low | LFO rate for modulation |
| ChorusModuleProcessor | 🟡 Medium-High | Low | LFO rate for modulation |
| SampleLoaderModuleProcessor | 🟢 Medium | Medium | Playback rate when looping |
| TTSPerformerModuleProcessor | 🟢 Medium | Medium | Playback rate for speech |
| TimePitchModuleProcessor | 🟢 Medium | Medium | Speed multiplier for time-stretching |
| ADSRModuleProcessor | 🔵 Low | Low | Envelope cycle time (questionable) |
| DelayModuleProcessor | 🔵 Low | Medium | Delay time as musical interval |
| GranulatorModuleProcessor | 🔵 Low | Medium | Grain rate (typically audio range) |
| SequentialSwitchModuleProcessor | 🔵 Low | Medium | Switching rate (if applicable) |

---

## Recommendations

### Most Valuable Additions:

1. **RandomModuleProcessor** - Clear rate parameter, straightforward conversion
2. **PhaserModuleProcessor** - LFO rate is useful for tempo matching modulation
3. **ChorusModuleProcessor** - Same as Phaser
4. **SampleLoaderModuleProcessor** - If looping, playback rate is meaningful

### Questionable Value:

- **ADSRModuleProcessor** - Envelopes are triggered, not self-oscillating
- **DelayModuleProcessor** - Only meaningful if delay time is set to musical intervals
- **GranulatorModuleProcessor** - Grain rates are typically in audio range, not BPM range
- **TimePitchModuleProcessor** - Only meaningful if input has known BPM

### Implementation Order:

1. Start with **RandomModuleProcessor** (simplest, clear use case)
2. Then **PhaserModuleProcessor** and **ChorusModuleProcessor** (similar pattern)
3. Then **SampleLoaderModuleProcessor** (more complex but valuable)
4. Evaluate others based on user feedback

---

## Notes

- **LFO rates** (Phaser, Chorus) are typically in the audio frequency range (0.1-20 Hz), which converts to 6-1200 BPM. This is useful for tempo matching but may seem high.
- **Random rate** is typically lower (0.01-10 Hz), converting to 0.6-600 BPM, which is more in the traditional BPM range.
- **Playback rates** (SampleLoader, TTS) are multipliers (0.25x-4x), so BPM depends on the source material's inherent tempo.
- Consider adding a **"BPM range filter"** to the BPM Monitor UI to hide sources outside typical BPM ranges (e.g., 20-300 BPM) if desired.

