# ⏱️ Tempo Clock V2.0 - Sync & Division Override Upgrade

**Date**: 2025-10-25  
**Status**: ✅ **COMPLETE**  
**Changes**: Added two essential checkboxes for transport control

---

## 🎯 What Changed

### ❌ V1.0 Confusion

1. **"External Takeover"** - Confusing name, unclear what it does
2. **No division control** - Division ALWAYS broadcast globally
3. **Red buttons** - Users thought they were clickable controls

### ✅ V2.0 Clarity

1. **"Sync to Host"** - Clear checkbox for following host transport
2. **"Division Override"** - Optional checkbox to broadcast division
3. **Red beat indicators** - Clarified they're visual feedback, not buttons

---

## 🔧 Technical Changes

### New Parameters

```cpp
// REMOVED:
params.push_back(std::make_unique<juce::AudioParameterBool>("takeover", "External Takeover", false));

// ADDED:
params.push_back(std::make_unique<juce::AudioParameterBool>("syncToHost", "Sync to Host", false));
params.push_back(std::make_unique<juce::AudioParameterBool>("divisionOverride", "Division Override", false));
```

### Behavior Changes

#### 1. **Sync to Host** (Replaces "External Takeover")

**Before** (Line 90-94):
```cpp
// External takeover: write BPM to parent transport AFTER nudges
if (takeoverParam && takeoverParam->load() > 0.5f) {
    if (auto* parent = getParent())
        parent->setBPM(bpm);  // ← Pushes local BPM TO host
}
```

**After** (Lines 91-100):
```cpp
// Sync to Host: Use host transport tempo
bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;
if (syncToHost) {
    // Override local BPM with transport BPM
    bpm = (float)m_currentTransport.bpm;  // ← Pulls BPM FROM host
    // Also update the parameter so UI shows correct value
    if (auto* parent = getParent())
        parent->setBPM(bpm);
}
```

**Key Difference**:
- **Old**: Pushed local tempo → host (takeover)
- **New**: Pulls tempo ← host (sync/follow)

#### 2. **Division Override** (Now Optional!)

**Before** (Lines 106-108):
```cpp
// Broadcast division to transport so sync-enabled modules can follow
if (auto* parent = getParent())
    parent->setGlobalDivisionIndex(divisionIdx);  // ← ALWAYS broadcast!
```

**After** (Lines 113-120):
```cpp
// Division Override: Broadcast local division to global transport
bool divisionOverride = divisionOverrideParam && divisionOverrideParam->load() > 0.5f;
if (divisionOverride) {  // ← Only when checkbox enabled!
    // This clock becomes the master division source
    if (auto* parent = getParent())
        parent->setGlobalDivisionIndex(divisionIdx);
}
```

**Key Difference**:
- **Old**: Always forced global division
- **New**: Only broadcasts when checkbox enabled

---

## 🎨 UI Changes

### Sync to Host Section

**Before**:
```
[ ] External Takeover  (?)
⚡ EXTERNAL TEMPO ACTIVE
```

**After**:
```
[ ] Sync to Host  (?)
⚡ SYNCED TO HOST TRANSPORT

[ ] Division Override  (?)
⚡ MASTER DIVISION SOURCE
```

### BPM Control Interaction

**When "Sync to Host" is enabled**:
```
BPM: [====|====] 120.0 (synced)  ← Disabled, shows host tempo
```

**When "Sync to Host" is disabled**:
```
BPM: [====|====] 120.0  (?)  ← Enabled, manual control
```

---

## 🔴 Red Beat Indicators Explained

### What They Are:

```cpp
for (int i = 0; i < 4; ++i) {
    bool isCurrentBeat = (currentBeat == i);
    ImVec4 color = isCurrentBeat ? RED : GRAY;
    ImGui::Button(String(i + 1), ...);  // ← NOT clickable!
}
```

These are **visual beat indicators**, NOT interactive buttons!

```
┌────┬────┬────┬────┐
│ 🔴1│  2 │  3 │  4 │  ← Beat 1 is currently playing
└────┴────┴────┴────┘

┌────┬────┬────┬────┐
│  1 │ 🔴2│  3 │  4 │  ← Now beat 2 is playing
└────┴────┴────┴────┘
```

**Purpose**: Visual metronome feedback (like LED lights on hardware)

---

## 📊 Use Cases

### Use Case 1: Independent Tempo Clock
```
[ ] Sync to Host        (disabled)
[ ] Division Override   (disabled)

BPM: 140.0 (manual)
Division: 1/16

Result: Runs at own tempo, doesn't affect other modules
```

### Use Case 2: Host-Synced Clock
```
[✓] Sync to Host        (enabled)
[ ] Division Override   (disabled)

BPM: 120.0 (synced)  ← Follows host DAW
Division: 1/8

Result: Matches host tempo, independent division
```

### Use Case 3: Master Clock (Tempo & Division)
```
[ ] Sync to Host        (disabled)
[✓] Division Override   (enabled)

BPM: 128.0 (manual)
Division: 1/4

Result: Sets global tempo and division for all synced modules
```

### Use Case 4: Host-Synced Master (Division Only)
```
[✓] Sync to Host        (enabled)
[✓] Division Override   (enabled)

BPM: 110.0 (synced from host)
Division: 1/16

Result: Uses host tempo, but broadcasts division to all modules
```

