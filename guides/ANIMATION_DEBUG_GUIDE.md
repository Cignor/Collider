# Animation Debugging Guide

## Current Status

After fixing the root bone crash, two new issues have surfaced:

1. **FBX files crash** during loading (new crash location)
2. **GLB files render as a single dot** (visualization problem)

## Issue 1: FBX Loading Crash

### What We Added

**File**: `FbxLoader.cpp` (lines 144-153)

Added validation logging to help identify the crash:
```cpp
// --- DEBUG: Validate node structure ---
juce::Logger::writeToLog("FbxLoader: Validating node structure...");
int nodesWithoutParent = 0;
for (size_t i = 0; i < rawData->nodes.size(); ++i) {
    if (rawData->nodes[i].parentIndex == -1) {
        nodesWithoutParent++;
        juce::Logger::writeToLog("FbxLoader: Root node found: " + rawData->nodes[i].name);
    }
}
juce::Logger::writeToLog("FbxLoader: Found " + juce::String(nodesWithoutParent) + " root nodes in hierarchy.");
```

### What You Need to Do

1. **Rebuild the project** with the new logging
2. **Run in the debugger**
3. **Load the FBX file** that crashes
4. **Get the call stack** when it crashes
5. **Share the call stack screenshot** so we can identify the exact crash location

### Expected Behavior

If FBX has multiple root nodes (common in complex scenes), the AnimationBinder expects exactly one. This might be the new issue.

---

## Issue 2: GLB Single Dot Rendering

### The Problem

✅ **Data is loading correctly** (bone dropdown is populated)
❌ **Rendering shows one dot** (all bones at same position)

This means either:
- Global transforms are all identity/zero
- Camera/projection is incorrect
- Drawing code has a bug

### What We Added

**File**: `AnimationModuleProcessor.cpp` (lines 331-347)

Added frame-by-frame position logging:
```cpp
// --- DEBUG: Log bone positions to diagnose rendering issues ---
static int debugFrameCounter = 0;
if (++debugFrameCounter % 60 == 0 && !finalMatrices.empty()) // Log once per second
{
    juce::Logger::writeToLog("=== Animation Frame Debug ===");
    juce::Logger::writeToLog("Total bones: " + juce::String(finalMatrices.size()));
    
    // Log the first 3 bone positions
    for (size_t i = 0; i < std::min(size_t(3), finalMatrices.size()); ++i)
    {
        glm::vec3 pos = finalMatrices[i][3];
        juce::Logger::writeToLog("Bone[" + juce::String(i) + "] Position: (" + 
            juce::String(pos.x, 2) + ", " + 
            juce::String(pos.y, 2) + ", " + 
            juce::String(pos.z, 2) + ")");
    }
}
```

**File**: `Animator.cpp` (lines 32-39, 72-79)

Added transform calculation logging:
```cpp
// DEBUG: Log animation update
static int updateCounter = 0;
if (++updateCounter % 240 == 0) // Log every ~4 seconds
{
    DBG("Animator::Update - Current time: " << m_CurrentTime << 
        " / " << m_CurrentAnimation->durationInTicks << 
        " Animation: " << m_CurrentAnimation->name);
}

// ... later in CalculateBoneTransform ...

// DEBUG: Log first bone's transform
static int transformCounter = 0;
if (boneIndex == 0 && ++transformCounter % 240 == 0)
{
    glm::vec3 pos = globalTransform[3];
    DBG("Bone[0] '" << nodeName << "' Global Position: (" << 
        pos.x << ", " << pos.y << ", " << pos.z << ")");
}
```

### What You Need to Do

1. **Rebuild the project** with the new logging
2. **Load the GLB file** that shows the single dot
3. **Watch the console/log output** for the debug messages
4. **Interpret the results:**

#### Scenario A: All positions are (0, 0, 0)
**Problem**: Animation transforms aren't being calculated

**Possible causes**:
- Animation clip isn't playing (check "Animator::Update" logs)
- Keyframe data is missing or wrong
- Transform calculation has a bug

**Next steps**: Check if the animation is playing and has valid keyframes

#### Scenario B: Positions are varying but reasonable (e.g., Bone[0] at (0.5, 1.2, 0.3))
**Problem**: Rendering/projection is wrong

**Possible causes**:
- Camera zoom/pan too extreme
- Projection matrix incorrect
- Coordinate system mismatch

**Next steps**: Click "Frame View" button to auto-fit the camera

#### Scenario C: Positions are huge/NaN (e.g., (1e10, NaN, -5000))
**Problem**: Data corruption or matrix math error

**Possible causes**:
- Uninitialized matrices
- Math error in transform calculation
- Bad data from loader

**Next steps**: Check loader output, validate bone offset matrices

---

## Rebuild Instructions

⚠️ **MUST REBUILD** to get the new debug logging!

### Visual Studio
```
Build → Clean Solution
Build → Rebuild Solution
```

### CMake Command Line
```bash
cd juce/out/build/x64-Debug
cmake --build . --clean-first
```

---

## Checklist

After rebuilding:

### For FBX Crash:
- [ ] Load FBX file in debugger
- [ ] Capture call stack when crash occurs
- [ ] Note any log messages before crash
- [ ] Check if "Found X root nodes" message appears

### For GLB Rendering:
- [ ] Load GLB file
- [ ] Look for "=== Animation Frame Debug ===" messages
- [ ] Note the bone positions (are they all 0? varying? huge?)
- [ ] Look for "Animator::Update" messages (is animation playing?)
- [ ] Try clicking "Frame View" button
- [ ] Try adjusting Zoom slider

---

## What to Share

### For FBX:
1. **Call stack screenshot** (most important!)
2. Log output showing:
   - "FbxLoader: Found X root nodes in hierarchy"
   - Any error messages

### For GLB:
1. Log output showing:
   - "=== Animation Frame Debug ===" sections
   - "Animator::Update" messages
   - Bone position values
2. Screenshot of the UI showing:
   - The single dot
   - Current zoom/pan values
   - Which bone is selected (if any)

---

## Quick Fix Attempts

### For GLB Single Dot (try these first):

1. **Click "Frame View" button** - auto-adjusts camera
2. **Increase Zoom slider** to 20-30 - skeleton might be tiny
3. **Select different bone** from dropdown - see if dot moves
4. **Play animation** - check if position values in log change

If none of these work, the log output will tell us if it's a data problem or rendering problem.



