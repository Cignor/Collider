# Automation Lane - Debug Checklist

## ✅ All Registrations Verified

### 1. Factory Registration
**File:** `juce/Source/audio/graph/ModularSynthProcessor.cpp`
- Line 43: `#include "../modules/AutomationLaneModuleProcessor.h"` ✅
- Line 993: `reg("automation_lane", [] { return std::make_unique<AutomationLaneModuleProcessor>(); });` ✅

### 2. Build System
**File:** `juce/CMakeLists.txt`
- Line 861: `Source/audio/modules/AutomationLaneModuleProcessor.h` ✅
- Line 862: `Source/audio/modules/AutomationLaneModuleProcessor.cpp` ✅
- Also at lines 1272-1273 (PresetCreatorApp target) ✅

### 3. PinDatabase
**File:** `juce/Source/preset_creator/PinDatabase.cpp`
- Line 60: Description ✅
- Lines 543-553: Pin definitions ✅

### 4. Menu Registrations
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`
- Line 3203: Left panel menu (Sequencers section) ✅
- Line 6799-6800: Right-click context menu (Sequencers submenu) ✅
- Line 13075: Search category detection ✅
- Line 13253: Search database entry ✅

### 5. User Manual
**File:** `USER_MANUAL/Nodes_Dictionary.md`
- Table of contents entry ✅
- Full documentation section ✅

---

## 🔍 Why It Might Not Appear

### Most Likely Causes:

1. **Build Not Complete** ⚠️
   - The linker error (`LNK1201`) means the build failed
   - The executable was not created with the new module
   - **Solution:** Fix linker error and rebuild completely

2. **Application Not Restarted** 🔄
   - If build succeeded, you must restart the application
   - Old process is still running without the new module
   - **Solution:** Close and reopen the application

3. **Cached Build** 🗑️
   - Old object files might be cached
   - **Solution:** Clean build directory and rebuild

---

## 🔧 Immediate Steps to Debug

### Step 1: Verify Build Status
Check if the build completed successfully:
- Look for build errors in the output
- Check if `AutomationLaneModuleProcessor.cpp` compiled
- Verify the linker step completed

### Step 2: Check Application Version
The application must be running a build that includes AutomationLane:
- If using a release build, rebuild in release mode
- If using debug build, rebuild in debug mode
- Close the application completely before rebuilding

### Step 3: Verify Module Files Exist
Check that the files are actually in the source directory:
- `juce/Source/audio/modules/AutomationLaneModuleProcessor.h` should exist
- `juce/Source/audio/modules/AutomationLaneModuleProcessor.cpp` should exist

### Step 4: Test Factory Registration
Add a simple test in code to verify the factory works:
```cpp
// In ModularSynthProcessor.cpp, after line 993:
// Test that it's in the factory
auto test = getModuleFactory();
auto it = test.find("automation_lane");
if (it == test.end()) {
    juce::Logger::writeToLog("ERROR: automation_lane not in factory!");
}
```

### Step 5: Check Console for Errors
When clicking the menu item, check console/log for:
- "Unknown module type: automation_lane"
- Any exceptions during module creation
- Link errors at runtime

---

## 📋 Quick Verification Commands

### Check if files exist:
```bash
dir juce\Source\audio\modules\AutomationLane*.cpp
dir juce\Source\audio\modules\AutomationLane*.h
```

### Check if registered in code:
```bash
findstr /n "automation_lane" juce\Source\preset_creator\ImGuiNodeEditorComponent.cpp
findstr /n "automation_lane" juce\Source\audio\graph\ModularSynthProcessor.cpp
```

---

## 🎯 Expected Behavior

Once build succeeds and app restarts:

1. **Left Panel Menu:**
   - Open "Sequencers" section
   - Should see "Automation Lane" button after "Timeline"

2. **Right-Click Menu:**
   - Right-click on canvas
   - Navigate to "Sequencers" submenu
   - Should see "Automation Lane" menu item

3. **Search:**
   - Type "automation" or "lane" in search
   - Should see "Automation Lane" in results
   - Should show description tooltip

4. **Clicking Menu Item:**
   - Should create the node
   - Should display with proper pins
   - Should show the timeline UI

---

## ⚠️ If Still Not Appearing After Successful Build

1. Check for compilation errors in AutomationLaneModuleProcessor files
2. Verify no syntax errors in the registration code
3. Check if the module processor class compiles without errors
4. Ensure all dependencies are available
5. Check if there's a runtime check that filters modules

---

**Status:** All code registrations are correct. The issue is likely build-related.

