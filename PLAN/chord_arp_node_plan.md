# Chord & Arpeggiator Node – Detailed Plan

## 1. High-Level Goal

Design and implement a **Chord & Arpeggiator brain node** that:
- Centralizes **scale, key, chord progression, and voicing rules**.
- Accepts **monophonic pitch CV and/or MIDI** plus clock/reset.
- Outputs **polyphonic pitch CV + gate** and/or compact “chord CV” encodings.
- Provides a built-in **arpeggiator engine** (Up/Down/Random/Patterns).
- Integrates cleanly with existing nodes: `sequencer`, `multi_sequencer`, `polyvco`, `quantizer`, `timeline`, `tempo_clock`, `track_mixer`.

It should feel like a **“Harmony Brain”** for Collider – not another generic sequencer, but a node that:
- Knows about **chords, scales, and voice-leading**.
- Can be the harmonic center of a patch.

Working name: **`chord_brain`** or **`chord_arp`** (final naming TBD).

---

## 2. User Stories

### 2.1 Core Harmony

- *As a user*, I want to **pick a scale and key** and have incoming CV quantized to that harmony, without manually wiring `quantizer` everywhere.
- *As a user*, I want to feed a **simple root-sequence** (from `sequencer` or `midi_cv`) into this node and get **full triads or 7th chords** out, without patching 3–4 pitch lanes per voice.

### 2.2 Chords to Polyphony

- *As a user*, I want the node to output **N parallel pitch+gate lanes** (e.g. 4 voices) so I can plug them directly into `polyvco` or multiple `vco`+`adsr` pairs.
- *As a user*, I want to choose **voicing modes** (close, spread, drop-2, bass+chord, etc.) without reprogramming patches.

### 2.3 Arpeggiator

- *As a user*, I want to feed this node chord information, and get **clock-synced arpeggios**:
  - Modes: Up, Down, Up/Down, Random, As Played, Pattern string (e.g. `0,1,2,0,2,1`).
  - Divisions synced to `tempo_clock` / transport.
- *As a user*, I want to **switch between chord output and arp output** (or even both) depending on patch requirements.

### 2.4 Performance & Modulation

- *As a user*, I want to **modulate**:
  - Chord type / degree (I, IV, V, vi, etc.).
  - Inversion / spread.
  - Arp rate / swing / pattern select.
- *As a user*, I want a **small, clear UI** in the node:
  - Scale/Key selection.
  - A compact chord progression editor (I–V–vi–IV style).
  - Arp mode & rate.

---

## 3. Functional Scope (MVP vs Advanced)

### 3.1 MVP (Recommended First Iteration)

**Core features:**
- Scale & key selection:
  - Scales: Major, Natural Minor, Harmonic Minor, Pentatonic Major/Minor, Dorian, Mixolydian.
  - Key: 12 semitones.
- Chord generation:
  - From incoming **degree index** (0–6) or **root pitch CV**.
  - Chord types: Triads (maj, min, dim), basic 7ths (maj7, dom7, min7).
  - Fixed voicing size (e.g. 4-note voice stack; missing notes = duplicates).
- Arpeggiator:
  - Input: chord definition from internal chord logic.
  - Modes: Up, Down, Up/Down, Random.
  - Division: synced to `tempo_clock` (1/1 … 1/16), plus free-rate option.
  - Single pitch+gate output (arp) + optional “held chord” poly outputs (N lanes).

**I/O (MVP):**
- **Inputs**
  - `Degree In` (Raw/CV) – 0–6 → I…VII (or -1 for off).
  - `Root CV In` (CV) – Alternative to degree; raw pitch CV; quantized to scale.
  - `Chord Select Mod` (CV) – Morph/change chord type (triad vs 7th vs sus, etc. in later phases).
  - `Arp Clock In` (Gate) – Optional external clock; otherwise uses `tempo_clock` via transport.
  - `Reset In` (Gate) – Reset arp step index & internal phase.
