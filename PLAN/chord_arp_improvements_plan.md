## Chord Arp Node – Improvement Plan

### 1. Goals

- **Make `chord_arp` musically useful** beyond the current “safe stub”:
  - Real scale/degree → chord mapping.
  - Proper voicings and inversion logic.
  - Time/transport-aware arpeggiator that fits the rest of the ecosystem.
- **Keep it light and debuggable**:
  - Follow BestPractice / Modulation Trinity.
  - Preserve clean visualization and rhythm introspection (`getRhythmInfo`).

---

### 2. Current State (Baseline)

- **Inputs/Outputs already wired** and visible in UI; node loads and builds:
  - Inputs: `Degree In`, `Root CV In`, `Chord Mode Mod`, `Arp Rate Mod`.
  - Outputs: `Pitch 1-4`, `Gate 1-4`, `Arp Pitch`, `Arp Gate`.
- **Parameters present**: `Scale`, `Key`, `Chord Mode`, `Voicing`, `Arp Mode`, `Arp Division`, `Use External Clock`, `Voices`.
- **Behavior is skeletal**:
  - Root CV is treated as a normalized value; chord intervals are fixed offsets.
  - No real scale quantization, chord-degree mapping, or inversion logic.
  - Arp steps by a simple phase counter using a local `rateHz` from `arpRateCv`.
  - Gates stay high while transport plays (no per-note envelope-style gates).
- **Visualization**:
  - Shows degree/root CV, four voice bars, arp bar, and `Arp Gate` border.

---

### 3. Improvement Axes

#### 3.1 Harmony & Pitch Model

- **Objective**: Turn `Scale`, `Key`, `Degree In`, and `Chord Mode` into meaningful, repeatable musical output.
- **Concrete tasks**:
  1. Define an **internal semitone model**:
     - Use 0–11 semitones for scale degrees relative to `Key`.
     - Tables for each scale: Major, Natural Minor, Harmonic Minor, Dorian, Mixolydian, Pent Maj, Pent Min.
  2. Map **`Degree In` (0–1)** to a **discrete degree index**:
     - E.g. `degreeIndex = floor(D * numDegrees)` clamped to `[0, numDegrees-1]`.
     - Optionally add a **“smooth vs snapped” degree mode** later.
  3. Define **chord templates** per `Chord Mode`:
     - Triad: {0, +2, +4} scale steps; Seventh: {0, +2, +4, +6}.
     - Convert scale degrees to **absolute semitones** (relative to `Key`).
  4. Convert **semitones to normalized pitch CV**:
     - Either:
       - (a) Let `Root CV In` be a V/Oct-ish normalized CV and add semitone offsets via a helper (`semitonesToNorm`), or
       - (b) Treat `Root CV In` as 0–1 and map to `[baseNote, baseNote+N]` internally.
     - Choose (a) for compatibility with `quantizer`/`midi_cv` (preferred), document mapping.
  5. Implement **voicing logic**:
     - “Close”: voices stacked within one or two octaves (minimal spread).
     - “Spread”: spread across two–three octaves (add octaves to some chord tones).
  6. Optionally add **inversion** concept:
     - Implicitly via “degree” or explicit inversion param (future).

#### 3.2 Arpeggiator & Timing

- **Objective**: Make arp timing match the rest of the transport ecosystem.
- **Concrete tasks**:
  1. Define a **division table** for `Arp Division`:
     - Map index to note-length multipliers (e.g. 1/1, 1/2, 1/4, …).
  2. Use `TransportState` to derive **samples per step**:
     - If `Use External Clock` is off:
       - Use `currentTransport.bpm` (or parent’s `TransportState`) + division to compute `samplesPerStep`.
     - If `Use External Clock` is on:
       - Respect external clock / division when available (TempoClock global division).
  3. Keep **CV-based modulation of rate**:
     - Blend `Arp Rate Mod` with the division-based rate (e.g. multiplicative factor).
  4. Improve **gate envelope behavior**:
     - Instead of one-sample pulses, define gate-on duration as a fraction of `samplesPerStep` (e.g. 10–30%).
  5. Ensure `getRhythmInfo()`:
     - Computes `bpm` from the **same timing path** as the arp (division + BPM), not a separate approximation.

#### 3.3 Gates & Voice Life-Cycle

- **Objective**: Make `Gate 1-4` and `Arp Gate` musically meaningful and not always high.
- **Concrete tasks**:
  1. Introduce a simple **step clock** for chord on/off:
     - Option A: Always gate chords high while transport is playing (current behavior, keep as fallback).
     - Option B: Introduce a “Chord Rate” concept (could reuse `Arp Division`) for on/off pulses.
  2. Track **note on/off** states per voice:
     - Turn gates on at chord step boundaries, off after `gateFraction * samplesPerStep`.
  3. Optionally expose a parameter **“Gate Length”** (0–1) later.

#### 3.4 Modulation Integration

- **Objective**: Make the current modulation CVs actually drive musical changes.
- **Concrete tasks**:
  1. Implement `Chord Mode Mod`:
     - Map 0–1 to discrete chord modes (Triad/Seventh, future extensions).
     - Decide blending rule: when mod input is connected, override APVTS param or treat as offset.
  2. Implement `Arp Rate Mod`:
     - Map 0–1 to a multiplier (e.g. 0.25× … 4×) against division-derived rate.
  3. Consider future **additional virtual mod targets**:
     - `degree_mod`, `voicing_mod`, maybe `inversion_mod`.

#### 3.5 Visualization & UX

