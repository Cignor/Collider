# 🎛️ Tempo Clock UI Control Upgrade

**Date**: 2025-10-25  
**Status**: ✅ **COMPLETE**  
**Feature**: Tempo Clock now controls the top bar BPM display when "Sync to Host" is disabled

---

## 🎯 What Changed

### ❌ Previous Behavior
- Top bar BPM control was always editable
- No visual feedback when a Tempo Clock module was controlling the global tempo
- Confusing UX: users could change the top bar BPM, but it would be immediately overridden

### ✅ New Behavior
- When a Tempo Clock has **"Sync to Host" disabled**: 
  - It **pushes** its BPM to the global transport
  - The top bar BPM control becomes **greyed out** (disabled)
  - Hovering shows a tooltip explaining the control is disabled
- When a Tempo Clock has **"Sync to Host" enabled**:
  - It **pulls** BPM from the global transport
  - The top bar BPM control remains editable
- When no Tempo Clock exists (or all have sync enabled):
  - The top bar BPM control is fully editable

---

## 🔧 Technical Implementation

### 1. Added Flag to `TransportState`

**File**: `juce/Source/audio/modules/ModuleProcessor.h`

```cpp
struct TransportState {
    bool isPlaying = false;
    double bpm = 120.0;
    double songPositionBeats = 0.0;
    double songPositionSeconds = 0.0;
    int globalDivisionIndex = -1;
    
    // NEW: Flag to indicate if a Tempo Clock module is controlling the BPM
    bool isTempoControlledByModule = false;
};
```

**Purpose**: Signal to the UI that tempo is being controlled by a module

---

### 2. Updated Tempo Clock Logic

**File**: `juce/Source/audio/modules/TempoClockModuleProcessor.cpp`

#### Before:
```cpp
bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;
if (syncToHost)
{
    // Override local BPM with transport BPM
    bpm = (float)m_currentTransport.bpm;
    if (auto* parent = getParent())
        parent->setBPM(bpm);
}
```

#### After:
```cpp
bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;
if (syncToHost)
{
    // Pull tempo FROM host transport
    bpm = (float)m_currentTransport.bpm;
}
else
{
    // Push tempo TO host transport (Tempo Clock controls the global BPM)
    if (auto* parent = getParent())
    {
        parent->setBPM(bpm);
        parent->setTempoControlledByModule(true);  // Signal that UI should be greyed
    }
}
```

**Key Changes**:
- **Sync enabled**: Pull tempo from transport (Tempo Clock follows)
- **Sync disabled**: Push tempo to transport + set control flag (Tempo Clock leads)

---

### 3. Added Flag Reset in Audio Processing

**File**: `juce/Source/audio/graph/ModularSynthProcessor.cpp`

```cpp
void ModularSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    try {
        // Reset tempo control flag (will be set by Tempo Clock modules if active)
        m_transportState.isTempoControlledByModule = false;
        
        // ... rest of processing
    }
}
```

**Purpose**: Reset flag each block so it only stays true while an active Tempo Clock is controlling

---

### 4. Added Setter Method

**File**: `juce/Source/audio/graph/ModularSynthProcessor.h`

```cpp
void setTempoControlledByModule(bool controlled) 
{ 
    m_transportState.isTempoControlledByModule = controlled; 
}
```

---

### 5. Updated Top Bar UI

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

#### Before:
```cpp
// BPM control
float bpm = static_cast<float>(transportState.bpm);
ImGui::SetNextItemWidth(80.0f);
if (ImGui::DragFloat("BPM", &bpm, 0.1f, 20.0f, 999.0f, "%.1f"))
    synth->setBPM(static_cast<double>(bpm));
```

#### After:
```cpp
// BPM control (greyed out if controlled by Tempo Clock module)
float bpm = static_cast<float>(transportState.bpm);
ImGui::SetNextItemWidth(80.0f);

bool isControlled = transportState.isTempoControlledByModule;
if (isControlled)
    ImGui::BeginDisabled();
    
if (ImGui::DragFloat("BPM", &bpm, 0.1f, 20.0f, 999.0f, "%.1f"))
    synth->setBPM(static_cast<double>(bpm));
    
if (isControlled)
{
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Tempo Clock Module Active");
        ImGui::TextUnformatted("A Tempo Clock node with 'Sync to Host' disabled is controlling the global BPM.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
```

