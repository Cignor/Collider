# 🎵 MIDI Pad Module - Design Specification

**Version**: 1.0  
**Date**: 2025-10-25  
**Status**: ✅ Approved for Implementation  

---

## 📋 Overview

The **MIDI Pad Module** is a specialized MIDI-to-CV converter optimized for pad controllers (Akai MPD, Novation Launchpad, etc.). It provides 16 independent trigger/gate outputs with velocity capture, designed for drum programming, sample triggering, and rhythmic modulation.

### Fills Critical Gap in MIDI Family

| Module | Domain | MIDI Pad? |
|--------|--------|-----------|
| MIDI CV | Keyboard (monophonic melodic) | ❌ |
| MIDI Faders | CC controllers (continuous) | ❌ |
| MIDI Knobs | CC controllers (continuous) | ❌ |
| MIDI Buttons | CC controllers (on/off) | ❌ |
| MIDI Jog Wheel | CC controllers (rotary) | ❌ |
| **MIDI Pads** | **Percussion (polyphonic triggers)** | ✅ **NEW!** |

---

## 🎯 Design Goals

1. **Polyphonic trigger handling** - multiple simultaneous pad hits
2. **Visual feedback** - animated grid mirroring pad state
3. **Flexible routing** - per-pad gate and velocity outputs
4. **Smart trigger modes** - trigger, gate, toggle, latch
5. **Velocity processing** - curves for dynamic response
6. **Device/channel filtering** - multi-MIDI support like MIDI CV
7. **Low latency** - real-time performance for live drumming

---

## 🏗️ Technical Architecture

### Audio Bus Configuration

```cpp
BusesProperties()
    .withOutput("Main", juce::AudioChannelSet::discreteChannels(33), true)
    // 33 channels:
    //   0-15:  Pad 1-16 Gate outputs
    //   16-31: Pad 1-16 Velocity outputs
    //   32:    Global velocity (last hit)
```

### MIDI Processing

- **Polyphonic note tracking** - each pad has independent state
- **Note-to-pad mapping** - configurable MIDI note ranges
- **Device filtering** - route specific MIDI controllers
- **Channel filtering** - MIDI channel 1-16 or all

### Pad State Management

```cpp
struct PadState {
    std::atomic<bool> gateHigh{false};
    std::atomic<float> velocity{0.0f};
    std::atomic<int> midiNote{-1};
    std::atomic<double> triggerStartTime{0.0};
    bool isActive() const;  // For UI feedback
};

std::array<PadState, 16> padStates;
```

---

## 🎛️ Parameters (APVTS)

### 1. MIDI Routing Parameters

| ID | Name | Type | Range | Default | Description |
|----|------|------|-------|---------|-------------|
| `midiDevice` | MIDI Device | Choice | Device list | 0 (All) | Which MIDI device to listen to |
| `midiChannel` | MIDI Channel | Int | 0-16 | 0 (All) | Which MIDI channel (0 = all) |

### 2. Pad Configuration

| ID | Name | Type | Range | Default | Description |
|----|------|------|-------|---------|-------------|
| `startNote` | Start Note | Int | 0-127 | 36 (C1) | First pad's MIDI note |
| `noteLayout` | Note Layout | Choice | Chromatic/Row | 0 | How notes map to grid |

### 3. Trigger Behavior

| ID | Name | Type | Range | Default | Description |
|----|------|------|-------|---------|-------------|
| `triggerMode` | Trigger Mode | Choice | 4 modes | 0 (Trigger) | Gate/trigger behavior |
| `triggerLength` | Trigger Length | Float | 1-500 ms | 10 ms | Pulse duration (Trigger mode) |
| `velocityCurve` | Velocity Curve | Choice | 4 curves | 0 (Linear) | Velocity response shape |

### 4. Visual Feedback

| ID | Name | Type | Range | Default | Description |
|----|------|------|-------|---------|-------------|
| `colorMode` | Color Mode | Choice | 3 modes | 0 (Velocity) | Grid coloring scheme |

---

## 🎨 UI Design (ImGui Node)

### Layout Structure

