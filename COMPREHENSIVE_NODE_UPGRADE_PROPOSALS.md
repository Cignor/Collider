# 🎨 Comprehensive Node UI Upgrade Proposals
**Date**: 2025-10-24  
**Design Standard**: MIDIFaders quality (v2.2)  
**Status**: **AWAITING APPROVAL - DO NOT IMPLEMENT YET**

---

## 📊 Overview
This document contains detailed upgrade proposals for **61 remaining modules**. Each proposal includes:
- Current state analysis
- Proposed section organization
- Specific UI improvements
- Code snippets (not to be implemented yet!)

---

# 🌊 Category 1: Modulation Modules (6 modules)

## 1.1 LFO Module ⭐ HIGH PRIORITY

### Current State
- ✅ Has modulation indicators
- ✅ Transport sync support
- ❌ No section headers
- ❌ No tooltips
- ❌ Flat layout

### Proposed Upgrade
**Add 3 sections with tooltips:**

```cpp
// === LFO PARAMETERS SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "LFO Parameters");
ImGui::Spacing();

// Rate slider with tooltip
[Rate slider with (mod) indicator]
ImGui::SameLine();
HelpMarker("LFO rate in Hz\nLogarithmic scale from 0.05 Hz to 20 Hz");

// Depth slider with tooltip
[Depth slider with (mod) indicator]
ImGui::SameLine();
HelpMarker("LFO depth/amplitude (0-1)\nControls output signal strength");

// Wave combo with tooltip
[Wave combo with (mod) indicator]
ImGui::SameLine();
HelpMarker("Waveform shape:\nSine = smooth\nTri = linear\nSaw = ramp");

// Bipolar checkbox
[Bipolar checkbox]
ImGui::SameLine();
HelpMarker("Bipolar: -1 to +1\nUnipolar: 0 to +1");

ImGui::Spacing();
ImGui::Spacing();

// === TRANSPORT SYNC SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Transport Sync");
ImGui::Spacing();

// Sync checkbox
[Sync checkbox]
ImGui::SameLine();
HelpMarker("Sync LFO rate to host transport tempo");

if (sync)
{
    // Division combo
    [Division combo]
    ImGui::SameLine();
    HelpMarker("Note division for tempo sync\n1/16 = sixteenth notes, 1 = whole notes, etc.");
}

ImGui::Spacing();

// === LIVE OUTPUT SECTION (New!) ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live Output");
ImGui::Spacing();

// Visual LFO output indicator (like MIDI CV's animated gate)
float currentValue = lastOutputValues.size() > 0 ? lastOutputValues[0]->load() : 0.0f;
ImGui::Text("Output: %.3f", currentValue);

// Progress bar showing current LFO position
float normalizedValue = bipolar ? (currentValue + 1.0f) / 2.0f : currentValue;
ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.55f, 0.7f, 0.8f).Value);
ImGui::ProgressBar(normalizedValue, ImVec2(itemWidth, 0), "");
ImGui::PopStyleColor();
```

**Benefits**:
- Clear visual hierarchy
- Educational tooltips for beginners
- Live output visualization (like MIDI CV)
- No functional changes

---

## 1.2 ADSR Module ⭐ HIGH PRIORITY

### Current State
- ✅ Has modulation indicators
- ✅ Logarithmic sliders for time params
- ❌ No sections (all sliders in one list)
- ❌ No tooltips
- ❌ No visual envelope display

### Proposed Upgrade
**Add 2 sections + visual envelope preview:**

