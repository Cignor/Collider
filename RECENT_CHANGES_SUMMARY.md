# Recent Changes Summary - Cable Inspector Crash Investigation

## Overview
This document summarizes ALL changes made since the last git push to identify the source of the cable inspector crash that occurs 2 seconds after hovering over a cable.

---

## 🔴 CRITICAL CHANGE: Pin ID System Overhaul

### Old System (Attribute Registry):
- Used `pinToAttr` and `attrToPin` hash maps to maintain stable IDs
- `getAttrId()` created persistent registry entries
- `decodeAttr()` looked up pins from the registry
- Registry persisted across frames
- IDs were allocated incrementally via `nextAttrId`

### New System (Direct Bitmask Encoding):
- **REMOVED** all registry maps (`pinToAttr`, `attrToPin`, `nextAttrId`)
- **NEW** `encodePinId(PinID)` - directly encodes pin data into 32-bit integer
- **NEW** `decodePinId(int)` - directly decodes pin data from integer
- Pin IDs are now **stateless** - generated on-the-fly each frame
- Bit layout:
  - Bit 31: PIN_ID_FLAG (always 1)
  - Bit 30: IS_INPUT_FLAG
  - Bits 16-29: Channel (14 bits)
  - Bits 0-15: Logical ID (16 bits)

**POTENTIAL ISSUE**: Links drawn in one frame reference pin IDs that may not exist in the next frame if nodes are deleted/recreated.

---

## 🔴 Frame Rendering Changes

### Stateless Rendering:
```cpp
// At START of renderImGui() [Line 292]:
linkIdToAttrs.clear();
linkToId.clear();
nextLinkId = 1000;
```

**ISSUE FOUND**: `linkIdToAttrs.clear()` was also called at line 900 (DUPLICATE CLEAR - NOW REMOVED)

### Link ID Generation:
- Uses `getLinkId(srcAttr, dstAttr)` which looks up or creates entries in `linkToId` map
- `linkToId` is cleared every frame → link IDs are regenerated each frame
- Link IDs start from 1000 each frame

**POTENTIAL ISSUE**: If ImNodes returns a link ID from a previous frame, it won't exist in `linkIdToAttrs`.

---

## 🔴 Cable Inspector Implementation Changes

### Old Implementation (Cached):
- Static variables cached module ID, name, channel
- Data persisted across multiple frames
- **PROBLEM**: Module pointers could become stale after `commitChanges()`

### Current Implementation (Cache-Free):
```cpp
// Line 2139-2168
if (isLinkHovered && hoveredLinkId != -1 && synth != nullptr)
{
    auto it = linkIdToAttrs.find(hoveredLinkId);
    if (it != linkIdToAttrs.end())
    {
        auto srcPin = decodePinId(it->second.first);
        auto dstPin = decodePinId(it->second.second);
        
        hoveredLinkSrcId = srcPin.logicalId;
        hoveredLinkDstId = (dstPin.logicalId == 0) ? kOutputHighlightId : dstPin.logicalId;
        
        if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId))
        {
            const float liveValue = srcModule->getOutputChannelValue(srcPin.channel);
            // ... tooltip display
        }
    }
}
```

**CRITICAL QUESTIONS**:
1. What if `hoveredLinkId` from ImNodes doesn't exist in `linkIdToAttrs`?
2. What if `srcPin.channel` is out of bounds for the module?
3. What if the module was deleted between `EndNodeEditor()` and the cable inspector check?

---

## 🟡 New Features Added

### 1. CommentModuleProcessor
- New module type for annotations
- Added to CMakeLists.txt
- Has `nodeWidth` and `nodeHeight` properties
- Uses `pendingNodeSizes` map to apply sizes

### 2. Grid System
- Visual grid at 64px spacing
- Origin (0,0) marked with thicker lines
- Scale markers every 400 units
- Mouse coordinate display (bottom-left)

### 3. Module Category Color-Coding
- `getModuleCategory()` - categorizes modules
- `getImU32ForCategory()` - assigns colors
- Categories: Source, Effect, Modulator, Utility, Analysis, Comment, Plugin
- Title bars color-coded by category

### 4. Compatible Pin Highlighting
- During link drag, compatible target pins highlighted in yellow
- Checks pin data types for compatibility
- CV→Gate connections allowed

### 5. Smart Mixer Insertion (Ctrl+T)
- Calculates average position of destination nodes
- Places mixer halfway between source and destinations
- More intelligent positioning than fixed offset

### 6. Recorder Output Shortcut (Ctrl+R)
- New menu item and keyboard shortcut
- Calls `handleRecordOutput()`

### 7. Preset Status Overlay
- Shows current preset filename
- Shows EDITED/SAVED status
- Always visible in top-left

---

## 🟡 Code Quality Improvements

### Pin Drawing Code:
- Added safety checks for compatible pin highlighting during drag
- Simplified color logic with `finalPinColor` variable
- Better comments explaining each step

### Connection Drawing:
- Improved formatting (one statement per line)
- Better logging for skipped connections
- Color-coded links by data type

### Node Rendering:
- Proper style push/pop order documented
- Category colors applied before hover/mute overrides
- Muted node styles properly stacked

---

