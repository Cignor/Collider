# ImGui Stack Balance Issues - Comprehensive Analysis Report

## Executive Summary

**12 modules** have identical ImGui state stack management issues that cause runtime errors. All issues stem from **two common anti-patterns** that conflict with how the parent `ImGuiNodeEditorComponent` manages ID scoping.

**Affected Modules:**
1. FunctionGeneratorModuleProcessor
2. VCAModuleProcessor
3. MixerModuleProcessor
4. CVMixerModuleProcessor
5. LagProcessorModuleProcessor
6. QuantizerModuleProcessor
7. RateModuleProcessor
8. LogicModuleProcessor
9. SequentialSwitchModuleProcessor
10. VocalTractFilterModuleProcessor
11. NoiseModuleProcessor ✅ (already fixed)
12. RandomModuleProcessor ✅ (already fixed)

---

## Root Cause Analysis

### The Parent Context

**In `ImGuiNodeEditorComponent.cpp` (lines 2758-3188):**

```cpp
ImGui::PushID((int)lid);  // Parent pushes ID BEFORE calling drawParametersInNode
mp->drawParametersInNode(nodeContentWidth, isParamModulated, onModificationEnded);
// ... error checking ...
ImGui::PopID();  // Parent pops ID AFTER drawParametersInNode returns
```

**Key insight:** The parent **already manages** the ID scope for each module. Child modules should **NOT** push/pop their own IDs at function boundaries.

---

## Issue #1: PushID/PopID Mismatch (CRITICAL)

### The Problem Pattern

**All broken modules follow this pattern:**

```cpp
void ModuleProcessor::drawParametersInNode(float itemWidth, ...)
{
    ImGui::PushID(this);  // ❌ WRONG: Conflicts with parent's PushID(lid)
    ImGui::PushItemWidth(itemWidth);
    
    // ... UI code ...
    
    if (ImGui::BeginChild("ModuleViz", ...))
    {
        // ... drawing code ...
        ImGui::EndChild();
    }
    
    ImGui::PopItemWidth();
    ImGui::PopID();  // ❌ WRONG: Parent also pops ID!
}
```

### Why This Causes Errors

1. **Double PushID conflict:**
   - Parent pushes: `PushID(lid)` 
   - Module pushes: `PushID(this)`
   - Result: ID stack depth = 2

2. **Double PopID mismatch:**
   - Module pops: `PopID()` (removes `this`)
   - Parent pops: `PopID()` (expects `lid`, but stack is wrong)
   - Result: **"Calling PopID() too many times!"** or **"Missing PopID()"**

### The Correct Pattern (VCOModuleProcessor)

```cpp
void VCOModuleProcessor::drawParametersInNode(float itemWidth, ...)
{
    ImGui::PushItemWidth(itemWidth);  // ✅ NO PushID at start
    
    // ... UI code ...
    
    // ONLY push ID if needed INSIDE for visualization scope
    ImGui::PushID(this);  // ✅ INSIDE, before BeginChild
    if (ImGui::BeginChild("VCOOscilloscope", ...))
    {
        // ... drawing code ...
        ImGui::EndChild();
    }
    ImGui::PopID();  // ✅ RIGHT after EndChild
    
    ImGui::PopItemWidth();  // ✅ At end
}
```

**Key differences:**
- ✅ **NO** `PushID()` at function start
- ✅ `PushID()` only used **INSIDE** for visualization scope (if needed)
- ✅ `PopID()` matches the internal push
- ✅ Function does **NOT** pop parent's ID

---

## Issue #2: SetCursorScreenPos() Boundary Extension

### The Problem Pattern

**All broken modules use:**

```cpp
if (ImGui::BeginChild("ModuleViz", ...))
{
    // ... drawing code ...
    
    ImGui::SetCursorScreenPos(ImVec2(vizOrigin.x + 4.0f, waveMax.y + 4.0f));  // ❌ SCREEN-space
    ImGui::Text("Rate: %.2f Hz", currentRate);
    // ❌ NO Dummy() or item after SetCursorScreenPos
    
    ImGui::EndChild();
}
```

### Why This Causes Errors

According to ImGui documentation and error messages:
- `SetCursorScreenPos()` positions the cursor in **screen-space coordinates**
- This **extends window boundaries** artificially
- ImGui requires an **item** (like `Dummy()`, `Text()`, etc.) **after** `SetCursorScreenPos()` to properly calculate layout
- When used inside child windows without proper items, it causes: **"Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries. Please submit an item e.g. Dummy() afterwards"**

### The Correct Pattern (VCOModuleProcessor)