- **Outputs**
  - `Pitch 1-4` (CV) – 4-voice chord pitches (poly).
  - `Gate 1-4` (Gate) – 4-voice gates (held while chord active).
  - `Arp Pitch` (CV) – Arpeggiated pitch (one voice).
  - `Arp Gate` (Gate) – Arpeggiated gate.
  - `Chord Degree` (Raw/CV) – For debugging & interop (e.g. drive other sequencers).

**Parameters (APVTS):**
- `Scale` (choice) – scale type.
- `Key` (choice) – root note.
- `Chord Mode` (choice) – Triad, 7th, Power, etc. (MVP: Triad + 7th only).
- `Voicing` (choice) – Close / Spread / Drop2 (MVP: Close + simple spread).
- `Use External Clock` (bool) – Arp uses external gate vs internal timebase.
- `Arp Mode` (choice) – Up/Down/UpDown/Random.
- `Arp Division` (choice) – 1/1 … 1/16; only active when sync’d.

### 3.2 Advanced Phases

Add later:
- Detailed **chord progression editor**: bar-length sequence of degrees/chords.
- Rich chord palette: sus2, sus4, add9, 6/9, altered dominants.
- **Voice-leading logic** to minimize jumps between chords.
- Per-voice **velocity** or “importance CV” outputs.
- More arp patterns: user-editable pattern string, Euclidean, random-walk.
- Microtonal/alternate tunings (if/when global tuning support arrives).

---

## 4. Technical Design

### 4.1 Node Type & Category

- Node ID: `chord_arp` (TBD).
- **Category** in dictionary: **Sequencer Nodes** or **Modulator / Harmony**.
- Inherits from `ModuleProcessor`.
- Implements:
  - `prepareToPlay`, `processBlock`, `setTimingInfo`.
  - `getAPVTS`, `getParamRouting`.
  - `getRhythmInfo` (for `bpm_monitor` introspection – important).

### 4.2 Audio / CV Buses

This is mostly CV/Gate; no audio generation.

```cpp
BusesProperties()
    .withInput ("CV In", juce::AudioChannelSet::discreteChannels (4), true)   // Degree, Root CV, Chord Select Mod, Arp Clock In (optional)
    .withOutput("Poly Out", juce::AudioChannelSet::discreteChannels (8), true) // Pitch1-4, Gate1-4
    .withOutput("Arp Out",  juce::AudioChannelSet::discreteChannels (2), true); // Arp Pitch, Arp Gate
```

We treat these as **CV/Gate lanes**, but use audio-rate buffers like elsewhere.

### 4.3 Parameter Layout

APVTS parameters (non-exhaustive, MVP):

- `scale` (choice) – e.g. `{"Major", "Natural Minor", "Harmonic Minor", "Dorian", "Mixolydian", "Pent Maj", "Pent Min"}`.
- `key` (choice) – `{"C", "C#", ..., "B"}`.
- `chord_mode` (choice) – `{"Triad", "Seventh"}`.
- `voicing` (choice) – `{"Close", "Spread"}`.
- `arp_mode` (choice) – `{"Off", "Up", "Down", "UpDown", "Random"}`.
- `arp_division` (choice) – `{"1/1", "1/2", "1/4", "1/8", "1/16"}`.
- `use_external_clock` (bool).
- `base_degree` (int / choice) – default chord degree when no input.
- `num_voices` (int) – 1–4 (for poly outputs).

Modulation “virtual” IDs (no real APVTS params, but for Modulation Trinity):
- `degree_mod` – maps to input channel 0.
- `root_cv_mod` – maps to input channel 1.
- `chord_mode_mod` – input channel 2 (0–1 → discrete mapping).
- `arp_rate_mod` – input channel 3 (scale arp division or free rate).

### 4.4 Harmony Engine (Internal Data Structures)

Represent pitches as **semitones relative to C** in 12-TET.

```cpp
enum class ScaleType { Major, NaturalMinor, HarmonicMinor, Dorian, Mixolydian, PentMaj, PentMin };

struct ScaleDefinition
{
    ScaleType type;
    std::array<int, 7> degrees; // interval in semitones from root for 7-degree scales; for pentatonic we re-use subset
};

enum class ChordMode { Triad, Seventh };

struct ChordDefinition
{
    // Offsets in scale degrees for chord tones: 1-3-5-(7)
    std::vector<int> degreeOffsets;
};
```

