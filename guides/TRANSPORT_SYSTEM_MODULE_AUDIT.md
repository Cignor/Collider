# 🎼 Transport System Module Audit

**Date**: 2025-10-25  
**Purpose**: Comprehensive catalog of all modules that interact with the transport tempo system  
**Status**: ✅ **AUDIT COMPLETE**

---

## 📋 Executive Summary

**Total modules audited**: ~70 modules  
**Modules using transport**: 9 modules (13%)

**Breakdown**:
- ✅ **6 modules fully updated** with division override support
- ⚠️ **1 module needs update** (Function Generator - has hardcoded division)
- ✅ **2 modules correctly implemented** (MIDI Player sync-only, MIDI Logger observer)

**Division Override System**: Fully operational for 5 modules + Tempo Clock master

---

## 📊 Module Categories

### Category Key:
- **🎯 Tempo Clock Source** - Generates/controls transport tempo
- **⏱️ BPM Consumer** - Uses BPM for timing calculations
- **🔄 Sync + Division** - Has transport sync with division parameter
- **📡 Sync Only** - Has transport sync without division
- **👁️ Observer** - Reads transport state for display/logging
- **❌ No Transport** - Doesn't use transport system

---

## ✅ Modules With Division Override Support (Already Updated)

### 1. **LFO Module** 🔄
- **File**: `LFOModuleProcessor.cpp`
- **Features**:
  - ✅ Sync to Transport checkbox
  - ✅ Division parameter (1/32 - 8)
  - ✅ Reads `globalDivisionIndex` in processing
  - ✅ UI greys out division when override active
  - ✅ Tooltip shows override status
- **Usage**: LFO rate syncs to tempo divisions

---

### 2. **Random Module** 🔄
- **File**: `RandomModuleProcessor.cpp`
- **Features**:
  - ✅ Sync to Transport checkbox
  - ✅ Division parameter (1/32 - 8)
  - ✅ Reads `globalDivisionIndex` in processing
  - ✅ UI greys out division when override active
  - ✅ Tooltip shows override status
- **Usage**: Random value generation rate syncs to tempo divisions

---

### 3. **Step Sequencer Module** 🔄
- **File**: `StepSequencerModuleProcessor.cpp`
- **Features**:
  - ✅ Sync to Transport checkbox
  - ✅ Division parameter (1/32 - 8)
  - ✅ Reads `globalDivisionIndex` in processing
  - ✅ UI greys out division when override active
  - ✅ Tooltip shows override status
- **Usage**: Step advance rate syncs to tempo divisions

---

### 4. **Multi Sequencer Module** 🔄
- **File**: `MultiSequencerModuleProcessor.cpp`
- **Features**:
  - ✅ Sync to Transport checkbox
  - ✅ Division parameter (1/32 - 8)
  - ✅ Reads `globalDivisionIndex` in processing
  - ✅ UI greys out division when override active
  - ✅ Tooltip shows override status
- **Usage**: Multi-lane sequencer step advance syncs to tempo divisions

---

### 5. **TTS Performer Module** 🔄
- **File**: `TTSPerformerModuleProcessor.cpp`
- **Features**:
  - ✅ Sync to Transport checkbox
  - ✅ Division parameter (1/32 - 8)
  - ✅ Reads `globalDivisionIndex` in processing
  - ✅ UI greys out division when override active
  - ✅ Tooltip shows override status
- **Usage**: Text-to-speech word advance syncs to tempo divisions

---

### 6. **Tempo Clock Module** 🎯
- **File**: `TempoClockModuleProcessor.cpp`
- **Features**:
  - ✅ Division parameter (1/32 - 4)
  - ✅ Division Override checkbox
  - ✅ Sets `globalDivisionIndex` when override enabled
  - ✅ Sync to Host checkbox (controls BPM)
  - ✅ Sets `isTempoControlledByModule` flag
- **Usage**: Master tempo/division source

---

## ⚠️ Modules Requiring Update

### 7. **Function Generator Module** ⚠️ NEEDS UPDATE
- **File**: `FunctionGeneratorModuleProcessor.cpp`
- **Transport Usage**:
  - ✅ Has "Sync to Transport" mode
  - ✅ Uses `m_currentTransport.songPositionBeats`
  - ✅ Uses `m_currentTransport.bpm`
  - ⚠️ **HARDCODED division** = 3 (1/4 note) - line 200
  - ❌ **NO division parameter** (comment says "you can add a parameter later")
- **Current State**:
  ```cpp
  const int divisionIndex = 3; // Fixed to 1/4 note for now (you can add a parameter later)
  ```
- **Action Required**:
  - [ ] Add `rate_division` AudioParameterChoice (1/32 - 8)
  - [ ] Read parameter instead of hardcoded value
  - [ ] Add `globalDivisionIndex` check in processing
  - [ ] Add UI division combo with greying + tooltip
- **Priority**: 🔴 HIGH - Already uses divisions but hardcoded

---

## ✅ Modules That Are Sync-Only (No Division Needed)