```cpp
if (ImGui::BeginChild("VCOOscilloscope", ...))
{
    // ... drawing code ...
    
    ImGui::SetCursorPos(ImVec2(4, 4));  // ✅ WINDOW-space (POS), not SCREEN-space
    ImGui::TextColored(...);  // ✅ Item immediately after
    
    ImGui::SetCursorPos(ImVec2(4, graphSize.y - 20));
    ImGui::TextColored(...);  // ✅ Another item
    
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##drag", graphSize);  // ✅ Drag blocker as item
    
    ImGui::EndChild();
}
```

**Key differences:**
- ✅ Uses `SetCursorPos()` (**window-space**) not `SetCursorScreenPos()` (**screen-space**)
- ✅ Every `SetCursorPos()` is **immediately followed by an item** (`Text`, `InvisibleButton`, etc.)
- ✅ Alternative: Use normal cursor flow (`Spacing()`, `Text()`) instead of manual positioning

---

## Detailed Module-by-Module Analysis

### ✅ VCOModuleProcessor (REFERENCE - WORKING)

**Structure:**
```cpp
Line 60:  PushItemWidth(itemWidth)
Line 304: PushID(this)  // INSIDE, before BeginChild
Line 322: BeginChild("VCOOscilloscope", ...)
Line 379: SetCursorPos(...)  // Window-space
Line 391: EndChild()
Line 393: PopID()  // Right after EndChild
Line 422: PopItemWidth()
```

**✅ Correct:** No ID management at function boundaries, uses window-space positioning.

---

### ❌ FunctionGeneratorModuleProcessor (BROKEN)

**Issues:**
- Line 330: `PushID(this)` at function start ❌
- Line 453: `BeginChild("FunctionGenCanvas", ...)`
- Line 516: `EndChild()`
- Line 519: `PopItemWidth()`
- Line 520: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 330
2. Remove `PopID()` at line 520
3. No `SetCursorScreenPos()` issue (uses InvisibleButton correctly)

---

### ❌ VCAModuleProcessor (BROKEN)

**Issues:**
- Line 211: `PushID(this)` at function start ❌
- Line 233: `BeginChild("VCAViz", ...)`
- Line 306: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 324: `EndChild()`
- Line 343: `PopItemWidth()`
- Line 344: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 211
2. Remove `PopID()` at line 344
3. Replace `SetCursorScreenPos()` at line 306 with `Spacing()` + normal cursor flow

---

### ❌ MixerModuleProcessor (BROKEN)

**Issues:**
- Line 285: `PushID(this)` at function start ❌
- Line 337: `BeginChild("MixerViz", ...)`
- Line 435: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 468: `EndChild()`
- Line 508: `PopItemWidth()`
- Line 509: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 285
2. Remove `PopID()` at line 509
3. Replace `SetCursorScreenPos()` at line 435 with `Spacing()` + normal cursor flow

---

### ❌ CVMixerModuleProcessor (BROKEN)

**Issues:**
- Line 346: `PushID(this)` at function start ❌
- Line 355: `BeginChild("CVMixerViz", ...)`
- Line 459: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 484: `EndChild()`
- Line 552: `PopItemWidth()`
- Line 553: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 346
2. Remove `PopID()` at line 553
3. Replace `SetCursorScreenPos()` at line 459 with `Spacing()` + normal cursor flow

---

### ❌ LagProcessorModuleProcessor (BROKEN)

**Issues:**
- Line 204: `PushID(this)` at function start ❌
- Line 230: `BeginChild("LagViz", ...)`
- Line 307: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 312: `EndChild()`
- Line 352: `PopItemWidth()`
- Line 353: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 204
2. Remove `PopID()` at line 353
3. Replace `SetCursorScreenPos()` at line 307 with `Spacing()` + normal cursor flow

---

### ❌ QuantizerModuleProcessor (BROKEN)

**Issues:**
- Line 202: `PushID(this)` at function start ❌
- Line 215: `BeginChild("QuantizerViz", ...)`
- Line 268: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 277: `EndChild()`
- Line 310: `PopItemWidth()`
- Line 311: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 202
2. Remove `PopID()` at line 311
3. Replace `SetCursorScreenPos()` at line 268 with `Spacing()` + normal cursor flow

---

### ❌ RateModuleProcessor (BROKEN)

**Issues:**
- Line 145: `PushID(this)` at function start ❌
- Line 158: `BeginChild("RateViz", ...)`
- Line 236: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 242: `EndChild()`
- Line 285: `PopItemWidth()`
- Line 286: `PopID()` at end ❌

**Fix needed:**
1. Remove `PushID(this)` at line 145
2. Remove `PopID()` at line 286
3. Replace `SetCursorScreenPos()` at line 236 with `Spacing()` + normal cursor flow

---

### ❌ LogicModuleProcessor (BROKEN)

