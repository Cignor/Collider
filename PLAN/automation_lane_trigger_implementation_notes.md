# Automation Lane Trigger Feature - Implementation Notes

## Key Patterns from Reference Modules

### 1. StrokeSequencerModuleProcessor Pattern

**Edge Detection Method: Line Segment Intersection**
- Uses geometric line segment crossing detection
- Tracks previous position and current position
- Checks if the line segment from (prevX, prevY) to (currX, currY) crosses a horizontal threshold line
- Very precise - detects crossings even when value changes between samples

**Key Code Pattern:**
```cpp
bool lineSegmentCrossesHorizontalLine(float x1, float y1, float x2, float y2, float lineY) const
{
    // Check if line segment from (x1, y1) to (x2, y2) crosses horizontal line at y = lineY
    const float d1 = y1 - lineY;
    const float d2 = y2 - lineY;
    if (d1 * d2 < 0.0f)  // Opposite signs = crossing
        return true;
    return false;
}
```

**Trigger Logic:**
- One trigger per segment per loop (uses `m_hasTriggeredThisSegment` flag)
- Requires "priming" - must be on-stroke for at least one sample before triggers can fire
- Stores previous Y position for comparison
- Triggers fire instantly when crossing detected (no pulse duration tracking in this example)

**Visualization:**
- Draws threshold lines with gradients below them
- Uses theme colors with fallbacks
- Line position: `canvas_p0.y + (1.0f - liveThresholds[t]) * canvas_size.y`
- Line thickness: 2.0f pixels
- Includes gradient zones for visual feedback

### 2. FrequencyGraphModuleProcessor Pattern

**Edge Detection Method: State Comparison**
- Simple comparison: current value vs threshold
- Tracks previous gate state (`lastGateState`)
- Detects rising edge: `gateState && !lastGateState`

**Key Code Pattern:**
```cpp
bool gateState = bandEnergyDb[band] > bandAnalysers[band].thresholdDb;
if (gateState && !bandAnalysers[band].lastGateState)  // Rising edge
{
    bandAnalysers[band].triggerSamplesRemaining = (int)(sampleRate * 0.001);  // 1ms pulse
}
bandAnalysers[band].lastGateState = gateState;
```

**Trigger Output:**
- Uses pulse duration counter (`triggerSamplesRemaining`)
- 1ms pulse = `(int)(sampleRate * 0.001)` samples
- Counter decrements each sample in the output loop
- Output is 1.0f when counter > 0, else 0.0f

**Visualization:**
- Simple horizontal lines drawn at threshold positions
- Uses `juce::jmap()` to convert dB to Y coordinate
- Line color from theme with fallback

### 3. FunctionGeneratorModuleProcessor Pattern

**Threshold Line Drawing:**
```cpp
// Draw Trigger Threshold line (Red)
const float trig_line_y = canvas_p0.y + (1.0f - trigThresh) * graphSize.y;
draw_list->AddLine(ImVec2(canvas_p0.x, trig_line_y), 
                   ImVec2(canvas_p1.x, trig_line_y), 
                   IM_COL32(255, 0, 0, 200), 2.0f);
```

**Key Observations:**
- Simple single-line drawing (no gradients)
- Color: Red with transparency (200 alpha)
- Thickness: 2.0f
- Position calculation: `(1.0f - threshold)` inverts Y (0 at top, 1 at bottom)

## Recommended Approach for Automation Lane

### Edge Detection: Line Segment Intersection (like StrokeSequencer)

**Why:**
- More precise than simple state comparison
- Handles rapid value changes gracefully
- Already have previous/current position tracking in automation lane
- Works well with continuous automation curves

**Implementation:**
```cpp
bool lineSegmentCrossesThreshold(float prevBeat, float prevValue, 
                                  float currBeat, float currValue, 
                                  float threshold, int edgeMode) const
{
    // Rising edge: crossing from below to above
    // Falling edge: crossing from above to below
    // Both: either direction
    
    const float d1 = prevValue - threshold;
    const float d2 = currValue - threshold;
    
    if (edgeMode == 0) // Rising
        return (d1 < 0.0f && d2 >= 0.0f);
    else if (edgeMode == 1) // Falling
        return (d1 > 0.0f && d2 <= 0.0f);
    else // Both
        return (d1 * d2 < 0.0f);  // Opposite signs
}
```