```cpp
// === ENVELOPE PARAMETERS SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Envelope Shape");
ImGui::Spacing();

// Attack
[Attack slider with (mod)]
ImGui::SameLine();
HelpMarker("Attack time in seconds\nTime to reach peak from gate trigger");

// Decay
[Decay slider with (mod)]
ImGui::SameLine();
HelpMarker("Decay time in seconds\nTime to reach sustain level");

// Sustain
[Sustain slider with (mod)]
ImGui::SameLine();
HelpMarker("Sustain level (0-1)\nLevel maintained while gate is held");

// Release
[Release slider with (mod)]
ImGui::SameLine();
HelpMarker("Release time in seconds\nTime to fade to zero after gate off");

ImGui::Spacing();
ImGui::Spacing();

// === VISUAL ENVELOPE PREVIEW (New!) ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Envelope Preview");
ImGui::Spacing();

// Draw ADSR curve using ImGui::PlotLines
float adsrCurve[100];
// Generate curve based on A/D/S/R values
// ... (calculate normalized time-based curve)

ImGui::PlotLines("##envelope", adsrCurve, 100, 0, nullptr, 0.0f, 1.0f, ImVec2(itemWidth, 60));

// Show current stage and value
float currentEnvValue = lastOutputValues.size() > 0 ? lastOutputValues[0]->load() : 0.0f;
ImGui::Text("Current: %.3f", currentEnvValue);

// Color-coded stage indicator
const char* stageNames[] = { "Idle", "Attack", "Decay", "Sustain", "Release" };
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.7f, 1.0f));
ImGui::Text("Stage: %s", stageNames[currentStage]);  // Would need to expose stage
ImGui::PopStyleColor();
```

**Benefits**:
- Visual envelope preview (educational!)
- Live stage indicator
- Clear parameter organization
- Tooltips explaining ADSR stages

---

## 1.3 Random Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Random Parameters" (rate, min, max)
- Section: "Output Type" (smooth vs stepped)
- Section: "Live Output" with value display + histogram
- Tooltips for all controls

---

