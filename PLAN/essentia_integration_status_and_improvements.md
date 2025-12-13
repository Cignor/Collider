# Essentia Integration Status & Improvement Plan

## Current Status

### ✅ Completed
- **Essentia Wrapper**: Basic initialization/shutdown implemented
- **essentia_onset_detector**: Module skeleton created, registered in factory
- **Build System**: Essentia library detection and linking configured
- **Module Registration**: Onset detector registered in factory and menus

### ⚠️ Issues Found

#### 1. **essentia_onset_detector Not Using Essentia**
**Problem**: The `processBlock()` method never actually calls the Essentia algorithm. It only uses a simple energy-based fallback detection.

**Current Code** (Lines 161-202):
```cpp
// Simple fallback onset detection (energy-based)
// This works even without Essentia library
static float lastEnergy = 0.0f;
// ... energy-based detection only
```

**Root Cause**: 
- Algorithm is created in `initializeEssentiaAlgorithms()` but never used
- No buffering strategy to accumulate audio for analysis
- No call to `onsetDetector->compute()`

**Impact**: Module doesn't actually use Essentia's sophisticated onset detection algorithms.

---

### ❌ Missing Modules (Per Plan)

According to `essentia_stk_integration_plan.md`, 4 Essentia modules are still missing:

1. **essentia_beat_tracker** - Beat detection and tempo tracking
2. **essentia_key_detector** - Musical key detection (C major, A minor, etc.)
3. **essentia_pitch_tracker** - Pitch detection and tracking
4. **essentia_audio_to_cv** - Multi-feature audio-to-CV converter

---

## Improvement Plan

### Phase 1: Fix Existing Onset Detector (Priority: HIGH)

#### 1.1 Implement Proper Essentia Algorithm Usage

**Strategy**: Buffer audio samples and call Essentia's `OnsetRate` algorithm periodically.

**Implementation Pattern**:
```cpp
void EssentiaOnsetDetectorModuleProcessor::processBlock(...)
{
    // 1. Read input audio
    const float* input = inBus.getReadPointer(0);
    
    // 2. Accumulate samples into analysis buffer
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        analysisBuffer[bufferWritePos] = input[i];
        bufferWritePos = (bufferWritePos + 1) % ANALYSIS_BUFFER_SIZE;
        
        // When buffer is full, run Essentia analysis
        if (bufferWritePos == 0 && onsetDetector)
        {
            // Convert to Essentia Real vector
            std::vector<essentia::Real> signal(analysisBuffer.begin(), analysisBuffer.end());
            
            // Set inputs
            onsetDetector->input("signal").set(signal);
            
            // Prepare outputs
            std::vector<essentia::Real> onsets;
            essentia::Real onsetRate;
            onsetDetector->output("onsets").set(onsets);
            onsetDetector->output("onsetRate").set(onsetRate);
            
            // Compute
            onsetDetector->compute();
            
            // Process results - convert onset times to sample positions
            for (const auto& onsetTime : onsets)
            {
                int samplePos = (int)(onsetTime * currentSampleRate);
                pendingOnsets.push_back(samplePos);
            }
        }
    }
    
    // 3. Output gates for detected onsets
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float currentSample = bufferWritePos + i;
        
        // Check if any pending onset matches this sample
        while (!pendingOnsets.empty() && pendingOnsets.front() <= currentSample)
        {
            int onsetSample = pendingOnsets.front();
            pendingOnsets.pop_front();
            
            if (onsetSample >= bufferWritePos && onsetSample < bufferWritePos + buffer.getNumSamples())
            {
                int localIdx = onsetSample - bufferWritePos;
                onsetOut[localIdx] = 1.0f;
                // Calculate velocity/confidence from onset strength
            }
        }
    }
}
```

**Key Considerations**:
- **Buffer Size**: `ANALYSIS_BUFFER_SIZE = 2048` (~46ms at 44.1kHz) is reasonable
- **Latency**: Analysis introduces ~46ms latency (acceptable for onset detection)
- **Thread Safety**: Essentia standard algorithms are not thread-safe - must run on audio thread
- **Sample Rate**: OnsetRate requires 44100Hz (hardcoded in algorithm)

#### 1.2 Handle Sample Rate Mismatch

**Problem**: OnsetRate is hardcoded to 44100Hz, but user's project may be different.

**Solution Options**:
1. **Resample input** to 44100Hz before analysis
2. **Use different algorithm** that supports variable sample rates
3. **Document limitation** and warn user if sample rate != 44100Hz

**Recommended**: Option 1 - Resample using JUCE's `ResamplingAudioSource` or similar.

#### 1.3 Add Fallback for Non-44100Hz

If resampling is too complex, keep fallback for non-44100Hz:
```cpp
if (currentSampleRate != 44100.0 && onsetDetector)
{
    // Essentia OnsetRate requires 44100Hz
    // Use fallback detection
    useFallbackDetection = true;
}
```

---

### Phase 2: Implement Missing Modules (Priority: MEDIUM)

#### 2.1 essentia_beat_tracker

**Complexity**: HIGH (requires background thread, FIFO management)

