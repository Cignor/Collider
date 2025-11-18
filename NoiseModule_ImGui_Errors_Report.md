# NoiseModuleProcessor ImGui State Stack Error Report

## Executive Summary

The NoiseModuleProcessor's `drawParametersInNode` function has **ImGui state stack mismatches** that are causing multiple runtime errors. The issues stem from improper use of `SetCursorPos()` inside a child window and potential early returns that could skip cleanup code.

---

## Errors Observed (from screenshot)

1. **"Calling PopItemWidth() too many times!"** (in NoiseViz window)
2. **"Calling PopID() too many times!"** (4 instances, in NoiseViz window)
3. **"Must call EndChild() and not End()!"** (in scrolling_region)
4. **"Missing PopID()"** (3 instances, in scrolling_region)
5. **"Missing End()"** (in Preset Creator window)
6. **"Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries. Please submit an item e.g. Dummy() afterwards in order to grow window/parent boundaries."** (in Preset Creator window)

---

## Root Cause Analysis

### Issue 1: SetCursorPos() Inside Child Window (CRITICAL)

**Location:** Lines 336-344 in `NoiseModuleProcessor.cpp`

**Problem:**
```cpp
if (ImGui::BeginChild("##NoiseViz", ...))
{
    // ... validation and drawing code ...
    if (actualWidth > 0.0f && actualHeight > 0.0f && validOrigin && validClip && !isWindowCollapsed)
    {
        // ... drawing code ...
        
        // Display live parameter values - positioned below waveform
        // Use SetCursorPos (relative to child window) instead of SetCursorScreenPos
        ImGui::SetCursorPos(ImVec2(4.0f, waveHeight + 4.0f));  // ❌ PROBLEM
        const float liveLevelDb = vizData.currentLevelDb.load();
        const float liveRms = vizData.outputRms.load();
        const char* colourNames[] = { "White", "Pink", "Brown" };
        const char* currentColourName = (currentColour >= 0 && currentColour < 3) ? colourNames[currentColour] : "Unknown";
        ImGui::Text("%s Noise  |  Level: %.1f dB  |  RMS: %.3f", currentColourName, liveLevelDb, liveRms);
        // Add Dummy() after SetCursorPos() to tell ImGui we've used the space (prevents boundary extension error)
        ImGui::Dummy(ImVec2(0.0f, 0.0f));  // ⚠️ Insufficient
    }
    ImGui::EndChild();
}
```

**Why this is wrong:**
1. **SetCursorPos() inside child windows** requires careful handling. Even with `Dummy()`, positioning text manually after drawing operations can confuse ImGui's layout system.
2. The text is positioned **inside a conditional block** (`if (actualWidth > 0.0f ...)`), meaning it may not always execute, causing inconsistent layout.
3. According to the NodeVisualizationGuide.md (Section 3), child windows should use normal cursor flow rather than manual positioning when possible.
4. The `Dummy(ImVec2(0.0f, 0.0f))` doesn't actually reserve space - it needs a non-zero size to be effective.

**Reference:** See `DriveModuleProcessor::drawParametersInNode` (lines 273-274) which uses:
```cpp
ImGui::SetCursorScreenPos(ImVec2(origin.x, rectMax.y));
ImGui::Dummy(ImVec2(itemWidth, 0));  // ✅ Non-zero width
```

---

### Issue 2: Conditional Text Positioning

**Problem:** The live parameter text (noise type, level, RMS) is placed inside the `if (actualWidth > 0.0f ...)` validation block. This means:
- When the window is out of view or invalid, the text doesn't render
- The child window's layout becomes inconsistent
- ImGui's boundary calculation gets confused

**Solution:** Move the text outside the validation block, using normal cursor flow (Spacing + Text) rather than SetCursorPos.

---

### Issue 3: Stack Balance Verification

**Current Stack Structure:**
```
Line 187: PushID(this)         ✅
Line 188: PushItemWidth(...)   ✅
...
Line 233: BeginChild(...)      ✅
...
Line 346: EndChild()           ✅
...
Line 374: PopItemWidth()       ✅
Line 375: PopID()              ✅
```

**Analysis:** The push/pop structure appears balanced at first glance. However, the errors suggest that:
- Extra PopID() calls are happening (4 instances)
- Extra PopItemWidth() calls are happening

**Hypothesis:** When the validation fails (`if (actualWidth > 0.0f ...)`), the code path might be skipping expected operations, or the SetCursorPos/Dummy sequence is causing ImGui to internally push/pop state that doesn't match.

---

## Comparison with Working Reference Implementation

### DriveModuleProcessor (Working)

```cpp
void DriveModuleProcessor::drawParametersInNode(...)
{
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);
    
    // ... UI elements ...
    
    // Uses SetCursorScreenPos OUTSIDE child window context
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, rectMax.y));  // ✅ Outside child
    ImGui::Dummy(ImVec2(itemWidth, 0));  // ✅ Non-zero size
    
    // ... more UI elements ...
    
    ImGui::PopItemWidth();
    ImGui::PopID();
}
```

