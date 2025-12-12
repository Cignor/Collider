# Experimental Audio Libraries - Benefits for Pikon Raditsz

This document explains how each JUCE-compatible, real-time audio library could enhance **Pikon Raditsz**, your modular synthesizer that combines audio processing, CV modulation, and real-time computer vision.

## Project Context

**Pikon Raditsz** is a modular synthesizer with:
- Node-based patching interface (ImNodes)
- CV modulation system
- Real-time computer vision integration (OpenCV)
- Existing modules: VCO, VCF, delay, reverb, chorus, phaser, compressor, granular synthesis, etc.
- MIDI and OSC support
- Polyphonic synthesis (PolyVCO)
- Time-stretching (SoundTouch, RubberBand)

---

## 🎹 DSP & Synthesis Libraries

### **1. Synthesis Toolkit (STK)**

**What it adds:**
- Physical modeling synthesis algorithms
- Realistic instrument emulation (strings, winds, percussion)
- Karplus-Strong string synthesis
- Modal synthesis for percussive sounds

**Specific Module Proposals:**

#### **stk_string**
**Physical Modeling String Synthesizer**

A physical modeling string instrument module using Karplus-Strong and other string algorithms. Generates realistic plucked, bowed, or struck string sounds.

**Inputs:**
- `Frequency` (CV) - Pitch modulation input
- `Pluck/Bow` (Gate) - Trigger pluck or bow attack
- `Velocity` (CV) - Attack velocity/intensity (0-1V)
- `Damping` (CV) - String damping modulation
- `Pickup Pos` (CV) - Pickup position along string (0-1V)

**Outputs:**
- `Out` (Audio) - Mono string sound output

**Parameters:**
- `Frequency` (20 Hz - 2 kHz) - Base pitch
- `Instrument Type` (Choice) - Guitar, Violin, Cello, Bass, Sitar, Banjo
- `Excitation Type` (Choice) - Pluck, Bow, Strike, Pick
- `Damping` (0-1) - String decay/damping amount
- `Pickup Position` (0-1) - Where along string the pickup is positioned
- `Brightness` (0-1) - High-frequency content
- `Body Size` (0-1) - Resonant body size (affects timbre)

**How to Use:**
1. Select instrument type (guitar, violin, etc.)
2. Choose excitation type (pluck for guitar, bow for violin)
3. Connect gate to trigger notes
4. Use CV to modulate frequency for vibrato/pitch bends
5. Adjust damping for decay time
6. Modulate pickup position for timbral variations
7. Combine with VCF and reverb for realistic instrument sounds

**CV Integration Ideas:**
- Connect LFO to Frequency for vibrato
- Connect ADSR to Velocity for dynamic attacks
- Use Sequencer gates to trigger plucks/bows
- Connect hand_tracker CV to Pickup Pos for gesture control

---

#### **stk_wind**
**Physical Modeling Wind Instrument Synthesizer**

Emulates wind instruments (flute, clarinet, saxophone, trumpet) using physical modeling algorithms.

**Inputs:**
- `Frequency` (CV) - Pitch modulation input
- `Breath` (CV) - Breath pressure/intensity (0-1V)
- `Gate` (Gate) - Note on/off trigger
- `Vibrato` (CV) - Vibrato depth modulation
- `Tongue` (Gate) - Tongue articulation trigger

**Outputs:**
- `Out` (Audio) - Mono wind instrument output

**Parameters:**
- `Frequency` (20 Hz - 2 kHz) - Base pitch
- `Instrument Type` (Choice) - Flute, Clarinet, Saxophone, Trumpet, Oboe, Bassoon
- `Breath Pressure` (0-1) - Base breath intensity
- `Vibrato Rate` (0-20 Hz) - Vibrato speed
- `Vibrato Depth` (0-1) - Vibrato amount
- `Attack Time` (0-1000 ms) - Note attack time
- `Reed Stiffness` (0-1) - Reed hardness (affects timbre)

**How to Use:**
1. Select wind instrument type
2. Connect gate for note on/off
3. Modulate breath pressure with CV for expression
4. Use vibrato CV for musical vibrato
5. Connect tongue gate for staccato articulation
6. Adjust reed stiffness for different timbres
7. Combine with delay/reverb for realistic space

**CV Integration Ideas:**
- Connect pose_estimator to Breath for gesture-controlled wind instruments
- Use LFO on Vibrato for automatic vibrato
- Connect ADSR to Breath for dynamic phrasing

---

#### **stk_percussion**
**Modal Synthesis Percussion**

Generates realistic drum and percussion sounds using modal synthesis (resonant modes).

**Inputs:**
- `Strike` (Gate) - Strike/trigger input
- `Velocity` (CV) - Strike velocity/intensity (0-1V)
- `Damping` (CV) - Decay time modulation
- `Pitch` (CV) - Pitch modulation (for pitched drums)

