# Tap Tempo Diagnosis and Fix

## 🐛 Problem Analysis

### Current Implementation (BROKEN)

Line 87 in `TempoClockModuleProcessor.cpp`:
```cpp
if (edge(tapCV, lastTapHigh))   { samplesSinceLastTap = 0.0; }
```

### Issues Found

1. **`samplesSinceLastTap` is never incremented** - The variable is reset to 0.0 on each tap, but never incremented during processing
2. **No BPM calculation** - There's no code that uses `samplesSinceLastTap` to calculate BPM
3. **Non-functional feature** - Tap input does absolutely nothing except reset an unused variable

### What Tap Tempo Should Do

1. **Track elapsed time** between taps
2. **Calculate BPM** from the interval: `BPM = 60.0 / secondsBetweenTaps`
3. **Update the BPM parameter** when a valid tap is detected
4. **Optionally average** multiple taps for stability

---

## ✅ Proposed Fix

### Option 1: Simple Two-Tap System (Recommended)

Calculate BPM from the interval between two consecutive taps:

**Header additions** (`TempoClockModuleProcessor.h`):
```cpp
private:
    // ... existing members ...
    double samplesSinceLastTap { 0.0 };
    bool hasPreviousTap { false };  // NEW: Track if we have a valid previous tap
```

**Implementation** (`TempoClockModuleProcessor.cpp`):
```cpp
void TempoClockModuleProcessor::processBlock(...)
{
    // ... existing code ...
    
    // BEFORE the main processing loop, increment tap counter
    if (tapMod && tapCV)  // Only increment if tap input is connected
    {
        samplesSinceLastTap += numSamples;
    }
    
    // Handle edge controls
    auto edge = [&](const float* cv, bool& last){ 
        bool now = (cv && cv[0] > 0.5f); 
        bool rising = now && !last; 
        last = now; 
        return rising; 
    };
    
    if (edge(playCV, lastPlayHigh))   if (auto* p = getParent()) p->setPlaying(true);
    if (edge(stopCV, lastStopHigh))   if (auto* p = getParent()) p->setPlaying(false);
    if (edge(resetCV, lastResetHigh)) if (auto* p = getParent()) p->resetTransportPosition();
    
    // TAP TEMPO FIX
    if (edge(tapCV, lastTapHigh))
    {
        if (hasPreviousTap && samplesSinceLastTap > 0.0)
        {
            // Calculate BPM from time between taps
            const double secondsBetweenTaps = samplesSinceLastTap / sampleRateHz;
            
            // Sanity check: prevent extreme values (20-300 BPM range)
            // Min interval: 0.2 seconds (300 BPM)
            // Max interval: 3.0 seconds (20 BPM)
            if (secondsBetweenTaps >= 0.2 && secondsBetweenTaps <= 3.0)
            {
                float newBPM = 60.0f / (float)secondsBetweenTaps;
                
                // Clamp to valid range
                bpm = juce::jlimit(20.0f, 300.0f, newBPM);
                
                // Update the parameter so it persists
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(
                    apvts.getParameter(paramIdBpm)))
                {
                    *p = bpm;
                }
                
                juce::Logger::writeToLog("[TempoClock] Tap tempo: " + 
                    juce::String(secondsBetweenTaps, 3) + "s interval = " + 
                    juce::String(bpm, 1) + " BPM");
            }
        }
        
        // Reset counter and mark that we have a valid tap
        samplesSinceLastTap = 0.0;
        hasPreviousTap = true;
    }
    
    // TAP TIMEOUT: Reset if no tap for 4 seconds
    if (hasPreviousTap && samplesSinceLastTap > sampleRateHz * 4.0)
    {
        hasPreviousTap = false;
        samplesSinceLastTap = 0.0;
    }
    
    // ... rest of existing code (nudge, sync, etc.) ...
}
```

---

### Option 2: Four-Tap Averaging System (More Sophisticated)

Track the last 4 taps and average them for smoother results:

**Header additions**:
```cpp
private:
    // ... existing members ...
    double samplesSinceLastTap { 0.0 };
    std::array<double, 4> tapIntervals { 0.0, 0.0, 0.0, 0.0 };  // Last 4 intervals
    int tapCount { 0 };  // How many valid taps we've received
```