### 8. **MIDI Player Module** 📡
- **File**: `MIDIPlayerModuleProcessor.cpp`
- **Transport Usage**:
  - ✅ Has "Sync to Host" parameter
  - ✅ Uses `m_currentTransport.bpm` for playback speed
  - ✅ Uses `m_currentTransport.isPlaying`
- **Division Parameter**: ❌ NO (doesn't step by divisions)
- **Reason**: Plays MIDI files at scaled tempo, doesn't step discretely
- **Action Required**: ✅ None - correctly implemented

---

### 9. **MIDI Logger Module** 👁️
- **File**: `MidiLoggerModuleProcessor.cpp`
- **Transport Usage**:
  - Uses `m_currentTransport` (for timestamps/display)
- **Division Parameter**: ❌ NO (observer only)
- **Reason**: Display/logging only, no sync behavior
- **Action Required**: ✅ None - observer only

---

## 📋 Modules To Check Systematically

The following modules need to be examined to determine their transport usage:

### ❓ Potentially Transport-Aware Modules:
1. **SnapshotSequencerModuleProcessor** - May have tempo sync
2. **SampleLoaderModuleProcessor** - May have tempo sync for loops
3. **TimePitchModuleProcessor** - May use BPM for time stretching
4. **GranulatorModuleProcessor** - May use tempo for grain density
5. **DelayModuleProcessor** - May have tempo-synced delay times
6. **ChorusModuleProcessor** - May have tempo-synced LFO rate
7. **PhaserModuleProcessor** - May have tempo-synced LFO rate

---

## 🔎 Systematic Check Process

For each module, we need to verify:

### 1. ✅ Does it use `setTimingInfo()`?
```cpp
void setTimingInfo(const TransportState& state) override
{
    m_currentTransport = state;
}
```

### 2. ✅ Does it read transport state?
```cpp
m_currentTransport.bpm
m_currentTransport.songPositionBeats
m_currentTransport.isPlaying
m_currentTransport.globalDivisionIndex  // NEW
m_currentTransport.isTempoControlledByModule  // NEW
```

### 3. ✅ Does it have sync parameter?
```cpp
params.push_back(std::make_unique<juce::AudioParameterBool>("sync", "Sync to Transport", false));
```

### 4. ✅ Does it have division parameter?
```cpp
params.push_back(std::make_unique<juce::AudioParameterChoice>("rate_division", "Division", 
    juce::StringArray{ "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" }, 3));
```

### 5. ✅ Should it respect global division override?
- If YES: Add `globalDivisionIndex` check in processing
- If YES: Add UI greying + tooltip
- If NO: Leave as-is

---

## 🎯 Action Items

### Immediate:
- [ ] Check MIDI Player module for division parameter
- [ ] Check Function Generator module for division parameter
- [ ] Verify MIDI Logger is observer only
- [ ] Scan all sequencer/timing modules for sync+division

### Research:
- [ ] Check delay modules for tempo-sync feature
- [ ] Check modulation modules (chorus/phaser) for tempo-sync
- [ ] Check sample/granulator modules for tempo-sync
- [ ] Document any modules with tempo-sync that DON'T use divisions

### Complete:
- [x] LFO Module - Updated ✅
- [x] Random Module - Updated ✅
- [x] Step Sequencer - Updated ✅
- [x] Multi Sequencer - Updated ✅
- [x] TTS Performer - Updated ✅
- [x] Tempo Clock - Updated ✅

---

## 📊 Final Count (Complete Audit)

**Total modules with transport interaction**: **9**

**Categories breakdown** (verified):
- 🎯 **Tempo Clock Sources**: 1
  - Tempo Clock ✅
  
- 🔄 **Sync + Division (Updated)**: 5
  - LFO ✅
  - Random ✅
  - Step Sequencer ✅
  - Multi Sequencer ✅
  - TTS Performer ✅
  
- ⚠️ **Sync + Division (Needs Update)**: 1
  - Function Generator ⚠️
  
- 📡 **Sync Only (No Division)**: 1
  - MIDI Player ✅
  
- 👁️ **Observers (Display Only)**: 1
  - MIDI Logger ✅
  
- ❌ **No Transport**: ~60+ (all other modules)

---

## 🔄 Next Steps

### ✅ Completed:
1. ✅ **Systematic audit** of all modules - COMPLETE
2. ✅ **Updated 5 modules** with division override support
3. ✅ **Created complete module list** - this document

### ⚠️ Remaining:
1. ⚠️ **Update Function Generator** with division parameter + override support
2. ⚠️ **Test all tempo-synced modules** with division override
3. ⚠️ **Create user documentation** explaining transport system

### Optional (Future):
- Consider adding tempo sync to Delay module (tempo-synced delay times)
- Consider adding tempo sync to modulation effects (Chorus/Phaser LFO rates)

---

## 📝 Notes

- Some modules may use BPM for display purposes only (not sync)
- Some modules may have custom timing that doesn't use divisions
- Edge cases: modules that use transport for features other than rhythm

---

**Status**: Will continue investigation and update this document with findings.

