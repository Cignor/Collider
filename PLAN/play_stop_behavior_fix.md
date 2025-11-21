# Play/Stop Behavior Fix - Comprehensive Plan

**Date**: 2025-01-XX  
**Feature**: Fix all play/stop behavior inconsistencies  
**Status**: Planning Phase

---

## Executive Summary

This plan addresses 20 identified inconsistencies in the play/stop behavior system, including:
- Transport state not being saved/restored from XML
- UI buttons and CV inputs bypassing unified control function
- Modules auto-starting playback when they shouldn't
- No post-load synchronization of transport state
- Inconsistent handling of sync-to-transport parameters

**Goal**: Ensure consistent, predictable play/stop behavior across all modules, UI controls, and patch loading scenarios.

---

## Problem Analysis

### Root Causes Identified

1. **Transport State Management**
   - Transport `isPlaying` state is not persisted to XML
   - Transport state is not explicitly synchronized after patch load
   - No mechanism to force all modules to respect transport state

2. **Control Path Inconsistencies**
   - Multiple code paths control playback (buttons, spacebar, CV, modules)
   - Only spacebar uses unified `setMasterPlayState()` function
   - Buttons and CV inputs bypass audio callback management

3. **Module Initialization Issues**
   - Modules' `reset()` methods unconditionally set `isPlaying = true`
   - No transport state check during initialization
   - Some modules auto-start when clips/samples are loaded

4. **Sync Parameter Handling**
   - Modules with `syncToTransport = false` ignore transport state
   - No post-load synchronization for independent modules
   - Inconsistent behavior between synced and unsynced modules

---

## Solution Architecture

### Phase 1: Transport State Persistence (Foundation)

**Changes Required**:

1. **Save Transport State to XML**
   - Location: `ModularSynthProcessor::getStateInformation()`
   - Add `isPlaying` to saved state
   - Save as boolean attribute in root XML element

2. **Restore Transport State from XML**
   - Location: `ModularSynthProcessor::setStateInformation()`
   - Read `isPlaying` from XML (default to `false` if missing)
   - Set transport state before module restoration

3. **Post-Load Synchronization**
   - After `setStateInformation()` completes
   - Explicitly call `setPlaying(false)` to ensure stopped state
   - Broadcast timing info to all modules
   - Force all modules to respect transport state

**Files to Modify**:
- `juce/Source/audio/graph/ModularSynthProcessor.cpp` (getStateInformation, setStateInformation)
- `juce/Source/audio/graph/ModularSynthProcessor.h` (add helper method for post-load sync)

**Risk**: Low  
**Difficulty**: Easy  
**Confidence**: High (95%)

---

### Phase 2: Unified Control Function (Critical Fix)

**Changes Required**:

1. **Make setMasterPlayState() Accessible**
   - Location: `PresetCreatorComponent.h`
   - Make `setMasterPlayState()` public or add public wrapper
   - Or move to a shared interface/manager

2. **Update UI Buttons**
   - Location: `ImGuiNodeEditorComponent.cpp:1556-1571`
   - Replace `synth->setPlaying()` calls with `setMasterPlayState()`
   - Ensure buttons work the same as spacebar

3. **Update TempoClock CV Inputs**
   - Location: `TempoClockModuleProcessor.cpp:112-113`
   - Instead of `getParent()->setPlaying()`, need to call unified function
   - Challenge: Module doesn't have direct access to `PresetCreatorComponent`
   - Solution: Add callback mechanism or use message thread

**Files to Modify**:
- `juce/Source/preset_creator/PresetCreatorComponent.h` (make function accessible)
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (button handlers)
- `juce/Source/audio/modules/TempoClockModuleProcessor.cpp` (CV handlers)
- `juce/Source/audio/graph/ModularSynthProcessor.h` (add callback mechanism)

**Architecture Decision**: 
- Option A: Add callback from ModularSynthProcessor to PresetCreatorComponent
- Option B: Move audio callback management into ModularSynthProcessor
- Option C: Use message thread to post commands to UI thread

**Recommended**: Option B (move audio callback management to ModularSynthProcessor)
- Cleaner architecture
- Modules can directly control transport + audio
- No circular dependencies

**Risk**: Medium  
**Difficulty**: Medium  
**Confidence**: Medium (70%) - depends on architecture choice

---

### Phase 3: Module Reset() Behavior Fix

**Changes Required**:

1. **Add Transport State Check to reset() Methods**
   - Location: Multiple modules (SampleLoader, TTSPerformer, etc.)
   - Check transport state before setting `isPlaying = true`
   - Only start playback if transport is playing

