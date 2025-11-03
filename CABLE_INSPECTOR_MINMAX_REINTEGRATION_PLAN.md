# Cable Inspector Min/Max Value Reintegration Plan

## Overview

This plan describes how to reintegrate the feature that displays the minimum and maximum values observed over a configurable time window when hovering over cables in the node editor. The infrastructure already exists but needs to be connected.

---

## Current State Analysis

### ✅ Already Implemented

1. **Data Structures** (in `ImGuiNodeEditorComponent.h` lines 296-299):
   - `ChannelHistory` struct: `std::deque<std::pair<double, float>>` - stores (timestamp, value) pairs
   - `inspectorHistory` map: keyed by `(logicalId, channel)` pair to track history per channel
   - `inspectorWindowSeconds` float: configurable time window (0.5-20.0 seconds, default 5.0)

2. **UI Control** (in `ImGuiNodeEditorComponent.cpp` line 850-853):
   - "Inspector" menu in top bar
   - Slider: "Window (s)" that controls `inspectorWindowSeconds`

3. **Value Query System**:
   - Modules update `lastOutputValues` (atomic floats) in `processBlock()`
   - `getOutputChannelValue(channel)` reads from `lastOutputValues`
   - Cable inspector currently queries live value via `getOutputChannelValue()`

### ❌ Missing Implementation

1. **History Tracking**: Values are NOT being stored in `inspectorHistory`
2. **History Cleanup**: Old samples outside time window are NOT being removed
3. **Min/Max Calculation**: Min/max not calculated from history
4. **Tooltip Display**: Tooltip only shows live value, not min/max stats

---

## Implementation Plan

### Phase 1: Add History Tracking

**Location**: `ImGuiNodeEditorComponent.cpp` around line 3806 (in the cable inspector tooltip section)

**Changes**:
1. Get current timestamp using `juce::Time::getMillisecondCounterHiRes() / 1000.0` (convert to seconds)
2. Create history key: `std::pair<juce::uint32, int>(srcPin.logicalId, srcPin.channel)`
3. Add current (timestamp, value) to the deque: `inspectorHistory[key].samples.push_back({timestamp, liveValue})`
4. **Thread Safety**: The inspectorHistory map is only accessed from the UI thread (renderImGui), so no locking needed

**Code Pattern**:
```cpp
const double currentTimeSec = juce::Time::getMillisecondCounterHiRes() / 1000.0;
auto historyKey = std::make_pair(srcPin.logicalId, srcPin.channel);

// Get the history entry
auto& history = inspectorHistory[historyKey];

// Update access time (for Phase 5 stale cleanup)
history.lastAccessTime = currentTimeSec;

// Add new sample
history.samples.push_back({currentTimeSec, liveValue});
```

### Phase 2: Clean Old Samples

**Location**: Same section, right after adding the new sample

**Changes**:
1. Calculate cutoff time: `double cutoffTime = currentTimeSec - inspectorWindowSeconds`
2. Remove samples older than cutoff: iterate deque and `pop_front()` until first sample's time >= cutoff
3. This keeps the deque size bounded and ensures we only analyze relevant samples

**Code Pattern**:
```cpp
double cutoffTime = currentTimeSec - inspectorWindowSeconds;
auto& samples = inspectorHistory[historyKey].samples;
while (!samples.empty() && samples.front().first < cutoffTime)
    samples.pop_front();
```

### Phase 3: Calculate Min/Max

**Location**: Before rendering tooltip, after cleaning history

**Changes**:
1. If history has samples (after cleanup):
   - Initialize `minVal = std::numeric_limits<float>::max()`, `maxVal = std::numeric_limits<float>::lowest()`
   - Iterate through samples in deque and track min/max
2. If history is empty (newly hovered or no samples in window):
   - Use live value for both: `minVal = maxVal = liveValue`
   - Or display "No data" if preferred

**Code Pattern**:
```cpp
float minVal = liveValue, maxVal = liveValue;
auto& samples = inspectorHistory[historyKey].samples;
if (!samples.empty())
{
    minVal = std::numeric_limits<float>::max();
    maxVal = std::numeric_limits<float>::lowest();
    for (const auto& [time, value] : samples)
    {
        minVal = std::min(minVal, value);
        maxVal = std::max(maxVal, value);
    }
}
```