```
┌─────────────────────────────────────────┐
│ MIDI Pads                               │ ← Title bar
├─────────────────────────────────────────┤
│ MIDI Routing                            │ ← Section 1
│   Device: All Devices         (?)       │
│   Channel: [Combo: All]       (?)       │
│                                         │
│ Pad Grid (4x4)                          │ ← Section 2
│   ┌────┬────┬────┬────┐                │
│   │ 1● │ 2  │ 3  │ 4● │  ← Visual grid │
│   ├────┼────┼────┼────┤                │
│   │ 5  │ 6● │ 7  │ 8  │                │
│   ├────┼────┼────┼────┤                │
│   │ 9  │ 10 │ 11●│ 12 │                │
│   ├────┼────┼────┼────┤                │
│   │ 13 │ 14 │ 15 │ 16●│                │
│   └────┴────┴────┴────┘                │
│   ● = active (pulsing red)              │
│                                         │
│ Settings                                │ ← Section 3
│   Mode: [Combo: Trigger]      (?)       │
│   Velocity Curve: [Linear]    (?)       │
│   Trigger Length: [10 ms]     (?)       │
│                                         │
│ Note Mapping                            │ ← Section 4
│   Start Note: C1 (#36)        (?)       │
│   Layout: [Chromatic]         (?)       │
│                                         │
│ Statistics                              │ ← Section 5
│   Active Pads: 4/16                     │
│   Last Hit: Pad 6 (vel: 0.87)           │
└─────────────────────────────────────────┘
```

### Custom Grid Drawing

```cpp
// 4x4 grid with custom ImDrawList rendering
const float cellSize = (itemWidth - 16.0f) / 4.0f;
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImVec2 gridStart = ImGui::GetCursorScreenPos();

for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
        int padIdx = row * 4 + col;
        bool isActive = padStates[padIdx].isActive();
        float velocity = padStates[padIdx].velocity.load();
        
        ImVec2 cellPos(
            gridStart.x + col * (cellSize + 4.0f) + 2.0f,
            gridStart.y + row * (cellSize + 4.0f) + 2.0f
        );
        
        ImVec2 cellEnd(
            cellPos.x + cellSize,
            cellPos.y + cellSize
        );
        
        // Background
        ImU32 bgColor = IM_COL32(40, 40, 40, 255);
        drawList->AddRectFilled(cellPos, cellEnd, bgColor, 3.0f);
        
        // Active indicator (pulsing)
        if (isActive) {
            float pulse = 0.6f + 0.4f * std::sin(ImGui::GetTime() * 8.0f);
            int brightness = static_cast<int>(255 * velocity * pulse);
            ImU32 activeColor = IM_COL32(brightness, brightness/4, brightness/4, 255);
            drawList->AddRectFilled(cellPos, cellEnd, activeColor, 3.0f);
        }
        
        // Border
        ImU32 borderColor = isActive 
            ? IM_COL32(255, 100, 100, 255)
            : IM_COL32(100, 100, 100, 255);
        drawList->AddRect(cellPos, cellEnd, borderColor, 3.0f, 0, 2.0f);
        
        // Label
        char label[8];
        sprintf(label, "%d", padIdx + 1);
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos(
            cellPos.x + (cellSize - textSize.x) * 0.5f,
            cellPos.y + (cellSize - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), label);
    }
}

// Reserve space
float gridHeight = 4.0f * (cellSize + 4.0f) + 4.0f;
ImGui::Dummy(ImVec2(itemWidth, gridHeight));
```

### Color Modes

1. **Velocity** (default): Brightness = velocity
2. **Row Colors**: Each row different hue (kick=red, snare=blue, hats=green, perc=yellow)
3. **Fixed**: All pads same color when active

---

## 🔌 I/O Pin Configuration

### Output Pins (33 total)

Using **parallel pin layout** (like PolyVCO):

```cpp
void drawIoPins(const NodePinHelpers& helpers) override
{
    for (int i = 0; i < 16; ++i)
    {
        juce::String padNum = juce::String(i + 1);
        
        // Gate and Velocity on same row
        helpers.drawParallelPins(
            ("Pad " + padNum + " Gate").toRawUTF8(),
            i,                              // Gate output channel
            ("Pad " + padNum + " Vel").toRawUTF8(),
            16 + i                          // Velocity output channel
        );
    }
    
    // Global velocity output (last hit)
    helpers.drawAudioOutputPin("Global Vel", 32);
}
```

