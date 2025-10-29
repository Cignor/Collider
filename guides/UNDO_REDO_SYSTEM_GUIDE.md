# Undo/Redo System - Comprehensive Guide

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Core Components](#core-components)
4. [How It Works](#how-it-works)
5. [Snapshot Creation Strategy](#snapshot-creation-strategy)
6. [User Interface Integration](#user-interface-integration)
7. [Module-Level Undo Support](#module-level-undo-support)
8. [Best Practices](#best-practices)
9. [Implementation Examples](#implementation-examples)
10. [Limitations and Considerations](#limitations-and-considerations)

---

## Overview

The undo/redo system in this JUCE-based modular synthesizer provides comprehensive history tracking for all user modifications to patches. It captures the **complete state** of both the audio graph and the visual node editor, allowing users to freely navigate through their editing history.

### Key Features:
- **Full State Capture**: Saves both audio processing state and UI layout
- **Unlimited History**: No hard limit on undo stack size (limited only by memory)
- **Keyboard Shortcuts**: Standard `Ctrl+Z` (undo) and `Ctrl+Y` (redo)
- **Automatic Snapshot Creation**: Strategic placement ensures all major operations are undoable
- **Thread-Safe**: Designed to work safely with real-time audio processing

---

## Architecture

The undo/redo system is implemented in the `ImGuiNodeEditorComponent` class, which serves as the main UI controller for the node editor. It uses a **dual-stack architecture** to maintain history.

### File Locations:
- **Header**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.h`
- **Implementation**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

### High-Level Design:

```
┌─────────────────────────────────────────────┐
│      ImGuiNodeEditorComponent               │
│                                              │
│  ┌────────────────────────────────────────┐ │
│  │  Undo Stack (std::vector<Snapshot>)   │ │
│  │  ┌────────┐ ┌────────┐ ┌────────┐     │ │
│  │  │ State  │ │ State  │ │ State  │ ... │ │
│  │  │   1    │ │   2    │ │   3    │     │ │
│  │  └────────┘ └────────┘ └────────┘     │ │
│  │                            ↑           │ │
│  │                      Current State     │ │
│  └────────────────────────────────────────┘ │
│                                              │
│  ┌────────────────────────────────────────┐ │
│  │  Redo Stack (std::vector<Snapshot>)   │ │
│  │  ┌────────┐ ┌────────┐                │ │
│  │  │ State  │ │ State  │ ...            │ │
│  │  │   4    │ │   5    │                │ │
│  │  └────────┘ └────────┘                │ │
│  └────────────────────────────────────────┘ │
└─────────────────────────────────────────────┘
```

---

## Core Components

### 1. The Snapshot Structure

Located in `ImGuiNodeEditorComponent.h` (lines 301-305):

```cpp
struct Snapshot
{
    juce::MemoryBlock synthState;  // Complete audio graph state
    juce::ValueTree   uiState;     // Complete UI/editor state
};
```

**What's Stored:**

#### `synthState` (MemoryBlock):
- All module processor states (APVTS for each module)
- Audio graph connections (node relationships)
- Module parameter values
- Internal audio routing configuration
- Module-specific state data

#### `uiState` (ValueTree):
- Node positions in the editor grid
- Node selection state
- Muted/bypassed state for each node
- Output node position (ID 0)
- Visual editor layout

### 2. The Stack Storage

Located in `ImGuiNodeEditorComponent.h` (lines 306-307):

```cpp
std::vector<Snapshot> undoStack;  // History of previous states
std::vector<Snapshot> redoStack;  // History of undone states
```

- **undoStack**: Grows as user makes changes; the **back()** is always the current state
- **redoStack**: Populated when user undos; **cleared** when user makes a new change

### 3. Core Methods

```cpp
void pushSnapshot();                    // Create new undo point
void restoreSnapshot(const Snapshot& s); // Restore a previous state
```

---

## How It Works

### Creating a Snapshot (`pushSnapshot()`)

Located in `ImGuiNodeEditorComponent.cpp` (lines 4194-4237):

#### The Process:

1. **Capture UI State**: Calls `getUiValueTree()` to capture all node positions and visual state
2. **Capture Audio State**: Calls `synth->getStateInformation()` to serialize the audio graph
3. **Apply Pending Positions**: Merges any pending node position updates into the snapshot
4. **Push to Stack**: Adds the snapshot to the `undoStack`
5. **Clear Redo Stack**: Any new action invalidates the redo history
6. **Mark Patch as Dirty**: Sets `isPatchDirty = true` to indicate unsaved changes

#### Code Flow:

```cpp
void ImGuiNodeEditorComponent::pushSnapshot()
{
    // First check if we have pending positions to apply (for nodes just created)
    if (!pendingNodePositions.empty())
    {
        juce::ValueTree applied = getUiValueTree();
        
        // Apply any pending positions to the captured state
        for (const auto& kv : pendingNodePositions)
        {
            for (int i = 0; i < applied.getNumChildren(); ++i)
            {
                auto n = applied.getChild(i);
                if (n.hasType("node") && (int)n.getProperty("id", -1) == kv.first)
                {
                    n.setProperty("x", kv.second.x, nullptr);
                    n.setProperty("y", kv.second.y, nullptr);
                    break;
                }
            }
        }
        
        // Create snapshot with applied positions
        Snapshot s;
        s.uiState = applied;
        if (synth != nullptr)
            synth->getStateInformation(s.synthState);
        
        undoStack.push_back(std::move(s));
        redoStack.clear();
        isPatchDirty = true;
        return;
    }
    
    // Normal snapshot creation
    Snapshot s;
    s.uiState = getUiValueTree();
    if (synth != nullptr)
        synth->getStateInformation(s.synthState);
    
    undoStack.push_back(std::move(s));
    redoStack.clear();
    isPatchDirty = true;
}
```

### Restoring a Snapshot (`restoreSnapshot()`)

Located in `ImGuiNodeEditorComponent.cpp` (lines 4239-4245):

#### The Process:

1. **Restore Audio State**: Calls `synth->setStateInformation()` with the saved memory block
2. **Restore UI State**: Calls `applyUiValueTreeNow()` to restore node positions
3. **Clear Transient Data**: Clears frame-specific data like link ID mappings

#### Code:

```cpp
void ImGuiNodeEditorComponent::restoreSnapshot(const Snapshot& s)
{
    if (synth != nullptr && s.synthState.getSize() > 0)
        synth->setStateInformation(s.synthState.getData(), (int)s.synthState.getSize());
    
    // Restore UI positions exactly as saved
    applyUiValueTreeNow(s.uiState);
}
```

### Undo Operation (Ctrl+Z)

Located in `ImGuiNodeEditorComponent.cpp` (lines 3857-3869):

#### The Algorithm:

```
if undoStack.size() > 1:
    1. Save current state to redoStack
    2. Remove current state from undoStack
    3. Restore the previous state (now at undoStack.back())
    4. Clear transient link maps
```

#### Code:

```cpp
if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z))
{
    if (undoStack.size() > 1)
    {
        Snapshot current = undoStack.back();
        redoStack.push_back(current);
        undoStack.pop_back();
        restoreSnapshot(undoStack.back());
        
        // Clear transient data
        linkIdToAttrs.clear();
    }
}
```

**Why `size() > 1`?**
- The undo stack always contains at least the current state
- We need at least 2 states (current + previous) to perform an undo

### Redo Operation (Ctrl+Y)

Located in `ImGuiNodeEditorComponent.cpp` (lines 3870-3880):

#### The Algorithm:

```
if redoStack is not empty:
    1. Pop state from redoStack
    2. Restore that state
    3. Push it onto undoStack
    4. Clear transient link maps
```

#### Code:

```cpp
if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
{
    if (!redoStack.empty())
    {
        Snapshot s = redoStack.back();
        redoStack.pop_back();
        restoreSnapshot(s);
        undoStack.push_back(s);
        linkIdToAttrs.clear();
    }
}
```

---

## Snapshot Creation Strategy

The system strategically creates snapshots after **significant user actions**. Here's where `pushSnapshot()` is called:

### 1. Node Operations

| Operation | Location | Example Code |
|-----------|----------|--------------|
| **Add Node** | After module creation | `pushSnapshot();` |
| **Delete Node** | After node removal | `snapshotAfterEditor = true;` |
| **Duplicate Node(s)** | After duplication | Line 3932 |
| **Move Node** | After drag ends | Line 2806 (conditional) |

### 2. Connection Operations

| Operation | Location | Example Code |
|-----------|----------|--------------|
| **Create Connection** | After link created | Implicit in graph rebuild |
| **Delete Connection** | After link removed | Implicit in graph rebuild |
| **Clear Connections** | Menu operations | Lines 599, 619 |
| **Insert Mixer** | After mixer insertion | Line 2593 |
| **Insert Node on Link** | After node insertion | Line 3164 |

### 3. Complex Operations

| Operation | Location | Purpose |
|-----------|----------|---------|
| **Randomize Patch** | `handleRandomizePatch()` | Line 3717 |
| **Beautify Layout** | `handleBeautifyLayout()` | Line 4682 |
| **Auto-Connect Operations** | Various handlers | Lines 4975, 5167, 6492, etc. |
| **Build Drum Kit** | StrokeSequencer operation | Line 7365 |
| **Meta Module Operations** | Meta module editing | Lines 7619, 7623, 7648 |

### 4. State-Modifying Menu Actions

| Menu Item | Location | Purpose |
|-----------|----------|---------|
| **Clear Output Connections** | Edit menu | Line 599 |
| **Clear Selected Node Connections** | Edit menu | Line 619 |

---

## User Interface Integration

### Keyboard Shortcuts

Implemented in `ImGuiNodeEditorComponent.cpp` (lines 3856-3880):

```cpp
// Check if keyboard input should be processed
if (!ImGui::GetIO().WantCaptureKeyboard)
{
    bool ctrl = ImGui::GetIO().KeyCtrl;
    bool shift = ImGui::GetIO().KeyShift;
    
    // Undo: Ctrl+Z
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        // [Undo implementation]
    }
    
    // Redo: Ctrl+Y
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        // [Redo implementation]
    }
}
```

**WantCaptureKeyboard Check:**
- Prevents undo/redo when user is typing in text fields
- Only processes shortcuts when ImGui doesn't need keyboard input

### Model Reset on Load

Located in `ImGuiNodeEditorComponent.h` (lines 68-73):

```cpp
void setModel(ModularSynthProcessor* model) 
{ 
    synth = model; 
    undoStack.clear();   // Clear undo history
    redoStack.clear();   // Clear redo history
}
```

**Why Clear History?**
- Loading a new synth/patch represents a clean slate
- Previous undo states reference the old synth pointer
- Prevents crashes from restoring invalid state

---

## Module-Level Undo Support

Individual modules can trigger undo states through the **`onModificationEnded`** callback pattern.

### The Callback Pattern

Located in `ImGuiNodeEditorComponent.cpp` (line 1796):

```cpp
// Create a callback that modules can invoke when user finishes an edit
auto onModificationEnded = [&](){ this->pushSnapshot(); };
```

This callback is passed to modules via `drawParametersInNode()`:

```cpp
mp->drawParametersInNode(
    nodeContentWidth,
    isParamModulated,
    onModificationEnded  // Callback to create undo state
);
```

### Module Integration

Modules call `onModificationEnded()` after **discrete, user-initiated actions**. Examples:

#### Example 1: Stroke Sequencer Preset Loading

Located in `StrokeSequencerModuleProcessor.cpp` (line 753):

```cpp
if (ImGui::Combo("##StrokePreset", &selectedStrokePresetIndex, names.data(), (int)names.size()))
{
    if (selectedStrokePresetIndex >= 0)
    {
        activeStrokePresetName = presetNames[selectedStrokePresetIndex];
        juce::ValueTree presetData = presetManager.loadPreset(/*...*/);
        setExtraStateTree(presetData);
        onModificationEnded(); // Create undo state after preset loaded
    }
}
```

#### Example 2: MIDI Controller Modules

Pattern used in:
- `MIDIKnobsModuleProcessor.cpp` (line 251)
- `MIDIFadersModuleProcessor.cpp` (line 301)
- `MIDIButtonsModuleProcessor.cpp` (line 298)

```cpp
// When user finishes editing the MIDI mapping or controller state
onModificationEnded(); // Create an undo state
```

#### Example 3: Comment Module Resizing

Located in `CommentModuleProcessor.cpp` (line 92):

```cpp
// Just finished resizing, trigger undo snapshot
onModificationEnded();
```

### When NOT to Call onModificationEnded

❌ **During Continuous Parameter Changes** (slider dragging)
- Would create hundreds of undo states
- Only call after user releases the control

❌ **During Real-Time Audio Processing**
- `onModificationEnded` is a UI-thread operation
- Never call from `processBlock()`

❌ **For Internal State Updates**
- Only call for user-initiated actions
- Automatic/computed updates should not create history

---

## Best Practices

### 1. Strategic Snapshot Timing

✅ **Good Times to Create Snapshots:**
- After user completes an action (mouse release, menu selection)
- Before complex multi-step operations
- After batch operations complete

❌ **Bad Times to Create Snapshots:**
- During continuous drag operations
- Every frame of an animation
- Inside tight loops

### 2. Granularity Guidelines

**Single Snapshot for Atomic Operations:**
```cpp
// Good: One snapshot for the entire "Insert Mixer" operation
void handleInsertMixer()
{
    // 1. Create mixer node
    // 2. Disconnect original links
    // 3. Connect mixer in chain
    // 4. Position mixer
    
    pushSnapshot(); // One undo state for all steps
}
```

**Avoid Over-Snapshotting:**
```cpp
// Bad: Multiple snapshots for one logical operation
void handleInsertMixer()
{
    createMixer();
    pushSnapshot(); // ❌
    
    disconnectLinks();
    pushSnapshot(); // ❌
    
    connectMixer();
    pushSnapshot(); // ❌ User can't undo the whole operation at once
}
```

### 3. Memory Considerations

Each snapshot stores the **complete** synth state. For large patches:
- A single snapshot can be several hundred KB
- 100 undo states ≈ tens of MB

**Future Optimization Ideas:**
- Implement delta-based snapshots (store only changes)
- Add configurable undo limit
- Compress snapshots using JUCE's `GZIPCompressorOutputStream`

### 4. Thread Safety

The undo system runs on the **UI thread** (ImGui render thread), separate from the audio thread:

✅ **Safe:**
- Calling `pushSnapshot()` from UI event handlers
- Calling `restoreSnapshot()` from keyboard shortcuts
- Accessing `undoStack`/`redoStack` in render loop

❌ **Unsafe:**
- Calling undo methods from `processBlock()`
- Accessing undo stacks from audio callback

**The synth's state serialization is thread-safe** via JUCE's audio processor lock.

---

## Implementation Examples

### Example 1: Adding Undo to a New Menu Action

```cpp
// In ImGuiNodeEditorComponent.cpp - Menu rendering code
if (ImGui::MenuItem("My New Action"))
{
    if (synth != nullptr)
    {
        // Perform the action
        synth->performCustomOperation();
        
        // Make it undoable
        pushSnapshot();
    }
}
```

### Example 2: Adding Undo to a Custom Module Operation

```cpp
// In your CustomModuleProcessor.cpp
void CustomModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String&)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    if (ImGui::Button("Randomize Settings"))
    {
        randomizeInternalSettings();
        onModificationEnded(); // Create undo state
    }
}
```

### Example 3: Undo After Complex Multi-Step Operation

```cpp
void ImGuiNodeEditorComponent::handleCustomAutoConnect()
{
    // Step 1: Create multiple nodes
    auto osc1 = synth->addModule("VCO");
    auto osc2 = synth->addModule("VCO");
    auto mixer = synth->addModule("Mixer");
    
    // Step 2: Connect them
    synth->connect(osc1, 0, mixer, 0);
    synth->connect(osc2, 0, mixer, 1);
    
    // Step 3: Position them nicely
    pendingNodePositions[(int)osc1Lid] = ImVec2(100, 100);
    pendingNodePositions[(int)osc2Lid] = ImVec2(100, 300);
    pendingNodePositions[(int)mixerLid] = ImVec2(400, 200);
    
    // Step 4: ONE undo state for the entire operation
    pushSnapshot();
}
```

---

## Limitations and Considerations

### Current Limitations

1. **No Undo Limit**: The stack grows unbounded (until memory exhaustion)
   - **Mitigation**: Could add `maxUndoStackSize` parameter

2. **Full State Storage**: Every snapshot stores the complete patch
   - **Mitigation**: Could implement delta/diff-based snapshots

3. **No Undo Across Sessions**: Undo history is cleared when synth is destroyed
   - **Mitigation**: Could serialize undo stack to disk

4. **No Undo for File Operations**: Loading/saving patches clears undo history
   - **Mitigation**: Could preserve undo stack through loads

5. **Memory Usage**: Large patches with long history can consume significant RAM
   - **Mitigation**: Add optional compression for old snapshots

### Design Trade-offs

**Simplicity vs. Efficiency:**
- Current design prioritizes simplicity and reliability
- Full state snapshots are easier to implement and debug
- Delta-based systems are more complex but more efficient

**Consistency vs. Granularity:**
- System creates snapshots at logical operation boundaries
- User can't undo individual sub-steps of complex operations
- This prevents "partial undo" bugs where patch is in invalid state

---

## State Restoration Details

### What Gets Restored?

#### Audio Graph State:
- All module processor parameter values (APVTS state)
- Audio connection topology (which pins connect to which)
- Modulation routing (parameter modulation connections)
- Module-specific internal state (sequencer patterns, sample references, etc.)

#### UI/Editor State:
- Node positions (X, Y coordinates in grid space)
- Node muted/bypassed states
- Output node position
- **NOT restored**: selection state, zoom level (these are transient)

### Restore Order

The restoration happens in two phases:

1. **Audio State First** (line 4241-4242):
   ```cpp
   synth->setStateInformation(s.synthState.getData(), (int)s.synthState.getSize());
   ```
   - Reconstructs the entire audio graph
   - May add/remove modules
   - Reconnects all audio and modulation routing

2. **UI State Second** (line 4244):
   ```cpp
   applyUiValueTreeNow(s.uiState);
   ```
   - Repositions nodes to match saved layout
   - Restores visual properties

**Why This Order?**
- UI state depends on nodes existing in the synth
- Restoring audio first ensures all node IDs are valid
- UI restore then matches visual layout to restored graph

### Graph Rebuild After Restore

After restoration, the system triggers a graph rebuild:
```cpp
graphNeedsRebuild = true;
```

This flag tells the renderer to:
1. Clear all transient rendering data
2. Rebuild ImNodes visual representation
3. Recompute link IDs and pin positions
4. Update node appearance based on new state

---

## Advanced Topics

### Dirty State Tracking

The `isPatchDirty` flag (line 4236, 4227) tracks whether the patch has unsaved changes:

```cpp
pushSnapshot()
{
    // ... create snapshot ...
    isPatchDirty = true; // Mark patch as having unsaved changes
}
```

This flag is used to:
- Display "*" in window title
- Prompt user before closing unsaved work
- Determine if save is needed

**Reset when:**
- Patch is saved to disk
- New patch is loaded
- Synth model is replaced

### Transient Data Clearing

After undo/redo, certain frame-specific data is cleared:

```cpp
linkIdToAttrs.clear();
```

**Why?**
- `linkIdToAttrs` maps ImNodes link IDs to audio connection attributes
- These IDs are assigned per-frame during rendering
- After graph structure changes, old IDs are invalid
- Clearing forces regeneration on next frame

### Pending Position Handling

The `pendingNodePositions` map stores positions for nodes that will exist after current operation:

```cpp
std::unordered_map<int, ImVec2> pendingNodePositions;
```

**Used for:**
- Positioning newly created nodes before they render
- Applying positions after batch operations
- Ensuring snapshots capture intended layout

**Special handling in `pushSnapshot()`:**
- Merges pending positions into captured UI state
- Ensures undo will restore correct positions
- Prevents (0,0) placeholder positions from being saved

---

## Debugging Tips

### Inspecting Undo Stack

Add debug logging to track snapshot creation:

```cpp
void ImGuiNodeEditorComponent::pushSnapshot()
{
    Snapshot s;
    s.uiState = getUiValueTree();
    if (synth != nullptr)
        synth->getStateInformation(s.synthState);
    
    // Debug: Log snapshot details
    DBG("=== SNAPSHOT CREATED ===");
    DBG("Undo stack size: " + juce::String(undoStack.size()));
    DBG("Synth state size: " + juce::String(s.synthState.getSize()) + " bytes");
    DBG("UI nodes: " + juce::String(s.uiState.getNumChildren()));
    
    undoStack.push_back(std::move(s));
    redoStack.clear();
    isPatchDirty = true;
}
```

### Validating Restoration

Add assertions to ensure restoration succeeds:

```cpp
void ImGuiNodeEditorComponent::restoreSnapshot(const Snapshot& s)
{
    jassert(s.synthState.getSize() > 0); // Should have valid state
    
    if (synth != nullptr && s.synthState.getSize() > 0)
    {
        synth->setStateInformation(s.synthState.getData(), (int)s.synthState.getSize());
        DBG("Restored synth state: " + juce::String(synth->getModulesInfo().size()) + " modules");
    }
    
    applyUiValueTreeNow(s.uiState);
    DBG("Restored UI state: " + juce::String(s.uiState.getNumChildren()) + " nodes");
}
```

### Common Issues

**Issue: Undo doesn't restore node positions**
- Check that `getUiValueTree()` is capturing positions correctly
- Verify `applyUiValueTreeNow()` is applying them
- Ensure `graphNeedsRebuild` isn't preventing position queries

**Issue: Undo causes audio glitches**
- Restoration happens on UI thread; shouldn't affect audio
- If glitches occur, investigate audio processor lock contention
- Check if modules are properly implementing `setStateInformation()`

**Issue: Redo stack unexpectedly cleared**
- This is intentional - any new action invalidates redo
- If you want to preserve redo after specific operations, modify `pushSnapshot()`

---

## Future Enhancement Ideas

### 1. Delta-Based Snapshots

Instead of storing full state each time, store only what changed:

```cpp
struct DeltaSnapshot
{
    std::vector<ModuleChange> moduleChanges;  // Only changed modules
    std::vector<ConnectionChange> linkChanges; // Only changed connections
    std::vector<PositionChange> positionChanges; // Only moved nodes
};
```

**Pros:**
- Much smaller memory footprint
- Faster snapshot creation
- Can support longer history

**Cons:**
- More complex implementation
- Harder to debug
- Requires careful change tracking

### 2. Undo Limit with Circular Buffer

Implement a maximum stack size with automatic pruning:

```cpp
const size_t MAX_UNDO_STATES = 100;

void pushSnapshot()
{
    // ... create snapshot ...
    
    undoStack.push_back(std::move(s));
    
    // Prune oldest if over limit
    if (undoStack.size() > MAX_UNDO_STATES)
        undoStack.erase(undoStack.begin());
    
    redoStack.clear();
}
```

### 3. Persistent Undo Across Sessions

Save undo history with the patch:

```cpp
void savePatchWithHistory(const juce::File& file)
{
    juce::ValueTree root("PatchData");
    
    // Save current state
    root.addChild(getCurrentPatchTree(), -1, nullptr);
    
    // Save undo history
    juce::ValueTree history("UndoHistory");
    for (const auto& snapshot : undoStack)
        history.addChild(serializeSnapshot(snapshot), -1, nullptr);
    
    root.addChild(history, -1, nullptr);
    
    // Write to disk
    auto xml = root.createXml();
    xml->writeTo(file);
}
```

### 4. Undo Coalescing

Merge consecutive similar operations:

```cpp
void pushSnapshot(SnapshotType type = SnapshotType::Normal)
{
    // If last snapshot was a parameter change and this is too,
    // replace instead of adding new
    if (!undoStack.empty() &&
        type == SnapshotType::ParameterChange &&
        undoStack.back().type == SnapshotType::ParameterChange)
    {
        undoStack.back() = createSnapshot();
        return;
    }
    
    undoStack.push_back(createSnapshot());
    redoStack.clear();
}
```

### 5. Branching History (Git-like)

Allow exploring alternate edit branches:

```
        ┌─ Branch A ─┐
State 1 ─┤            ├─ State 3 (current)
        └─ Branch B ─┘
```

This would require a tree structure instead of linear stacks.

---

## Summary

The undo/redo system is a **full-state snapshot architecture** that captures both audio processing and UI layout at strategic points in the user's workflow. It prioritizes:

1. **Reliability**: Complete state capture eliminates partial-undo bugs
2. **Simplicity**: Linear history is easy to reason about and debug
3. **Consistency**: Operations undo as complete units, not individual sub-steps
4. **Integration**: Works seamlessly with JUCE's state serialization

The system is extensible and can be enhanced with delta-based storage, undo limits, and persistent history as needed. For most use cases, the current implementation provides robust, predictable undo/redo functionality with minimal complexity.

---

**Last Updated**: October 2025  
**Version**: 1.0  
**Related Files**:
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h`
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`
- `juce/Source/audio/modules/ModuleProcessor.h`