**Note on Algorithm Choice**: The simple O(n) iteration is the correct choice here. While O(1) "running update" sounds faster, it's complex and would require full recalculation when the min/max sample is removed via `pop_front()`. Given that `n` is small (~300 samples for 5 seconds at 60Hz), iterating 300 floats is negligible and much more robust.

### Phase 4: Update Tooltip Display

**Location**: In `ImGui::BeginTooltip()` / `ImGui::EndTooltip()` section (line 3811-3816)

**Changes**:
1. Add min/max display after the live value
2. Format: `"Min: %.3f  Max: %.3f"` (matching existing style)
3. Optionally show number of samples: `"Samples: %d"` for debugging

**Code Pattern**:
```cpp
ImGui::BeginTooltip();
ImGui::Text("Value: %.3f", liveValue);
ImGui::Text("Min: %.3f  Max: %.3f", minVal, maxVal);
ImGui::Text("From: %s (ID %u)", srcName.toRawUTF8(), (unsigned)srcPin.logicalId);
if (srcLabel.isNotEmpty())
    ImGui::Text("Pin: %s", srcLabel.toRawUTF8());
ImGui::EndTooltip();
```

### Phase 5: Periodic Stale History Cleanup (Optimization)

**Location**: At the end of `renderImGui()`, after all cable inspector logic

**Purpose**: Clean up history entries for modules that were deleted or cables that haven't been hovered in a long time (prevents memory leak)

**Implementation Steps**:

1. **Modify `ChannelHistory` struct** (in `ImGuiNodeEditorComponent.h` line 297):
   ```cpp
   struct ChannelHistory 
   { 
       std::deque<std::pair<double, float>> samples; 
       double lastAccessTime = 0.0; // Track when this history was last accessed
   };
   ```

2. **Update Phase 1 Logic**: The `lastAccessTime` is already updated in Phase 1 (see code pattern above)

3. **Implement Periodic Cleanup**:
   ```cpp
   // At the end of renderImGui(), after cable inspector code
   static double lastCleanupTime = 0.0;
   const double currentTimeSec = juce::Time::getMillisecondCounterHiRes() / 1000.0;
   
   if (currentTimeSec - lastCleanupTime > 10.0) // Run every 10 seconds
   {
       lastCleanupTime = currentTimeSec;
       // Set cutoff for "stale" entries (2x the max window size = 40 seconds)
       const double staleCutoffTime = currentTimeSec - (20.0 * 2.0);
       
       for (auto it = inspectorHistory.begin(); it != inspectorHistory.end(); /* no increment */)
       {
           if (it->second.lastAccessTime < staleCutoffTime)
           {
               it = inspectorHistory.erase(it); // Erase stale entry
           }
           else
           {
               ++it;
           }
       }
   }
   ```

**Why This Approach**:
- Only cleans up entries that haven't been accessed in 40+ seconds (stale)
- Active entries are preserved regardless of window size
- No complex module-deletion tracking needed
- Runs infrequently (every 10 seconds) to minimize overhead

---

## Performance Considerations

### Memory Usage
- **Per Channel**: ~16 bytes per sample (8 bytes timestamp + 4 bytes float + deque overhead)
- **Example**: 60 FPS × 5 seconds = 300 samples per channel = ~5 KB per channel
- **With 100 channels**: ~500 KB total (acceptable)

### CPU Usage
- **History Addition**: O(1) - deque push_back
- **Cleanup**: O(n) where n = samples to remove (typically 1-10 per frame)
- **Min/Max Calculation**: O(m) where m = samples in window (typically 50-300)
- **Total per frame**: ~O(300) operations per hovered cable (negligible)

### Sampling Rate
- Currently samples every frame (~60 Hz)
- Could throttle to 30 Hz if needed: `static double lastSampleTime = 0; if (currentTimeSec - lastSampleTime > 0.033) { ... }`
- **Recommendation**: Keep at 60 Hz for smooth min/max updates

---

## Edge Cases to Handle

1. **New Cable Hovered**: History empty → show live value as min/max
2. **Window Size Changed**: Old samples remain until naturally expired (acceptable)
3. **Module Deleted**: History entry remains in map (memory leak risk - see Phase 5)
4. **Transport Paused**: Values still sampled, min/max still calculated (acceptable)
5. **Multiple Cables**: Each tracked independently via map key
6. **Negative Values**: Min/max calculation handles negative correctly

---

## Testing Checklist