- **Objective**: Make the viz more informative without breaking performance or ImGui invariants.
- **Concrete tasks**:
  1. Reflect **scale degree and chord quality** in labels:
     - “Degree 3 (III) | Root F#” instead of raw 0.00 values.
  2. Color-code **active arp voice** vs static chord voices:
     - E.g. different color for the bar corresponding to the current arp index.
  3. Optionally show **step timing**:
     - Subtle progress bar along the bottom of the viz showing phase within `samplesPerStep`.
  4. Keep all drawing inside the existing child window, clip rect, and `PushID`/`PopID` pattern.

---

### 4. Implementation Tiers (Difficulty Levels)

#### Level 1 – Musical Chords Only (Low Risk, Low/Medium Effort)

- Implement:
  - Scale/Key tables and Degree → chord semitones mapping.
  - Proper voicings (close/spread) and V/Oct-friendly normalization.
  - Leave arp behavior roughly as is (only minor clean-up).
- **Pros**:
  - Immediate musical payoff: chords follow scales and keys.
  - Minimal impact on timing and rhythm; easy to debug.
- **Cons**:
  - Arp still feels basic and not fully transport-aware.
  - Gates remain simplistic (always on while playing).

#### Level 2 – Chords + Transport-Synced Arp (Recommended MVP)

- Level 1 +:
  - Arp division table + BPM/Transport-based `samplesPerStep`.
  - `Use External Clock` semantics; `Arp Rate Mod` as multiplier.
  - Gate shaping for `Arp Gate` and (optionally) chord gates.
- **Pros**:
  - Node becomes a serious **rhythmic + harmonic** tool.
  - Integrates tightly with `tempo_clock` and `bpm_monitor`.
  - Still manageable complexity and test surface.
- **Cons**:
  - More risk of timing bugs or off-by-one issues.
  - Needs careful testing with transport stop/pause/seek.

#### Level 3 – Full Harmony Brain (High Effort, Higher Risk)

- Level 2 +:
  - Inversions, extended chord types (9ths, sus, addX).
  - Multiple voicing strategies (drop-2, drop-3, spread rules).
  - Potential pattern memory (chord progressions rather than single degree).
  - Richer visualizations for progression/degree history.
- **Pros**:
  - Extremely powerful node, center of harmonic workflows.
  - Could replace several manual sequencer/quantizer patches.
- **Cons**:
  - Scope creep risk; large surface for subtle musical bugs.
  - Harder to document and for users to mentally model quickly.

---

### 5. Risks & Mitigations

- **Risk: Timing & transport bugs** (arp not lining up, stop/pause glitches).
  - Mitigation: Reuse patterns from `MultiSequencer` and `TempoClock` for division/BPM handling; add tests/DebugInput patches that flip transport modes (play/pause/stop) while watching gates.
- **Risk: Pitch mismatches** (off-by-one degree, wrong semitone mapping).
  - Mitigation: Unit-style tests or Debug node patches that print expected semitone offsets; cross-check against `quantizer` scales.
- **Risk: Backwards-compatibility** for existing patches using the stub behavior.
  - Mitigation: Introduce a **“Legacy Mode”** flag if necessary, or ensure that default parameters map the old “static offsets” behavior to a valid chord (e.g. treat old outputs as a Major triad at degree 0).
- **Risk: CPU overhead** from additional calculations and visualization complexity.
  - Mitigation: Keep harmony calculations per-block or cheap per-sample; no allocations; keep viz logic simple (bars only).
- **Risk: UX overload** (too many parameters/mods for early version).
  - Mitigation: Hide advanced settings behind future extensions; keep initial UI minimal (Scale, Key, Chord Mode, Voicing, Arp Mode, Division, Voices).

---

### 6. Confidence Rating

- **Overall confidence**: **8/10** that this plan will yield a powerful yet understandable node without destabilizing the system.
  - **Strong points**:
    - Leverages well-established patterns from `quantizer`, `MultiSequencer`, `TempoClock`, and other rhythm-aware modules.
    - Clear separation into tiers allows safe incremental rollout.
    - Uses existing Modulation Trinity and visualization infrastructure (no new frameworks).
  - **Weak points**:
    - Musical theory subtleties (inversions, voicing taste) are subjective and may need iteration.
    - Exact CV mapping for pitch (normalized vs true V/Oct) must be chosen carefully to avoid user confusion.

---

### 7. Suggested Implementation Order

1. **Harmony Core (Level 1)**  
   - Add scale/key tables and Degree → chord semitone mapping.  
   - Convert to V/Oct-friendly normalized pitch outputs; wire into existing processBlock.

2. **Arp Timing Integration (Level 2, part 1)**  
   - Implement division table and BPM-based `samplesPerStep`.  
   - Update `getRhythmInfo()` to use this timing.

3. **Gates & Arp Behavior (Level 2, part 2)**  
   - Improve `Arp Gate` pulses and optionally voice gates.  
   - Ensure stop/pause/reset semantics match other sequencers.

4. **Modulation Semantics**  
   - Wire `Chord Mode Mod` and `Arp Rate Mod` to the new harmony/timing logic.  
   - Consider adding live telemetry keys for debugger/visualization.

5. **Visualization Enhancements**  
   - Adjust labels and colors to reflect real degrees/keys and active arp step.  
   - Keep within child-window guidelines.

6. **Optional Advanced Features (Level 3)**  
   - Inversions, extended chords, chord progressions, additional voicing modes—only after the MVP has seen real-world patches.


