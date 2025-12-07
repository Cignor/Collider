# Automation Lane Trigger Output Feature - Implementation Plan

## Overview
Add a trigger output to the Automation Lane node that fires when the automation curve crosses a user-defined threshold line. The trigger can fire on rising edges (curve crosses above threshold) or falling edges (curve crosses below threshold).

## 1. Requirements Analysis

### 1.1 Trigger Output
- **Output Pin**: Add a 5th output pin called "Trigger"
- **Signal Type**: Digital pulse (0.0 or 1.0)
- **Pulse Duration**: 1ms (0.001 * sampleRate samples)
- **Behavior**: Emits a 1.0 pulse when threshold is crossed, then returns to 0.0

### 1.2 Trigger Threshold
- **Parameter**: Float value from 0.0 to 1.0 (matches automation value range)
- **Parameter ID**: `paramIdTriggerThreshold` = "triggerThreshold"
- **Default Value**: 0.5 (middle of range)
- **CV Modulation**: Optional (can be added later)
- **Save/Load**: Stored in APVTS (automatically saved with preset)

### 1.3 Edge Detection
- **Rising Edge**: Trigger fires when automation value crosses FROM below TO above threshold
- **Falling Edge**: Trigger fires when automation value crosses FROM above TO below threshold
- **Edge Selection**: Parameter to choose "Rising", "Falling", or "Both"
- **Parameter ID**: `paramIdTriggerEdge` = "triggerEdge"
- **Default**: "Rising"

### 1.4 Threshold Line Visualization
- **Visual Indicator**: Horizontal line drawn on the timeline graph
- **Position**: Y position = `canvasPos.y + canvasSize.y * (1.0f - thresholdValue)`
- **Color**: Distinct from playhead (yellow) - could use orange/red (IM_COL32(255, 150, 0, 200) or similar)
- **Style**: 2.0f line thickness, semi-transparent
- **Visibility**: Always visible on timeline, moves as slider is adjusted

### 1.5 UI Controls
- **Threshold Slider**: Horizontal slider to adjust threshold position (0.0 to 1.0)
- **Edge Selection**: Combo box to choose Rising/Falling/Both
- **Placement**: Above the timeline canvas, near other controls
- **Tooltip**: Explain what the trigger does

## 2. Technical Implementation Details

### 2.1 Parameter Layout Changes

**Add to `createParameterLayout()`:**
```cpp
// Trigger threshold (0.0 to 1.0)
params.push_back(std::make_unique<juce::AudioParameterFloat>(
    paramIdTriggerThreshold, "Trigger Threshold",
    juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

// Trigger edge selection (Rising, Falling, Both)
juce::StringArray edgeChoices = {"Rising", "Falling", "Both"};
params.push_back(std::make_unique<juce::AudioParameterChoice>(
    paramIdTriggerEdge, "Trigger Edge", edgeChoices, 0));
```

### 2.2 Output Bus Changes

**Current**: 4 discrete output channels
- OUTPUT_VALUE = 0
- OUTPUT_INVERTED = 1
- OUTPUT_BIPOLAR = 2
- OUTPUT_PITCH = 3

**New**: 5 discrete output channels
- OUTPUT_VALUE = 0
- OUTPUT_INVERTED = 1
- OUTPUT_BIPOLAR = 2
- OUTPUT_PITCH = 3
- OUTPUT_TRIGGER = 4

**Update Constructor:**
```cpp
BusesProperties().withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)
```

### 2.3 Process Block Changes

**State Variables Needed:**
- `bool lastValueAboveThreshold` - Track previous sample's relationship to threshold
- `int triggerPulseRemaining` - Counter for pulse duration (0 = no pulse)