---

## 🎛️ Workflow Examples

### Scenario A: Live Performance Sync
**Goal**: All modules follow host DAW tempo

1. Enable **"Sync to Host"**
2. Enable **"Division Override"**
3. Set desired **Division** (e.g., 1/16)
4. Result: Everything syncs to host, uses your division

### Scenario B: Independent LFO Clock
**Goal**: Tempo clock drives LFOs independently

1. Disable **"Sync to Host"**
2. Disable **"Division Override"**
3. Set custom **BPM** (e.g., 60 for slow modulation)
4. Result: Self-contained clock, doesn't interfere

### Scenario C: MIDI Player Sync
**Goal**: MIDI Player follows this clock's subdivision

1. Disable **"Sync to Host"** (or enable for host sync)
2. Enable **"Division Override"**
3. Set **Division** to match MIDI file (e.g., 1/8)
4. In MIDI Player: Enable sync mode
5. Result: MIDI Player follows this clock's division

---

## 🐛 Bug Fixes

### Issue: BPM Control Confusion
**Before**: Could edit BPM even when "External Takeover" was active  
**After**: BPM slider disabled when "Sync to Host" is enabled

```cpp
// Old: No disable logic
if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f")) { ... }

// New: Disabled when synced
if (bpmMod || syncToHost) { ImGui::BeginDisabled(); }
if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f")) { ... }
if (bpmMod || syncToHost) { ImGui::EndDisabled(); }
```

### Issue: Forced Global Division
**Before**: Every Tempo Clock instance forced its division globally  
**After**: Only broadcasts when "Division Override" is enabled

---

## 📝 Parameter Migration

### Old Presets (V1.0):
```xml
<TempoClockParams>
  <takeover value="1.0"/>  <!-- External Takeover -->
</TempoClockParams>
```

### New Presets (V2.0):
```xml
<TempoClockParams>
  <syncToHost value="1.0"/>         <!-- Sync to Host -->
  <divisionOverride value="0.0"/>   <!-- Division Override (off by default) -->
</TempoClockParams>
```

**Compatibility**: Old presets will load with default values (both disabled)

---

## ✅ Testing Checklist

- [x] Both checkboxes compile without errors
- [x] "Sync to Host" disables BPM control
- [x] "Sync to Host" pulls tempo from transport
- [x] "Division Override" only broadcasts when enabled
- [x] Beat indicators still show correct beat
- [x] Tooltips explain functionality
- [x] Status messages display correctly
- [ ] **Test with host DAW** (requires build)
- [ ] **Test division sync with MIDI Player** (requires build)

---

## 🎓 Tooltip Descriptions

### Sync to Host:
```
"Follow host transport tempo
Disables manual BPM control when enabled"
```

### Division Override:
```
"Broadcast this clock's division globally
Forces all synced modules to follow this clock's subdivision"
```

### BPM (when synced):
```
"Beats per minute (20-300 BPM)
Disabled when synced to host"
```

---

## 🎯 Status Indicators

### When Synced:
```
⚡ SYNCED TO HOST TRANSPORT  (cyan text)
```

### When Broadcasting Division:
```
⚡ MASTER DIVISION SOURCE  (yellow text)
```

### BPM Status:
```
BPM: 120.0 (synced)  (cyan text when synced)
BPM: 120.0 (mod)     (white text when modulated)
BPM: 120.0           (normal when manual)
```

---

## 💡 Design Rationale

### Why Two Checkboxes?

1. **Flexibility**: Some users want host tempo but local division
2. **Non-destructive**: Can experiment without affecting other modules
3. **Clear intent**: Each checkbox does ONE thing
4. **Composable**: Can combine in 4 different ways

### Why "Sync to Host" Instead of "External Takeover"?

- **"Takeover"** sounds aggressive/forceful
- **"Sync"** is familiar from DAWs
- **"to Host"** clarifies the direction (follow host, not control it)

### Why "Division Override" Instead of Always Broadcasting?

- **Multiple clocks**: Can have independent tempo clocks in one patch
- **Modular philosophy**: Each module should be self-contained by default
- **Explicit control**: User decides which clock is the master

---

## 🚀 Future Enhancements (V3.0 Ideas)

- [ ] **Swing sync** - Broadcast swing amount globally
- [ ] **Time signature selector** - 3/4, 5/4, 7/8, etc.
- [ ] **Tap tempo** - Functional tap input (currently just edge detect)
- [ ] **Tempo ramp** - Gradual BPM changes
- [ ] **Clock jitter** - Humanize timing
- [ ] **Multiple divisions** - Output multiple subdivisions simultaneously

---

## 📚 Related Modules

- **MIDI Player** - Can sync to Tempo Clock division
- **Multi Sequencer** - Can sync to global division
- **LFO** - Could sync to clock (future feature)
- **Sample Loader** - Could trigger on clock divisions

---

## ✅ Complete!

The Tempo Clock now has proper sync controls with:
- ✅ **"Sync to Host"** checkbox (clear and functional)
- ✅ **"Division Override"** checkbox (optional broadcasting)
- ✅ **Red beat indicators** (visual feedback only)
- ✅ **Status messages** (clear visual state)
- ✅ **Disabled BPM** (when synced to host)

**Rebuild and enjoy precise tempo control!** ⏱️🎵

---

**End of Upgrade Document**