**Implementation**:
```cpp
// TAP TEMPO FIX (4-tap averaging)
if (edge(tapCV, lastTapHigh))
{
    if (tapCount > 0 && samplesSinceLastTap > 0.0)
    {
        const double secondsBetweenTaps = samplesSinceLastTap / sampleRateHz;
        
        // Sanity check
        if (secondsBetweenTaps >= 0.2 && secondsBetweenTaps <= 3.0)
        {
            // Store interval in circular buffer
            tapIntervals[tapCount % 4] = secondsBetweenTaps;
            tapCount++;
            
            // Calculate average of available taps (up to 4)
            const int numToAverage = juce::jmin(tapCount, 4);
            double sum = 0.0;
            for (int i = 0; i < numToAverage; ++i)
                sum += tapIntervals[i];
            
            const double avgInterval = sum / numToAverage;
            float newBPM = 60.0f / (float)avgInterval;
            bpm = juce::jlimit(20.0f, 300.0f, newBPM);
            
            // Update parameter
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(
                apvts.getParameter(paramIdBpm)))
            {
                *p = bpm;
            }
            
            juce::Logger::writeToLog("[TempoClock] Tap tempo (" + 
                juce::String(numToAverage) + " taps averaged): " + 
                juce::String(bpm, 1) + " BPM");
        }
    }
    else
    {
        tapCount = 1;  // First tap
    }
    
    samplesSinceLastTap = 0.0;
}

// TAP TIMEOUT: Reset if no tap for 4 seconds
if (tapCount > 0 && samplesSinceLastTap > sampleRateHz * 4.0)
{
    tapCount = 0;
    samplesSinceLastTap = 0.0;
    juce::Logger::writeToLog("[TempoClock] Tap tempo reset (timeout)");
}
```

---

## 🎯 Recommended Solution: Option 1 (Simple Two-Tap)

**Why:**
- Simple and predictable
- Immediate response (no need to tap 4 times)
- Easy to understand for users
- Less state to manage
- Sufficient for most use cases

**Option 2** is better for:
- Live performance where stability is critical
- Users who prefer smoother BPM transitions
- When taps might be slightly imprecise

---

## 🔧 Implementation Checklist

- [ ] Add `hasPreviousTap` member variable to header
- [ ] Increment `samplesSinceLastTap` each block when tap input connected
- [ ] Calculate BPM from interval on tap edge
- [ ] Validate interval is in reasonable range (0.2-3.0 seconds)
- [ ] Update BPM parameter on valid tap
- [ ] Add timeout to reset tap state after 4 seconds
- [ ] Add logging for debugging
- [ ] Test with various tap patterns

---

## 📊 Expected Behavior After Fix

### Scenario 1: Tapping at 120 BPM
```
Tap 1 at 0.0s   → No action (first tap)
Tap 2 at 0.5s   → Calculate: 60 / 0.5 = 120 BPM ✅
Tap 3 at 1.0s   → Calculate: 60 / 0.5 = 120 BPM ✅
Tap 4 at 1.5s   → Calculate: 60 / 0.5 = 120 BPM ✅
```

### Scenario 2: Timeout
```
Tap 1 at 0.0s   → No action (first tap)
... 5 seconds pass ...
Tap 2 at 5.0s   → No action (first tap after timeout)
Tap 3 at 5.4s   → Calculate: 60 / 0.4 = 150 BPM ✅
```

### Scenario 3: Invalid Intervals
```
Tap 1 at 0.0s   → No action (first tap)
Tap 2 at 0.05s  → Ignored (too fast, < 0.2s)
Tap 3 at 0.6s   → Calculate: 60 / 0.55 = 109 BPM ✅
```

---

## 🚨 Edge Cases to Handle

1. **First tap** - Should NOT calculate BPM (need interval)
2. **Too fast** - Taps < 0.2s apart (> 300 BPM) should be ignored
3. **Too slow** - Taps > 3.0s apart (< 20 BPM) should be ignored
4. **Timeout** - After 4s, reset state so next tap is treated as "first tap"
5. **Disconnected** - Only increment counter when tap input is connected
6. **Parameter updates** - BPM changes should update the APVTS parameter for persistence

---

## 💡 User Experience

### Before Fix
- Tap input does nothing
- User confusion: "Is tap broken?"
- No way to set BPM by tapping

### After Fix
- Tap twice to set BPM
- Immediate visual feedback in UI
- Timeout prevents stale state
- Logging shows tap intervals for debugging

---

## 🧪 Testing Strategy

1. **Manual Testing**
   - Tap at known BPM (use metronome)
   - Verify BPM updates correctly
   - Test timeout behavior
   - Test edge cases (very fast/slow taps)

2. **Integration Testing**
   - Verify tap works with syncToHost enabled/disabled
   - Check interaction with BPM CV modulation
   - Test with division override active

3. **Performance Testing**
   - Ensure no audio dropouts during tap
   - Verify thread safety of parameter updates

---

## 📝 Summary

The current tap tempo implementation is **completely non-functional** - it only resets a counter that is never used. The fix requires:

1. Incrementing `samplesSinceLastTap` each block
2. Calculating BPM from the interval on each tap
3. Validating intervals are in reasonable range
4. Updating the BPM parameter
5. Adding timeout logic

**Recommended**: Implement Option 1 (simple two-tap) for immediate, predictable results.


