# MIDI Player Tempo System Analysis & Fix Plan

## 🔴 **CRITICAL BUG IDENTIFIED**

### Current Behavior (BROKEN):
The tempo calculation system (`syncToHost` + `tempoMultiplier` → `finalBpm`) is **correctly computed** but **NOT applied** to playback!

### Root Cause:
**Line 130 in `MIDIPlayerModuleProcessor.cpp`:**
```cpp
currentPlaybackTime += deltaTime * speed;
```

This uses the **old `speed` parameter** instead of the calculated `finalBpm`.

---

## 📋 **What's Currently Happening**

### 1. **Parameter System** ✅ CORRECT
- `syncToHostParam` (bool) - Default: false
- `tempoMultiplierParam` (float, 0.25x to 4.0x) - Default: 1.0x
- `speedParam` (float, 0.25 to 4.0) - **Legacy parameter, CONFLICTS with tempo system**

### 2. **Tempo Calculation in `processBlock()`** ✅ CORRECT
```cpp
// Line 93: Start with file's tempo
double activeBpm = fileBpm;

// Line 95-108: Override with host BPM if synced
if (syncToHostParam && syncToHostParam->get())
{
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
            if (pos->getBpm().hasValue())
                activeBpm = *pos->getBpm();
}

// Line 111-112: Apply multiplier
const float tempoMult = tempoMultiplierParam->get();
const double finalBpm = activeBpm * tempoMult;

// Line 115-116: Store for display (BUT NOT USED FOR PLAYBACK!)
tempoParam->store((float)finalBpm);
```

### 3. **Playback Time Update** ❌ BROKEN
```cpp
// Line 119-121: Uses SPEED parameter (NOT tempo!)
float speed = speedParam->load();
if (isParamInputConnected("speed"))
    speed *= juce::jmap(getBusBuffer(buffer, true, 0).getReadPointer(0)[0], 0.0f, 1.0f, 0.25f, 4.0f);

// Line 130: Updates playback using SPEED, not tempo
currentPlaybackTime += deltaTime * speed;
```

### 4. **UI Display** ✅ CORRECT
- Shows correct BPM in tooltip
- Shows correct multiplier value
- Shows correct base tempo source (File/Host)

**But the UI is lying - it shows the right numbers but the playback doesn't follow them!**

---

## 🎯 **What SHOULD Be Happening**

### Conceptual Model:

1. **Base Tempo Source** (Priority):
   - If "Sync to Host" = ON → Use application/host BPM
   - If "Sync to Host" = OFF → Use MIDI file's embedded tempo

2. **Tempo Multiplier**:
   - Applied to whichever base tempo is active
   - Example: File = 120 BPM, Multiplier = 2.0x → Play at 240 BPM

3. **Playback Speed Calculation**:
   - MIDI files store timing in beats/ticks, not seconds
   - Playback speed = `finalBpm / fileBpm` (ratio of current tempo to original)
   - If file was recorded at 120 BPM and we play at 240 BPM → 2x faster

4. **The `speed` Parameter**:
   - **Should be REMOVED** or **repurposed** as an additional multiplier on top of tempo
   - Having both `speed` and `tempoMultiplier` is confusing and redundant

---

## 🔧 **THE FIX: Three-Step Solution**

### **Option A: Pure Tempo-Based Playback (RECOMMENDED)**

#### Step 1: Remove `speed` parameter from playback logic
Replace:
```cpp
float speed = speedParam->load();
currentPlaybackTime += deltaTime * speed;
```

With:
```cpp
// Calculate playback speed ratio based on tempo
const double speedRatio = finalBpm / fileBpm;
currentPlaybackTime += deltaTime * speedRatio;
```

#### Step 2: Update note timing interpretation
Notes are currently interpreted using file tempo. They should use `finalBpm`:
```cpp
// Convert note start/end times (which are in file's tempo) to playback time
// This is already partially done, but needs to account for tempo changes
```

#### Step 3: Remove or repurpose Speed parameter
- **Option 3A**: Delete `speedParam` entirely, use only tempo system
- **Option 3B**: Keep `speedParam` as "Transport Speed" (master override, like tape speed)

---

### **Option B: Hybrid Speed/Tempo System**

Keep both parameters but make them cooperate:
```cpp
// 1. Calculate tempo-based speed
const double tempoSpeedRatio = finalBpm / fileBpm;

// 2. Apply master speed control on top
float masterSpeed = speedParam->load();
if (isParamInputConnected("speed"))
    masterSpeed *= juce::jmap(...);

// 3. Combine both
const double combinedSpeed = tempoSpeedRatio * masterSpeed;
currentPlaybackTime += deltaTime * combinedSpeed;
```