**Key differences:**
1. DriveModule doesn't use `BeginChild()` for its visualization
2. Uses `SetCursorScreenPos()` (screen-space) rather than `SetCursorPos()` (window-space)
3. Uses `Dummy()` with non-zero width to reserve space

---

## Recommended Fixes

### Fix 1: Remove SetCursorPos, Use Normal Cursor Flow

**Change lines 336-343:**

**FROM:**
```cpp
// Display live parameter values - positioned below waveform
// Use SetCursorPos (relative to child window) instead of SetCursorScreenPos
ImGui::SetCursorPos(ImVec2(4.0f, waveHeight + 4.0f));
const float liveLevelDb = vizData.currentLevelDb.load();
const float liveRms = vizData.outputRms.load();
const char* colourNames[] = { "White", "Pink", "Brown" };
const char* currentColourName = (currentColour >= 0 && currentColour < 3) ? colourNames[currentColour] : "Unknown";
ImGui::Text("%s Noise  |  Level: %.1f dB  |  RMS: %.3f", currentColourName, liveLevelDb, liveRms);
// Add Dummy() after SetCursorPos() to tell ImGui we've used the space (prevents boundary extension error)
ImGui::Dummy(ImVec2(0.0f, 0.0f));
```

**TO:**
```cpp
// Display live parameter values - positioned below waveform
// Use normal cursor flow with Spacing to position text (avoids SetCursorPos state issues)
ImGui::Spacing();
const float liveLevelDb = vizData.currentLevelDb.load();
const float liveRms = vizData.outputRms.load();
const char* colourNames[] = { "White", "Pink", "Brown" };
const char* currentColourName = (currentColour >= 0 && currentColour < 3) ? colourNames[currentColour] : "Unknown";
ImGui::Text("%s Noise  |  Level: %.1f dB  |  RMS: %.3f", currentColourName, liveLevelDb, liveRms);
```

**Rationale:** This follows the NodeVisualizationGuide.md recommendation to use normal cursor flow inside child windows rather than manual positioning.

---

### Fix 2: Move Text Outside Validation Block

**Restructure the child window code:**

**FROM:**
```cpp
if (ImGui::BeginChild("##NoiseViz", ImVec2(itemWidth, vizHeight), false, childFlags))
{
    // ... validation code ...
    
    if (actualWidth > 0.0f && actualHeight > 0.0f && validOrigin && validClip && !isWindowCollapsed)
    {
        // ... drawing code ...
        
        // Text is here (inside validation block)
        ImGui::SetCursorPos(...);
        ImGui::Text(...);
        ImGui::Dummy(...);
    }
    
    ImGui::EndChild();
}
```

**TO:**
```cpp
if (ImGui::BeginChild("##NoiseViz", ImVec2(itemWidth, vizHeight), false, childFlags))
{
    // ... validation code ...
    
    // Drawing block (conditional)
    if (actualWidth > 0.0f && actualHeight > 0.0f && validOrigin && validClip && !isWindowCollapsed)
    {
        // ... drawing code only (no text positioning) ...
        ImGui::PopClipRect();
    }
    
    // Text is always rendered (outside validation block)
    ImGui::Spacing();
    const float liveLevelDb = vizData.currentLevelDb.load();
    const float liveRms = vizData.outputRms.load();
    const char* colourNames[] = { "White", "Pink", "Brown" };
    const int currentColour = vizData.currentColour.load();
    const char* currentColourName = (currentColour >= 0 && currentColour < 3) ? colourNames[currentColour] : "Unknown";
    ImGui::Text("%s Noise  |  Level: %.1f dB  |  RMS: %.3f", currentColourName, liveLevelDb, liveRms);
    
    ImGui::EndChild();
}
```

**Rationale:** Ensures consistent layout regardless of validation state, preventing ImGui boundary calculation issues.

---

### Fix 3: Verify ClipRect Balance

**Ensure** that every `PushClipRect()` has a matching `PopClipRect()`:
- Line 302: `ImGui::PushClipRect(origin, rectMax, true);`
- Line 333: `ImGui::PopClipRect();`

✅ These are balanced, but verify they're in the same conditional block scope.

---

## Testing Checklist

After applying fixes:

1. **Canvas sanity:** Create multiple Noise nodes, move them around, verify minimap doesn't disappear
2. **Error log:** Check ImGui error log - all state stack errors should be resolved
3. **Visualization:** Verify noise waveform renders correctly when node is in/out of view
4. **Text display:** Confirm live parameter values (noise type, level, RMS) always display correctly
5. **Theme switching:** Change themes and verify visualization updates properly

---

## References

- **NodeVisualizationGuide.md** - Section 3: "Clip Rects, Child Windows, and State Hygiene"
- **DriveModuleProcessor.cpp** - Working reference implementation (lines 201-300)
- **DebugModuleProcessor.cpp** - Another child window implementation (lines 345-449)

---

## Priority

**HIGH** - These errors indicate ImGui state corruption that can cause:
- Minimap rendering glitches
- Window boundary calculation failures
- Potential crashes or undefined behavior

---

**Report Generated:** Based on screenshot analysis and code review of `NoiseModuleProcessor.cpp` and `NodeVisualizationGuide.md`