**Issues:**
- Line 212: `PushItemWidth(itemWidth)` (no PushID at start - ✅ good)
- Line 232: `PushID(this)` **INSIDE**, before BeginChild (✅ good pattern)
- Line 235: `BeginChild("LogicViz", ...)`
- Line 373: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 380: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child (second call)
- Line 391: `EndChild()`
- Line 396: `PopID()` **INSIDE**, after EndChild (✅ good pattern)
- Line 397: `PopItemWidth()`

**Partial fix needed:**
- ✅ ID management is correct (matches VCO pattern)
- ❌ Replace both `SetCursorScreenPos()` calls (lines 373, 380) with `Spacing()` + normal cursor flow

---

### ❌ SequentialSwitchModuleProcessor (BROKEN)

**Issues:**
- Line 260: `PushItemWidth(itemWidth)` (no PushID at start - ✅ good)
- Line 266: `PushID(this)` **INSIDE**, before BeginChild (✅ good pattern)
- Line 269: `BeginChild("SequentialSwitchViz", ...)`
- Line 406: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child
- Line 417: `SetCursorScreenPos(...)` ❌ SCREEN-space, inside child (second call)
- Line 421: `EndChild()`
- Line 427: `PopID()` **INSIDE**, after EndChild (✅ good pattern)
- Line 501: `PopItemWidth()`

**Partial fix needed:**
- ✅ ID management is correct (matches VCO pattern)
- ❌ Replace both `SetCursorScreenPos()` calls (lines 406, 417) with `Spacing()` + normal cursor flow

---

### ❌ VocalTractFilterModuleProcessor (BROKEN)

**Issues:**
- Line 390: `PushItemWidth(itemWidth)` (no PushID at start - ✅ good)
- Line 396: `PushID(this)` **INSIDE**, before BeginChild (✅ good pattern)
- Line 406: `BeginChild("VocalTractWaveforms", ...)`
- Line 463: `EndChild()`
- Line 470: `BeginChild("VocalTractFormantMap", ...)` (second child)
- Line 515: `EndChild()`
- Line 523: `BeginChild("##readouts", ...)` (third child)
- Line 532: `EndChild()`
- Line 597: `PopID()` **INSIDE**, after all children (✅ good pattern)
- Line 598: `PopItemWidth()`

**Status:**
- ✅ ID management is correct (matches VCO pattern)
- ✅ No `SetCursorScreenPos()` usage detected
- **This module may already be correct!** Verify if errors are coming from elsewhere.

---

### ✅ NoiseModuleProcessor (FIXED)

**Current structure (correct):**
- Line 187: `PushItemWidth(itemWidth)` (no PushID at start - ✅)
- Line 233: `BeginChild("##NoiseViz", ...)`
- Line 236: `GetCursorScreenPos()` - only for reading coordinates (✅ OK)
- Line 241: `Dummy()` - reserves space (✅ OK)
- Line 308: Uses `Spacing()` + normal cursor flow (✅ OK)
- Line 316: `EndChild()`
- Line 344: `PopItemWidth()`

**✅ Already fixed:** No ID management issues, proper space reservation.

---

### ✅ RandomModuleProcessor (FIXED)

**Current structure (correct):**
- Line 272: `PushItemWidth(itemWidth)` (no PushID at start - ✅)
- Line 416: `BeginChild("RandomViz", ...)`
- Line 526: Uses `Spacing()` + normal cursor flow (✅ OK)
- Line 533: `EndChild()`
- Line 591: `PopItemWidth()`

**✅ Already fixed:** No ID management issues, proper cursor flow.

---

## Summary of Required Fixes

### Category A: Full Fix Required (PushID + SetCursorScreenPos)

| Module | Remove PushID | Remove PopID | Fix SetCursorScreenPos |
|--------|---------------|--------------|------------------------|
| FunctionGenerator | Line 330 | Line 520 | N/A |
| VCA | Line 211 | Line 344 | Line 306 |
| Mixer | Line 285 | Line 509 | Line 435 |
| CVMixer | Line 346 | Line 553 | Line 459 |
| LagProcessor | Line 204 | Line 353 | Line 307 |
| Quantizer | Line 202 | Line 311 | Line 268 |
| Rate | Line 145 | Line 286 | Line 236 |

### Category B: Partial Fix Required (SetCursorScreenPos only)

| Module | Fix SetCursorScreenPos |
|--------|------------------------|
| Logic | Lines 373, 380 |
| SequentialSwitch | Lines 406, 417 |

### Category C: Verify Status

| Module | Notes |
|--------|-------|
| VocalTractFilter | ID management correct, no SetCursorScreenPos - verify if errors exist |

---

## Standard Fix Pattern

### For Category A Modules:

