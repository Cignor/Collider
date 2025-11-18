# ImGui Child Window Visualization Fix Guide

## Problem Statement

Several module visualizations (`NoiseModuleProcessor`, `RandomModuleProcessor`, and others) were experiencing critical ImGui state stack errors when rendering custom child windows:

- **"Calling PopID() too many times!"** (repeated 3+ times)
- **"Calling PopItemWidth() too many times!"**
- **"Must call EndChild() and not End()!"**
- **"Missing PopID()"** (repeated multiple times)
- **"Missing End()"**
- **"Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries"**

These errors caused:
1. Minimap corruption when nodes were moved off-screen
2. UI becoming unresponsive
3. Potential crashes from ImGui state corruption

## Root Cause Analysis

The working reference implementation (`VCOModuleProcessor`) uses a specific pattern for child window visualization that ensures proper ImGui stack balance. The broken modules deviated from this pattern in several critical ways:

### Key Differences (Broken vs. Working)

| Issue | Broken Pattern | Working Pattern (VCO) |
|-------|---------------|----------------------|
| **Child ID** | Uses `##` prefix (`"##NoiseViz"`) | No prefix (`"VCOOscilloscope"`) |
| **Coordinate System** | Uses `GetWindowSize()` to get dynamic child size | Uses `graphSize` calculated from `itemWidth` + `waveHeight` |
| **Position Calculation** | `vizOrigin = GetWindowPos()`, `vizSize = GetWindowSize()` | `p0 = GetWindowPos()`, `p1 = p0 + graphSize` |
| **Data Reading** | Inside `BeginChild()` block | Before `BeginChild()` call |
| **Conditional Rendering** | Wraps clip rect ops in `if (validCoords)` | Always executes clip rect operations |
| **Text Rendering** | Uses `ImGui::Text()` or `ImGui::Spacing()` | Uses `ImGui::TextColored()` with `SetCursorPos()` |
| **InvisibleButton Size** | Uses `itemWidth` and `vizHeight` | Uses `graphSize` |

### Why This Matters

1. **`GetWindowSize()` inside child windows** can return inconsistent values, especially when the window is being scrolled or resized. Using a pre-calculated `graphSize` ensures consistency.

2. **Conditional clip rect operations** can cause stack imbalance if coordinates are invalid—ImGui expects every `PushClipRect` to have a matching `PopClipRect`.

3. **Reading data inside the child window** can interfere with ImGui's layout calculations, especially if the data affects window sizing.

4. **Using `##` prefix on child IDs** combined with `PushID(this)` can cause ID collisions when multiple instances exist, leading to stack imbalances.

5. **Placing `EndChild()` inside the `if (BeginChild())` block** causes stack imbalance. `EndChild()` must always be called OUTSIDE the conditional block, even if `BeginChild()` returns false.

6. **Child windows outside PushID scope** using `##` prefix can cause ID collisions. Use descriptive names without `##` when the child window is created after `PopID()`.

## Solution: Match VCO Pattern Exactly

### Step-by-Step Fix Strategy

1. **Move data reading before `BeginChild()`**
   - Read all `VizData` atomic values into local arrays/variables
   - Do this BEFORE the `PushID(this)` or immediately after

2. **Use `graphSize` consistently**
   - Calculate: `const ImVec2 graphSize(itemWidth, waveHeight);`
   - Use `graphSize` for `BeginChild()` size parameter
   - Use `graphSize` for `InvisibleButton()` size
   - Use `graphSize.x` and `graphSize.y` for all coordinate calculations

3. **Calculate positions from `graphSize`**
   - `const ImVec2 p0 = ImGui::GetWindowPos();`
   - `const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);`
   - NEVER use `GetWindowSize()` inside the child

4. **Remove conditional clip rect operations**
   - Always call `drawList->PushClipRect(p0, p1, true);`
   - Always call `drawList->PopClipRect();` before `EndChild()`
   - Remove any `if (validCoords)` blocks around clip operations

5. **Remove `##` prefix from child ID**
   - Use descriptive names: `"NoiseViz"`, `"RandomViz"` (not `"##NoiseViz"`)
   - The `PushID(this)` already provides unique scoping

6. **Use `TextColored` with `SetCursorPos()` for overlays**
   - Position text with: `ImGui::SetCursorPos(ImVec2(4, 4));`
   - Render with: `ImGui::TextColored(ImVec4(...), "text");`
   - This prevents boundary extension errors

7. **Match the exact structure**
   ```
   ImGui::PushID(this);
   
   // Read data here (before BeginChild)
   
   const ImVec2 graphSize(itemWidth, waveHeight);
   if (ImGui::BeginChild("Name", graphSize, false, flags))
   {
       ImDrawList* drawList = ImGui::GetWindowDrawList();
       const ImVec2 p0 = ImGui::GetWindowPos();
       const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
       
       // Background
       drawList->AddRectFilled(p0, p1, bgColor);
       
       // Clip
       drawList->PushClipRect(p0, p1, true);
       
       // Drawing operations...
       
       drawList->PopClipRect();
       
       // Text overlay
       ImGui::SetCursorPos(ImVec2(4, 4));
       ImGui::TextColored(...);
       
       // Invisible drag blocker
       ImGui::SetCursorPos(ImVec2(0, 0));
       ImGui::InvisibleButton("##drag", graphSize);
   }
   ImGui::EndChild();  // CRITICAL: Must be OUTSIDE the if block!
   
   ImGui::PopID();
   ```