Steps:
1. Determine **global root note in semitones** (key + optional `Root CV`).
2. Determine **scale** (array of pitch-class offsets).
3. Interpret **degree input** (0–6) → pick subset of scale degrees.
4. Map chord degrees to actual semitone pitches for each voice.
5. Apply voicing rule (close/spread) by adding octaves.

### 4.5 Arpeggiator Engine

Maintain arp state:

```cpp
struct ArpState
{
    int currentIndex = 0;
    double phase = 0.0;      // 0..1 over one arp step
    double samplesPerStep = 0.0;
    bool gateOn = false;
};
```

Two timing modes:
- **Synced**: use `TransportState` (like sequencers) + `tempo_clock` BPM + `arp_division`.
- **External clock**: detect rising edges of `Arp Clock In` CV and step instantly.

Per chord:
- Build vector of active notes:
  - `notes[]` from chord engine (length = `num_voices`).
- For arp:
  - Step index `i` through `notes` according to `arp_mode`.
  - Set `Arp Pitch` = `notes[i]` (as CV).
  - Generate `Arp Gate` pulses (configurable gate length; start with 50% duty).

### 4.6 CV Mapping & Modulation Trinity

- Implement `getParamRouting`:
  - `degree_mod` → in bus 0, ch 0.
  - `root_cv_mod` → in bus 0, ch 1.
  - `chord_mode_mod` → in bus 0, ch 2.
  - `arp_rate_mod` → in bus 0, ch 3.
- In `processBlock`:
  - Read CV **before clearing any buffers** (buffer aliasing safety).
  - Convert continuous CV (0–1 or pitch CV) → discrete parameters:
    - `degree` = round(clamp(CV * 7)).
    - `chord_mode` index from 0–1.
    - `arp_rate` = map to allowed divisions.
  - Update `std::atomic`/simple member fields used for audio-rate logic.
  - Update `setLiveParamValue()` so UI shows live values when modulated.

### 4.7 Transport Integration

- Implement `setTimingInfo` similar to `StepSequencer` / `MultiSequencer`:
  - Track `TransportCommand` and `isPlaying`.
  - **Pause**:
    - Freeze arp phase/index; do not reset chord/harmony state.
  - **Stop**:
    - Reset phase to 0; optionally reset `currentIndex` = 0.
  - **Play**:
    - Resume from paused state; if starting fresh after stop, start from step 0.
- Provide `getRhythmInfo()` for `bpm_monitor`:
  - Report primary arp rate/division as `RhythmInfo`.

### 4.8 UI / ImGui Node

In `drawParametersInNode`:
- Left side:
  - Scale & key dropdowns (scroll-edit enabled).
  - Chord mode & voicing dropdowns.
- Middle:
  - A minimal **degree view**: e.g. live display of current degree (I–VII).
  - Optional 1–bar chord preview (for later progression support).
- Right side:
  - Arp mode dropdown.
  - Arp division slider/choice.
  - Toggle for external clock vs internal.

Follow existing patterns:
- Modulated parameters grayed/disabled.
- Scroll-edit on all sliders & choices.
- Node pins drawn with `drawParallelPins` for poly outs vs arp outs.

---

## 5. Integration with Existing Nodes

- **With `sequencer` / `multi_sequencer`**:
  - Use sequencer pitch as **degree or root CV** input.
  - Let `chord_arp` handle harmony; sequencer only handles rhythmic structure.
- **With `polyvco`**:
  - `Pitch 1-4` → `Freq 1-4 Mod`, `Gate 1-4` → `Gate 1-4 Mod`.
  - Very direct polyphonic voice wiring.
- **With `timeline`**:
  - Automate chord parameters (scale, key, voicing) over time by modulating APVTS params or feeding degree CV.
- **With `bpm_monitor`**:
  - `getRhythmInfo` exposes arp’s effective BPM/division.