2. **Create resetPosition() Alternative**
   - Some modules already have this (SampleLoader)
   - Ensure all modules that need it have `resetPosition()` method
   - Use `resetPosition()` during initialization/patch load

3. **Update Module Initialization**
   - During `prepareToPlay()`, use `resetPosition()` not `reset()`
   - During patch load, use `resetPosition()` not `reset()`
   - Only use `reset()` when explicitly starting playback

**Files to Modify**:
- `juce/Source/audio/voices/SampleVoiceProcessor.h` (reset() method)
- `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp` (reset() calls)
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp` (startPlayback() method)
- All modules that call `reset()` during initialization

**Specific Changes**:

```cpp
// SampleVoiceProcessor::reset()
void reset() override { 
    readPosition = startSamplePos; 
    timePitch.reset(); 
    // Check transport state before starting
    isPlaying = false; // Default to stopped, let transport control it
}

// Add new method for explicit start
void resetAndPlay() {
    readPosition = startSamplePos;
    timePitch.reset();
    isPlaying = true;
}
```

**Risk**: Medium  
**Difficulty**: Medium-Hard  
**Confidence**: Medium (75%) - many modules to update

---

### Phase 4: Module Auto-Start Prevention

**Changes Required**:

1. **TTSPerformer startPlayback() Check**
   - Location: `TTSPerformerModuleProcessor.cpp:88`
   - Check transport state before setting `isPlaying = true`
   - Only start if transport is playing OR explicit user trigger

2. **TTSPerformer setStateInformation() Defer**
   - Location: `TTSPerformerModuleProcessor.cpp:1697`
   - Don't start playback immediately during patch load
   - Defer until after transport state is synchronized
   - Use flag to indicate "should start after sync"

3. **SampleLoader Trigger Handling**
   - Location: `SampleLoaderModuleProcessor.cpp:596`
   - Check transport state before calling `reset()`
   - Or use `resetPosition()` and then check if should play

4. **VideoFileLoaderModule Initialization**
   - Ensure `playing` is false after patch load
   - Add explicit initialization in `setStateInformation()` if it exists

**Files to Modify**:
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`
- `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`
- `juce/Source/audio/modules/VideoFileLoaderModule.cpp` (if has setStateInformation)

**Risk**: Medium  
**Difficulty**: Medium  
**Confidence**: High (85%)

---

### Phase 5: Post-Load Synchronization System

**Changes Required**:

1. **Add Force Stop All Mechanism**
   - Location: `ModularSynthProcessor.h`
   - New method: `forceStopAllModules()`
   - Iterates all modules and sets their `isPlaying = false`
   - Works regardless of `syncToTransport` setting

2. **Call After Patch Load**
   - Location: `ModularSynthProcessor::setStateInformation()`
   - After all modules are restored
   - Call `setPlaying(false)` (already broadcasts timing)
   - Call `forceStopAllModules()` to ensure independent modules also stop

3. **Handle Modules with syncToTransport = false**
   - These modules maintain independent playback state
   - Force stop ensures they start from stopped state
   - User can then manually start them if needed

**Files to Modify**:
- `juce/Source/audio/graph/ModularSynthProcessor.h` (add forceStopAllModules)
- `juce/Source/audio/graph/ModularSynthProcessor.cpp` (implement and call)
- `juce/Source/audio/modules/ModuleProcessor.h` (add virtual stop method if needed)

**Risk**: Low-Medium  
**Difficulty**: Easy-Medium  
**Confidence**: High (90%)

---

### Phase 6: Scrubbing State Preservation Fix

**Changes Required**:

1. **SampleLoader Scrubbing Logic**
   - Location: `SampleLoaderModuleProcessor.cpp:390`
   - Don't unconditionally set `isPlaying = true` when scrubbing
   - Check transport state: if stopped, keep stopped
   - If playing, preserve playing state

**Files to Modify**:
- `juce/Source/audio/modules/SampleLoaderModuleProcessor.cpp`

**Risk**: Low  
**Difficulty**: Easy  
**Confidence**: High (95%)

---

## Implementation Phases

### Phase 1: Foundation (Must Do First)
- Transport state persistence
- Post-load synchronization
- **Estimated Time**: 2-3 hours
- **Risk**: Low
- **Blocks**: Nothing

### Phase 2: Unified Control (Critical)
- Make setMasterPlayState() accessible
- Update UI buttons
- Update TempoClock CV
- **Estimated Time**: 4-6 hours
- **Risk**: Medium
- **Blocks**: Phase 1 (needs transport state management)

### Phase 3: Module Reset Behavior (Important)
- Fix reset() methods
- Add resetPosition() where needed
- Update initialization calls
- **Estimated Time**: 6-8 hours
- **Risk**: Medium
- **Blocks**: Phase 1 (needs transport state check)

