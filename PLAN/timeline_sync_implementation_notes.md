# Timeline Sync Implementation Notes

**Date**: 2025-11-18  
**Status**: Ready for Implementation

---

## Key Clarifications from Codebase Review

### 1. SampleLoader Position Tracking
- **Mechanism**: Advances based on audio processing (samples processed), NOT transport state
- **Units**: `readPosition` is in **samples** (not seconds)
- **Independence**: Currently independent - does NOT use `setTimingInfo()` or `m_currentTransport`
- **Implementation**: Use `sampleProcessor->getCurrentPositionSeconds()` at START of block

### 2. Module Processing Order
- **Determination**: JUCE's `AudioProcessorGraph` uses topological sort (connection-based)
- **TempoClock Position**: May run early or late depending on graph topology
- **Delay Strategy**: **Accept one-block delay** - update in block N, applies to block N+1 (standard behavior)

### 3. Exposing `activeAudioProcessors`
- **Action**: Add public getter to `ModularSynthProcessor.h`:
```cpp
std::shared_ptr<const std::vector<std::shared_ptr<ModuleProcessor>>> getActiveAudioProcessors() const 
{ 
    return activeAudioProcessors.load(); 
}
```

### 4. Master Mode Implementation
- **Key Insight**: SampleLoader is ALREADY independent (acts as master by default)
- **Implementation**: Only need to add reporting interface, NO changes to playback logic
- **Position**: Calculate at START of block, store in atomic

### 5. Position Calculation
- **Timing**: Use **START of block** position
- **Reasoning**: TransportState represents time at sample index 0
- **Calculation**: `atomicPosition = sampleProcessor->getCurrentPositionSeconds()` (before renderBlock)

### 6. UI Thread Updates
- **Mechanism**: Use `juce::MessageManager::callAsync`
- **Pattern**:
```cpp
juce::MessageManager::callAsync([this]() {
    if (auto* p = apvts.getParameter(paramIdTimelineSourceId))
        p->setValueNotifyingHost(0.0f); 
});
```

### 7. VideoFileLoaderModule
- **Tracking**: Uses `currentAudioSamplePosition` (atomic int64) - easier than SampleLoader
- **Implementation**:
  - Position: `currentAudioSamplePosition.load() / sourceAudioSampleRate.load()`
  - Duration: `totalDurationMs.load() / 1000.0`
  - Active: `playing.load()`

### 8. BPM Derivation
- **Strategy**: Calculate every block (trivial math, safer than caching)
- **Location**: Inside `TempoClock::processBlock`

### 9. Transport Position Update
- **Location**: Inside `TempoClockModuleProcessor::processBlock`
- **Flow**:
  1. TempoClock reads atomic from target module
  2. Calls `parent->setTransportPositionSeconds(...)`
  3. ModularSynthProcessor updates `m_transportState`
  4. Broadcasts to all modules via `setTimingInfo` (applies to next block)

### 10. Module Deletion Handling
- **Strategy**: Immediate fail-safe + UI cleanup
- **Steps**:
  1. Stop syncing immediately (fallback to internal clock)
  2. Set `timelineSourceIdParam` to 0 (atomic, safe)
  3. Fire `callAsync` to update UI dropdown

---

## Implementation Checklist

### Phase 1: Foundation
- [ ] Add `getActiveAudioProcessors()` getter to `ModularSynthProcessor.h`
- [ ] Add timeline reporting interface to `ModuleProcessor.h`:
  - `canProvideTimeline()`
  - `getTimelinePositionSeconds()`
  - `getTimelineDurationSeconds()`
  - `isTimelineActive()`
- [ ] Add `setTransportPositionSeconds()` to `ModularSynthProcessor`
- [ ] Add `setTimelineMaster()` / `isModuleTimelineMaster()` methods

### Phase 2: Module Reporting
- [ ] Add atomic variables to `SampleLoaderModuleProcessor`:
  - `std::atomic<double> reportPosition { 0.0 }`
  - `std::atomic<double> reportDuration { 0.0 }`
  - `std::atomic<bool> reportActive { false }`
- [ ] Update atomics at START of `processBlock()` in SampleLoader
- [ ] Override reporting interface methods in SampleLoader
- [ ] Implement reporting interface in VideoFileLoaderModule
- [ ] Verify `SampleVoiceProcessor` exposes `getCurrentPositionSeconds()` (or add it)

### Phase 3: TempoClock Parameters
- [ ] Add parameters to `TempoClockModuleProcessor`:
  - `syncToTimeline`
  - `timelineSourceId`
  - `enableBPMDerivation`
  - `beatsPerTimeline`
- [ ] Initialize parameter pointers in constructor

### Phase 4: UI Dropdown
- [ ] Implement dropdown in `drawParametersInNode()`
- [ ] Scan `activeAudioProcessors` snapshot on UI thread
- [ ] Handle selection changes
- [ ] Show source status indicators

### Phase 5: Sync Logic
- [ ] Get `activeAudioProcessors` snapshot in `TempoClock::processBlock`
- [ ] Find target module by logical ID
- [ ] Read atomics directly (zero locks)
- [ ] Update transport position (every block)
- [ ] Calculate derived BPM (if enabled)
- [ ] Handle module deletion (fail-safe + UI update)

### Phase 6: State Persistence
- [ ] Verify APVTS handles parameter persistence automatically
- [ ] Test preset save/load with timeline sync enabled

### Phase 7: Edge Cases
- [ ] Test module deletion during sync
- [ ] Test source becoming inactive
- [ ] Test position clamping
- [ ] Test BPM derivation edge cases
- [ ] Add logging for debugging

---

## Critical Implementation Details

### SampleLoader Position Calculation
```cpp
// At START of processBlock, BEFORE renderBlock:
double currentPosSeconds = sampleProcessor->getCurrentPositionSeconds();
reportPosition.store(currentPosSeconds, std::memory_order_relaxed);
reportDuration.store(sampleDurationSeconds, std::memory_order_relaxed);
reportActive.store(isPlaying && (readPosition < sampleDurationSeconds * sampleSampleRate), 
                  std::memory_order_relaxed);
```

### TempoClock Sync Logic
```cpp
// Get snapshot (thread-safe)
auto currentProcessors = parent->getActiveAudioProcessors();
if (currentProcessors && syncToTimeline)
{
    juce::uint32 targetId = (juce::uint32)timelineSourceIdParam->load();
    bool found = false;
    
    for (const auto& mod : *currentProcessors)
    {
        if (mod && mod->getLogicalId() == targetId)
        {
            if (mod->canProvideTimeline() && mod->isTimelineActive())
            {
                double pos = mod->getTimelinePositionSeconds();
                double dur = mod->getTimelineDurationSeconds();
                pos = juce::jlimit(0.0, dur, pos);
                
                parent->setTransportPositionSeconds(pos);
                parent->setTimelineMaster(targetId);
                
                // BPM derivation...
            }
            found = true;
            break;
        }
    }
    
    if (!found && targetId != 0)
    {
        *timelineSourceIdParam = 0;
        juce::MessageManager::callAsync([this]() {
            if (auto* p = apvts.getParameter(paramIdTimelineSourceId))
                p->setValueNotifyingHost(0.0f);
        });
    }
}
```

---

## Notes

- **One-block delay is acceptable** - standard audio practice
- **SampleLoader is already independent** - no playback logic changes needed
- **Use start of block position** - matches TransportState semantics
- **Calculate BPM every block** - simpler than caching
- **Immediate fail-safe on deletion** - atomic parameter reset + async UI update

---

**Ready to proceed with Phase 1!**