**Implementation Strategy** (from plan):
- Use Essentia streaming algorithms
- Background analysis thread
- FIFO for audio data and beat triggers
- Outputs: Beat gate, Tempo CV, Confidence CV, Phase CV

**Key Algorithms**:
- `BeatTrackerMultiFeature` or `BeatTrackerDegara`
- Requires longer analysis windows (2-4 seconds)

**Estimated Time**: 3-5 days

---

#### 2.2 essentia_key_detector

**Complexity**: MEDIUM (can run less frequently)

**Implementation Strategy**:
- Use `KeyExtractor` algorithm
- Analyze every 1-2 seconds
- Outputs: Key CV (0-11 for C-B), Mode CV (0=major, 1=minor), Confidence CV

**Key Algorithms**:
- `KeyExtractor` (requires chroma/PCP features)

**Estimated Time**: 2-3 days

---

#### 2.3 essentia_pitch_tracker

**Complexity**: LOW-MEDIUM (real-time capable)

**Implementation Strategy**:
- Use `PitchYin` or `PitchYinFFT` algorithm
- Low latency (can run every frame)
- Outputs: Pitch CV (normalized frequency), Confidence CV

**Key Algorithms**:
- `PitchYin` (efficient, low latency)
- `PitchYinFFT` (more accurate, slightly higher latency)

**Estimated Time**: 2-3 days

---

#### 2.4 essentia_audio_to_cv

**Complexity**: MEDIUM (multiple features)

**Implementation Strategy**:
- Extract multiple features: RMS, Peak, Spectral Centroid, Zero Crossing Rate, etc.
- Each feature outputs to separate CV channel
- Can run in real-time

**Key Algorithms**:
- `RMS`, `PeakDetection`, `SpectralCentroid`, `ZeroCrossingRate`, etc.

**Estimated Time**: 3-4 days

---

## Implementation Priority

### Immediate (Fix Current Issues)
1. ✅ Fix `essentia_onset_detector` to use Essentia algorithms
2. ✅ Add proper buffering and analysis calls
3. ✅ Handle sample rate requirements

### Short Term (Complete MVP)
1. ⏳ Implement `essentia_pitch_tracker` (easiest, most useful)
2. ⏳ Implement `essentia_audio_to_cv` (useful, moderate complexity)

### Medium Term (Advanced Features)
1. ⏳ Implement `essentia_key_detector`
2. ⏳ Implement `essentia_beat_tracker` (most complex)

---

## Technical Considerations

### Essentia Algorithm Types

**Standard Algorithms** (synchronous, not thread-safe):
- Run on audio thread
- Blocking compute() calls
- Use for: OnsetRate, KeyExtractor, PitchYin

**Streaming Algorithms** (asynchronous, thread-safe):
- Can run on background thread
- Non-blocking
- Use for: BeatTracker (complex analysis)

### Buffer Management

**For Real-Time Analysis**:
- Small buffers (1024-2048 samples)
- Frequent analysis (every buffer)
- Low latency, higher CPU

**For Complex Analysis**:
- Large buffers (2-4 seconds)
- Less frequent analysis
- Higher latency, lower CPU

### Thread Safety

**Audio Thread Only**:
- Standard algorithms
- Simple feature extraction
- Low-latency requirements

**Background Thread**:
- Complex analysis (beat tracking)
- Long analysis windows
- Can tolerate latency

---

## Testing Requirements

### Unit Tests
- [ ] Test onset detection with various audio signals
- [ ] Test sample rate handling
- [ ] Test parameter ranges
- [ ] Test CV modulation

### Integration Tests
- [ ] Test with real audio input
- [ ] Test CPU usage
- [ ] Test latency measurements
- [ ] Test thread safety

### User Testing
- [ ] Test with various music styles
- [ ] Test accuracy vs. fallback
- [ ] Test parameter sensitivity
- [ ] Gather user feedback

---

## Success Criteria

### Must Have (MVP)
- ✅ Onset detector actually uses Essentia
- ✅ Works at 44100Hz sample rate
- ✅ Produces accurate onset detection
- ✅ No crashes or audio glitches

### Should Have
- ✅ Support for other sample rates (resampling)
- ✅ All 5 Essentia modules implemented
- ✅ Good performance (low CPU usage)
- ✅ Proper error handling

### Nice to Have
- ✅ Advanced algorithm options
- ✅ Visualization of detection
- ✅ Preset configurations
- ✅ Example patches

---

## Next Steps

1. **Immediate**: Fix `essentia_onset_detector` to use Essentia algorithms
2. **Short Term**: Implement `essentia_pitch_tracker` (easiest next module)
3. **Medium Term**: Implement remaining 3 modules
4. **Long Term**: Optimize and polish all modules

---

## References

- **Plan**: `PLAN/essentia_stk_integration_plan.md`
- **Example**: `vendor/essentia-2.1_beta5/src/examples/onset_detector.cpp`
- **Current Implementation**: `juce/Source/audio/modules/essentia/EssentiaOnsetDetectorModuleProcessor.cpp`