### Visual Layout

```
                      ┌─────────────┐
                      │  MIDI Pads  │
                      ├─────────────┤
                      │ [4x4 Grid]  │
                      │             │
                      │ [Settings]  │
                      └─────────────┘
                       │           │
     ┌─────────────────┘           └─────────────────┐
     │                                               │
Pad 1 Gate ───┐                         ┌─── Pad 1 Vel
Pad 2 Gate ───┤                         ├─── Pad 2 Vel
Pad 3 Gate ───┤                         ├─── Pad 3 Vel
    ...       │  Parallel pin layout    │      ...
Pad 16 Gate ──┘                         └─── Pad 16 Vel
                                        
                      Global Vel ───
```

---

## 🎮 Trigger Modes

### Mode 1: Trigger (Default)
- Brief pulse on each pad hit
- Duration controlled by `triggerLength` parameter
- Note-off ignored
- **Use case**: Sample triggering, drum hits

```
Hit:  ↓
Gate: ┐___┐      (fixed length pulse)
      │   │
      └───┘
```

### Mode 2: Gate
- Gate high while pad held
- Gate low on note-off
- **Use case**: Sustained notes, envelope triggering

```
Hit:  ↓         ↑
Gate: ┐_________┘
      │         │
      └─────────┘
```

### Mode 3: Toggle
- Each hit toggles gate state
- Note-off ignored
- **Use case**: On/off switches, step sequencer input

```
Hit:  ↓    ↓    ↓    ↓
Gate: ┐____┘    ┐____┘
      │         │
      └─────────┘
```

### Mode 4: Latch
- Gate high on hit
- Stays high until another pad hit
- **Use case**: One-shot triggers, sample selection

```
Pad 1: ↓              ↓
Pad 2:      ↓    ↓
Gate1: ┐____┘    ┐____┘
       │         │
       └─────────┘
```

---

## 📊 Velocity Curves

### Linear (Default)
```
Output = Input
Simple 1:1 mapping
```

### Exponential
```
Output = Input^2
More dynamic range
Soft hits compressed, loud hits emphasized
```

### Logarithmic
```
Output = log(1 + 9*Input) / log(10)
Compressed dynamic range
Evens out soft and loud hits
```

### Fixed
```
Output = 1.0 (always)
Ignores velocity completely
All hits same volume
```

---

## 🗺️ Note Mapping Modes

### Chromatic (Default)
Pads map to consecutive semitones:
```
C1  C#1  D1  D#1
E1  F1   F#1 G1
G#1 A1   A#1 B1
C2  C#2  D2  D#2
```

### Row Layout
Each row is an octave apart:
```
C1  C#1  D1  D#1
C2  C#2  D2  D#2
C3  C#3  D3  D#3
C4  C#4  D4  D#4
```

### Learn Mode (Future)
- Hit pads in order to assign notes
- Store custom mappings per preset

---

## 💾 Preset Integration

### Example Preset: "Standard Drum Kit"
```xml
<MIDIPads>
  <startNote>36</startNote>  <!-- C1 = Kick -->
  <triggerMode>0</triggerMode>  <!-- Trigger -->
  <triggerLength>15</triggerLength>
  <velocityCurve>1</velocityCurve>  <!-- Exponential -->
  <colorMode>1</colorMode>  <!-- Row colors -->
</MIDIPads>
```

### Patch Examples

1. **16-Voice Drum Machine**
   - Each pad gate → Sample Loader
   - Pad velocity → Sample Loader volume

2. **Live Performance Sampler**
   - Pads 1-8: Drum hits
   - Pads 9-12: Bass samples
   - Pads 13-16: FX triggers

3. **Modulation Matrix**
   - Pad velocities → Multiple VCO frequencies
   - Create rhythmic timbre changes

---

## 🧪 Testing Checklist

