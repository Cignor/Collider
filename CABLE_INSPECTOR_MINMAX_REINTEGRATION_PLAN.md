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
inspectorHistory[historyKey].samples.push_back({currentTimeSec, liveValue});
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
if (!inspectorHistory[historyKey].samples.empty())
{
    minVal = std::numeric_limits<float>::max();
    maxVal = std::numeric_limits<float>::lowest();
    for (const auto& [time, value] : inspectorHistory[historyKey].samples)
    {
        minVal = std::min(minVal, value);
        maxVal = std::max(maxVal, value);
    }
}
```

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

### Phase 5: Periodic Cleanup (Optional Optimization)

**Location**: Add a cleanup function called periodically or when window changes

**Purpose**: Prevent unbounded growth if many cables are hovered over time

**Implementation**:
- Option A: Clean all history entries periodically (e.g., every 5 seconds)
- Option B: Clean entries not accessed in last `inspectorWindowSeconds * 2`
- Option C: Limit max samples per channel (e.g., 1000 samples max)

**Recommendation**: Start with Option A - simple periodic cleanup in the render loop.

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

- [ ] Hover cable → see live value + min/max
- [ ] Change window slider → min/max updates after window period
- [ ] Hover different cables → independent histories
- [ ] Hover same cable multiple times → history accumulates correctly
- [ ] Let cable sit idle → old samples cleaned up
- [ ] Rapid value changes → min/max tracks correctly
- [ ] Negative values → min/max correct
- [ ] Very large values → min/max correct
- [ ] Delete module → history cleaned up (if Phase 5 implemented)
- [ ] Performance: No frame drops with multiple cables hovered

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

1. **Phase 1** (History Tracking) - Core functionality
2. **Phase 4** (Display) - Immediate user feedback  
3. **Phase 2** (Cleanup) - Prevent memory issues
4. **Phase 3** (Min/Max) - Complete feature
5. **Phase 5** (Periodic Cleanup) - Polish/optimization

---

## Notes for Expert Review

1. **Thread Safety**: `inspectorHistory` is only accessed from UI thread (`renderImGui`), so no locks needed
2. **Timestamp Precision**: Using `getMillisecondCounterHiRes()` provides microsecond precision (sufficient)
3. **Deque Choice**: `std::deque` allows efficient push_back and pop_front (FIFO semantics)
4. **Map Key**: `(logicalId, channel)` uniquely identifies a signal source
5. **Alternative Approaches**:
   - Circular buffer with fixed size (more predictable memory)
   - Separate min/max tracking with running updates (O(1) instead of O(n))
   - Sample throttling to reduce history size

---

## Success Criteria

✅ **Complete**: Tooltip shows min/max when hovering cables  
✅ **Responsive**: Updates smoothly as values change  
✅ **Configurable**: Window slider adjusts time range  
✅ **Performant**: No noticeable frame drops  
✅ **Memory Safe**: History bounded, old samples cleaned

---

**Status**: Ready for Expert Review and Validation