**Hierarchy**: Tempo (calculated) → Master Speed (user control)

---

## 🎨 **UI Improvements Needed**

### Current UI Issues:
1. "Sync" checkbox label is too terse (unclear what it does)
2. Tempo multiplier shows "0.52x" but doesn't explain it's relative to file/host
3. No visual indication of which tempo source is active

### Proposed UI:
```
┌─────────────────────────────────────────────────────────┐
│ [Load .mid]  File: "song.mid" (120 BPM)                │
├─────────────────────────────────────────────────────────┤
│ Track: [Show All Tracks ▼]                             │
│                                                         │
│ Tempo Source: [ ] Sync to Host (Current: 120 BPM)     │
│               [✓] Use File Tempo (120 BPM)             │
│                                                         │
│ Speed: [━━━━●━━━━] 1.0x  (Effective: 120 BPM)         │
│                                                         │
│ Zoom:  [━━━━●━━━━] 93 px/beat                         │
└─────────────────────────────────────────────────────────┘
```

**Key Changes:**
1. Radio buttons instead of checkbox (clearer intent)
2. Show current BPM for both sources
3. "Speed" slider replaces "Tempo Multiplier" (more intuitive name)
4. Show "Effective BPM" in real-time

---

## 🧪 **Testing Plan**

### Test Case 1: File Tempo Playback
1. Load MIDI file recorded at 120 BPM
2. Ensure "Sync to Host" is OFF
3. Set multiplier to 1.0x
4. **Expected**: Plays at 120 BPM (original speed)
5. Set multiplier to 2.0x
6. **Expected**: Plays at 240 BPM (2x faster)

### Test Case 2: Host Sync
1. Set application BPM to 140
2. Enable "Sync to Host"
3. Set multiplier to 1.0x
4. **Expected**: Plays at 140 BPM (ignoring file's 120 BPM)
5. Set multiplier to 0.5x
6. **Expected**: Plays at 70 BPM (half of host)

### Test Case 3: Visual Sync
1. Load file, start playback at 1.0x
2. Observe playhead movement against beat grid
3. **Expected**: Playhead should align with beat markers
4. Change multiplier to 2.0x while playing
5. **Expected**: Playhead moves exactly 2x faster

---

## 💡 **Recommendation for Expert AI Review**

### Critical Questions:

1. **Should we keep the `speed` parameter at all?**
   - Pro: Provides "tape speed" control independent of musical tempo
   - Con: Confusing to have both `speed` and `tempoMultiplier`

2. **How should tempo changes in the MIDI file itself be handled?**
   - Current: MIDI files can have tempo change events
   - Issue: Our `fileBpm` is set once at load time
   - Should we: Track tempo changes throughout the file?

3. **What about time signature?**
   - Current: Not considered
   - Issue: Beat grid assumes 4/4 time
   - Should we: Parse and display time signature changes?

4. **Real-time tempo changes during playback?**
   - Current: User can change multiplier while playing
   - Issue: Causes sudden speed changes
   - Should we: Smooth transitions? Quantize changes to measure boundaries?

---

## 🚀 **Implementation Priority**

### Phase 1: Fix Core Playback (URGENT)
- [ ] Replace `speed` with `tempoSpeedRatio` in line 130
- [ ] Test basic tempo-based playback
- [ ] Verify sync to host works

### Phase 2: UI Polish
- [ ] Rename "Tempo Multiplier" to "Speed"
- [ ] Add visual tempo source indicator
- [ ] Show effective BPM in real-time

### Phase 3: Advanced Features
- [ ] Handle MIDI tempo change events
- [ ] Time signature support
- [ ] Tempo ramping/smoothing

---

## 📝 **Code Locations**

| Component | File | Lines |
|-----------|------|-------|
| Parameter Definition | `MIDIPlayerModuleProcessor.cpp` | 33-64 |
| Tempo Calculation | `MIDIPlayerModuleProcessor.cpp` | 91-116 |
| **BROKEN Playback Update** | `MIDIPlayerModuleProcessor.cpp` | **119-130** |
| UI Controls | `MIDIPlayerModuleProcessor.cpp` | 766-805 |
| File Tempo Parsing | `MIDIPlayerModuleProcessor.cpp` | 569-608 |

---

## ✅ **Conclusion**

**The tempo system UI and calculation are correct.**
**The playback engine is ignoring the calculated tempo.**
**Fix: One line change in `processBlock()` to use `finalBpm` instead of `speed`.**

**Estimated Fix Time**: 10 minutes for core fix, 30 minutes for testing, 2 hours for UI improvements.