**Outputs:**
- `Out` (Audio) - Mono percussion output

**Parameters:**
- `Drum Type` (Choice) - Kick, Snare, Tom, Hi-Hat, Cymbal, Tabla, Djembe, Marimba
- `Pitch` (20 Hz - 1 kHz) - Base pitch (for pitched drums)
- `Decay` (0-5 seconds) - Decay time
- `Brightness` (0-1) - High-frequency content
- `Body Size` (0-1) - Resonant body size
- `Strike Position` (0-1) - Where on drum head (affects timbre)

**How to Use:**
1. Select drum type
2. Connect gate to Strike input
3. Modulate velocity for dynamic hits
4. Adjust decay for tail length
5. Use pitch modulation for pitch-bent drums
6. Combine multiple instances for drum kits
7. Route through compressor for punch

**CV Integration Ideas:**
- Connect Sequencer gates to Strike for drum patterns
- Use Random CV to Velocity for humanized drums
- Connect movement_detector to Strike for motion-triggered percussion

---

#### **stk_plucked**
**Karplus-Strong Plucked String**

Simple, efficient plucked string synthesis using Karplus-Strong algorithm. Lighter than stk_string, good for multiple voices.

**Inputs:**
- `Frequency` (CV) - Pitch modulation
- `Pluck` (Gate) - Pluck trigger
- `Damping` (CV) - String damping modulation

**Outputs:**
- `Out` (Audio) - Mono plucked string output

**Parameters:**
- `Frequency` (20 Hz - 2 kHz) - Base pitch
- `Damping` (0-1) - String decay
- `Pick Position` (0-1) - Where string is plucked
- `Loop Gain` (0-1) - Feedback amount

**How to Use:**
1. Connect gate to Pluck for notes
2. Modulate frequency with CV
3. Adjust damping for decay time
4. Use multiple instances for polyphonic plucks
5. Combine with chorus for rich textures

---

**Integration Effort**: Low - Direct C++ integration, well-documented API

**Priority**: ⭐⭐⭐⭐⭐ High - Adds unique synthesis capabilities not currently available

---

### **2. Soundpipe**

**⚠️ REASSESSMENT: Limited Unique Value**

**Critical Issues:**
- ❌ **Archived/Unmaintained**: Soundpipe repository was archived in January 2024 (read-only, no updates)
- ❌ **Significant Overlap**: Most proposed modules already exist or can be achieved with existing modules
- ⚠️ **Maintenance Risk**: Using unmaintained library poses long-term risks

**What You Already Have:**
- ✅ **bit_crusher** - Already exists! (sp_bitcrush would be redundant)
- ✅ **delay** - Can achieve comb filtering with feedback
- ✅ **phaser** - Similar to allpass filter effects
- ✅ **waveshaper** - Already exists with multiple algorithms
- ✅ **drive** - Distortion/saturation already covered
- ✅ **LFO + VCA** - Can create tremolo effects
- ✅ **VCO + Math** - Can create ring modulation

**What Soundpipe Could Add (Limited Unique Value):**

#### **Potentially Unique Algorithms:**

1. **Specific Filter Variants** (if different from your VCF)
   - Moog ladder filter variants
   - Butterworth filter implementations
   - But: You already have VCF, may not be worth it