## 🔴 LIKELY CRASH CAUSES (Ranked by Probability)

### 1. **Race Condition with `commitChanges()`** (MOST LIKELY)
**Scenario**:
1. User hovers cable → `linkIdToAttrs` populated → `hoveredLinkId` captured
2. Graph rebuild triggered (`graphNeedsRebuild` = true)
3. `synth->commitChanges()` deletes and recreates modules
4. Next frame: Cable inspector queries `srcModule->getOutputChannelValue()`
5. Module pointer is VALID (same logical ID) but INTERNALS may be uninitialized
6. **CRASH** when accessing output channel value

**Evidence**: User reports "crashes 2 sec later" - matches typical deferred rebuild timing

### 2. **Invalid Channel Index**
**Scenario**:
- `decodePinId()` returns a channel index
- Channel was valid when link was created
- Module configuration changed (fewer outputs)
- `getOutputChannelValue(srcPin.channel)` accesses out-of-bounds

### 3. **Link ID Mismatch**
**Scenario**:
- ImNodes returns a link ID from a previous frame
- That ID doesn't exist in current frame's `linkIdToAttrs`
- `linkIdToAttrs.find(hoveredLinkId)` returns `end()`
- Code should handle this gracefully, but may not

### 4. **Dangling `lastOutputValues` Pointer**
**Scenario**:
- `getOutputChannelValue()` accesses `lastOutputValues[channel]`
- After `commitChanges()`, this array may be reallocated
- Atomic pointer may be null or invalid
- Dereferencing causes crash

---

## 🔧 FIXES APPLIED IN THIS SESSION

### 1. Removed Duplicate `linkIdToAttrs.clear()`
- Line 900 clear was redundant
- Now only cleared once at line 292

### 2. Removed Cable Inspector Caching
- Eliminated all static cached variables
- Query everything fresh each frame
- Should prevent stale pointer issues

### 3. Added Safety Checks
- Check `synth != nullptr`
- Check `linkIdToAttrs.find()` result
- Check `srcModule != nullptr`

---

## 🔍 RECOMMENDED NEXT STEPS

### To Identify Crash:
1. **Add logging in `getOutputChannelValue()`**:
```cpp
virtual float getOutputChannelValue(int channel) const
{
    juce::Logger::writeToLog("[getOutputChannelValue] lid=" + juce::String((int)logicalId) + " ch=" + juce::String(channel) + " size=" + juce::String(lastOutputValues.size()));
    if (juce::isPositiveAndBelow(channel, (int)lastOutputValues.size()) && lastOutputValues[channel])
        return lastOutputValues[channel]->load();
    juce::Logger::writeToLog("[getOutputChannelValue] OUT OF BOUNDS or NULL!");
    return 0.0f;
}
```

2. **Add logging before cable inspector query**:
```cpp
juce::Logger::writeToLog("[Inspector] About to query: linkId=" + juce::String(hoveredLinkId) + 
                         " lid=" + juce::String(srcPin.logicalId) + 
                         " ch=" + juce::String(srcPin.channel));
```

3. **Add crash handler** to catch exact location

### To Fix Crash:
1. **Invalidate hover state after `commitChanges()`**:
```cpp
if (graphNeedsRebuild.load())
{
    synth->commitChanges();
    graphNeedsRebuild = false;
    // Force hover state to reset
    lastHoveredLinkId = -1;
}
```

2. **Bounds check in cable inspector**:
```cpp
if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId))
{
    if (srcPin.channel >= 0 && srcPin.channel < srcModule->getNumOutputs())
    {
        const float liveValue = srcModule->getOutputChannelValue(srcPin.channel);
        // ...
    }
}
```

3. **Double-check link still exists**:
```cpp
// Re-verify link exists each frame (in case it was just deleted)
if (linkIdToAttrs.find(hoveredLinkId) == linkIdToAttrs.end())
{
    // Link deleted - stop hovering
    lastHoveredLinkId = -1;
    return;
}
```

---

## 📋 ALL MODIFIED FILES

1. `ImGuiNodeEditorComponent.h` - Pin ID system, new methods
2. `ImGuiNodeEditorComponent.cpp` - Main rendering logic
3. `ModularSynthProcessor.cpp` - Graph management
4. `CommentModuleProcessor.cpp/h` - New module (UNTRACKED)
5. Multiple module processors - Updated for new pin system
6. `CMakeLists.txt` - Added Comment module
7. `PresetCreatorApplication.h` - Application setup
8. `PresetCreatorMain.cpp` - Main entry point
9. Preset XML files - Test configurations

---

## 🎯 CONCLUSION

The crash is **MOST LIKELY** caused by a race condition where:
- The cable inspector holds a reference to a link/module
- A graph rebuild occurs via `commitChanges()`
- The module is deleted and recreated at the same logical ID
- The cable inspector tries to access the module's output values
- The module's internal state (`lastOutputValues`) is not yet initialized
- **CRASH** on array access or null pointer dereference

**Primary suspects**:
1. `getOutputChannelValue()` accessing uninitialized `lastOutputValues`
2. Channel index out of bounds after module recreation
3. Link ID becoming invalid between frames

**Recommended fix**: Add bounds checking + invalidate hover state after graph rebuild.