- **With `midi_cv` / `midi_player`**:
  - Use pitch / gate from MIDI as **root pitch** and gate hints to “hold” chords/arp.

---

## 6. Risk Analysis

### 6.1 Main Risks

- **R1 – Scope Creep / Feature Bloat (High Impact, Medium Likelihood)**
  - Harmony & arpeggiation can grow **very deep** (extended chords, jazz voicings, complex rules).
  - Risk: node becomes overcomplicated, hard to finish, and confusing to use.
  - **Mitigation**:
    - Strict MVP: only a small set of scales & chord types.
    - Document “future phases” separately, avoid implementing them in v1.

- **R2 – UX Complexity (Medium–High Impact, Medium Likelihood)**
  - Too many options in a single node can alienate users who just want simple arps.
  - **Mitigation**:
    - Default mode: “Simple Arp” – only a few controls visible.
    - Advanced options hidden behind a collapsible “Advanced Harmony” section.

- **R3 – Timing / Rhythm Edge Cases (Medium Impact, Medium Likelihood)**
  - Misaligned arp stepping vs `tempo_clock`, jitter on external clocks, transport edges.
  - **Mitigation**:
    - Reuse timing patterns from `StepSequencer` / `MultiSequencer` (`TransportState`).
    - Add tests/patches to validate pause/resume/stop semantics (mirror sequencer rollout docs).

- **R4 – Voice Allocation & Polyphony (Medium Impact, Low–Medium Likelihood)**
  - Handling chords larger than `num_voices` (e.g. 7th chord when 3-voice output).
  - **Mitigation**:
    - MVP: fixed `num_voices = 4`, and simple mapping (truncate or duplicate notes).
    - Later: more flexible mapping and per-voice “importance”.

- **R5 – Interop with Quantizer / Scales (Low–Medium Impact, Low Likelihood)**
  - Double-quantization or conflicting scale rules if user also uses `quantizer`.
  - **Mitigation**:
    - Document that `chord_arp` already quantizes.
    - Provide optional “bypass quantize” mode if inputs are pre-quantized.

### 6.2 Risk Rating

- **Overall Risk Rating: Medium**
  - Technical complexity is manageable with a constrained MVP.
  - Main danger is **design bloat**, not core DSP or threading.

---

## 7. Difficulty Levels & Implementation Paths

### Level 1 – Minimal Harmony Arp (Low–Medium Difficulty)

**Scope:**
- Major / Natural Minor only.
- Degree → Triad only (no 7ths).
- 3-voice poly output + simple Up/Down/Random arp.

**Pros:**
- Fast to implement.
- Validates node concept & UX.
- Lower risk of design dead-ends.

**Cons:**
- Feels limited quickly (no 7ths / no richer harmony).
- Might need immediate follow-up for serious use.

### Level 2 – Full MVP as Described (Medium–High Difficulty, Recommended)

**Scope:**
- Multiple common scales.
- Triad + 7th chords.
- 4-voice poly output.
- Arp with divisions and a handful of modes.
- Basic voicing options (close/spread).

**Pros:**
- Strong sweet spot between power and complexity.
- Long-term useful for many users without immediate v2 pressure.

**Cons:**
- Requires careful parameter and UI design.
- Slightly more risk on timing & edge cases.

### Level 3 – Advanced Harmony Workstation (High Difficulty, High Risk)

**Scope:**
- Full chord palette (sus, add, altered dominants).
- Progression editor, voice-leading rules, custom tunings.
- Pattern-based arp rules, Euclidean patterns, etc.

**Pros:**
- Extremely powerful “Composer Brain” node.

**Cons:**
- Very high complexity; long implementation time.
- Harder to test exhaustively; risk of stale/unmaintainable code.

**Recommendation:** Start with **Level 2** MVP, design it so the internal harmony engine is extendable, but do **not** implement complex rule systems yet.

---

## 8. Confidence Assessment

### 8.1 Confidence Rating

- **Overall Confidence: 8/10**

### 8.2 Strong Points