### Functional Tests
- [ ] All 16 pads receive and respond to MIDI
- [ ] Polyphonic operation (multiple simultaneous pads)
- [ ] Gate outputs correct timing
- [ ] Velocity outputs accurate (0-1 range)
- [ ] All trigger modes work correctly
- [ ] Velocity curves apply properly
- [ ] Device filtering works
- [ ] Channel filtering works

### UI Tests
- [ ] Grid updates in real-time (<16ms latency)
- [ ] Active pads visually pulse
- [ ] All tooltips present and helpful
- [ ] No separator violations
- [ ] Responsive layout with itemWidth
- [ ] Help markers inline (not new line)

### Performance Tests
- [ ] CPU usage <5% with 16 active pads
- [ ] No audio dropouts during rapid hits
- [ ] Thread-safe MIDI handling
- [ ] Proper cleanup on module deletion

### Edge Cases
- [ ] Handle note-on with velocity 0 (treat as note-off)
- [ ] Multiple note-offs for same pad
- [ ] Rapid repeated hits on same pad
- [ ] All pads active simultaneously
- [ ] MIDI device hot-plug/unplug

---

## 📈 Future Enhancements (v2.0)

1. **Choke Groups**
   - Hi-hat open/closed mutual exclusion
   - Configurable choke assignments

2. **Aftertouch Support**
   - Per-pad pressure outputs
   - Continuous modulation source

3. **RGB Feedback (If Hardware Supports)**
   - Send colors back to pad controller
   - Visual feedback on hardware

4. **Pattern Recording**
   - Record pad hits to internal sequencer
   - Loop and playback

5. **Pad Banks**
   - Switch between 4 banks (64 pads total)
   - Bank selection via MIDI CC

6. **Custom Layouts**
   - 8x8 grid option (64 pads)
   - 4x2 grid option (8 pads)
   - Configurable per preset

---

## 🔧 Implementation Notes

### File Structure
```
juce/Source/audio/modules/
  ├── MIDIPadModuleProcessor.h       (Header with PadState struct)
  └── MIDIPadModuleProcessor.cpp     (Implementation)
```

### Key Classes/Structs

```cpp
struct PadState
{
    std::atomic<bool> gateHigh{false};
    std::atomic<float> velocity{0.0f};
    std::atomic<int> midiNote{-1};
    std::atomic<double> triggerStartTime{0.0};
    
    bool isActive() const {
        // Active if gate high OR recently triggered (for UI animation)
        double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        return gateHigh.load() || (now - triggerStartTime.load() < 0.1);
    }
};
```

### Thread Safety
- **MIDI thread** updates `padStates` atomically
- **Audio thread** reads `padStates` for CV generation
- **GUI thread** reads `padStates` for visual feedback
- All access via atomics - no locks needed!

### Code Reuse
- **MIDI device filtering**: Copy from `MIDICVModuleProcessor`
- **Parallel pin drawing**: Copy from `PolyVCOModuleProcessor`
- **Custom grid drawing**: Inspired by `MIDIJogWheelModuleProcessor` circular drawing
- **ImGui patterns**: Follow `IMGUI_NODE_DESIGN_GUIDE.md` v2.3.2

---

## 📚 References

- **MIDI Handling**: `juce_midi_events/COMPLETE_MIDI_HANDLING_GUIDE.md`
- **UI Patterns**: `IMGUI_NODE_DESIGN_GUIDE.md` v2.3.2
- **Similar Modules**: 
  - `MIDICVModuleProcessor.cpp` (MIDI device handling)
  - `PolyVCOModuleProcessor.cpp` (multi-output parallel pins)
  - `MIDIJogWheelModuleProcessor.cpp` (custom ImDrawList rendering)

---

## ✅ Implementation Status

- [x] Design document completed
- [ ] Header file (`MIDIPadModuleProcessor.h`)
- [ ] Implementation file (`MIDIPadModuleProcessor.cpp`)
- [ ] Integration into `ModularSynthProcessor.cpp`
- [ ] Testing with real MIDI pad controller
- [ ] Example presets created

---

**End of Design Specification**

*Ready for implementation! Estimated time: 4-6 hours*