### Trigger Output: Pulse Duration Counter (like FrequencyGraph)

**Why:**
- Standard pattern for trigger outputs
- Easy to implement and debug
- 1ms is standard pulse duration
- Counter decrements in same loop as output writing

**Implementation:**
```cpp
// State variables
int triggerPulseRemaining = 0;
bool lastValueAboveThreshold = false;

// In processBlock, per sample:
if (crossingDetected)
{
    triggerPulseRemaining = (int)(sampleRate * 0.001);  // 1ms
}

// Output
if (outTrigger)
    outTrigger[i] = (triggerPulseRemaining > 0) ? 1.0f : 0.0f;

if (triggerPulseRemaining > 0)
    --triggerPulseRemaining;

// Update state
lastValueAboveThreshold = (currentValue > threshold);
```

### Visualization: Simple Line (like FunctionGenerator)

**Why:**
- Automation lane already has complex drawing
- Simple line is cleaner for single threshold
- Can add gradient later if needed
- Matches existing playhead style

**Implementation:**
```cpp
// After grid lines, before curve drawing
const float thresholdY = p0.y + graphSize.y * (1.0f - triggerThreshold);
const ImU32 thresholdColor = IM_COL32(255, 150, 0, 200);  // Orange/amber
drawList->AddLine(ImVec2(p0.x, thresholdY), 
                  ImVec2(p1.x, thresholdY), 
                  thresholdColor, 2.0f);
```

**Position Calculation:**
- `1.0f - threshold` inverts Y (0 at top, 1 at bottom in canvas)
- Multiplied by `graphSize.y` to scale to canvas height
- Added to `p0.y` (top of canvas)

### State Tracking Needed

**Per-Sample State:**
- `float previousValue` - previous automation value
- `double previousBeat` - previous beat position (already tracked for drawing)
- `bool lastValueAboveThreshold` - previous threshold state
- `int triggerPulseRemaining` - counter for pulse duration

**Initialization:**
- Initialize `lastValueAboveThreshold` based on first sample value vs threshold
- Prevents false trigger on first processBlock

### Edge Cases to Handle

1. **Loop Boundaries**: Reset trigger state when loop wraps (check `justWrapped` or similar)
2. **Transport Stopped**: Don't fire triggers when not playing
3. **Threshold at Extremes**: Handle gracefully (0.0 and 1.0)
4. **Multiple Crossings**: Only fire once per crossing (use flag if needed)

## Implementation Checklist

- [ ] Add threshold parameter (0.0-1.0, default 0.5)
- [ ] Add edge mode parameter (Rising/Falling/Both)
- [ ] Update output bus to 5 channels
- [ ] Add OUTPUT_TRIGGER enum value
- [ ] Implement line segment crossing detection function
- [ ] Add state variables for tracking
- [ ] Implement trigger logic in processBlock
- [ ] Add pulse duration counter
- [ ] Write trigger output
- [ ] Draw threshold line in UI
- [ ] Add threshold slider in UI
- [ ] Add edge selection combo in UI
- [ ] Update pin labels
- [ ] Test edge detection accuracy
- [ ] Test pulse duration
- [ ] Test with looping
- [ ] Test with transport stop/start

## UI Layout Recommendation

```
[Division/Rate] [REC/EDIT] [Value Slider]
[Duration Mode] [Custom Duration]
[Trigger Threshold: 0.50] [Edge: Rising ▼]
[Timeline Canvas - with threshold line visible]
```

Threshold slider should be:
- Horizontal slider
- Range 0.0 to 1.0
- Format: "%.2f"
- Tooltip: "Threshold level for trigger output"

Edge combo should be:
- Small combo box
- Options: "Rising", "Falling", "Both"
- Tooltip: "Trigger on rising edge, falling edge, or both"

