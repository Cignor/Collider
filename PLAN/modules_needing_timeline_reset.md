# Modules That Should Handle Timeline Reset

**Date**: 2025-01-XX  
**Context**: When a timeline source (SampleLoader/VideoLoader) loops and is the timeline master, modules that sync to transport should reset their internal state.

---

## Criteria for Reset Handling

A module should handle `forceGlobalReset` if it:
1. ✅ **Syncs to transport** (uses `m_currentTransport.songPositionBeats` or `songPositionSeconds`)
2. ✅ **Has internal phase/position/sequencer state** that should reset when timeline loops
3. ✅ **Would produce incorrect output** if state doesn't reset

---

## Complete List

### ✅ **IMPLEMENTED** (Handle Reset Correctly)

1. **LFOModuleProcessor**
   - **State**: Oscillator phase
   - **Reset**: `osc.reset()` (resets phase to 0)
   - **Location**: `LFOModuleProcessor.cpp:190-194`

2. **StepSequencerModuleProcessor**
   - **State**: Current step index, phase
   - **Reset**: `currentStep.store(0)`, `phase = 0.0`
   - **Location**: `StepSequencerModuleProcessor.cpp:318-323`

3. **MultiSequencerModuleProcessor** ✅ **JUST ADDED**
   - **State**: Current step index, phase
   - **Reset**: `currentStep.store(0)`, `phase = 0.0`
   - **Location**: `MultiSequencerModuleProcessor.cpp:251-256`

4. **FunctionGeneratorModuleProcessor** ✅ **JUST ADDED**
   - **State**: `phase`, `lastPhase`
   - **Reset**: `phase = 0.0`, `lastPhase = 0.0`
   - **Location**: `FunctionGeneratorModuleProcessor.cpp:150-156` (before loop)

5. **StrokeSequencerModuleProcessor** ✅ **JUST ADDED**
   - **State**: `playheadPosition`
   - **Reset**: `playheadPosition = 0.0`
   - **Location**: `StrokeSequencerModuleProcessor.cpp:223-226` (before sync calculation)

6. **RandomModuleProcessor** ✅ **JUST ADDED**
   - **State**: `lastScaledBeats`
   - **Reset**: `lastScaledBeats = 0.0`
   - **Location**: `RandomModuleProcessor.cpp:133-138` (inside sync mode check)

7. **TimelineModuleProcessor** ✅ **JUST ADDED**
   - **State**: `m_internalPositionBeats`, `m_lastPositionBeats`, `m_lastKeyframeIndexHints`
   - **Reset**: Reset position to 0.0 and clear hints
   - **Location**: `TimelineModuleProcessor.cpp:77-84` (before timing calculation)

---

### ⚠️ **DO NOT NEED RESET** (Don't Sync to Transport)

These modules have phase/sequencer state but **don't sync to transport**, so they don't need reset handling:

- **ClockDividerModuleProcessor**: Uses clock input, not transport
- **SnapshotSequencerModuleProcessor**: Uses clock input, not transport
- **GranulatorModuleProcessor**: Uses clock input, not transport
- **VCOModuleProcessor**: Free-running or uses triggers, not transport
- **ChorusModuleProcessor**: Delay lines don't need reset (they're continuous)
- **DelayModuleProcessor**: Delay lines don't need reset (they're continuous)
- **PhaserModuleProcessor**: Phase state is continuous, doesn't need reset

---

## Implementation Pattern

### Standard Pattern

```cpp
void YourModuleProcessor::processBlock(...)
{
    // ... other code ...
    
    // Check Global Reset (pulse from Timeline Master loop)
    // When SampleLoader/VideoLoader loops and is timeline master, all synced modules reset
    if (m_currentTransport.forceGlobalReset.load())
    {
        // Reset module-specific state to initial values
        phase = 0.0;
        lastPhase = 0.0;
        // ... other state resets ...
    }
    
    // Then proceed with normal sync logic
    if (syncEnabled && m_currentTransport.isPlaying)
    {
        // Use m_currentTransport.songPositionBeats for sync
    }
}
```

### Placement
- **Check reset BEFORE using transport position**
- This ensures state is reset before calculating new position
- Reset happens once per loop (flag is cleared after one block)

---

## Summary

### Modules That Need Reset Handling Added:

1. **FunctionGeneratorModuleProcessor** - Reset `phase` and `lastPhase`
2. **StrokeSequencerModuleProcessor** - Reset `playheadPosition`
3. **RandomModuleProcessor** - Reset `lastScaledBeats`
4. **TimelineModuleProcessor** - Check what state needs reset

### Total Count:
- **Implemented**: 7 modules ✅ **ALL COMPLETE**
  - LFO, StepSequencer, MultiSequencer (original 3)
  - FunctionGenerator, StrokeSequencer, Random, Timeline (just added 4)
- **Missing**: 0 modules
- **Total needed**: 7 modules

---

## Status: ✅ **COMPLETE**

All modules that sync to transport and have phase/position state now handle timeline reset correctly!

