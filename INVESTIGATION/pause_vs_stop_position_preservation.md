# Pause vs Stop Position Preservation Investigation

**Date**: 2025-01-XX  
**Issue**: Play always starts from position 0, even after Pause (should resume from paused position)  
**Status**: Investigation Complete - No Code Changes

---

## Expected Behavior

- **Pause Button**: Should stop playback but **preserve position** (resume from where it stopped)
- **Stop Button**: Should stop playback and **reset position to 0**

---

## Current Implementation Analysis

### Button Code (ImGuiNodeEditorComponent.cpp, lines 1754-1794)

**Pause Button**:
```cpp
if (ImGui::Button("Pause"))
{
    if (presetCreator != nullptr)
        presetCreator->setMasterPlayState(false);  // ✅ Should preserve position
    else
        synth->setPlaying(false);
}
```

**Stop Button**:
```cpp
if (ImGui::Button("Stop"))
{
    if (presetCreator != nullptr)
        presetCreator->setMasterPlayState(false);  // ✅ Stops playback
    else
        synth->setPlaying(false);
    
    synth->resetTransportPosition();  // ✅ Explicitly resets position
}
```

**Analysis**: Button code looks correct - Pause doesn't call `resetTransportPosition()`, Stop does.

---

## Transport Position Management

### Position Tracking (ModularSynthProcessor.h, line 254)
```cpp
juce::uint64 m_samplePosition { 0 };  // Initialized to 0
```

### Position Updates (ModularSynthProcessor.cpp, lines 230-235)
```cpp
if (m_transportState.isPlaying && shouldAdvanceTransport)
{
    m_samplePosition += buffer.getNumSamples();  // ✅ Only advances when playing
    m_transportState.songPositionSeconds = m_samplePosition / getSampleRate();
    m_transportState.songPositionBeats = (m_transportState.songPositionSeconds / 60.0) * m_transportState.bpm;
}
```

**Analysis**: Position only advances when `isPlaying == true`. When paused (`isPlaying == false`), position should remain unchanged.

### Position Reset Locations

**1. resetTransportPosition()** (ModularSynthProcessor.h, line 114):
```cpp
void resetTransportPosition() { 
    m_samplePosition = 0; 
    m_transportState.songPositionBeats = 0.0; 
    m_transportState.songPositionSeconds = 0.0; 
}
```
- **Called by**: Stop button only (line 1793)

**2. Global Reset Request** (ModularSynthProcessor.cpp, lines 240-246):
```cpp
if (m_globalResetRequest.exchange(false))
{
    m_transportState.forceGlobalReset.store(true);
    m_samplePosition = 0;  // Reset internal counter
    m_transportState.songPositionSeconds = 0.0;
    m_transportState.songPositionBeats = 0.0;
}
```
- **Triggered by**: Timeline master modules (e.g., SampleLoader) when they loop
- **Not triggered by**: Pause/Play buttons

**3. setTransportPositionSeconds()** (ModularSynthProcessor.cpp, lines 1698-1712):
```cpp
m_transportState.songPositionSeconds = positionSeconds;
// ... calculates beats ...
m_samplePosition = (juce::uint64)(positionSeconds * sampleRate);
```
- **Called by**: TempoClock module when syncing to timeline
- **Not called by**: Pause/Play buttons

---

## setPlaying() Function Analysis

### Implementation (ModularSynthProcessor.h, lines 90-99)
```cpp
void setPlaying(bool playing) {
    m_transportState.isPlaying = playing;
    // Immediately broadcast timing change to modules even if audio callback is stopped
    if (auto processors = activeAudioProcessors.load())
    {
        for (const auto& modulePtr : *processors)
            if (modulePtr)
                modulePtr->setTimingInfo(m_transportState);  // ✅ Broadcasts current state (preserves position)
    }
}
```

**Analysis**: 
- ✅ `setPlaying()` does NOT reset position
- ✅ It broadcasts current transport state (including current position) to all modules
- ✅ Position should be preserved

---

## setMasterPlayState() Function Analysis

### Implementation (PresetCreatorComponent.cpp, lines 164-189)
```cpp
void PresetCreatorComponent::setMasterPlayState(bool shouldBePlaying)
{
    if (synth == nullptr)
        return;

    // 1. Control the Audio Engine (start/stop pulling audio)
    if (shouldBePlaying)
    {
        if (!auditioning)
        {
            deviceManager.addAudioCallback(&processorPlayer);  // ✅ Starts audio callback
            auditioning = true;
        }
    }
    else
    {
        if (auditioning)
        {
            deviceManager.removeAudioCallback(&processorPlayer);  // ✅ Stops audio callback
            auditioning = false;
        }
    }

    // 2. Control the synth's internal transport clock
    synth->setPlaying(shouldBePlaying);  // ✅ Does NOT reset position
}
```