### Phase 4: Auto-Start Prevention (Important)
- Fix TTSPerformer
- Fix SampleLoader triggers
- Fix VideoFileLoader
- **Estimated Time**: 4-5 hours
- **Risk**: Medium
- **Blocks**: Phase 1, Phase 3

### Phase 5: Force Stop System (Important)
- Add forceStopAllModules()
- Integrate into patch load
- **Estimated Time**: 2-3 hours
- **Risk**: Low-Medium
- **Blocks**: Phase 1

### Phase 6: Scrubbing Fix (Nice to Have)
- Fix scrubbing state preservation
- **Estimated Time**: 1 hour
- **Risk**: Low
- **Blocks**: Phase 1

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM** (6/10)

### High-Risk Areas

#### 1. Architecture Change for Unified Control (Risk: 7/10)
**Issue**: Moving audio callback management or adding callbacks
- Could create circular dependencies
- Could break existing functionality
- Requires careful design

**Mitigation**:
- Prefer moving audio callback management to ModularSynthProcessor
- Use existing message thread mechanisms
- Extensive testing after changes

**Impact**: If broken, buttons/CV won't work at all

#### 2. Module Reset() Changes (Risk: 6/10)
**Issue**: Many modules to update, easy to miss one
- Some modules might break if reset() behavior changes
- Need to find all call sites
- Some modules might depend on current behavior

**Mitigation**:
- Comprehensive search for all reset() calls
- Test each module type after changes
- Add unit tests if possible

**Impact**: Some modules might not start when they should

#### 3. Post-Load Synchronization (Risk: 5/10)
**Issue**: Force stopping all modules might break some use cases
- Modules with syncToTransport=false might be intentionally independent
- Force stopping might interfere with user intent
- Need to ensure it only happens on patch load

**Mitigation**:
- Only force stop during patch load
- Add flag to prevent force stop in other scenarios
- Document behavior clearly

**Impact**: Modules might not behave as expected after patch load

### Medium-Risk Areas

#### 4. Transport State Persistence (Risk: 4/10)
**Issue**: Adding new state to XML
- Need to handle legacy presets (no isPlaying attribute)
- Default to false if missing (safe)
- Need to test save/load cycle

**Mitigation**:
- Default to false (stopped) if attribute missing
- Test with old presets
- Backward compatible

**Impact**: Old presets might not restore playing state (acceptable)

#### 5. Module Auto-Start Prevention (Risk: 4/10)
**Issue**: Modules might not start when user expects
- Need to balance "don't auto-start" with "start when user wants"
- Some modules might need to start on trigger even if transport stopped
- Need clear distinction between auto-start and user-triggered start

**Mitigation**:
- Only prevent auto-start during initialization/patch load
- Allow explicit user triggers to start playback
- Clear documentation of behavior

**Impact**: User might need to manually start some modules

### Low-Risk Areas

#### 6. Scrubbing Fix (Risk: 2/10)
**Issue**: Simple logic change
- Low complexity
- Easy to test
- Minimal impact

**Impact**: Scrubbing might behave slightly differently

---

## Confidence Rating

### Overall Confidence: **MEDIUM-HIGH** (75%)

### Strong Points

1. **Clear Problem Identification**
   - All 20 inconsistencies documented
   - Root causes understood
   - Solution paths identified

2. **Incremental Approach**
   - Phases can be implemented independently
   - Each phase builds on previous
   - Can test after each phase

3. **Low-Risk Foundation First**
   - Phase 1 (transport persistence) is low risk
   - Provides foundation for other fixes
   - Can validate approach early

4. **Existing Patterns to Follow**
   - SampleLoader already has `resetPosition()` pattern
   - Some modules already check transport state
   - Can follow existing good patterns

### Weak Points

1. **Architecture Uncertainty**
   - Best way to unify control function unclear
   - Multiple options, each with trade-offs
   - Need to choose carefully

2. **Many Modules to Update**
   - Phase 3 requires updating many modules
   - Easy to miss one
   - Need comprehensive testing

3. **Edge Cases**
   - Modules with syncToTransport=false
   - User-triggered vs auto-start distinction
   - Legacy preset compatibility

4. **Testing Complexity**
   - Need to test all module types
   - Need to test all control paths (buttons, spacebar, CV)
   - Need to test patch load scenarios

---

## Potential Problems & Mitigations

### Problem 1: Breaking Existing Functionality
**Risk**: Changes might break modules that currently work
**Mitigation**: 
- Comprehensive testing after each phase
- Test with existing presets
- Gradual rollout (one phase at a time)

