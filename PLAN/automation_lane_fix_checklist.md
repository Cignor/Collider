# Automation Lane Node - Fix Checklist

## Current Status

✅ **COMPLETED:**
- Node is registered in `ModularSynthProcessor.cpp` (line 993)
- Basic implementation exists
- Compilation type warnings fixed

❌ **MISSING/NEEDS FIX:**
1. Not in PinDatabase.cpp
2. Not in any menus (left panel, right-click, insert between)
3. Not in search system
4. Visualization has thread safety issues
5. Visualization doesn't follow NodeVisualizationGuide patterns

## Required Fixes

### 1. Add to PinDatabase.cpp

**Location:** `juce/Source/preset_creator/PinDatabase.cpp`

**Add module description:**
```cpp
// Around line 59 (after function_generator)
descriptions["automation_lane"] = "Draw automation curves on an infinitely scrolling timeline with fixed center playhead.";
```

**Add pin info:**
```cpp
// After function_generator pin info (around line 540)
db["automation_lane"] = ModulePinInfo(
    NodeWidth::Big,
    {}, // No inputs
    {
        AudioPin("Value", 0, PinDataType::CV),
        AudioPin("Inverted", 1, PinDataType::CV),
        AudioPin("Bipolar", 2, PinDataType::CV),
        AudioPin("Pitch", 3, PinDataType::CV)
    },
    {}
);
```

### 2. Add to Left Panel Menu

**Location:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` ~line 3202

**Add to Sequencers section:**
```cpp
// After Timeline button
addModuleButton("Automation Lane", "automation_lane");
```

### 3. Add to Right-Click Context Menu

**Location:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` ~line 6797

**Add to Sequencers submenu:**
```cpp
// After Timeline menu item
if (ImGui::MenuItem("Automation Lane"))
    addAtMouse("automation_lane");
```

### 4. Add to Search System

**Location:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` ~line 13243

**Add to search database:**
```cpp
// After Function Generator entry
{"Automation Lane", {"automation_lane", "Draw automation curves on scrolling timeline"}},
```

### 5. Fix Visualization Issues (CRITICAL)

**Location:** `juce/Source/audio/modules/AutomationLaneModuleProcessor.cpp` ~line 324

**Issues Found:**
1. ❌ **Missing PushID/PopID** - Can cause ID collisions
2. ❌ **Reading state inside BeginChild** - Should read before
3. ❌ **Direct modification of chunk->samples** (line 571) - **THREAD SAFETY VIOLATION**
4. ❌ **No theme compliance** - Hard-coded colors
5. ❌ **Using GetWindowSize()** instead of pre-calculated graphSize
6. ❌ **Missing clip rect management** - Not following guide pattern
7. ❌ **No InvisibleButton for drag blocking**

**Fix Pattern (based on FunctionGenerator and guides):**
```cpp
void AutomationLaneModuleProcessor::drawParametersInNode(...)
{
    ImGui::PushID(this); // CRITICAL: Protect ID space
    
    // Read state BEFORE BeginChild (thread-safe)
    AutomationState::Ptr state = activeState.load();
    
    // ... controls ...
    
    // Calculate graph size BEFORE BeginChild
    const float timelineHeight = 150.0f;
    const ImVec2 graphSize(itemWidth, timelineHeight);
    
    // Read any live data before child window
    const double currentPhaseRead = currentPhase; // Copy atomic/double
    
    if (ImGui::BeginChild("AutomationTimeline", graphSize, false, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Theme compliance
        const auto& theme = ThemeManager::getInstance().getCurrentTheme();
        ImU32 bgColor = theme.canvas.canvas_background == 0 
            ? IM_COL32(30, 30, 30, 255) 
            : theme.canvas.canvas_background;
        
        drawList->AddRectFilled(p0, p1, bgColor);
        drawList->PushClipRect(p0, p1, true);
        
        // ... drawing operations ...
        
        drawList->PopClipRect();
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##automationDrag", graphSize);
    }
    ImGui::EndChild(); // OUTSIDE if block!
    
    ImGui::PopID();
}
```

**Thread Safety Fix (CRITICAL - Line 571):**
Cannot directly modify `chunk->samples[sampleIndex]` from UI thread!

**Solution:** Create new state with modified chunk:
```cpp
// Instead of:
chunk->samples[sampleIndex] = normalizedY; // ❌ UNSAFE!

// Do:
// 1. Create new state
auto newState = std::make_shared<AutomationState>();
newState->chunks = state->chunks; // Copy chunk pointers

// 2. Find and clone the chunk we're editing
for (auto& chunkPtr : newState->chunks) {
    if (chunkPtr->startBeat <= beat && beat < chunkPtr->startBeat + chunkPtr->numBeats) {
        // Create new chunk with modified data
        auto newChunk = std::make_shared<AutomationChunk>(
            chunkPtr->startBeat, 
            chunkPtr->numBeats, 
            chunkPtr->samplesPerBeat
        );
        newChunk->samples = chunkPtr->samples; // Copy samples
        // Now modify the copy
        int sampleIndex = ...;
        newChunk->samples[sampleIndex] = normalizedY;
        chunkPtr = newChunk; // Replace with modified chunk
        break;
    }
}

// 3. Update state atomically
updateState(newState);
```

### 6. Linker Error (PDB Write Failure)

**Error:** `LNK1201: error writing to program database`

**This is NOT a code issue** - it's a disk/permissions problem. Solutions:
- Check disk space
- Close other instances of the application
- Run IDE as administrator
- Clean build directory and rebuild

---

## Priority Order

1. 🔴 **CRITICAL:** Fix thread safety (direct chunk modification)
2. 🟡 **HIGH:** Add to PinDatabase.cpp
3. 🟡 **HIGH:** Fix visualization to follow guide patterns
4. 🟢 **MEDIUM:** Add to menus
5. 🟢 **MEDIUM:** Add to search system

---

## Testing After Fixes

- [ ] Node appears in left panel menu
- [ ] Node appears in right-click menu
- [ ] Node appears in search
- [ ] Node can be created and appears in UI
- [ ] No ImGui stack errors in console
- [ ] Drawing works without crashes
- [ ] Thread safety verified (no audio glitches)
- [ ] Theme colors apply correctly