**Edge Detection Logic:**
```cpp
float currentValue = value; // From automation curve
float threshold = *triggerThresholdParam;
bool isAboveThreshold = currentValue > threshold;

// Detect edge based on selected mode
int edgeMode = (int)*triggerEdgeParam;
bool shouldFire = false;

if (edgeMode == 0) // Rising
    shouldFire = isAboveThreshold && !lastValueAboveThreshold;
else if (edgeMode == 1) // Falling
    shouldFire = !isAboveThreshold && lastValueAboveThreshold;
else // Both
    shouldFire = (isAboveThreshold != lastValueAboveThreshold);

if (shouldFire)
    triggerPulseRemaining = (int)(0.001 * sampleRate); // 1ms pulse

lastValueAboveThreshold = isAboveThreshold;

// Output trigger signal
if (outTrigger)
    outTrigger[i] = (triggerPulseRemaining > 0) ? 1.0f : 0.0f;
    
if (triggerPulseRemaining > 0)
    --triggerPulseRemaining;
```

### 2.4 UI Drawing Changes

**Add Controls (above timeline):**
```cpp
// Trigger Threshold Slider
ImGui::PushItemWidth(itemWidth - 10);
float trigThresh = *triggerThresholdParam;
if (ImGui::SliderFloat("Trigger Threshold", &trigThresh, 0.0f, 1.0f, "%.2f"))
{
    *triggerThresholdParam = trigThresh;
    apvts.getParameter(paramIdTriggerThreshold)
        ->setValueNotifyingHost(apvts.getParameterRange(paramIdTriggerThreshold).convertTo0to1(trigThresh));
}
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Threshold level for trigger output");

// Edge Selection Combo
ImGui::SameLine();
int edgeIndex = (int)*triggerEdgeParam;
const char* edges[] = {"Rising", "Falling", "Both"};
if (ImGui::Combo("##edge", &edgeIndex, edges, 3))
{
    *triggerEdgeParam = (float)edgeIndex;
    apvts.getParameter(paramIdTriggerEdge)
        ->setValueNotifyingHost(apvts.getParameterRange(paramIdTriggerEdge).convertTo0to1((float)edgeIndex));
}
ImGui::PopItemWidth();
```

**Draw Threshold Line (inside timeline child window, after grid lines, before curves):**
```cpp
// Draw trigger threshold line
const float thresholdY = p0.y + graphSize.y * (1.0f - trigThresh);
const ImU32 thresholdColor = IM_COL32(255, 150, 0, 200); // Orange
drawList->AddLine(ImVec2(p0.x, thresholdY), ImVec2(p1.x, thresholdY), thresholdColor, 2.0f);
```

**Update drawIoPins():**
```cpp
void drawIoPins(const NodePinHelpers& helpers) override
{
    helpers.drawAudioOutputPin("Value", OUTPUT_VALUE);
    helpers.drawAudioOutputPin("Inverted", OUTPUT_INVERTED);
    helpers.drawAudioOutputPin("Bipolar", OUTPUT_BIPOLAR);
    helpers.drawAudioOutputPin("Pitch", OUTPUT_PITCH);
    helpers.drawAudioOutputPin("Trigger", OUTPUT_TRIGGER); // NEW
}
```

**Update getAudioOutputLabel():**
```cpp
juce::String getAudioOutputLabel(int channel) const override
{
    switch (channel)
    {
        case OUTPUT_VALUE: return "Value";
        case OUTPUT_INVERTED: return "Inverted";
        case OUTPUT_BIPOLAR: return "Bipolar";
        case OUTPUT_PITCH: return "Pitch";
        case OUTPUT_TRIGGER: return "Trigger"; // NEW
        default: return {};
    }
}
```

## 3. File Changes Required

### 3.1 AutomationLaneModuleProcessor.h
- Add `OUTPUT_TRIGGER = 4` to enum
- Add `static constexpr auto paramIdTriggerThreshold = "triggerThreshold";`
- Add `static constexpr auto paramIdTriggerEdge = "triggerEdge";`
- Add parameter pointers: `std::atomic<float>* triggerThresholdParam{nullptr};` and `std::atomic<float>* triggerEdgeParam{nullptr};`
- Add state variables: `bool lastValueAboveThreshold{false};` and `int triggerPulseRemaining{0};`