### Problem 2: User Confusion
**Risk**: Behavior changes might confuse users
**Mitigation**:
- Document new behavior clearly
- Ensure behavior is consistent (easier to learn)
- Provide migration notes if needed

### Problem 3: Performance Impact
**Risk**: Force stopping all modules might cause audio glitches
**Mitigation**:
- Only do during patch load (when audio might already be stopped)
- Use atomic operations where possible
- Test with large patches

### Problem 4: Circular Dependencies
**Risk**: Moving audio callback management might create dependencies
**Mitigation**:
- Careful architecture design
- Use callbacks/interfaces to break dependencies
- Prefer composition over inheritance

### Problem 5: Legacy Preset Compatibility
**Risk**: Old presets might not work correctly
**Mitigation**:
- Default to safe values (stopped state)
- Test with old presets
- Document migration if needed

### Problem 6: Modules That Should Auto-Start
**Risk**: Some modules might legitimately need to auto-start
**Mitigation**:
- Clear distinction: auto-start on load vs user trigger
- Allow explicit triggers to start even if transport stopped
- Document which modules should auto-start

### Problem 7: Race Conditions
**Risk**: Transport state changes during patch load
**Mitigation**:
- Use atomic operations
- Lock during critical sections
- Ensure proper sequencing (load → sync → ready)

### Problem 8: Testing Coverage
**Risk**: Hard to test all scenarios
**Mitigation**:
- Create test presets for each scenario
- Test with different module combinations
- Test all control paths (buttons, spacebar, CV)

---

## Testing Strategy

### Unit Tests (If Possible)
- Transport state save/load
- Module reset() behavior
- Force stop mechanism

### Integration Tests
- Patch load with various module types
- Button clicks vs spacebar vs CV
- Modules with syncToTransport=true vs false

### Manual Testing Checklist
- [ ] Load patch with SampleLoader - should not auto-play
- [ ] Load patch with TTSPerformer - should not auto-play
- [ ] Load patch with VideoFileLoader - should not auto-play
- [ ] Click Play button - should start transport and audio
- [ ] Click Stop button - should stop transport and audio
- [ ] Press spacebar - should toggle play/stop
- [ ] TempoClock CV play input - should start transport
- [ ] TempoClock CV stop input - should stop transport
- [ ] Load patch while playing - should stop after load
- [ ] Load patch while stopped - should remain stopped
- [ ] Save patch while playing - should restore as stopped (or playing if we save state)
- [ ] Modules with syncToTransport=false - should respect force stop on load
- [ ] Scrubbing while stopped - should remain stopped
- [ ] Scrubbing while playing - should continue playing

---

## Success Criteria

1. ✅ No modules auto-start after patch load (when transport is stopped)
2. ✅ Play/Stop buttons work identically to spacebar
3. ✅ TempoClock CV inputs work correctly
4. ✅ Transport state is saved and restored from XML
5. ✅ All modules respect transport state after patch load
6. ✅ Scrubbing preserves correct playback state
7. ✅ No regressions in existing functionality

---

## Timeline Estimate

**Total Estimated Time**: 19-26 hours

- Phase 1: 2-3 hours
- Phase 2: 4-6 hours
- Phase 3: 6-8 hours
- Phase 4: 4-5 hours
- Phase 5: 2-3 hours
- Phase 6: 1 hour
- Testing: 4-6 hours (throughout)

**Recommended Approach**: Implement phases sequentially, test after each phase.

---

## Alternative Approaches Considered

### Alternative 1: Only Fix Critical Issues
**Pros**: Faster, lower risk
**Cons**: Leaves some inconsistencies, might confuse users
**Verdict**: Not recommended - inconsistencies are interconnected

### Alternative 2: Complete Rewrite of Transport System
**Pros**: Clean slate, no legacy issues
**Cons**: High risk, long timeline, might break everything
**Verdict**: Not recommended - too risky, current system mostly works

### Alternative 3: Add Workarounds Instead of Fixes
**Pros**: Minimal changes
**Cons**: Technical debt, harder to maintain
**Verdict**: Not recommended - fixes are better long-term

---

## Conclusion

This plan addresses all 20 identified inconsistencies in a systematic, phased approach. The foundation (Phase 1) is low-risk and provides the base for other fixes. The most critical issue (unified control) is addressed in Phase 2, which has medium risk but high impact.

**Recommendation**: Proceed with implementation, starting with Phase 1. Test thoroughly after each phase before proceeding to the next.

**Confidence**: Medium-High (75%) - plan is solid, but implementation details (especially Phase 2 architecture) need careful consideration.

