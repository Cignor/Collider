# Smart Randomization: The "Happy Accident" Generator

## Core Concept
Instead of purely random connections (which result in noise), we implement **Procedural Patch Generation** based on **Archetypes**. The system acts as a "Modular Architect," following established synthesis rules while randomizing the specific parameters and module choices within those rules.

## Implementation Strategy: `PatchGenerator` Class

We will create a helper class `PatchGenerator` that handles the logic.

### Key Functions
1.  **`generatePatch(Archetype type)`**: The main entry point. Clears the canvas and builds a specific style.
2.  **`createChain(std::vector<ModuleType> chain)`**: Auto-wires audio outputs to inputs for a list of modules.
3.  **`connectModulation(Source, Target, Intensity)`**: Wires a CV source to a parameter, setting a random attenuverter value.
4.  **`autoLayout()`**: Intelligently places modules:
    *   **Sources**: Left
    *   **Processors**: Center
    *   **Effects**: Right
    *   **Modulators**: Top/Bottom

---

## The Recipe Book (Archetypes)

### 1. The "East Coast" (Classic Subtractive)
*   **Vibe**: Moog-style, punchy bass, leads.
*   **Modules**:
    *   **Source**: 2x `VCO` (Saw/Square) -> `Mixer`.
    *   **Filter**: `VCF` (Lowpass).
    *   **Amp**: `VCA`.
    *   **Control**: `Sequencer` -> Pitch, `ADSR` -> Filter Cutoff, `ADSR` -> VCA Gain.
*   **Wiring Rules**:
    *   Sequencer Gate -> ADSR Triggers.
    *   VCOs slightly detuned.

### 2. The "West Coast" (Buchla/Serge Style)
*   **Vibe**: Experimental, metallic, plucky, organic.
*   **Modules**:
    *   **Source**: `VCO` (Sine) -> `Waveshaper` (Folder) -> `LFO` (Audio Rate FM).
    *   **Gate**: `LowPassGate` (LPG) instead of VCA.
    *   **Control**: `FunctionGenerator` (Looping) -> Waveshaper Amount. `Random` (S&H) -> Pitch.
*   **Wiring Rules**:
    *   FM modulation is mandatory.
    *   Envelopes modulate *timbre* (folding/FM index) more than volume.
    *   Polyrhythmic looping envelopes.

### 3. The "Deep Space" (Ambient Drone)
*   **Vibe**: Evolving, texture, no rhythm.
*   **Modules**:
    *   **Source**: 3x `VCO` (Sine/Tri) or `Granulator`.
    *   **Effects**: `Delay` (Long) -> `Reverb` (Huge).
    *   **Control**: 3x `LFO` (Very Slow).
*   **Wiring Rules**:
    *   LFOs -> Pitch (Drift), Pan, Filter Cutoff.
    *   No Gates/Envelopes (Always on).
    *   Feedback loops in delay line.

### 4. The "Acid Box" (Techno Bass)
*   **Vibe**: Squelchy, repetitive, hypnotic.
*   **Modules**:
    *   **Source**: `VCO` (Saw/Square).
    *   **Filter**: `VCF` (Resonant Lowpass) -> `Distortion`.
    *   **Control**: `Sequencer` (16 step).
*   **Wiring Rules**:
    *   Sequencer -> Pitch & Gate.
    *   **Accent Logic**: Sequencer Velocity -> Filter Cutoff & VCA Decay.
    *   High Resonance on VCF.

### 5. The "Glitch Machine" (IDM/Chaos)
*   **Vibe**: Unpredictable, rhythmic noise, granular.
*   **Modules**:
    *   **Source**: `Noise` or `Sampler`.
    *   **Processor**: `Granulator` or `Bitcrusher`.
    *   **Control**: `ClockDivider` -> `Random` (Trigger) -> `FunctionGenerator`.
*   **Wiring Rules**:
    *   Random Triggers -> Grain Position / Sample Start.
    *   Fast LFO -> Bit Depth.
    *   Clock Divider creates complex, non-4/4 rhythms.

### 6. The "Krell Patch" (Self-Generating)
*   **Vibe**: Classic sci-fi, self-playing automation.
*   **Modules**:
    *   **Source**: `VCO`.
    *   **Control**: `FunctionGenerator` (EOC Mode) -> `Random` (S&H).
*   **Wiring Rules**:
    *   **The Krell Loop**: Function Generator End-Of-Cycle (EOC) -> Random Trigger -> Random Value -> Function Generator Attack/Decay Time.
    *   The patch "decides" how long the next note is based on the previous one.
    *   Random Value -> Pitch.

### 7. The "Super Saw" (Trance/Pad)
*   **Vibe**: Thick, wide, detuned.
*   **Modules**:
    *   **Source**: 4x `VCO` (Saw).
    *   **Processor**: `StereoMixer` (if avail) or 2x `VCA` panned.
    *   **Effects**: `Chorus` -> `Reverb`.
*   **Wiring Rules**:
    *   VCOs heavily detuned (-15c, -5c, +5c, +15c).
    *   One LFO -> PWM (Pulse Width Modulation) on all VCOs.

### 8. The "Physics Lab" (Bouncing Ball)
*   **Vibe**: Natural, decaying rhythms.
*   **Modules**:
    *   **Source**: `VCO` (Sine) or `Resonator`.
    *   **Control**: `FunctionGenerator` (Logarithmic Decay).
    *   **Logic**: `Math` (Multiplier).
*   **Wiring Rules**:
    *   Simulate gravity: Envelope controls rate of re-triggering (making it faster as it "bounces" lower).
    *   FM synthesis for the "impact" sound.

---

## "Smart Wiring" Logic (The Nervous System)

When connecting modulation, we don't just pick random pins. We use **Tag-Based Matching**:

| Source Type | Preferred Targets | Avoid Targets |
| :--- | :--- | :--- |
| **LFO (Slow)** | Filter Cutoff, PWM, Pan, Delay Time | Audio In, Gate In |
| **LFO (Fast)** | VCO FM, VCA (AM), Ring Mod | Filter Cutoff (unless desired) |
| **ADSR / Env** | VCA Gain, Filter Cutoff | Pitch (unless percussion) |
| **Sequencer** | Pitch (V/Oct), Gate | Timbre parameters |
| **Random** | Sample & Hold In, Granular Position | Master Volume |

## User Interface

*   **"Surprise Me" Button**: Picks a random archetype.
*   **"Genre" Dropdown**: Lets the user select "East Coast", "West Coast", "Ambient", etc.
*   **"Chaos Slider"**:
    *   0% = Textbook implementation of the recipe.
    *   50% = Standard variations (extra LFOs, different effects).
    *   100% = Breaks the rules (feedback loops, wrong connections).