2. **Wavetable Oscillators** (if you don't have these)
   - High-precision wavetable synthesis
   - But: You have VCO and PolyVCO

3. **FM Synthesis Modules** (if you don't have FM)
   - FM synthesis algorithms
   - But: Can be achieved with VCO + CV modulation

4. **Specific Physical Modeling** (if different from STK)
   - Some Csound-derived physical models
   - But: STK is better for this purpose

**Honest Assessment:**

**❌ NOT RECOMMENDED** for the following reasons:

1. **Redundancy**: Most effects can be achieved with existing modules:
   - Ring mod = VCO (sine) × Audio (using Math module)
   - Comb filter = Delay with high feedback
   - Tremolo = LFO → VCA
   - Bit crusher = Already exists
   - Allpass = Similar to phaser
   - Waveshaper = Already exists

2. **Maintenance Risk**: Archived library means:
   - No bug fixes
   - No security updates
   - No compatibility with future systems
   - Potential build issues

3. **Integration Effort vs Value**: 
   - Requires C wrapper implementation
   - Module registration and UI work
   - Testing and optimization
   - **ROI is low** given redundancy

4. **Better Alternatives**:
   - **STK** - Better for physical modeling (actively maintained)
   - **Essentia** - Unique analysis capabilities
   - **Faust** - User customization (more valuable)
   - **Gamma** - Spectral processing (unique)

**Recommendation:**

**Skip Soundpipe** and focus on:
- ✅ **STK** - Adds unique physical modeling synthesis
- ✅ **Essentia** - Adds unique audio analysis capabilities
- ✅ **Faust** - Enables user customization (high value)
- ✅ **Gamma** - Adds spectral processing (unique)

**If you really want these effects**, implement them directly:
- Ring modulator: Simple multiplication (VCO × Audio)
- Comb filter: Delay with feedback (you have delay)
- Tremolo: LFO → VCA (you have both)
- Allpass: Can add to phaser module if needed

**Priority**: ⭐⭐ **Low** - Not recommended due to redundancy and maintenance concerns

---

### **3. CREATE Signal Library (CSL)**

**What it adds:**
- Unit generator architecture (similar to your modular system)
- Sound synthesis and signal processing functions
- Portable framework

**Benefits for Pikon Raditsz:**

1. **Unit Generator Architecture Match**
   - CSL's architecture aligns perfectly with your modular system
   - Easy to map CSL unit generators to your node modules
   - Natural fit for your patchable interface

2. **Additional Synthesis Methods**
   - Add synthesis techniques not in your current system
   - Expand your sound design capabilities
   - Provide more options for users

3. **Research-Grade Algorithms**
   - CSL is used in academic research
   - Well-tested, reliable algorithms
   - High-quality implementations

4. **Modular Integration**
   - Each CSL unit generator can become a node module
   - CV inputs can control CSL parameters
   - Seamless integration with your existing system

**Integration Effort**: Medium - C++ framework, needs architecture mapping

**Priority**: ⭐⭐⭐ Medium - Good fit architecturally, but may overlap with existing capabilities

---

### **4. Gamma DSP Library**

**⚠️ REASSESSMENT: Limited Unique Value**

**Critical Issues:**
- ❌ **Significant Overlap**: Most proposed modules overlap with existing capabilities
- ⚠️ **Niche Use Cases**: Spectral effects are specialized and may have limited appeal
- ⚠️ **STK Better for Physical Modeling**: STK is more mature and feature-rich for physical modeling

**What You Already Have:**
- ✅ **granulator** - Full-featured granular synthesizer/effect
- ✅ **spatial_granulator** - Advanced visual canvas granulator with chorus
- ✅ **frequency_graph** - Real-time spectrum analyzer with frequency-based gates
- ✅ **graphic_eq** - 8-band graphic equalizer (frequency-domain processing)
- ✅ **8bandshaper** - Multi-band waveshaper (frequency-specific processing)
- ✅ **VCF** - Time-domain filtering (often more efficient than spectral)

**What Gamma Could Add (Limited Unique Value):**

#### **Potentially Unique Algorithms:**

1. **Spectral Filtering (FFT-based)**
   - FFT-based filtering vs time-domain VCF
   - **But**: Time-domain filters (VCF) are usually more efficient and sound better for most use cases
   - **Value**: Low - Niche use case, FFT adds latency

2. **Spectral Morphing**
   - Morph between two sources in frequency domain
   - **But**: Can achieve similar results with crossfading + EQ
   - **Value**: Low-Medium - Creative but niche

3. **Spectral Freeze**
   - Freeze spectrum for sustained textures
   - **But**: Can achieve with granulator freeze + reverb
   - **Value**: Low-Medium - Creative but niche

4. **Enhanced Granular Synthesis**
   - More granular options
   - **But**: You already have `granulator` and `spatial_granulator` (which is quite advanced!)
   - **Value**: Low - Redundant

5. **Modal Synthesis**
   - Physical modeling with modal resonators
   - **But**: STK is better for physical modeling (more instruments, better algorithms)
   - **Value**: Low - STK is superior

**Honest Assessment:**

**❌ NOT RECOMMENDED** for the following reasons:

1. **Granular Synthesis Redundancy**:
   - You already have `granulator` (full-featured)
   - You already have `spatial_granulator` (advanced visual canvas version)
   - Gamma's granular synthesis adds little value

2. **Spectral Processing Niche**:
   - FFT-based effects add latency (not ideal for real-time)
   - Time-domain filters (VCF) are usually better
   - Spectral morphing/freeze are creative but niche
   - Limited user appeal vs development effort

3. **Physical Modeling Redundancy**:
   - STK is better for physical modeling
   - More instruments, better algorithms, actively maintained
   - Gamma's modal synthesis is redundant if integrating STK

4. **Better Alternatives**:
   - **STK** - Better for physical modeling (actively maintained, more features)
   - **Essentia** - Unique analysis capabilities (no overlap)
   - **Faust** - User customization (high value, unique capability)
   - **Your existing modules** - Already cover most use cases

**If You Want Spectral Effects:**

Consider implementing directly if needed:
- **Spectral Freeze**: Can be achieved with granulator (freeze buffer) + reverb
- **Spectral Morphing**: Crossfade + EQ can achieve similar results
- **Spectral Filtering**: FFT-based filtering adds latency; VCF is usually better

**Recommendation:**

**Skip Gamma** and focus on:
- ✅ **STK** - Adds unique physical modeling synthesis
- ✅ **Essentia** - Adds unique audio analysis capabilities
- ✅ **Faust** - Enables user customization (high value)

**Priority**: ⭐⭐ **Low** - Not recommended due to redundancy and niche use cases

---

## 🔧 Functional/Declarative Audio Languages

### **5. Faust (Functional Audio Stream)**

**What it adds:**
- High-level functional language for audio DSP
- Compiles to optimized C++ code
- Can generate JUCE plugin code directly
- Extensive DSP library

**Specific Module Proposals:**

#### **faust_editor**
**Faust Code Editor and Compiler**

Allows users to write Faust DSP code and compile it into a custom module in real-time. This is a meta-module that creates other modules.

**Inputs:**
- `Audio In 1-8` (Audio) - Audio inputs (configurable)
- `CV In 1-16` (CV) - CV inputs (configurable)

**Outputs:**
- `Audio Out 1-8` (Audio) - Audio outputs (configurable)
- `CV Out 1-16` (CV) - CV outputs (configurable)

**Parameters:**
- `Faust Code` (Text) - Faust DSP code editor
- `Compile` (Button) - Compile Faust code to module
- `Num Audio Ins` (1-8) - Number of audio inputs
- `Num Audio Outs` (1-8) - Number of audio outputs
- `Num CV Ins` (0-16) - Number of CV inputs
- `Num CV Outs` (0-16) - Number of CV outputs
- `Sample Rate` (Choice) - 44.1k, 48k, 96k
- `Buffer Size` (64-2048) - Audio buffer size

**How to Use:**
1. Write Faust DSP code in the editor
2. Define inputs/outputs in Faust code
3. Click Compile to create module
4. New module appears in patch with your custom DSP
5. Use like any other module
6. Save/load Faust code as preset
7. Share Faust code with community

**Example Faust Code:**
```faust
import("stdfaust.lib");

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
process = os.osc(freq) * gain;
```

**CV Integration Ideas:**
- Faust code can use CV inputs as parameters
- Map CV to any Faust parameter
- Create custom CV-controlled effects

---

#### **faust_library_* (Multiple Modules)**

Pre-built Faust modules from the Faust library, compiled to JUCE modules:

#### **faust_freeverb**
**Freeverb Reverb**

Classic reverb algorithm from Faust library.

**Inputs:**
- `Audio In` (Audio) - Audio input
- `Room Size` (CV) - Room size modulation (0-1V)
- `Damping` (CV) - Damping modulation (0-1V)

**Outputs:**
- `Out L` (Audio) - Left output
- `Out R` (Audio) - Right output

**Parameters:**
- `Room Size` (0-1) - Reverb room size
- `Damping` (0-1) - High-frequency damping
- `Wet Level` (0-1) - Reverb mix
- `Dry Level` (0-1) - Dry signal level
- `Width` (0-1) - Stereo width

---

#### **faust_zita_rev1**
**Zita Reverb**

Advanced reverb algorithm with more control.

**Inputs:**
- `Audio In` (Audio) - Audio input
- `Room Size` (CV) - Room size modulation
- `Low Cut` (CV) - Low frequency cutoff modulation
- `High Cut` (CV) - High frequency cutoff modulation

**Outputs:**
- `Out L` (Audio) - Left output
- `Out R` (Audio) - Right output

**Parameters:**
- `Room Size` (0-1) - Room size
- `Low Cut` (20-200 Hz) - Low frequency cutoff
- `High Cut` (2000-20000 Hz) - High frequency cutoff
- `Early/Late` (0-1) - Early vs late reflections balance
- `Wet Level` (0-1) - Reverb mix

---

#### **faust_phaser**
**Phaser Effect**

Classic phaser effect from Faust library.

**Inputs:**
- `Audio In` (Audio) - Audio input
- `Rate` (CV) - LFO rate modulation (0-1V = 0-20 Hz)
- `Depth` (CV) - Phaser depth modulation (0-1V)
- `Feedback` (CV) - Feedback modulation (0-1V)

**Outputs:**
- `Out` (Audio) - Phased output

**Parameters:**
- `Rate` (0-20 Hz) - LFO speed
- `Depth` (0-1) - Modulation depth
- `Feedback` (0-0.99) - Feedback amount
- `Stages` (2-12) - Number of allpass stages
- `Mix` (0-1) - Dry/wet mix

---

#### **faust_chorus**
**Chorus Effect**

Classic chorus effect with multiple voices.

**Inputs:**
- `Audio In` (Audio) - Audio input
- `Rate` (CV) - LFO rate modulation (0-1V = 0-20 Hz)
- `Depth` (CV) - Chorus depth modulation (0-1V)
- `Delay` (CV) - Delay time modulation (0-1V = 0-50 ms)

**Outputs:**
- `Out L` (Audio) - Left output
- `Out R` (Audio) - Right output

**Parameters:**
- `Rate` (0-20 Hz) - LFO speed
- `Depth` (0-1) - Modulation depth
- `Delay` (0-50 ms) - Base delay time
- `Voices` (1-4) - Number of chorus voices
- `Mix` (0-1) - Dry/wet mix

---

**Integration Effort**: Medium - Need Faust compiler integration, but can generate JUCE code

**Priority**: ⭐⭐⭐⭐⭐ Very High - Unique capability, enables rapid development and user customization

---

### **6. libpd (Pure Data Embedded)**

**What it adds:**
- Run Pure Data patches without GUI
- Extensive community patch library
- Real-time audio processing
- MIDI support

**Benefits for Pikon Raditsz:**

1. **Massive Patch Library**
   - Access to thousands of Pure Data patches
   - Convert popular Pd patches to your modules
   - Leverage decades of Pure Data community work

2. **Pd Patch Player Module**
   - **Pure Data Player Module**: Load and run Pure Data patches as modules
   - Users can import Pd patches
   - Bridge between Pure Data and Pikon Raditsz communities

3. **Rapid Algorithm Import**
   - Convert proven Pd algorithms to your system
   - Test algorithms before full implementation
   - Learn from Pd's extensive library

4. **Community Compatibility**
   - Users familiar with Pure Data can use their patches
   - Import existing Pd work
   - Expand user base

5. **Educational Resource**
   - Pure Data patches are often well-documented
   - Learn from community examples
   - Understand algorithm implementations

**Integration Effort**: Medium - C library, needs wrapper and patch loading system

**Priority**: ⭐⭐⭐ Medium - Powerful but adds dependency, may be complex to integrate

---

### **7. Csound**

**What it adds:**
- Extensive synthesis capabilities
- Decades of development
- C API for integration
- Very powerful but heavier

**Benefits for Pikon Raditsz:**

1. **Comprehensive Synthesis**
   - Access to Csound's vast synthesis capabilities
   - Add synthesis methods not available elsewhere
   - Professional-grade algorithms

2. **Csound Engine Module**
   - **Csound Processor Module**: Run Csound code as a module
   - Users can write Csound code for custom synthesis
   - Leverage Csound's power

3. **Advanced Algorithms**
   - Additive synthesis, FOF synthesis, etc.
   - Advanced effects and processing
   - Research-grade implementations

4. **Educational Value**
   - Csound is widely used in education
   - Well-documented synthesis techniques
   - Learning resource for users

**Integration Effort**: High - Heavier dependency, more complex integration

**Priority**: ⭐⭐ Low-Medium - Very powerful but heavy, may be overkill for most use cases

---

## 📊 Audio Analysis & Feature Extraction

### **8. Essentia**

**What it adds:**
- Real-time audio feature extraction
- Music information retrieval
- Beat tracking, key detection, etc.
- Native C++ library

**Specific Module Proposals:**

#### **essentia_beat_tracker**
**Beat Detection and Tempo Tracker**

Detects beats and estimates tempo from incoming audio. Outputs gate triggers on beats and CV for tempo.

**Inputs:**
- `Audio In` (Audio) - Audio input to analyze

**Outputs:**
- `Beat` (Gate) - Gate trigger on each detected beat
- `Tempo CV` (CV) - Detected tempo as CV (0-1V maps to 60-200 BPM)
- `Confidence` (CV) - Detection confidence (0-1V)
- `Phase` (CV) - Beat phase position (0-1V, cycles per beat)

**Parameters:**
- `Min Tempo` (60-200 BPM) - Minimum tempo to detect
- `Max Tempo` (60-200 BPM) - Maximum tempo to detect
- `Sensitivity` (0-1) - Beat detection sensitivity
- `Lookahead` (0-500 ms) - Prediction lookahead time
- `Reset` (Button) - Reset beat tracking

**How to Use:**
1. Connect audio input (from audio_input, sample_loader, or any audio source)
2. Beat gate triggers on detected beats - connect to sequencer clock or other modules
3. Tempo CV can drive tempo_clock module for automatic sync
4. Use confidence CV to gate other modules (only trigger when confident)
5. Phase CV can drive LFOs or other cyclic modules
6. Adjust sensitivity for different music styles
7. Combine with tempo_clock for automatic tempo following

**CV Integration Ideas:**
- Connect Beat gate to sequencer reset for auto-sync
- Use Tempo CV to modulate tempo_clock BPM
- Connect Confidence to VCA for gating effects
- Use Phase CV to sync LFOs to detected beats

---

#### **essentia_key_detector**
**Musical Key Detection**

Detects the musical key (C major, A minor, etc.) from audio input.

**Inputs:**
- `Audio In` (Audio) - Audio input to analyze

**Outputs:**
- `Key CV` (CV) - Detected key as CV (0-1V = C to B)
- `Mode CV` (CV) - Major (0V) or Minor (1V)
- `Confidence` (CV) - Detection confidence (0-1V)
- `Key Change` (Gate) - Trigger on key change

**Parameters:**
- `Analysis Window` (1-10 seconds) - Time window for analysis
- `Update Rate` (0.1-10 Hz) - How often to update detection
- `Smoothing` (0-1) - Smooth key changes
- `Reset` (Button) - Reset detection

**How to Use:**
1. Connect audio input
2. Key CV can drive quantizer for scale-aware quantization
3. Mode CV indicates major/minor - use for harmonic effects
4. Key Change gate triggers when key changes
5. Use confidence to gate other modules
6. Combine with quantizer for automatic scale quantization
7. Display detected key in UI for user feedback

**CV Integration Ideas:**
- Connect Key CV to quantizer root note
- Use Mode CV to switch between major/minor scales
- Connect Key Change gate to sequencer for key-aware sequences

---

#### **essentia_onset_detector**
**Note Onset Detection**

Detects note onsets (attacks) in audio, useful for triggering events or analyzing rhythm.

**Inputs:**
- `Audio In` (Audio) - Audio input to analyze

**Outputs:**
- `Onset` (Gate) - Trigger on each detected onset
- `Velocity` (CV) - Onset velocity/intensity (0-1V)
- `Confidence` (CV) - Detection confidence (0-1V)

**Parameters:**
- `Threshold` (0-1) - Onset detection threshold
- `Min Interval` (0-1000 ms) - Minimum time between onsets
- `Sensitivity` (0-1) - Detection sensitivity
- `Method` (Choice) - Detection algorithm (Energy, Spectral, Complex)

**How to Use:**
1. Connect audio input
2. Onset gate triggers on each detected note attack
3. Use Velocity CV for dynamic responses
4. Connect to sequencer for rhythm extraction
5. Use to trigger envelopes or other modules
6. Adjust threshold for different audio types
7. Combine with delay for rhythmic echoes

**CV Integration Ideas:**
- Connect Onset gate to ADSR trigger
- Use Velocity CV to modulate VCA or filter
- Connect to sequencer for automatic rhythm capture

---

#### **essentia_pitch_tracker**
**Pitch Detection and Tracking**

Extracts fundamental frequency (pitch) from audio input. Useful for audio-to-MIDI or pitch-following effects.

**Inputs:**
- `Audio In` (Audio) - Audio input to analyze

**Outputs:**
- `Pitch CV` (CV) - Detected pitch as CV (0-1V = 20Hz to 2kHz, exponential)
- `Pitch Hz` (Raw) - Detected pitch in Hz
- `Confidence` (CV) - Tracking confidence (0-1V)
- `Pitch Bend` (CV) - Pitch change velocity (0-1V)

**Parameters:**
- `Min Frequency` (20-200 Hz) - Minimum pitch to track
- `Max Frequency` (200-2000 Hz) - Maximum pitch to track
- `Smoothing` (0-1) - Pitch smoothing amount
- `Method` (Choice) - Tracking algorithm (YIN, MPM, etc.)

**How to Use:**
1. Connect audio input (monophonic works best)
2. Pitch CV can drive VCO frequency for pitch-following
3. Use Pitch Hz for frequency displays
4. Confidence CV indicates tracking quality
5. Pitch Bend CV shows pitch change rate
6. Combine with VCO for vocoder-like effects
7. Use for audio-to-MIDI conversion

**CV Integration Ideas:**
- Connect Pitch CV to VCO frequency for pitch-following synth
- Use Confidence to gate output (only when tracking well)
- Connect Pitch Bend to filter cutoff for pitch-reactive filtering

---

#### **essentia_audio_to_cv**
**Multi-Feature Audio-to-CV Converter**

Extracts multiple audio features and converts them to CV signals for modulation.

**Inputs:**
- `Audio In` (Audio) - Audio input to analyze

**Outputs:**
- `RMS` (CV) - RMS energy (0-1V)
- `Peak` (CV) - Peak amplitude (0-1V)
- `Spectral Centroid` (CV) - Brightness/timbre (0-1V)
- `Spectral Rolloff` (CV) - High-frequency content (0-1V)
- `Zero Crossing Rate` (CV) - Noisiness (0-1V)
- `MFCC 1-13` (CV) - Mel-frequency cepstral coefficients (timbre features)

**Parameters:**
- `Analysis Window` (10-1000 ms) - Analysis window size
- `Update Rate` (10-1000 Hz) - CV update rate
- `Smoothing` (0-1) - CV smoothing
- `Normalize` (Bool) - Normalize CV outputs

**How to Use:**
1. Connect audio input
2. RMS CV for overall energy/volume
3. Peak CV for transient detection
4. Spectral Centroid for brightness (high = bright)
5. Spectral Rolloff for high-frequency content
6. Zero Crossing Rate for noisiness (high = noisy)
7. MFCC outputs for detailed timbre analysis
8. Use CVs to modulate any module parameters
9. Create audio-reactive patches

**CV Integration Ideas:**
- Connect RMS to VCA for auto-gain
- Use Spectral Centroid to modulate filter cutoff
- Connect Peak to compressor threshold
- Use MFCC outputs for complex timbre-based modulation

---

**Integration Effort**: Low - Native C++ library, perfect for JUCE

**Priority**: ⭐⭐⭐⭐⭐ Very High - Adds unique analysis capabilities, enables audio-reactive systems

---

### **9. aubio**

**What it adds:**
- Onset detection
- Beat tracking
- Pitch detection
- Tempo estimation

**Benefits for Pikon Raditsz:**

1. **Lightweight Analysis Alternative**
   - Lighter than Essentia
   - Focused on tempo/beat/pitch
   - Good for specific use cases

2. **Real-Time Tempo Detection**
   - **Tempo Tracker Module**: Real-time tempo detection
   - Sync modules to live audio tempo
   - Follow tempo changes dynamically

3. **Onset Detection**
   - **Onset Detector Module**: Detect note onsets
   - Trigger events on audio onsets
   - Create reactive patches

4. **Pitch Tracking**
   - **Pitch Tracker Module**: Extract pitch from audio
   - Convert audio to CV based on pitch
   - Audio-to-MIDI conversion

5. **Complement Essentia**
   - Use aubio for lightweight tasks
   - Use Essentia for comprehensive analysis
   - Choose based on needs

**Integration Effort**: Low - C library, easy C++ integration

**Priority**: ⭐⭐⭐ Medium - Good if you want lighter alternative to Essentia, or use both

---

## 🎯 Integration Priority Summary

### **Tier 1: Highest Impact, Easy Integration**
1. **Essentia** ⭐⭐⭐⭐⭐
   - Adds unique audio analysis capabilities
   - Enables audio-reactive systems
   - Perfect JUCE integration

2. **Faust** ⭐⭐⭐⭐⭐
   - Rapid module development
   - User customization potential
   - Unique selling point

3. **STK** ⭐⭐⭐⭐⭐
   - Adds physical modeling synthesis
   - Complements existing modules
   - Great CV integration potential

### **Tier 2: High Value, Moderate Effort**
4. **Soundpipe** ⭐⭐⭐⭐
   - Massive expansion of module library
   - Easy to integrate
   - Quick wins

5. **Gamma** ⭐⭐⭐⭐
   - Spectral processing
   - Enhanced granular synthesis
   - Modern C++ design

### **Tier 3: Specialized Use Cases**
6. **aubio** ⭐⭐⭐
   - Lightweight analysis alternative
   - Specific tempo/pitch needs

7. **libpd** ⭐⭐⭐
   - Access to Pure Data library
   - Community compatibility

8. **CSL** ⭐⭐⭐
   - Good architectural fit
   - May overlap with existing capabilities

9. **Csound** ⭐⭐
   - Very powerful but heavy
   - May be overkill

---

## 🚀 Recommended Integration Order

### **Phase 1: Analysis & Rapid Development**
1. **Essentia** - Add audio analysis modules, enable audio-reactive systems
2. **Faust** - Enable rapid module development and user customization

### **Phase 2: Synthesis Expansion**
3. **STK** - Add physical modeling synthesis modules
4. **Soundpipe** - Rapidly expand effect module library

### **Phase 3: Advanced Features**
5. **Gamma** - Add spectral processing and enhanced granular synthesis
6. **aubio** - Add lightweight analysis alternatives (if needed)

### **Phase 4: Community & Compatibility** (Optional)
7. **libpd** - Enable Pure Data patch import
8. **CSL** - Additional synthesis methods (if needed)

---

## 💡 Unique Synergies with Pikon Raditsz Features

### **Computer Vision + Audio Libraries**
- **STK + CV**: Gesture-controlled physical modeling (bow position, breath pressure)
- **Essentia + CV**: Multi-modal feature extraction (audio + visual)
- **Faust + CV**: User-written Faust modules controlled by CV gestures

### **CV Modulation + Libraries**
- All libraries can expose parameters for CV control
- Create dynamic, evolving sounds
- Real-time parameter modulation

### **Modular Architecture + Libraries**
- Each library algorithm becomes a node module
- Seamless integration with existing patch system
- Expand capabilities without changing architecture

### **MIDI/OSC + Libraries**
- Libraries can respond to MIDI/OSC
- Integrate with existing MIDI/OSC modules
- Multi-protocol control

---

## 📝 Implementation Notes

- All libraries are **real-time capable** - perfect for your audio system
- All libraries are **JUCE-compatible** - easy integration
- Consider **licensing** - most are open-source, compatible with your project
- **Modular approach** - integrate libraries incrementally
- **User experience** - new modules should feel native to Pikon Raditsz
- **Documentation** - document new modules in your node dictionary

---

This integration would significantly expand Pikon Raditsz's capabilities while maintaining its modular, patchable philosophy!

---

## 📋 Complete Module List Summary

### **STK (Synthesis Toolkit) - Physical Modeling**
1. **stk_string** - Physical modeling string synthesizer (guitar, violin, cello, etc.)
2. **stk_wind** - Physical modeling wind instruments (flute, clarinet, saxophone, etc.)
3. **stk_percussion** - Modal synthesis percussion (drums, cymbals, etc.)
4. **stk_plucked** - Karplus-Strong plucked string synthesis

### **Essentia - Audio Analysis**
5. **essentia_beat_tracker** - Beat detection and tempo tracking
6. **essentia_key_detector** - Musical key detection (C major, A minor, etc.)
7. **essentia_onset_detector** - Note onset/attack detection
8. **essentia_pitch_tracker** - Pitch detection and tracking
9. **essentia_audio_to_cv** - Multi-feature audio-to-CV converter

### **Soundpipe - DSP Effects**
10. **sp_ringmod** - Ring modulator
11. **sp_comb** - Comb filter
12. **sp_bitcrush** - Bit crusher
13. **sp_allpass** - Allpass filter
14. **sp_tremolo** - Tremolo effect
15. **sp_waveshaper** - Waveshaper distortion

### **Faust - User-Customizable DSP**
16. **faust_editor** - Faust code editor and compiler (meta-module)
17. **faust_freeverb** - Freeverb reverb algorithm
18. **faust_zita_rev1** - Advanced Zita reverb
19. **faust_phaser** - Phaser effect
20. **faust_chorus** - Chorus effect
*(Plus unlimited user-created modules via faust_editor)*

### **Gamma - Advanced DSP**
21. **gamma_granulator** - Advanced granular synthesizer
22. **gamma_spectral_filter** - FFT-based spectral filtering
23. **gamma_spectral_morph** - Spectral morphing between sources
24. **gamma_spectral_freeze** - Spectral freeze effect
25. **gamma_modal_synth** - Modal synthesis

---

## 🎯 Module Categories

### **Source Modules** (Generate Audio)
- stk_string, stk_wind, stk_percussion, stk_plucked
- gamma_modal_synth

### **Effect Modules** (Process Audio)
- sp_ringmod, sp_comb, sp_bitcrush, sp_allpass, sp_tremolo, sp_waveshaper
- faust_freeverb, faust_zita_rev1, faust_phaser, faust_chorus
- gamma_granulator, gamma_spectral_filter, gamma_spectral_morph, gamma_spectral_freeze

### **Analysis Modules** (Extract Features)
- essentia_beat_tracker, essentia_key_detector, essentia_onset_detector
- essentia_pitch_tracker, essentia_audio_to_cv

### **Utility Modules** (Convert/Transform)
- essentia_audio_to_cv (audio → CV)
- faust_editor (code → module)

---

## 🔗 Integration with Existing Modules

### **Works Great With:**
- **VCO/VCF**: STK modules complement subtractive synthesis
- **Granulator**: Gamma granulator adds advanced granular options
- **Reverb/Delay**: Soundpipe and Faust add alternative reverb algorithms
- **Sequencer**: Essentia beat tracker can sync sequencers
- **Quantizer**: Essentia key detector can set quantizer scales
- **Computer Vision**: STK modules can be gesture-controlled
- **CV Modulation**: All modules expose CV inputs for modulation

### **New Capabilities:**
- **Physical Modeling**: Realistic instrument emulation (STK)
- **Spectral Processing**: Frequency-domain effects (Gamma)
- **Audio Analysis**: Extract features from audio (Essentia)
- **User Customization**: Write custom DSP code (Faust)
- **Expanded Effects**: 100+ new effects (Soundpipe)

---

## 💡 Quick Start Ideas

### **Physical Modeling Orchestra**
1. Add stk_string, stk_wind, stk_percussion modules
2. Connect sequencers to trigger notes
3. Use CV for expression (breath, bow pressure)
4. Combine with reverb for realistic space

### **Audio-Reactive Patch**
1. Connect audio_input to essentia_beat_tracker
2. Use Beat gate to trigger sequencer
3. Connect essentia_audio_to_cv RMS to VCA
4. Use Spectral Centroid CV to modulate filter cutoff

### **User-Custom Effect**
1. Open faust_editor module
2. Write Faust DSP code
3. Compile to create custom module
4. Use in patch like any other module

### **Spectral Sound Design**
1. Connect VCO to gamma_spectral_filter
2. Modulate cutoff with LFO
3. Freeze spectrum with gamma_spectral_freeze
4. Morph between sources with gamma_spectral_morph

---

All modules follow Pikon Raditsz's existing architecture:
- ✅ CV modulation inputs
- ✅ Gate/trigger inputs where appropriate
- ✅ Audio I/O matching existing modules
- ✅ Parameter ranges and UI consistent with current modules
- ✅ Integration with existing CV, MIDI, OSC, and Computer Vision systems