- **Good fit in ecosystem**: Node plugs directly into existing `sequencer`, `polyvco`, `timeline`, `tempo_clock`, and `bpm_monitor`. It fills a clearly missing role (harmony brain).
- **Reuse of patterns**: Transport handling, `getRhythmInfo`, CV routing patterns, and ImGui UI flows all have strong precedents in current code.
- **Contained DSP**: Harmony and arpeggiation are **logic-heavy** but not computationally heavy; no tricky audio processing or external libraries.

### 8.3 Weak Points / Unknowns

- **Harmony UX**: The “right” way to expose chords, scales, and progressions may need iteration and user testing.
- **Interoperability semantics**: How users combine `chord_arp` with `quantizer`, `timeline`, and other sequencers could reveal unexpected workflows that require tweaks.
- **Future tuning & microtonal support**: If you introduce alternate tunings later, this node’s design must either adapt or clearly declare “12-TET only”.

---

## 9. Potential Problems & Mitigations

### 9.1 Overlapping Responsibilities

- **Problem**: Users may be confused: “Should I use `sequencer`, `multi_sequencer`, `midi_player`, or `chord_arp` for X?”
- **Mitigation**:
  - Clear labeling in node dictionary (“Harmony Brain / Chord + Arp”).
  - Usage examples in manual: `sequencer` for raw step patterns; `chord_arp` for harmonic structure.

### 9.2 Graph Complexity

- **Problem**: Another “big brain” node like `physics` or `tts_performer` can intimidate beginners.
- **Mitigation**:
  - Default to **simple mode** on first insert (few visible controls).
  - Provide presets: “Basic Triads”, “Simple Up Arp”, etc.

### 9.3 Timing Mismatch / Jitter

- **Problem**: Arp might drift or misalign if both internal and external clocks are in play.
- **Mitigation**:
  - Clear mode toggles: “Use External Clock” vs “Use Transport”.
  - Documented best practices and example patches.

### 9.4 Performance on Large Patches

- **Problem**: If chord_arp is used multiple times in big patches, small inefficiencies could accumulate.
- **Mitigation**:
  - Keep `processBlock` O(voices) and O(scales), which is tiny per block.
  - Avoid dynamic allocations; preallocate vectors.

### 9.5 Backwards Compatibility (Future)

- **Problem**: Changes to harmony representation later might break saved patches.
- **Mitigation**:
  - Store a clear version tag in `getExtraStateTree` for chord/harmony data.
  - If you expand chord types later, keep indices of existing ones stable.

---

## 10. Proposed Implementation Steps (Concrete)

1. **Finalize MVP Scope (Level 2)**
   - Confirm: scales, chord modes, number of voices, arp modes/divisions.
2. **Design Data Structures**
   - `ScaleDefinition`, `ChordDefinition`, and mapping functions.
3. **Create Module Skeleton**
   - New processor file `ChordArpModuleProcessor` (name TBD).
   - Basic APVTS, buses, and stub `processBlock`.
4. **Implement Harmony Engine**
   - Map degree/root inputs to chord notes (in semitones).
   - Convert semitones → pitch CV consistent with existing pitch conventions.
5. **Implement Poly Output**
   - Write `Pitch 1-4` and `Gate 1-4` outputs from current chord (held).
6. **Implement Arpeggiator**
   - Arp state, modes, and timing from `TransportState` or external clock.
   - Output `Arp Pitch` and `Arp Gate`.
7. **Wire CV Modulation**
   - Implement `getParamRouting`, CV decoding, and live telemetry.
8. **UI Integration**
   - Minimal ImGui UI with scroll-edit, modulation-aware controls.
9. **Pin Database & Manual Entry**
   - Add node to `PinDatabase.cpp` and `USER_MANUAL/Nodes_Dictionary.md`.
10. **Testing & Example Patches**
    - Make 3–4 example patches:
      - Basic chord arps into `polyvco`.
      - Degree sequence from `sequencer` into `chord_arp`.
      - Integration with `timeline` for harmonic changes.

Once this is stable, we can revisit **progression editing** and richer chord sets as a separate plan.