**Visual Features**:
- BPM control becomes visually greyed out
- Tooltip with yellow header explains the situation
- User can still hover over the disabled control

---

## 🎨 User Experience

### Scenario 1: No Tempo Clock
```
Top Bar: [BPM: 120.0 ▼]  ← Editable, normal appearance
```

### Scenario 2: Tempo Clock with Sync Enabled
```
Tempo Clock Node: [✓ Sync to Host]
Top Bar: [BPM: 120.0 ▼]  ← Editable, normal appearance
(Tempo Clock follows the top bar BPM)
```

### Scenario 3: Tempo Clock with Sync Disabled
```
Tempo Clock Node: [☐ Sync to Host] [BPM: 140.0]
Top Bar: [BPM: 140.0 ▼]  ← GREYED OUT
                         ↑ Hover shows: "Tempo Clock Module Active"
(Top bar follows the Tempo Clock node)
```

### Scenario 4: Multiple Tempo Clocks
```
Clock A: [☐ Sync to Host] [BPM: 140.0]  ← This one controls
Clock B: [✓ Sync to Host]               ← This one follows
Top Bar: [BPM: 140.0 ▼]  ← GREYED OUT
```

---

## 🔄 Control Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Start of processBlock()                                   │
│    └─► m_transportState.isTempoControlledByModule = false    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Tempo Clock Module processBlock()                         │
│    ├─► Sync to Host = ON?                                    │
│    │   └─► Pull BPM from transport (flag stays false)        │
│    │                                                          │
│    └─► Sync to Host = OFF?                                   │
│        └─► Push BPM to transport                             │
│            └─► setTempoControlledByModule(true)              │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. ImGui UI Render (next frame)                              │
│    ├─► Check: transportState.isTempoControlledByModule       │
│    │                                                          │
│    ├─► TRUE?  → Grey out BPM control + show tooltip          │
│    └─► FALSE? → BPM control remains editable                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 🧪 Testing Checklist

- [x] No Tempo Clock: Top bar BPM is editable
- [x] Tempo Clock with sync ON: Top bar BPM is editable
- [x] Tempo Clock with sync OFF: Top bar BPM is greyed out
- [x] Hover tooltip shows explanation
- [x] Multiple Tempo Clocks: Any with sync OFF greys out the bar
- [x] Deleting Tempo Clock: Top bar becomes editable again
- [x] No linter errors

---

## 📝 Related Files Modified

1. `juce/Source/audio/modules/ModuleProcessor.h` - Added `isTempoControlledByModule` flag
2. `juce/Source/audio/modules/TempoClockModuleProcessor.cpp` - Updated BPM control logic
3. `juce/Source/audio/graph/ModularSynthProcessor.h` - Added setter method
4. `juce/Source/audio/graph/ModularSynthProcessor.cpp` - Added flag reset in processBlock
5. `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` - Updated BPM UI with disabled state

---

## 🎓 Design Rationale

### Why Grey Out Instead of Hide?
- **Visibility**: User can still see the current BPM value
- **Discoverability**: Greyed control + tooltip teaches the user about the feature
- **Consistency**: Matches ImGui design patterns (disabled vs hidden)

### Why Reset Flag Each Block?
- **Automatic**: No manual cleanup needed
- **Correct**: If all Tempo Clocks are deleted/disabled, flag automatically clears
- **Thread-Safe**: Flag is only modified on audio thread

### Why Check in UI Instead of Audio Thread?
- **Separation of Concerns**: Audio thread sets state, UI reads state
- **No Locks**: Direct read of primitive bool (atomic not needed for read-only UI)
- **Performance**: No audio thread overhead for UI logic

---

## 🚀 Future Enhancements

### Potential Ideas:
1. **Show Controlling Node Name**: Tooltip could say "Controlled by Tempo Clock #3"
2. **Visual Indicator**: Small icon next to BPM showing module control
3. **Multi-Clock Warning**: If multiple clocks have sync OFF, show warning
4. **Override Button**: Quick toggle to disable all Tempo Clock control

---

## ✅ Conclusion

This upgrade significantly improves the UX of the Tempo Clock module by providing clear visual feedback about tempo control. Users now have an intuitive understanding of which component (top bar vs module) is controlling the global BPM at any given time.

The implementation is clean, performant, and follows JUCE/ImGui best practices for disabled UI elements.

