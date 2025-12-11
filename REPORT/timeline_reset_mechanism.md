# Timeline Reset Mechanism - Module Behavior Guide

**Date**: 2025-01-XX  
**Context**: When a timeline source (SampleLoader/VideoLoader) loops and is the timeline master, all synced modules should reset their state.

---

## How It Works

### 1. Reset Trigger
When a **timeline master** (SampleLoader or VideoLoader) loops:
- SampleLoader detects loop wrap: `currentSamplePos < lastReadPosition` (line 410)
- Only triggers if module is timeline master: `parentSynth->isModuleTimelineMaster(getLogicalId())`
- Calls: `parentSynth->triggerGlobalReset()`

### 2. Reset Propagation
In `ModularSynthProcessor::processBlock()`:
- Checks `m_globalResetRequest.exchange(false)` (atomic read-and-clear)
- Sets `m_transportState.forceGlobalReset.store(true)` for **one block**
- Next block: Sets `forceGlobalReset.store(false)` (cleared)
- Broadcasts `TransportState` to all modules via `setTimingInfo()`

### 3. Module Response
Modules that sync to transport should check `forceGlobalReset` and reset their state:

```cpp
// In processBlock(), before using transport position:
if (m_currentTransport.forceGlobalReset.load())
{
    // Reset module-specific state to initial values
    // Examples:
    // - Sequencers: Reset to step 0, phase to 0.0
    // - LFOs: Reset oscillator phase to 0
    // - Oscillators: Reset phase to 0
    // - Any time-based module: Reset to start state
}
```

---

## Modules That Should Handle Reset

### ✅ Currently Implemented

1. **LFOModuleProcessor** (line 190-194)
   ```cpp
   if (m_currentTransport.forceGlobalReset.load())
   {
       osc.reset(); // Reset oscillator phase to 0
   }
   ```

2. **StepSequencerModuleProcessor** (line 318-323)
   ```cpp
   if (m_currentTransport.forceGlobalReset.load())
   {
       currentStep.store(0);
       phase = 0.0;
   }
   ```

3. **MultiSequencerModuleProcessor** (line 251-256) ✅ **JUST ADDED**
   ```cpp
   if (m_currentTransport.forceGlobalReset.load())
   {
       currentStep.store(0);
       phase = 0.0;
   }
   ```

### ⚠️ Modules That May Need Reset Handling

These modules have phase/position state and sync to transport. They should check if they need reset handling:

1. **FunctionGeneratorModuleProcessor** - Has phase state
2. **RandomModuleProcessor** - May have internal state
3. **ClockDividerModuleProcessor** - Has phase/division state
4. **SnapshotSequencerModuleProcessor** - Has sequencer state
5. **GranulatorModuleProcessor** - Has grain position state
6. **HarmonicShaperModuleProcessor** - May have phase state
7. **ChorusModuleProcessor** - Has delay line positions

**Note**: Not all modules need reset. Only modules that:
- Sync to transport (`m_currentTransport.songPositionBeats`)
- Have internal phase/position state that should reset when timeline loops
- Would produce incorrect output if they don't reset

---

## Implementation Pattern

### Standard Pattern for Sequencers/LFOs

```cpp
void YourModuleProcessor::processBlock(...)
{
    // ... other code ...
    
    // Check Global Reset (pulse from Timeline Master loop)
    // When SampleLoader/VideoLoader loops and is timeline master, all synced modules reset
    if (m_currentTransport.forceGlobalReset.load())
    {
        // Reset module-specific state to initial values
        currentStep.store(0);        // For sequencers
        phase = 0.0;                 // For phase-based modules
        osc.reset();                  // For oscillators
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

## What Gets Reset

### Transport State (in ModularSynthProcessor)
- `m_samplePosition = 0`
- `m_transportState.songPositionSeconds = 0.0`
- `m_transportState.songPositionBeats = 0.0`
- `m_transportState.forceGlobalReset = true` (for one block)

### Module State (in each module)
Each module resets its own internal state:
- **Sequencers**: Step index → 0, Phase → 0.0
- **LFOs**: Oscillator phase → 0
- **Oscillators**: Phase → 0
- **Any time-based module**: Reset to initial state

---

## When Reset Happens

1. **Timeline Master Loops**: SampleLoader/VideoLoader detects loop wrap
2. **Only if Timeline Master**: Reset only triggers if the looping module is the timeline master
3. **One Block Pulse**: `forceGlobalReset` is true for exactly one block, then cleared
4. **All Modules Receive**: All modules get the reset flag via `setTimingInfo()`

---

## Testing Checklist

- [ ] SampleLoader loops → triggers reset
- [ ] VideoLoader loops → triggers reset (if timeline master)
- [ ] LFO resets phase when timeline loops
- [ ] StepSequencer resets to step 0 when timeline loops
- [ ] MultiSequencer resets to step 0 when timeline loops
- [ ] Reset only happens when module is timeline master
- [ ] Reset doesn't happen when not timeline master
- [ ] All synced modules reset simultaneously

---

## Notes

- **Reset is automatic**: Modules don't need to manually trigger it
- **One-block pulse**: Flag is cleared after one block, so modules must check it every block
- **Timeline master only**: Reset only happens when the looping module is the timeline master
- **Thread-safe**: Uses `std::atomic` for all state

---

## Summary

When a timeline source loops and is the timeline master:
1. **SampleLoader/VideoLoader** detects loop → calls `triggerGlobalReset()`
2. **ModularSynthProcessor** sets `forceGlobalReset = true` for one block
3. **All modules** receive reset flag via `setTimingInfo()`
4. **Each module** checks flag and resets its internal state
5. **Next block**: Flag is cleared, modules continue from reset state

**Key Point**: Modules that sync to transport and have phase/position state should check `forceGlobalReset` and reset their state to initial values.