### 3.2 AutomationLaneModuleProcessor.cpp
- Update constructor to set output bus to 5 channels
- Add parameters to `createParameterLayout()`
- Cache parameter pointers in constructor
- Add edge detection logic in `processBlock()`
- Add trigger output writing in `processBlock()`
- Add UI controls in `drawParametersInNode()`
- Add threshold line drawing in timeline visualization
- Update `drawIoPins()` and `getAudioOutputLabel()`

## 4. Implementation Order

1. **Step 1**: Add parameters to APVTS
   - Add parameter IDs to header
   - Add parameters to `createParameterLayout()`
   - Cache parameter pointers in constructor

2. **Step 2**: Update output bus configuration
   - Change from 4 to 5 channels
   - Add OUTPUT_TRIGGER enum value
   - Update pin drawing and labels

3. **Step 3**: Implement trigger logic in processBlock
   - Add state variables
   - Implement edge detection
   - Add pulse generation
   - Write trigger output

4. **Step 4**: Add UI controls
   - Add threshold slider
   - Add edge selection combo
   - Add threshold line visualization

5. **Step 5**: Test and verify
   - Test rising edge detection
   - Test falling edge detection
   - Test both edge detection
   - Verify pulse duration
   - Verify visualization updates

## 5. Visual Design Notes

### 5.1 Threshold Line
- **Color**: Orange/amber (IM_COL32(255, 150, 0, 200)) - distinct from yellow playhead
- **Thickness**: 2.0f pixels
- **Position**: Horizontal line across entire timeline width
- **Z-order**: Above grid lines, below automation curve (so curve can cross it visibly)

### 5.2 Control Layout
```
[Division/Rate] [REC/EDIT] [Value Slider]
[Duration Mode] [Custom Duration]
[Trigger Threshold Slider] [Edge: Rising/Falling/Both]
[Timeline Canvas with threshold line]
```

### 5.3 Pin Layout
- Output pins remain on right side
- Trigger pin added as 5th output
- Label: "Trigger"

## 6. Edge Cases and Considerations

### 6.1 Initialization
- `lastValueAboveThreshold` should be initialized based on first sample value vs threshold
- This prevents false triggers on first processBlock

### 6.2 Loop Boundaries
- When automation loops, edge detection should continue seamlessly
- No special handling needed if state persists across loops

### 6.3 Multiple Crossings in One Sample
- Edge detection is per-sample, so rapid oscillations may generate many triggers
- This is expected behavior for fast automation changes

### 6.4 Transport Stopped
- Trigger should not fire when transport is stopped
- Can check `m_currentTransport.isPlaying` before edge detection

### 6.5 Threshold at Extremes
- Threshold at 0.0: Only fires on falling edges when value goes below 0.0 (shouldn't happen, but handle gracefully)
- Threshold at 1.0: Only fires on rising edges when value goes above 1.0 (shouldn't happen, but handle gracefully)

## 7. Testing Checklist

- [ ] Trigger fires on rising edge when curve crosses threshold upward
- [ ] Trigger fires on falling edge when curve crosses threshold downward
- [ ] Trigger fires on both edges when "Both" mode selected
- [ ] Pulse duration is exactly 1ms (0.001 * sampleRate samples)
- [ ] Threshold slider updates line position in real-time
- [ ] Edge selection combo updates behavior immediately
- [ ] Threshold line is visible and positioned correctly
- [ ] Trigger output appears in pin list
- [ ] Parameter is saved/loaded with preset
- [ ] No crashes with extreme threshold values (0.0, 1.0)
- [ ] Works correctly when transport stops/starts
- [ ] Works correctly with looping enabled/disabled

## 8. Future Enhancements (Out of Scope)

- CV modulation of threshold
- Adjustable pulse duration (currently fixed at 1ms)
- Trigger counter/rate display
- Multiple threshold levels
- Hysteresis (different thresholds for rising vs falling)

