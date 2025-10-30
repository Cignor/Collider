# Menu Reorganization Summary

## Overview
All node menus in the ImGuiNodeEditorComponent have been reorganized to match the structure defined in `Nodes_Dictionary.md`.

## Changes Made

### 1. Updated Module Category Enum
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.h` (Line 393)

**Old:**
```cpp
enum class ModuleCategory { Source, Effect, Modulator, Utility, Analysis, Comment, Plugin, MIDI, Physics, OpenCV };
```

**New:**
```cpp
enum class ModuleCategory { Source, Effect, Modulator, Utility, Seq, MIDI, Analysis, TTS_Voice, Special_Exp, OpenCV, Sys, Comment, Plugin };
```

**Added Categories:**
- `Seq` - For all sequencer modules (renamed from `Sequencer` to avoid Windows conflicts)
- `TTS_Voice` - For text-to-speech modules (renamed from `TTS` to avoid Windows conflicts)
- `Special_Exp` - Replaces "Physics", includes experimental modules (renamed from `Special` to avoid Windows conflicts)
- `Sys` - For patch organization and system utilities (renamed from `System` to avoid Windows conflicts)

**Note:** The enum names had to be abbreviated to avoid conflicts with Windows macros and reserved keywords. The user-facing menu labels remain unchanged (e.g., "Sequencers", "TTS", "Special", "System").

### 2. Updated Left Panel Menu
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 1434-1611)

**New Category Order:**
1. **Sources** - VCO, PolyVCO, Noise, Audio Input, Sample Loader, Value
2. **Effects** - VCF, Delay, Reverb, Chorus, Phaser, Compressor, Limiter, Noise Gate, Drive, Graphic EQ, Waveshaper, 8-Band Shaper, Granulator, Harmonic Shaper, Time/Pitch Shifter, De-Crackle
3. **Modulators** - LFO, ADSR, Random, S&H, Function Generator, Shaping Oscillator
4. **Utilities & Logic** - VCA, Mixer, CV Mixer, Track Mixer, Attenuverter, Lag Processor, Math, Map Range, Quantizer, Rate, Comparator, Logic, Clock Divider, Sequential Switch
5. **Sequencers** - Sequencer, Multi Sequencer, Tempo Clock, Snapshot Sequencer, Stroke Sequencer
6. **MIDI** - MIDI CV, MIDI Player, MIDI Faders, MIDI Knobs, MIDI Buttons, MIDI Jog Wheel, MIDI Pads, MIDI Logger
7. **Analysis** - Scope, Debug, Input Debug, Frequency Graph
8. **TTS** - TTS Performer, Vocal Tract Filter
9. **Special** - Physics, Animation
10. **Computer Vision** - Webcam Loader, Video File Loader, Movement Detector, Human Detector
11. **Plugins / VST** - All loaded VST/AU plugins
12. **System** - Meta, Inlet, Outlet, Comment, Recorder, VST Host, Best Practice

**Key Changes:**
- Moved sequencers from "Sources" to new "Sequencers" category
- Moved "Tempo Clock" and "Snapshot Sequencer" from "Utilities & Logic" to "Sequencers"
- Created new "TTS" category for TTS Performer and Vocal Tract Filter
- Renamed "Physics Family" to "Special"
- Renamed "OpenCV (Computer Vision)" to "Computer Vision"
- Renamed "MIDI Family" to "MIDI"
- Created new "System" category for Meta, Inlet, Outlet, Comment, Recorder, VST Host, Best Practice
- Moved "Recorder" from "Effects" to "System"
- Moved "Best Practice" from "Utilities & Logic" to "System"
- Moved "De-Crackle" from "Utilities & Logic" to "Effects"

### 3. Updated Right-Click Context Menu
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 3630-3757)

Applied the same reorganization as the Left Panel Menu for consistency.

### 4. Updated Top Bar "Insert Between" Menu
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 692-759)

**Old Categories:**
- Audio Path
- Modulation Path

**New Categories:**
- Effects
- Modulators
- Utilities & Logic
- TTS
- Analysis

### 5. Updated "Insert on Cable" Menu
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 6381-6411)

Reorganized modules with comments indicating their categories for better maintainability.

### 6. Updated Module Categorization Function
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 7212-7291)

Updated `getModuleCategory()` function to properly categorize all modules according to the new structure. This function is used for:
- Node color coding
- Search categorization
- Visual organization

### 7. Updated Color Scheme Function
**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (Lines 7371-7400)

Updated `getImU32ForCategory()` function to include colors for all new categories:
- **Sequencer** - Light Green (90, 140, 90)
- **TTS** - Peach/Coral (255, 180, 100)
- **Special** - Cyan (50, 200, 200) - Reused from old Physics
- **System** - Lavender (120, 100, 140)

## Module Relocations Summary

### Moved to Sequencers:
- Sequencer (from Sources)
- Multi Sequencer (from Sources)
- Stroke Sequencer (from Sources)
- Tempo Clock (from Utilities & Logic)
- Snapshot Sequencer (from Utilities & Logic)

### Moved to TTS (new category):
- TTS Performer (from Sources)
- Vocal Tract Filter (from Effects)

### Moved to System (new category):
- Meta
- Inlet
- Outlet
- Comment (from Utilities & Logic)
- Recorder (from Effects)
- VST Host
- Best Practice (from Utilities & Logic)

### Moved to Effects:
- De-Crackle (from Utilities & Logic)

## Consistency Achieved

All five menu locations now use the same categorization:
1. Left Panel Module Browser
2. Right-Click Context Menu
3. Top Bar "Insert Between" Menu
4. "Insert on Cable" Popup
5. Search/Color Categorization System

## Benefits

1. **Better Organization** - Modules are now grouped by function rather than arbitrary categories
2. **Easier Navigation** - Users can find modules more intuitively
3. **Consistency** - All menus use the same structure
4. **Scalability** - New categories (TTS, Sequencer, System) make it easier to add new modules
5. **Clearer Naming** - "Special" instead of "Physics Family", "MIDI" instead of "MIDI Family", etc.
6. **Documentation Alignment** - Menus now match the Nodes Dictionary structure