8. **Child windows outside PushID scope**
   - If a child window is created outside the `PushID(this)` scope (e.g., after `PopID()`), use a descriptive name WITHOUT the `##` prefix
   - Example: `"VocalTractReadouts"` instead of `"##readouts"`
   - The `##` prefix is only safe when used within a `PushID` scope

### Critical Rules

1. **Never call `GetWindowSize()` inside a child window** - always use pre-calculated `graphSize`

2. **Always match Push/Pop operations** - every `PushClipRect` must have a `PopClipRect`, even if coordinates seem invalid

3. **Read data before `BeginChild()`** - don't perform any data reading or heavy computation inside the child window

4. **Use `graphSize` for everything** - child size, button size, all coordinate calculations should derive from the same `graphSize` constant

5. **Text positioning must use `SetCursorPos()`** - relative positioning prevents boundary extension errors

6. **`EndChild()` must be OUTSIDE the `if (BeginChild())` block** - This is critical! The pattern is:
   ```cpp
   if (ImGui::BeginChild(...))
   {
       // ... content ...
   }
   ImGui::EndChild();  // OUTSIDE the if block!
   ```
   Placing `EndChild()` inside the `if` block can cause stack imbalance errors.

7. **Child windows outside PushID scope** - If a child window is created after `PopID()`, use a descriptive name WITHOUT `##` prefix to avoid ID collisions

## Example: NoiseModuleProcessor Fix

### Before (Broken)
```cpp
if (ImGui::BeginChild("##NoiseViz", ImVec2(itemWidth, vizHeight), false, childFlags))
{
    const ImVec2 vizOrigin = ImGui::GetCursorScreenPos();
    const float waveHeight = 110.0f;
    const ImVec2 rectMax = ImVec2(vizOrigin.x + itemWidth, vizOrigin.y + waveHeight);
    
    if (validCoords && ...)  // Conditional!
    {
        drawList->PushClipRect(origin, rectMax, true);
        // ... drawing ...
        drawList->PopClipRect();
    }
    
    ImGui::Spacing();  // Wrong positioning
    ImGui::Text(...);  // Not TextColored
}
```

### After (Working)
```cpp
// Read data BEFORE BeginChild
float outputWave[VizData::waveformPoints];
for (int i = 0; i < VizData::waveformPoints; ++i)
    outputWave[i] = vizData.outputWaveform[i].load();

const float waveHeight = 110.0f;
const ImVec2 graphSize(itemWidth, waveHeight);  // Use graphSize

if (ImGui::BeginChild("NoiseViz", graphSize, false, childFlags))  // No ##
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);  // From graphSize
    
    drawList->AddRectFilled(p0, p1, bgColor);
    drawList->PushClipRect(p0, p1, true);  // Always execute
    
    // ... drawing operations ...
    
    drawList->PopClipRect();  // Always execute
    
    ImGui::SetCursorPos(ImVec2(4, 4));  // Window-space positioning
    ImGui::TextColored(ImVec4(...), "text");
    
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##noiseVizDrag", graphSize);  // Use graphSize
}
ImGui::EndChild();
```

## Testing Checklist

After applying the fix, verify:

1. ✅ **No ImGui errors** - Check console/log for stack balance errors
2. ✅ **Minimap stability** - Move nodes off-screen and verify minimap doesn't corrupt
3. ✅ **Multiple instances** - Create 2-3 instances of the same module and verify all render correctly
4. ✅ **Scrolling** - Scroll the canvas and verify visualizations remain stable
5. ✅ **Resizing** - Resize the node editor window and verify no errors

## Modules Fixed Using This Pattern

- ✅ `NoiseModuleProcessor` - Verified working
- ✅ `RandomModuleProcessor` - Verified working
- ✅ `FunctionGeneratorModuleProcessor` - Verified working (fixed EndChild placement, phase reading, InvisibleButton naming)
- ✅ `VCAModuleProcessor` - Verified working
- ✅ `MixerModuleProcessor` - Verified working
- ✅ `CVMixerModuleProcessor` - Verified working
- ✅ `LagProcessorModuleProcessor` - Verified working
- ✅ `QuantizerModuleProcessor` - Verified working
- ✅ `RateModuleProcessor` - Verified working
- ✅ `LogicModuleProcessor` - Verified working
- ✅ `SequentialSwitchModuleProcessor` - Verified working
- ✅ `VocalTractFilterModuleProcessor` - Verified working (fixed readouts child window ID naming)

## Reference Implementation

Always compare against `VCOModuleProcessor::drawParametersInNode()` in `juce/Source/audio/modules/VCOModuleProcessor.h` (lines 300-393). This is the canonical working implementation.