## 1.4 Sample & Hold (SAndH) Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Input/Clock"
- Section: "Sample Settings"
- Visual indicator showing held value
- Trigger LED (like MIDI CV's animated gate)

---

## 1.5 Function Generator Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Function Type" (linear, exponential, logarithmic)
- Section: "Range" (min, max, duration)
- Section: "Trigger"
- Visual curve preview (like ADSR)

---

## 1.6 Lag Processor Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Lag Time" (rise time, fall time)
- Section: "Response Curve"
- Visual: input vs output comparison graph

---

# 🎹 Category 2: Oscillators (3 remaining)

## 2.1 PolyVCO Module ⭐ HIGH PRIORITY

### Current State
- ✅ Dynamic voice count (1-32)
- ✅ Per-voice frequency, wave, gate
- ✅ Modulation indicators
- ❌ **MAJOR ISSUE**: Can show up to 32 voices × 3 params = 96 sliders!
- ❌ No visual organization
- ❌ No tooltips

### Proposed Upgrade
**Add collapsible voice sections + master controls:**

```cpp
// === MASTER CONTROLS SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Master Controls");
ImGui::Spacing();

// Num Voices slider
[NumVoices slider with (mod)]
ImGui::SameLine();
HelpMarker("Number of active voices (1-32)\nEach voice is an independent oscillator");

ImGui::Spacing();
ImGui::Spacing();

// === PER-VOICE CONTROLS SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Voice Parameters");
ImGui::Spacing();

// Use ImGui::TreeNode for collapsible voice groups!
// Show first 4 voices expanded by default, rest collapsed
for (int i = 0; i < activeVoices; ++i)
{
    juce::String voiceLabel = "Voice " + juce::String(i + 1);
    
    // Color-code voice number using HSV
    float hue = (float)i / (float)MAX_VOICES;
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.7f, 1.0f).Value);
    
    // Use TreeNode for collapsible sections
    bool expanded = (i < 4);  // First 4 voices open by default
    if (ImGui::CollapsingHeader(voiceLabel.toRawUTF8(), expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::PopStyleColor();
        ImGui::Indent();
        
        // Frequency
        [Freq slider with (mod)]
        ImGui::SameLine();
        HelpMarker("Voice frequency in Hz");
        
        // Waveform  
        [Wave combo with (mod)]
        ImGui::SameLine();
        HelpMarker("Oscillator waveform");
        
        // Gate
        [Gate slider with (mod)]
        ImGui::SameLine();
        HelpMarker("Voice amplitude (0-1)");
        
        ImGui::Unindent();
    }
    else
    {
        ImGui::PopStyleColor();
    }
}

ImGui::Spacing();

// === QUICK PRESETS (New!) ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Quick Presets");
ImGui::Spacing();

if (ImGui::Button("Major Chord", ImVec2(itemWidth * 0.48f, 0)))
{
    // Set first 3 voices to C-E-G (major triad)
    // Calculate frequencies...
}
ImGui::SameLine();
if (ImGui::Button("Minor Chord", ImVec2(itemWidth * 0.48f, 0)))
{
    // Set first 3 voices to C-Eb-G (minor triad)
}

if (ImGui::Button("Octaves", ImVec2(itemWidth * 0.48f, 0)))
{
    // Set voices to C2, C3, C4, C5...
}
ImGui::SameLine();
if (ImGui::Button("Unison", ImVec2(itemWidth * 0.48f, 0)))
{
    // Set all voices to same frequency
}
```

**Benefits**:
- **Collapsible voices** = dramatically reduced visual clutter
- Color-coded voice numbers (like MIDIFaders)
- Quick preset buttons for common configurations
- First 4 voices visible by default (80% use case)
- Scalable to 32 voices without overwhelming UI

---

## 2.2 ShapingOscillator Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Oscillator" (freq, wave)
- Section: "Waveshaping" (amount, type)
- Section: "Output" with waveform preview
- Visual: Show shaped waveform vs original

---

## 2.3 Noise Module

### Current State Analysis Needed  
**Proposed Approach**:
- Section: "Noise Type" (white, pink, brown)
- Section: "Filtering" (if applicable)
- Section: "Output Level"
- Visual: Spectrum analyzer or level meter

---

# ⏰ Category 3: Clock/Timing (3 modules)

## 3.1 TempoClock Module ⭐ HIGH PRIORITY

### Current State
- ✅ BPM control
- ✅ Swing, division, gate width
- ✅ Transport takeover
- ✅ Tap tempo, nudge, play/stop/reset inputs
- ❌ No visual feedback of clock state
- ❌ No tooltips

### Proposed Upgrade
**Add visual clock display + organized sections:**

```cpp
// === TEMPO CONTROLS SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Tempo");
ImGui::Spacing();

// BPM slider
[BPM slider with (mod)]
ImGui::SameLine();
HelpMarker("Beats per minute (40-300 BPM)");

// Swing slider
[Swing slider with (mod)]
ImGui::SameLine();
HelpMarker("Swing amount (0-100%)\nDelays every other beat for shuffle feel");

ImGui::Spacing();
ImGui::Spacing();

// === CLOCK DIVISION SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Clock Output");
ImGui::Spacing();

// Division combo
[Division combo]
ImGui::SameLine();
HelpMarker("Clock output division\n1/4 = quarter notes, 1/16 = sixteenth notes");

// Gate width slider
[Gate width slider with (mod)]
ImGui::SameLine();
HelpMarker("Gate/trigger pulse width (1-99%)");

ImGui::Spacing();
ImGui::Spacing();

// === LIVE CLOCK DISPLAY (New!) ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Clock Status");
ImGui::Spacing();

// Animated beat indicator (4 boxes for 4/4 time)
for (int i = 0; i < 4; ++i)
{
    if (i > 0) ImGui::SameLine();
    
    bool isCurrentBeat = (currentBeat % 4 == i);
    ImVec4 color = isCurrentBeat ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::Button(juce::String(i + 1).toRawUTF8(), ImVec2(itemWidth * 0.23f, 30));
    ImGui::PopStyleColor();
}

// Current BPM display (large)
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.7f, 1.0f));
ImGui::Text("♩ = %.1f BPM", currentBPM);
ImGui::PopStyleColor();

// Bar:Beat display
ImGui::Text("Bar %d | Beat %d", currentBar, currentBeat);

ImGui::Spacing();
ImGui::Spacing();

// === TRANSPORT SECTION ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Transport Sync");
ImGui::Spacing();

// Takeover checkbox
[Takeover checkbox]
ImGui::SameLine();
HelpMarker("Use host transport tempo instead of manual BPM\nDisables manual BPM control when enabled");
```

**Benefits**:
- **Visual beat indicators** (animated squares)
- Large, readable BPM display
- Bar:Beat counter
- Transport sync explained clearly
- Educational tooltips

---

## 3.2 ClockDivider Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Division Ratios" (×1, ×2, ×4, ×8, ×16, etc.)
- Section: "Output Enables" (checkboxes for each output)
- Visual: LED indicators for each divided clock
- Tooltips explaining clock division

---

## 3.3 Rate Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Rate Control"
- Section: "Multiplier/Divider"
- Visual indicator of current rate
- Tooltips

---

# 🎼 Category 4: Sequencers (3 modules)

## 4.1 StepSequencer Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Sequencer Controls" (steps, speed, mode)
- Section: "Step Values" (grid/table view)
- Section: "Playback" (play, reset, direction)
- Visual: Highlight current step
- Color-coded step values

---

## 4.2 MultiSequencer Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Sequence Selection" (which sequence is active)
- Section: "Step Editor"
- Section: "Playback Controls"
- Collapsible sequences (like PolyVCO voices)

---

## 4.3 SnapshotSequencer Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Snapshot Management" (save, recall, delete)
- Section: "Sequence Order"
- Section: "Playback"
- Visual snapshot previews

---

# 🎚️ Category 5: Filters (3 modules)

## 5.1 VCF (Voltage Controlled Filter) Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Filter Parameters" (cutoff, resonance, type)
- Section: "Filter Type" (LP, HP, BP, Notch with icons)
- Visual: Frequency response curve
- Tooltips explaining filter types

---

## 5.2 VocalTractFilter Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Formant Controls" (vowel selection or manual)
- Section: "Filter Response"
- Visual: Vowel chart or formant display
- Tooltips

---

## 5.3 DeCrackle Module

### Current State Analysis Needed
**Proposed Approach**:
- Section: "Detection Settings" (threshold, sensitivity)
- Section: "Processing" (repair method)
- Visual: Crackle detection meter
- Tooltips

---

# 🎛️ Category 6: Effects (10 modules)

## 6.1 Delay Module ✅ (Already Good Base)

### Current State
- ✅ Time, feedback, mix sliders
- ✅ Modulation indicators
- ❌ No sections
- ❌ No visual feedback

### Proposed Upgrade
```cpp
// === DELAY PARAMETERS ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Delay Parameters");
ImGui::Spacing();

[Time slider with (mod)]
ImGui::SameLine();
HelpMarker("Delay time in milliseconds (1-2000 ms)");

[Feedback slider with (mod)]
ImGui::SameLine();
HelpMarker("Feedback amount (0-95%)\nCreates repeating echoes");

[Mix slider with (mod)]
ImGui::SameLine();
HelpMarker("Dry/wet mix (0-100%)\n0% = dry signal only, 100% = delayed signal only");

ImGui::Spacing();
ImGui::Spacing();

// === VISUAL DELAY TAPS (New!) ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Delay Taps");
ImGui::Spacing();

// Show first 5 delay taps as dots
for (int i = 0; i < 5; ++i)
{
    if (i > 0) ImGui::SameLine();
    
    float tapLevel = feedback * std::pow(feedback, i);  // Exponential decay
    ImVec4 color = ImColor::HSV(0.15f, 0.7f, tapLevel).Value;
    
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::Button("•", ImVec2(itemWidth * 0.18f, 20));
    ImGui::PopStyleColor();
}
```

---

## 6.2 Reverb Module

### Proposed Upgrade
- Section: "Room Size & Decay"
- Section: "Tone Controls" (damping, brightness)
- Section: "Mix"
- Visual: Room size indicator or decay tail display

---

## 6.3 Chorus Module

### Proposed Upgrade
- Section: "Chorus Parameters" (rate, depth, voices)
- Section: "Mix"
- Visual: LFO modulation waveform
- Tooltips

---

## 6.4 Phaser Module

### Proposed Upgrade
- Section: "Phaser Parameters" (rate, depth, stages, feedback)
- Section: "Mix"
- Visual: Notch frequency sweep display
- Tooltips

---

## 6.5 Compressor Module

### Proposed Upgrade
- Section: "Dynamics" (threshold, ratio, attack, release)
- Section: "Makeup Gain"
- Visual: Gain reduction meter (real-time!)
- Tooltips explaining compression

---

## 6.6 Limiter Module

### Proposed Upgrade
- Section: "Limiting" (threshold, release)
- Visual: Level meter with limiting indicator
- Tooltips

---

## 6.7 Drive Module

### Proposed Upgrade
- Section: "Drive Amount & Type"
- Section: "Tone" (pre/post filtering)
- Visual: Waveform distortion preview
- Tooltips

---

## 6.8 Waveshaper Module

### Proposed Upgrade
- Section: "Shaping Curve" (amount, type)
- Visual: Transfer function graph (input vs output)
- Tooltips

---

## 6.9 HarmonicShaper Module

### Proposed Upgrade
- Section: "Harmonic Content" (odd/even harmonics)
- Section: "Amount"
- Visual: Harmonic spectrum display
- Tooltips

---

## 6.10 MultiBandShaper Module

### Proposed Upgrade
- Section: "Band Split" (crossover frequencies)
- Section: "Per-Band Shaping" (collapsible like PolyVCO)
- Visual: Multi-band level meters
- Tooltips

---

# 🔧 Category 7: Utility Modules (15 modules)

## 7.1 VCA Module ✅ (Already Minimal)

### Current State
- ✅ Simple gain slider
- ✅ Modulation indicator
- ❌ No tooltip
- ❌ No visual meter

### Proposed Upgrade
```cpp
// === AMPLIFIER ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Voltage Controlled Amplifier");
ImGui::Spacing();

[Gain slider with (mod)]
ImGui::SameLine();
HelpMarker("Gain in decibels (-60 to +6 dB)\n0 dB = unity gain (no change)");

// Visual: Level meter showing current gain
float linearGain = juce::Decibels::decibelsToGain(gainDb);
ImGui::ProgressBar(linearGain / 2.0f, ImVec2(itemWidth, 0), "");
```

---

## 7.2-7.15 Other Utility Modules

**Standard Pattern for Simple Utilities**:
- Single section with module name
- Tooltips for all controls
- Progress bar or meter where applicable
- Keep minimal (these are utilities, not complex instruments)

**Modules**:
- Mixer: Fader strips with level meters
- CVMixer: Similar to Mixer
- TrackMixer: Per-track sections, collapsible
- Attenuverter: Visual indicator of attenuation/inversion
- Math: Operation selector with visual formula
- Logic: Truth table visualization
- Comparator: Threshold with visual comparison
- MapRange: Visual input→output range mapping
- Quantizer: Scale selection with visual keyboard
- Gate: Threshold with LED indicator
- SequentialSwitch: Visual step indicator
- Value: Large numeric display
- Inlet/Outlet: Connection status indicator

---

# 📊 Category 8: Analysis Modules (4 modules)

## 8.1 Scope Module

### Proposed Upgrade
- Section: "Trigger Settings" (level, edge, mode)
- Section: "Time Base" (zoom, position)
- Enhanced waveform display (already visual)
- Tooltips

---

## 8.2 FrequencyGraph Module

### Proposed Upgrade
- Section: "Analysis Settings" (FFT size, window)
- Section: "Display Options" (range, scale)
- Enhanced spectrum display
- Tooltips

---

## 8.3 GraphicEQ Module

### Proposed Upgrade
- Section: "Band Controls" (per-band sliders)
- Visual: Frequency response curve
- Tooltips explaining frequency bands

---

## 8.4 Debug/InputDebug Modules

### Proposed Upgrade
- Section: "Monitoring"
- Visual value displays
- Tooltips

---

# 🎵 Category 9: Audio I/O (5 modules)

## 9.1 AudioInput Module

### Proposed Upgrade
- Section: "Input Selection" (channel routing)
- Visual: Level meters
- Tooltips

---

## 9.2 SampleLoader Module

### Proposed Upgrade
- Section: "Sample File" (load, info)
- Section: "Playback Controls" (trigger, loop, speed)
- Visual: Waveform display
- Drag-drop zone (like MIDI Player)
- Tooltips

---

## 9.3 Granulator Module

### Proposed Upgrade
- Section: "Sample Selection"
- Section: "Grain Parameters" (size, density, pitch)
- Section: "Spray Controls" (randomization)
- Visual: Grain cloud visualization
- Tooltips

---

## 9.4 TimePitch Module

### Proposed Upgrade
- Section: "Time Stretch"
- Section: "Pitch Shift"
- Section: "Quality Settings"
- Tooltips

---

## 9.5 Record Module

### Proposed Upgrade
- Section: "Recording Controls" (arm, record, stop)
- Section: "File Settings" (path, format)
- Visual: Recording indicator + level meters
- Tooltips

---

# ⚡ Category 10: Special Modules (5 modules)

## 10.1 Meta Module

### Proposed Upgrade
- Section: "Sub-Patch Management"
- Section: "I/O Mapping"
- Visual indicators
- Tooltips

---

## 10.2 VstHost Module

### Proposed Upgrade
- Section: "Plugin Selection"
- Section: "Plugin Controls" (pass-through to VST UI)
- Tooltips

---

## 10.3 TTSPerformer Module

### Proposed Upgrade
- Section: "Text Input"
- Section: "Voice Settings"
- Section: "Playback Controls"
- Tooltips

---

## 10.4 Comment Module

### Proposed Upgrade
- Section: "Text Editor" (multiline input)
- Section: "Formatting" (color, size)
- Tooltips

---

## 10.5 MIDIControlCenter Module

### Proposed Upgrade
- (Already perfect as part of MIDI family?)
- Follow MIDIFaders standard

---

# 📋 Implementation Priority Ranking

## ⭐ HIGH PRIORITY (Do First):
1. **LFO** - Most used modulation source
2. **ADSR** - Most used envelope
3. **PolyVCO** - Complex UI needs collapsing
4. **TempoClock** - Visual clock feedback important
5. **Delay** - Popular effect, easy win

## 🔸 MEDIUM PRIORITY:
6. VCF, Reverb, Compressor
7. StepSequencer, Scope
8. SampleLoader, Granulator
9. Mixer, TrackMixer

## 🔹 LOW PRIORITY (Simple Modules):
10. VCA, Attenuverter, Math, Logic
11. Value, Gate, Comparator
12. Comment, Debug modules

---

# 🎯 Universal Design Patterns

## Pattern 1: Standard Effect Module
```cpp
// === EFFECT PARAMETERS ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Effect Name");
ImGui::Spacing();

[Parameter sliders with tooltips and (mod) indicators]

ImGui::Spacing();
ImGui::Spacing();

// === MIX CONTROL ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output");
ImGui::Spacing();

[Mix slider]
ImGui::SameLine();
HelpMarker("Dry/wet mix");
```

## Pattern 2: Modulation Source
```cpp
// === PARAMETERS ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Parameters");
[Parameter controls]

// === LIVE OUTPUT ===
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live Output");
[Progress bar or value display]
```

## Pattern 3: Complex Multi-Element Module
```cpp
// === MASTER CONTROLS ===
[Global parameters]

// === ELEMENT CONTROLS ===
// Use ImGui::CollapsingHeader() for each element
for (int i = 0; i < numElements; ++i)
{
    if (ImGui::CollapsingHeader(...))
    {
        [Element-specific controls]
    }
}
```

---

# 📊 Estimated Work Summary

| Category | Modules | Est. Time | Complexity |
|----------|---------|-----------|------------|
| Modulation | 6 | 3 hours | Medium |
| Oscillators | 3 | 2 hours | Medium-High (PolyVCO) |
| Clock/Timing | 3 | 2 hours | Medium |
| Sequencers | 3 | 3 hours | High |
| Filters | 3 | 2 hours | Medium |
| Effects | 10 | 5 hours | Low-Medium |
| Utility | 15 | 4 hours | Low |
| Analysis | 4 | 2 hours | Medium |
| Audio I/O | 5 | 3 hours | Medium |
| Special | 5 | 2 hours | Variable |
| **TOTAL** | **61** | **28 hours** | - |

---

# ✅ Pre-Implementation Checklist

Before implementing ANY of these upgrades:
- [ ] User approves overall approach
- [ ] User selects priority order
- [ ] User reviews specific module proposals
- [ ] Confirm design guide v2.2 patterns
- [ ] Confirm no `-1` widths in progress bars
- [ ] Confirm all `Indent()` matched with `Unindent()`
- [ ] Confirm `ImGui::TextColored()` for sections (not SeparatorText)
- [ ] Confirm tooltips for all complex controls

---

**END OF PROPOSALS**  
**Date**: 2025-10-24  
**Status**: AWAITING USER APPROVAL