**BEFORE:**
```cpp
void ModuleProcessor::drawParametersInNode(float itemWidth, ...)
{
    ImGui::PushID(this);  // ❌ REMOVE THIS
    ImGui::PushItemWidth(itemWidth);
    
    // ... UI code ...
    
    if (ImGui::BeginChild("ModuleViz", ...))
    {
        // ... drawing code ...
        
        ImGui::SetCursorScreenPos(ImVec2(vizOrigin.x + 4.0f, waveMax.y + 4.0f));  // ❌ REPLACE THIS
        ImGui::Text("Value: %.2f", value);
        
        ImGui::EndChild();
    }
    
    ImGui::PopItemWidth();
    ImGui::PopID();  // ❌ REMOVE THIS
}
```

**AFTER:**
```cpp
void ModuleProcessor::drawParametersInNode(float itemWidth, ...)
{
    ImGui::PushItemWidth(itemWidth);
    // Note: Parent already manages ID scope with PushID(lid), so we don't push another ID here
    
    // ... UI code ...
    
    if (ImGui::BeginChild("ModuleViz", ...))
    {
        // ... drawing code ...
        
        // Live parameter readouts - positioned below waveform
        // Use normal cursor flow instead of SetCursorScreenPos to avoid boundary extension errors
        ImGui::Spacing();
        ImGui::Text("Value: %.2f", value);
        
        ImGui::EndChild();
    }
    
    ImGui::PopItemWidth();
}
```

### For Category B Modules:

**BEFORE:**
```cpp
if (ImGui::BeginChild("ModuleViz", ...))
{
    // ... drawing code ...
    
    ImGui::SetCursorScreenPos(ImVec2(waveOrigin.x + 4.0f, waveMax.y + 6.0f));  // ❌ REPLACE
    ImGui::Text("First line");
    
    ImGui::SetCursorScreenPos(ImVec2(waveOrigin.x + 4.0f, waveMax.y + 24.0f));  // ❌ REPLACE
    ImGui::Text("Second line");
    
    ImGui::EndChild();
}
```

**AFTER:**
```cpp
if (ImGui::BeginChild("ModuleViz", ...))
{
    // ... drawing code ...
    
    // Use normal cursor flow instead of SetCursorScreenPos
    ImGui::Spacing();
    ImGui::Text("First line");
    
    ImGui::Spacing();
    ImGui::Text("Second line");
    
    ImGui::EndChild();
}
```

---

## Key Principles (Based on VCOModuleProcessor Reference)

1. **No ID Management at Function Boundaries**
   - ❌ NEVER `PushID(this)` at function start
   - ❌ NEVER `PopID()` at function end
   - ✅ Parent (`ImGuiNodeEditorComponent`) handles ID scoping with `PushID(lid)` / `PopID()`

2. **ID Management Only for Internal Scopes (if needed)**
   - ✅ `PushID(this)` **INSIDE** function, before `BeginChild()` (for visualization scope)
   - ✅ `PopID()` **INSIDE** function, right after `EndChild()`
   - This matches VCOModuleProcessor's pattern

3. **Window-Space Positioning Inside Child Windows**
   - ❌ NEVER use `SetCursorScreenPos()` inside child windows
   - ✅ Use `SetCursorPos()` (window-space) if manual positioning needed
   - ✅ ALWAYS follow `SetCursorPos()` with an item (`Text`, `InvisibleButton`, etc.)
   - ✅ OR use normal cursor flow (`Spacing()`, `Text()`) - simpler and safer

4. **ItemWidth Management**
   - ✅ Always `PushItemWidth()` at function start
   - ✅ Always `PopItemWidth()` at function end
   - This is independent of ID management

---

## Testing Checklist

After applying fixes:

1. **Build and run** - All ImGui error messages should disappear
2. **Create multiple instances** - Add 2-3 instances of each fixed module to canvas
3. **Move nodes around** - Verify minimap doesn't disappear or glitch
4. **Theme switching** - Change themes, verify visualizations update correctly
5. **Modulation testing** - Enable CV inputs, verify visualizations work with modulation
6. **Error log clear** - Check ImGui error console - should be empty

---

## Priority

**CRITICAL** - These errors indicate ImGui state corruption that can cause:
- Minimap rendering failures
- Window boundary calculation errors
- Potential crashes or undefined behavior
- Poor user experience with error spam

---

## References

- **VCOModuleProcessor.h** (lines 36-423) - Working reference implementation
- **NoiseModuleProcessor.cpp** (lines 183-345) - Fixed implementation
- **RandomModuleProcessor.cpp** (lines 256-592) - Fixed implementation
- **NodeVisualizationGuide.md** - Best practices guide
- **ImGuiNodeEditorComponent.cpp** (lines 2758-3188) - Parent context showing ID management

---

**Report Generated:** Based on comprehensive analysis of 12 module processors and comparison with working VCOModuleProcessor reference implementation.