**Analysis**:
- ✅ Does NOT reset transport position
- ✅ Only controls audio callback and transport state
- ✅ Position should be preserved when pausing/resuming

---

## Potential Issues

### Issue 1: Audio Callback Restart
**Hypothesis**: When audio callback is removed and re-added, something might reset position.

**Investigation**:
- `addAudioCallback()` / `removeAudioCallback()` are JUCE functions - they don't interact with transport position
- `prepareToPlay()` is called when audio device changes, but not when callback is added/removed
- **Verdict**: ❌ Not the cause

### Issue 2: Module Position Reset
**Hypothesis**: Modules might reset their own positions when they receive `setTimingInfo()` with `isPlaying=true` after being paused.

**Investigation Needed**:
- Check if modules (SampleLoader, MIDIPlayer, etc.) reset their positions when `isPlaying` transitions from `false` to `true`
- Check if modules have logic like: "if was stopped and now playing, reset position"

**Potential Culprits**:
- `SampleLoaderModuleProcessor`: Might reset when transport starts
- `MIDIPlayerModuleProcessor`: Might reset when transport starts
- `VideoFileLoaderModule`: Might reset when transport starts

**Action Required**: Need to check module implementations for position reset logic on play start.

### Issue 3: Timeline Master Interference
**Hypothesis**: If a timeline master module (SampleLoader/VideoLoader) is active, it might be resetting transport position.

**Investigation**:
- Timeline masters can call `triggerGlobalReset()` when they loop (line 471 in SampleLoader)
- But this should only happen during playback, not when resuming from pause
- **Verdict**: ⚠️ Possible if module has logic that resets on play start

### Issue 4: Position Not Actually Preserved
**Hypothesis**: Position might be getting reset somewhere we haven't found yet.

**Investigation**:
- All reset locations identified:
  1. `resetTransportPosition()` - only called by Stop button ✅
  2. Global reset request - only triggered by timeline master loops ✅
  3. `setTransportPositionSeconds()` - only called by TempoClock sync ✅
- **Verdict**: ❓ Need to check if modules are resetting transport position

---

## Key Findings

### ✅ What Should Work
1. **Transport position tracking**: `m_samplePosition` is only advanced when playing, preserved when paused
2. **setPlaying()**: Does NOT reset position, only sets flag and broadcasts state
3. **setMasterPlayState()**: Does NOT reset position, only controls audio callback and transport
4. **Button code**: Pause doesn't call `resetTransportPosition()`, Stop does

### ⚠️ Potential Problems
1. **Module behavior**: Modules might reset their own positions when `isPlaying` becomes `true`
2. **Timeline master**: If a timeline master module is active, it might be interfering
3. **Position sync**: If modules are synced to transport, they might reset when transport starts

---

## Recommended Investigation Steps

### Step 1: Check Module Position Reset Logic
Search for modules that reset position when transport starts:
```cpp
// Look for patterns like:
if (state.isPlaying && !wasPlaying) {
    // Reset position
}
```

### Step 2: Check Timeline Master Behavior
If a SampleLoader or VideoLoader is the timeline master:
- Does it reset transport position when it starts?
- Does it call `triggerGlobalReset()` inappropriately?

### Step 3: Add Logging
Add logging to track position changes:
- Log position when `setPlaying(true)` is called
- Log position when `setPlaying(false)` is called
- Log position in `processBlock()` to see if it's being reset

### Step 4: Test Scenarios
1. **Test without timeline master**: Create a patch with no SampleLoader/VideoLoader as timeline master
   - Does position preserve after pause?
2. **Test with timeline master**: Create a patch with SampleLoader as timeline master
   - Does position preserve after pause?
3. **Test module-specific**: Test with different modules (MIDIPlayer, SampleLoader, etc.)
   - Which modules cause position reset?

---

## Conclusion

**Code Analysis**: The transport position management code appears correct. Position should be preserved when pausing.

**Most Likely Cause**: Individual modules (SampleLoader, MIDIPlayer, VideoLoader, etc.) are resetting their own positions or the transport position when they receive `setTimingInfo()` with `isPlaying=true` after being paused.

**Next Steps**: 
1. Investigate module implementations for position reset logic
2. Check if timeline master modules are interfering
3. Add logging to track position changes
4. Test with different module configurations

---

## Files to Investigate Further

1. `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`
   - Check `setTimingInfo()` implementation
   - Check if position is reset when transport starts

2. `juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp`
   - Check `setTimingInfo()` implementation
   - Check if position is reset when transport starts

3. `juce/Source/audio/modules/VideoFileLoaderModule.cpp`
   - Check `setTimingInfo()` implementation
   - Check if position is reset when transport starts

4. Any other modules that track playback position