### Core Functionality (Phases 1-4)
- [ ] Hover cable → see live value + min/max
- [ ] Change window slider → min/max updates after window period
- [ ] Hover different cables → independent histories
- [ ] Hover same cable multiple times → history accumulates correctly
- [ ] Let cable sit idle → old samples cleaned up (within window)
- [ ] Rapid value changes → min/max tracks correctly
- [ ] Negative values → min/max correct
- [ ] Very large values → min/max correct
- [ ] Performance: No frame drops with multiple cables hovered

### Memory Leak Prevention (Phase 5)
- [ ] **(Memory Leak Test)** Hover a cable, wait 5 seconds, then stop hovering. Wait for the stale cleanup period (~40-60 seconds) and confirm (via debugging) that the entry for that cable has been purged from the `inspectorHistory` map.
- [ ] Delete module → history entry eventually cleaned up by periodic cleanup
- [ ] Switch between many cables → old entries cleaned up after staleness period

---

## Code Integration Points

### File: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Primary Location**: Lines 3784-3820 (cable inspector tooltip section)

**Exact Insertion Point**: After line 3806 (get liveValue), before line 3810 (tooltip rendering)

**Total Code Addition**: ~20-30 lines

---

## Rollback Plan

If issues arise, the feature can be disabled by:
1. Commenting out history tracking (Phase 1)
2. Keeping tooltip as-is (current live value only)
3. Data structures remain unused (no harm)

---

## Implementation Order

### Recommended Order (Depth-First, Complete Feature First)

1. **Phase 1** (History Tracking) - Add sample to deque
2. **Phase 2** (Cleanup) - Remove old samples from current deque
3. **Phase 3** (Min/Max Calculation) - Calculate from current deque
4. **Phase 4** (Display) - Show live value + min/max in tooltip

This completes the entire user-facing feature in one go. You can test and validate it immediately.

5. **Phase 5** (Stale History Cleanup) - Periodic map-wide cleanup to prevent memory leaks

### Rationale

The depth-first approach completes the feature for a single cable first (Phases 1-4), allowing immediate testing and validation. Phase 5 can then be added as an optimization.

---

## Notes for Expert Review

1. **Thread Safety**: `inspectorHistory` is only accessed from UI thread (`renderImGui`), so no locks needed
2. **Timestamp Precision**: Using `getMillisecondCounterHiRes()` provides microsecond precision (sufficient)
3. **Deque Choice**: `std::deque` allows efficient push_back and pop_front (FIFO semantics)
4. **Map Key**: `(logicalId, channel)` uniquely identifies a signal source
5. **Min/Max Algorithm**: O(n) iteration is chosen over O(1) running updates because:
   - Running updates require full recalculation when min/max sample is removed
   - `n` is small (~300 samples) making iteration negligible
   - Simpler code is more robust and easier to debug
6. **Alternative Approaches Considered**:
   - Circular buffer with fixed size (more predictable memory) - rejected: less flexible
   - Sample throttling to reduce history size - rejected: reduces responsiveness
   - `lastAccessTime` tracking (chosen) - solves memory leak elegantly

---

## Success Criteria

✅ **Complete**: Tooltip shows min/max when hovering cables  
✅ **Responsive**: Updates smoothly as values change  
✅ **Configurable**: Window slider adjusts time range  
✅ **Performant**: No noticeable frame drops  
✅ **Memory Safe**: History bounded, old samples cleaned

---

**Status**: ✅ Expert Validated and Refined

### Expert Review Notes

**Validation**: Plan is sound, performant, and correct.

**Key Refinements Applied**:
1. ✅ Implementation order changed to depth-first (complete feature first: Phases 1-4, then Phase 5 optimization)
2. ✅ O(n) min/max algorithm confirmed as correct choice (over complex O(1) running updates)
3. ✅ Phase 5 enhanced with `lastAccessTime` tracking for targeted stale entry cleanup
4. ✅ Added memory leak test case to testing checklist

**Expert Reviewer Recommendations** (All Incorporated):
- **Depth-First Implementation**: Phases 1-4 complete the full user-facing feature, allowing immediate testing. Phase 5 is added as an optimization.
- **Algorithm Confirmation**: Stick with simple O(n) iteration for min/max - it's robust and fast enough for ~300 samples. O(1) running updates would require full recalculation anyway when the min/max sample is removed.
- **Enhanced Phase 5**: `lastAccessTime` field added to `ChannelHistory` struct, updated on each access, used for targeted cleanup of stale entries (not accessed in 40+ seconds).
- **Additional Test Case**: Memory leak test added to verify stale entries are purged correctly.

