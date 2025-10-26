# Multi Sequencer Scaling Issue - Diagnostic Analysis

## Current State

### What SHOULD Be Happening
- Node width: Fixed at `240px` (`nodeContentWidth` constant)
- Internal layout: Calculated responsive widths based on `itemWidth` parameter
- VSlider width: `sliderW = (itemWidth - spacing * (shown - 1)) / shown`
- No position-dependent behavior

### Code Analysis

#### 1. MultiSequencerModuleProcessor::drawParametersInNode()

```cpp
void drawParametersInNode(float itemWidth, ...)
{
    // itemWidth = 240.0f (passed from ImGuiNodeEditorComponent.cpp line 1619)
    
    // Steps slider - uses itemWidth directly
    ImGui::PushItemWidth(itemWidth);  // ✅ CORRECT
    ImGui::SliderInt("Steps", &displayedSteps, 1, boundMaxUi);
    ImGui::PopItemWidth();
    
    // VSlider grid - calculates width from itemWidth
    const int shown = displayedSteps;  // e.g., 8 steps
    const float spacing = 4.0f;
    const float sliderW = (itemWidth - spacing * (shown - 1)) / (float)shown;
    // For 8 steps: sliderW = (240 - 4*7) / 8 = (240 - 28) / 8 = 212 / 8 = 26.5px
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    ImGui::PushItemWidth(sliderW);
    
    for (int i = 0; i < shown; ++i) {
        if (i > 0) ImGui::SameLine();
        // EXPLICIT SIZE - should be stable!
        ImGui::VSliderFloat("##s", ImVec2(sliderW, 60.0f), ...);
    }
    
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
}
```

**Analysis**: This code looks CORRECT. VSliders have explicit `ImVec2(sliderW, 60.0f)` sizing where `sliderW` is derived from the stable `itemWidth` parameter.

#### 2. Pin Drawing (ImGuiNodeEditorComponent.cpp)

```cpp
// Line 2063-2067 (drawAudioOutputPin)
const float nodeWidth = ImNodes::GetNodeDimensions(lid).x;  // ← Queries current width
const float textColumnWidth = nodeWidth - pinColumnWidth - spacing;
ImGui::SetColumnWidth(0, textColumnWidth);
```

**Analysis**: This COULD cause feedback if the node width changes, but it shouldn't CAUSE the node to change width - it's a response, not a cause.

#### 3. ImNodes Node Sizing

ImNodes calculates node dimensions based on:
- Content bounding box (all widgets drawn inside)
- Title bar
- Padding/margins

**The node should be as wide as its widest content element.**

### Hypothesis: What Could Cause Position-Dependent Scaling?

#### Theory 1: Canvas Coordinate System Precision
- ImGui uses screen-space coordinates
- Moving a node far to the right increases X coordinates
- Floating-point precision issues?
- **UNLIKELY** - Would need to be thousands of pixels away

#### Theory 2: Hidden GetContentRegionAvail() Call
- Some widget is querying available space dynamically
- Need to check EVERY widget in the chain
- **POSSIBLE**

#### Theory 3: ImNodes Internal Bug
- ImNodes itself has position-dependent behavior
- Check ImNodes version/known issues
- **POSSIBLE**

#### Theory 4: Button Width Calculation
Lines 587-588:
```cpp
if (ImGui::Button("Connect to Samplers", ImVec2(itemWidth, 0))) { ... }
if (ImGui::Button("Connect to PolyVCO", ImVec2(itemWidth, 0))) { ... }
```
These buttons use `ImVec2(itemWidth, 0)` - the `0` means "auto height". Could this be causing issues?
**UNLIKELY** - Height shouldn't affect width

#### Theory 5: Checkbox Padding (Line 578)
```cpp
if (used < sliderW) { 
    ImGui::SameLine(0.0f, 0.0f); 
    ImGui::Dummy(ImVec2(sliderW - used, 0.0f)); 
}
```
This tries to pad checkboxes to match slider width. Could this create a feedback loop?
**POSSIBLE** - But why would it be position-dependent?

### Diagnostic Steps

1. **Add Debug Logging**
   ```cpp
   // At start of drawParametersInNode()
   juce::Logger::writeToLog(
       "MultiSeq itemWidth=" + juce::String(itemWidth) + 
       " shown=" + juce::String(shown) +
       " sliderW=" + juce::String(sliderW));
   ```

2. **Check Node Dimensions**
   ```cpp
   // After all content is drawn, before end of node
   ImVec2 nodeDims = ImNodes::GetNodeDimensions(lid);
   juce::Logger::writeToLog(
       "MultiSeq final nodeDims: " + juce::String(nodeDims.x) + 
       "x" + juce::String(nodeDims.y));
   ```

3. **Verify ItemWidth is Constant**
   - Confirm that `nodeContentWidth` (line 1619 in ImGuiNodeEditorComponent.cpp) is truly `240.0f`
   - Check if it's calculated differently for different nodes

4. **Test Without Custom Spacing**
   - Comment out `PushStyleVar(ImGuiStyleVar_ItemSpacing, ...)`
   - Use default spacing
   - Does problem persist?

5. **Test With Fixed Step Count**
   - Hardcode `shown = 8`
   - Does changing step count affect the issue?

### Expected vs. Actual

**Expected**:
- Node position (100, 100): Width = 240px
- Node position (2000, 100): Width = 240px
- No difference

**Actual** (as reported by user):
- "scaling according to it's position is stupid"
- Implies width changes based on X coordinate

### Next Steps

**USER: Please provide specific details**:
1. At X position 0-500: What is the node width?
2. At X position 1500-2000: What is the node width?
3. Does it scale continuously or jump at certain positions?
4. Does it affect ALL nodes or just MultiSequencer?
5. Does FrequencyGraph have the same issue?
6. Do simple nodes (VCO, BestPractice) have this issue?

This will help identify the root cause.

