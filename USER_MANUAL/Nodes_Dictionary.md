# Collider Modular Synthesizer - Node Dictionary

**Last Updated:** December 17, 2024  
**Version:** 1.1

---

## Table of Contents

### Quick Reference Index

#### 1. SOURCE NODES
- [VCO](#vco) - Voltage-Controlled Oscillator
- [PolyVCO](#polyvco) - Multi-Voice Oscillator Bank
- [Noise](#noise) - Noise Generator
- [Audio Input](#audio-input) - Hardware Audio Input
- [Sample Loader](#sample-loader) - Audio Sample Player
- [Value](#value) - Constant Value Generator

#### 2. EFFECT NODES
- [VCF](#vcf) - Voltage-Controlled Filter
- [Delay](#delay) - Stereo Delay Effect
- [Reverb](#reverb) - Stereo Reverb Effect
- [Chorus](#chorus) - Stereo Chorus Effect
- [Phaser](#phaser) - Stereo Phaser Effect
- [Compressor](#compressor) - Dynamic Range Compressor
- [Limiter](#limiter) - Audio Limiter
- [Gate](#gate) - Noise Gate
- [Drive](#drive) - Waveshaping Distortion
- [Graphic EQ](#graphic-eq) - 8-Band Graphic Equalizer
- [Waveshaper](#waveshaper) - Multi-Algorithm Waveshaper
- [8-Band Shaper](#8-band-shaper) - Multi-Band Waveshaper
- [Granulator](#granulator) - Granular Synthesizer/Effect
- [Harmonic Shaper](#harmonic-shaper) - Harmonic Content Shaper
- [TimePitch](#timepitch) - Time/Pitch Manipulation
- [De-Crackle](#de-crackle) - Click/Pop Reducer
- [Vocal Tract Filter](#vocal-tract-filter) - Formant Filter

#### 3. MODULATOR NODES
- [LFO](#lfo) - Low-Frequency Oscillator
- [ADSR](#adsr) - Envelope Generator
- [Random](#random) - Random Value Generator
- [S&H](#sh) - Sample & Hold
- [Function Generator](#function-generator) - Drawable Envelope/LFO
- [Shaping Oscillator](#shaping-oscillator) - Oscillator with Built-in Waveshaper

#### 4. UTILITY & LOGIC NODES
- [VCA](#vca) - Voltage-Controlled Amplifier
- [Mixer](#mixer) - Stereo Audio Mixer
- [CV Mixer](#cv-mixer) - Control Voltage Mixer
- [Track Mixer](#track-mixer) - Multi-Channel Mixer
- [Attenuverter](#attenuverter) - Attenuate/Invert Signal
- [Lag Processor](#lag-processor) - Slew Limiter/Smoother
- [Math](#math) - Mathematical Operations
- [MapRange](#maprange) - Value Range Mapper
- [Quantizer](#quantizer) - Musical Scale Quantizer
- [Rate](#rate) - Rate Value Converter
- [Comparator](#comparator) - Threshold Comparator
- [Logic](#logic) - Boolean Logic Operations
- [Clock Divider](#clock-divider) - Clock Division/Multiplication
- [Sequential Switch](#sequential-switch) - Signal Router

#### 5. SEQUENCER NODES
- [Sequencer](#sequencer) - 16-Step CV/Gate Sequencer
- [Multi Sequencer](#multi-sequencer) - Advanced Multi-Output Sequencer
- [Snapshot Sequencer](#snapshot-sequencer) - Patch State Sequencer
- [Stroke Sequencer](#stroke-sequencer) - Gesture-Based Sequencer
- [Tempo Clock](#tempo-clock) - Global Clock Generator

#### 6. MIDI NODES
- [MIDI CV](#midi-cv) - MIDI to CV Converter
- [MIDI Player](#midi-player) - MIDI File Player
- [MIDI Faders](#midi-faders) - MIDI-Learnable Faders (1-16)
- [MIDI Knobs](#midi-knobs) - MIDI-Learnable Knobs (1-16)
- [MIDI Buttons](#midi-buttons) - MIDI-Learnable Buttons (1-32)
- [MIDI Jog Wheel](#midi-jog-wheel) - MIDI Jog Wheel Control

#### 7. ANALYSIS NODES
- [Scope](#scope) - Oscilloscope
- [Debug](#debug) - Signal Value Logger
- [Input Debug](#input-debug) - Passthrough Debug Logger
- [Frequency Graph](#frequency-graph) - Spectrum Analyzer

#### 8. TTS (TEXT-TO-SPEECH) NODES
- [TTS Performer](#tts-performer) - Text-to-Speech Engine
- [Vocal Tract Filter](#vocal-tract-filter) - Formant Filter

#### 9. SPECIAL NODES
- [Physics](#physics) - 2D Physics Simulation
- [Animation](#animation) - 3D Animation Player

#### 10. COMPUTER VISION NODES
- [Webcam Loader](#webcam-loader) - Webcam Video Source
- [Video File Loader](#video-file-loader) - Video File Source
- [Movement Detector](#movement-detector) - Motion Detection
- [Human Detector](#human-detector) - Face/Body Detection
- [Object Detector](#object-detector) - Object Detection (YOLOv3)
- [Pose Estimator](#pose-estimator) - Body Keypoint Detection
- [Hand Tracker](#hand-tracker) - Hand Keypoint Tracking
- [Face Tracker](#face-tracker) - Facial Landmark Tracking
- [Color Tracker](#color-tracker) - Multi-Color Tracking
- [Contour Detector](#contour-detector) - Shape Detection
- [Semantic Segmentation](#semantic-segmentation) - Scene Segmentation

#### 11. SYSTEM NODES
- [Meta](#meta) - Meta Module Container
- [Inlet](#inlet) - Meta Module Input
- [Outlet](#outlet) - Meta Module Output
- [Comment](#comment) - Documentation Node
- [Recorder](#recorder) - Audio Recording to File
- [VST Host](#vst-host) - VST Plugin Host

---

## 1. SOURCE NODES

Source nodes generate or input signals into your patch.

### VCO
**Voltage-Controlled Oscillator**

A standard analog-style oscillator that generates periodic waveforms.

**Inputs:**
- `Frequency` (CV) - Frequency modulation input
- `Waveform` (CV) - Waveform selection modulation
- `Gate` (Gate) - Gate input for amplitude control

**Outputs:**
- `Out` (Audio) - Mono audio output

**Parameters:**
- `Frequency` (20 Hz - 20 kHz) - Base oscillator frequency
- `Waveform` (Choice) - Sine, Sawtooth, or Square wave
- `Relative Freq Mod` (Bool) - When enabled, CV modulates ±4 octaves around slider position. When disabled, CV directly maps to 20 Hz - 20 kHz
- `Portamento` (0-2 seconds) - Frequency glide/smoothing time

**How to Use:**
1. Connect the audio output to an effect or VCA
2. Set the frequency slider to your desired pitch
3. Optionally connect CV (from sequencer, LFO, or ADSR) to modulate frequency
4. Use the Relative Freq Mod toggle to choose between relative (musical) or absolute (full range) modulation
5. Connect a gate signal for amplitude gating if needed
6. Adjust portamento for smooth frequency transitions

---

### PolyVCO
**Multi-Voice Oscillator Bank**

A polyphonic oscillator module with up to 32 independent voices, ideal for creating rich, layered sounds or building polyphonic synthesizers.

**Inputs:**
- `Num Voices Mod` (Raw) - Control number of active voices (1-32)
- `Freq 1-32 Mod` (CV) - Individual frequency modulation for each voice
- `Wave 1-32 Mod` (CV) - Individual waveform modulation for each voice
- `Gate 1-32 Mod` (Gate) - Individual gate inputs for each voice

**Outputs:**
- `Out 1-32` (Audio) - 32 independent audio outputs (one per voice)

**Parameters:**
- `Num Voices` (1-32) - Number of active voices
- `Base Frequency` (20 Hz - 20 kHz) - Base frequency for all voices
- `Detune Amount` (0-100 cents) - Amount of random detuning between voices
- `Spread` (0-100%) - Frequency spread between voices
- `Waveform` (Choice) - Base waveform for all voices (Sine, Sawtooth, Square)

**How to Use:**
1. Set the number of voices you want active
2. Connect the voice outputs to a Track Mixer or individual effects
3. Use the detune parameter to create a chorus-like effect
4. Connect a Multi Sequencer's parallel outputs to the individual frequency and gate inputs for polyphonic melodies
5. Adjust spread to create harmonic stacks

---

### Noise
**Noise Generator**

Generates white, pink, or brown noise for percussion, ambience, or modulation.

**Inputs:**
- `Level Mod` (CV) - Level modulation input
- `Colour Mod` (CV) - Noise color modulation

**Outputs:**
- `Out L` (Audio) - Left channel output
- `Out R` (Audio) - Right channel output

**Parameters:**
- `Colour` (Choice) - White (flat spectrum), Pink (-3 dB/octave), or Brown (-6 dB/octave)
- `Level dB` (-60 to +6 dB) - Output level in decibels

**How to Use:**
1. Select the noise color (white for hi-hats, pink for general noise, brown for low rumble)
2. Adjust the level to taste
3. Optionally modulate the color with CV for dynamic timbral changes
4. Great for percussion synthesis when combined with envelopes and filters
5. Use as a modulation source for subtle random variations

---

### Audio Input
**Hardware Audio Input**

Brings external audio from your audio interface into the patch.

**Outputs:**
- `Out 1` (Audio) - Input channel 1
- `Out 2` (Audio) - Input channel 2
- `Gate` (Gate) - Gate signal when audio exceeds threshold
- `Trigger` (Gate) - Trigger signal on transients
- `EOP` (Gate) - End of phrase detection

**Parameters:**
- `Input Gain` (-60 to +20 dB) - Input gain control
- `Gate Threshold` (-60 to 0 dB) - Threshold for gate output
- `Trigger Sensitivity` (Low/Medium/High) - Transient detection sensitivity

**How to Use:**
1. Connect your external audio source (microphone, instrument, etc.) to your audio interface
2. Adjust input gain to get a healthy signal level
3. Use the gate and trigger outputs to create envelope followers or rhythm detection
4. Process the audio through effects or use it as a modulation source via envelope following

---

### Sample Loader
**Audio Sample Player**

Loads and plays audio samples with extensive playback control and modulation options.

**Inputs:**
- `Pitch Mod` (CV) - Pitch modulation in semitones
- `Speed Mod` (CV) - Playback speed modulation
- `Gate Mod` (CV) - Gate/trigger modulation
- `Trigger Mod` (Gate) - Retrigger the sample
- `Range Start Mod` (CV) - Modulate sample start point
- `Range End Mod` (CV) - Modulate sample end point
- `Randomize Trig` (Gate) - Randomize sample settings on trigger

**Outputs:**
- `Out L` (Audio) - Left channel output
- `Out R` (Audio) - Right channel output

**Parameters:**
- `File` (Button) - Load audio file (WAV, AIFF, FLAC, MP3)
- `Pitch` (-48 to +48 semitones) - Pitch shift amount
- `Speed` (0.1x to 4x) - Playback speed multiplier
- `Loop Mode` (Choice) - Off, Forward, Ping-Pong
- `Gate Mode` (Choice) - Trigger (one-shot), Gate (held), Free (always play)
- `Range Start` (0-100%) - Sample start position
- `Range End` (0-100%) - Sample end position
- `Reverse` (Bool) - Play sample in reverse
- `Randomize Range` (Bool) - Randomize start/end on each trigger

**How to Use:**
1. Click the "Load File" button and select an audio file
2. Set the pitch and speed for your desired sound
3. Choose a loop mode (Off for one-shots, Forward for sustained sounds)
4. Use Gate Mode: Trigger for drums, Gate for sustained tones, Free for continuous playback
5. Adjust Range Start/End to isolate specific portions of the sample
6. Connect CV to Pitch Mod for melodic playing
7. Use Trigger Mod to retrigger the sample rhythmically
8. Enable Randomize Range for variation on each hit

---

### Value
**Constant Value Generator**

Outputs a constant, adjustable numerical value in multiple formats.

**Outputs:**
- `Raw` (Raw) - Unprocessed value as-is
- `Normalized` (CV) - Value normalized to 0-1 range
- `Inverted` (Raw) - Negative of raw value
- `Integer` (Raw) - Truncated integer value
- `CV Out` (CV) - Scaled CV output (0-1 range)

**Parameters:**
- `Value` (-100 to +100) - The constant value to output

**How to Use:**
1. Adjust the value slider to your desired number
2. Connect the appropriate output to the destination:
   - Use `CV Out` for standard 0-1 modulation
   - Use `Raw` for custom ranges or mathematical operations with the Math node
   - Use `Integer` for step/index control
3. Great for setting static modulation amounts, offsets, or reference values
4. Combine multiple Value nodes with Math nodes for complex calculations

---

## 2. EFFECT NODES

Effect nodes process audio signals to shape tone, add space, or create sonic textures.

### VCF
**Voltage-Controlled Filter**

A resonant multi-mode filter for subtractive synthesis and tone shaping.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Cutoff Mod` (CV) - Cutoff frequency modulation
- `Resonance Mod` (CV) - Resonance amount modulation
- `Type Mod` (CV) - Filter type modulation

**Outputs:**
- `Out L` (Audio) - Left filtered output
- `Out R` (Audio) - Right filtered output

**Parameters:**
- `Cutoff` (20 Hz - 20 kHz) - Filter cutoff frequency
- `Resonance` (0.1 - 10.0) - Resonance/Q factor
- `Type` (Choice) - Low-pass, High-pass, or Band-pass
- `Relative Cutoff Mod` (Bool) - When enabled, CV modulates ±5 octaves around slider. When disabled, CV maps to full 20 Hz - 20 kHz range
- `Relative Resonance Mod` (Bool) - When enabled, CV scales resonance 0.25x-4x. When disabled, CV maps to full 0.1-10.0 range

**How to Use:**
1. Connect audio through the filter
2. Adjust cutoff to set the frequency where filtering occurs
3. Increase resonance for emphasis around the cutoff (be careful, high values can self-oscillate!)
4. Choose filter type: Low-pass removes highs, High-pass removes lows, Band-pass keeps only around cutoff
5. Modulate cutoff with envelopes or LFOs for classic synth sounds
6. Use Relative mode for musical modulation around a set position

---

### Delay
**Stereo Delay Effect**

A stereo delay effect with modulation and tempo sync capabilities.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Time Mod` (CV) - Delay time modulation
- `Feedback Mod` (CV) - Feedback amount modulation
- `Mix Mod` (CV) - Wet/dry mix modulation

**Outputs:**
- `Out L` (Audio) - Left delayed output
- `Out R` (Audio) - Right delayed output

**Parameters:**
- `Time (ms)` (1-2000 ms) - Delay time in milliseconds
- `Feedback` (0-0.95) - Amount of delayed signal fed back into the delay
- `Mix` (0-1) - Wet/dry balance (0=dry, 1=wet)
- `Relative Time Mod` (Bool) - Enable relative time modulation around slider position
- `Relative Feedback Mod` (Bool) - Enable relative feedback modulation
- `Relative Mix Mod` (Bool) - Enable relative mix modulation

**How to Use:**
1. Send audio through the delay
2. Set delay time to taste (short for slapback, long for echoes)
3. Adjust feedback for the number of repeats (be careful, high values can self-oscillate!)
4. Use mix to blend delayed signal with dry signal
5. Modulate time with LFOs for chorus-like effects
6. Connect to Tempo Clock's clock outputs and use short delay times for rhythmic effects

---

### Reverb
**Stereo Reverb Effect**

A stereo reverb effect that simulates acoustic spaces.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Size Mod` (CV) - Room size modulation
- `Damp Mod` (CV) - Damping modulation
- `Mix Mod` (CV) - Wet/dry mix modulation

**Outputs:**
- `Out L` (Audio) - Left reverb output
- `Out R` (Audio) - Right reverb output

**Parameters:**
- `Size` (0-1) - Room size (0=small, 1=large)
- `Damping` (0-1) - High frequency damping (0=bright, 1=dark)
- `Width` (0-1) - Stereo width
- `Mix` (0-1) - Wet/dry balance (0=dry, 1=wet)

**How to Use:**
1. Send audio through the reverb
2. Adjust size to set the perceived space (small room to large hall)
3. Use damping to control brightness (low damping=reflective surfaces, high damping=absorptive)
4. Adjust mix to blend reverb with dry signal
5. Great for adding depth and space to sounds
6. Use sparingly on bass-heavy sounds to avoid muddiness

---

### Chorus
**Stereo Chorus Effect**

A stereo chorus effect that creates thick, shimmering textures by layering slightly detuned copies of the signal.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Rate Mod` (CV) - LFO rate modulation
- `Depth Mod` (CV) - Effect depth modulation
- `Mix Mod` (CV) - Wet/dry mix modulation

**Outputs:**
- `Out L` (Audio) - Left chorus output
- `Out R` (Audio) - Right chorus output

**Parameters:**
- `Rate` (0.1-10 Hz) - LFO speed
- `Depth` (0-1) - Modulation depth
- `Mix` (0-1) - Wet/dry balance

**How to Use:**
1. Send audio through the chorus
2. Set rate for the speed of the sweeping effect (slow=gentle, fast=vibrato)
3. Adjust depth for intensity of detuning
4. Use mix to blend with dry signal
5. Great for thickening synth pads and leads
6. Use on clean guitars for classic 80s sounds

---

### Phaser
**Stereo Phaser Effect**

A stereo phaser effect that creates sweeping notches in the frequency spectrum.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Rate Mod` (CV) - LFO rate modulation
- `Depth Mod` (CV) - Effect depth modulation
- `Centre Mod` (CV) - Center frequency modulation
- `Feedback Mod` (CV) - Feedback amount modulation
- `Mix Mod` (CV) - Wet/dry mix modulation

**Outputs:**
- `Out L` (Audio) - Left phaser output
- `Out R` (Audio) - Right phaser output

**Parameters:**
- `Rate` (0.1-10 Hz) - LFO speed
- `Depth` (0-1) - Modulation depth
- `Centre Freq` (200-2000 Hz) - Center frequency of the sweep
- `Feedback` (0-0.95) - Amount of feedback (increases resonance)
- `Mix` (0-1) - Wet/dry balance

**How to Use:**
1. Send audio through the phaser
2. Adjust rate for sweep speed (slow=subtle, fast=intense)
3. Set centre frequency to target specific frequency ranges
4. Increase feedback for more pronounced notches
5. Great for adding movement to static sounds
6. Classic effect for electric pianos and guitars

---

### Compressor
**Dynamic Range Compressor**

Reduces the dynamic range of audio signals, making quiet parts louder and loud parts quieter.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Thresh Mod` (CV) - Threshold modulation
- `Ratio Mod` (CV) - Ratio modulation
- `Attack Mod` (CV) - Attack time modulation
- `Release Mod` (CV) - Release time modulation
- `Makeup Mod` (CV) - Makeup gain modulation

**Outputs:**
- `Out L` (Audio) - Left compressed output
- `Out R` (Audio) - Right compressed output

**Parameters:**
- `Threshold` (-60 to 0 dB) - Level above which compression starts
- `Ratio` (1:1 to 20:1) - Amount of compression
- `Attack` (0.1-100 ms) - How quickly compression engages
- `Release` (10-1000 ms) - How quickly compression disengages
- `Makeup Gain` (0-24 dB) - Output gain to compensate for level reduction

**How to Use:**
1. Set threshold to the level where you want compression to start
2. Adjust ratio (2:1 for gentle, 10:1+ for heavy compression)
3. Use fast attack to catch transients, slow attack to preserve punch
4. Set release to taste (fast for pumping effects, slow for smooth)
5. Adjust makeup gain to match the output level to the input
6. Great for controlling dynamics, adding sustain, and gluing mixes together

---

### Limiter
**Audio Limiter**

Prevents audio from exceeding a set level, acting as a "brick wall" for peaks.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Thresh Mod` (CV) - Threshold modulation
- `Release Mod` (CV) - Release time modulation

**Outputs:**
- `Out L` (Audio) - Left limited output
- `Out R` (Audio) - Right limited output

**Parameters:**
- `Threshold` (-60 to 0 dB) - Maximum allowed level
- `Release` (10-1000 ms) - Recovery time

**How to Use:**
1. Set threshold to the maximum level you want to allow
2. Adjust release time (fast for transparent, slow for smoother)
3. Use at the end of your signal chain to prevent clipping
4. Essential for mastering and protecting speakers
5. Can add punch and loudness when used aggressively

---

### Gate
**Noise Gate**

Silences signals below a threshold, useful for removing background noise or creating rhythmic effects.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input

**Outputs:**
- `Out L` (Audio) - Left gated output
- `Out R` (Audio) - Right gated output

**Parameters:**
- `Threshold` (-60 to 0 dB) - Level below which the gate closes
- `Attack` (0.1-100 ms) - How quickly the gate opens
- `Release` (10-1000 ms) - How quickly the gate closes
- `Range` (-60 to 0 dB) - Amount of attenuation when gate is closed

**How to Use:**
1. Set threshold just above your noise floor
2. Adjust attack and release for smooth or rhythmic gating
3. Use range to set how much the signal is reduced (not necessarily to silence)
4. Great for cleaning up noisy recordings
5. Use creatively with short releases for rhythmic chopping effects

---

### Drive
**Waveshaping Distortion**

A waveshaping distortion effect that adds harmonic content and saturation.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input

**Outputs:**
- `Out L` (Audio) - Left distorted output
- `Out R` (Audio) - Right distorted output

**Parameters:**
- `Drive` (0-100) - Amount of distortion
- `Type` (Choice) - Distortion algorithm (Soft clip, Hard clip, Foldback, etc.)
- `Output Gain` (-12 to +12 dB) - Output level compensation

**How to Use:**
1. Start with low drive and gradually increase
2. Try different distortion types for various tonal characters
3. Adjust output gain to compensate for level changes
4. Great for adding grit and harmonic richness
5. Use before or after filters for different tonal results

---

### Graphic EQ
**8-Band Graphic Equalizer**

An 8-band graphic equalizer with CV outputs for frequency-based triggering.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Band 1-8 Mod` (CV) - Individual band gain modulation
- `Gate Thresh Mod` (CV) - Gate threshold modulation
- `Trig Thresh Mod` (CV) - Trigger threshold modulation

**Outputs:**
- `Out L` (Audio) - Left EQ output
- `Out R` (Audio) - Right EQ output
- `Gate Out` (Gate) - Gates when signal exceeds gate threshold
- `Trig Out` (Gate) - Triggers on transients above trigger threshold

**Parameters:**
- `Gain Band 1-8` (-60 to +12 dB) - Gain for each frequency band (centered at: 60, 170, 310, 600, 1000, 3000, 6000, 12000 Hz)
- `Output Level` (-24 to +24 dB) - Overall output level
- `Gate Threshold` (-60 to 0 dB) - Threshold for gate output
- `Trigger Threshold` (-60 to 0 dB) - Threshold for trigger detection

**How to Use:**
1. Boost or cut specific frequency bands to shape your sound
2. Use negative gain to remove unwanted frequencies
3. Use the gate and trigger outputs for frequency-responsive triggering (great for kick/bass triggering)
4. Combine with other modules for frequency-dependent effects

---

### Waveshaper
**Multi-Algorithm Waveshaper**

A distortion effect with multiple waveshaping algorithms for varied saturation and distortion effects.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Drive Mod` (CV) - Drive amount modulation
- `Type Mod` (CV) - Algorithm selection modulation

**Outputs:**
- `Out L` (Audio) - Left waveshaped output
- `Out R` (Audio) - Right waveshaped output

**Parameters:**
- `Drive` (0-100) - Amount of waveshaping
- `Type` (Choice) - Waveshaping algorithm (Soft Clip, Hard Clip, Foldback, etc.)
- `Mix` (0-1) - Wet/dry balance

**How to Use:**
1. Choose a waveshaping algorithm
2. Gradually increase drive to add saturation
3. Try different algorithms for different characters
4. Use mix to blend with the dry signal

---

### 8-Band Shaper
**Multi-Band Waveshaper**

A multi-band waveshaper that applies frequency-specific distortion across 8 bands.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Drive 1-8 Mod` (CV) - Per-band drive modulation
- `Gain Mod` (CV) - Output gain modulation

**Outputs:**
- `Out L` (Audio) - Left processed output
- `Out R` (Audio) - Right processed output

**Parameters:**
- `Drive Band 1-8` (0-10) - Drive amount for each frequency band
- `Output Gain` (-24 to +24 dB) - Overall output level

**How to Use:**
1. Adjust individual band drives to add selective distortion
2. Drive bass frequencies differently than highs for balanced distortion
3. Great for adding harmonics to specific frequency ranges
4. Use sparingly for subtle enhancement or aggressively for heavy distortion

---

### Granulator
**Granular Synthesizer/Effect**

A granular processor that plays small grains of audio for textural and rhythmic effects.

**Inputs:**
- `In L` (Audio) - Left audio input (recorded to internal buffer)
- `In R` (Audio) - Right audio input (recorded to internal buffer)
- `Trigger In` (Gate) - Manual grain triggering
- `Density Mod` (CV) - Grain density modulation
- `Size Mod` (CV) - Grain size modulation
- `Position Mod` (CV) - Playback position modulation
- `Pitch Mod` (CV) - Pitch modulation
- `Gate Mod` (CV) - Gate amount modulation

**Outputs:**
- `Out L` (Audio) - Left granulated output
- `Out R` (Audio) - Right granulated output

**Parameters:**
- `Density` (0.1-100 Hz) - How often grains are triggered
- `Size` (5-500 ms) - Length of each grain
- `Position` (0-1) - Where in the buffer to read grains
- `Spread` (0-1) - Random variation in grain position
- `Pitch` (-24 to +24 semitones) - Pitch shift of grains
- `Pitch Random` (0-12 semitones) - Random pitch variation per grain
- `Pan Random` (0-1) - Random stereo placement per grain
- `Gate` (0-1) - Overall output level/gate

**How to Use:**
1. Audio is continuously recorded to a 2-second buffer
2. Adjust density for grain triggering rate (low=sparse, high=dense cloud)
3. Set grain size (small=rhythmic, large=smooth textures)
4. Use position to read from different parts of the buffer
5. Add spread for more random, evolving textures
6. Modulate position with LFOs for scanning effects
7. Great for creating ambient textures from any sound source

---

### Harmonic Shaper
**Harmonic Content Shaper**

Shapes the harmonic content of a signal using frequency-specific waveshaping.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Freq Mod` (CV) - Frequency modulation
- `Drive Mod` (CV) - Drive modulation

**Outputs:**
- `Out L` (Audio) - Left shaped output
- `Out R` (Audio) - Right shaped output

**Parameters:**
- `Master Frequency` (20 Hz - 20 kHz) - Center frequency for harmonic shaping
- `Master Drive` (0-10) - Amount of harmonic emphasis

**How to Use:**
1. Set the master frequency to target specific harmonics
2. Increase drive to emphasize those harmonics
3. Great for adding presence and character to sounds
4. Use on bass for sub-harmonic generation

---

### TimePitch
**Time/Pitch Manipulation**

Real-time pitch and time manipulation using the RubberBand library for high-quality time stretching and pitch shifting.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Speed Mod` (CV) - Playback speed modulation
- `Pitch Mod` (CV) - Pitch shift modulation

**Outputs:**
- `Out L` (Audio) - Left processed output
- `Out R` (Audio) - Right processed output

**Parameters:**
- `Speed` (0.25x to 4x) - Playback speed without affecting pitch
- `Pitch` (-24 to +24 semitones) - Pitch shift without affecting tempo
- `Formant` (Bool) - Preserve formants when pitch shifting

**How to Use:**
1. Adjust speed to time-stretch audio (0.5x=half speed, 2x=double speed)
2. Adjust pitch to transpose audio independently
3. Enable formant preservation for natural-sounding vocal pitch shifts
4. Great for creative effects and sound design

---

### De-Crackle
**Click/Pop Reducer**

A utility to reduce clicks and pops caused by discontinuous CV or audio signals.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input

**Outputs:**
- `Out L` (Audio) - Left de-clicked output
- `Out R` (Audio) - Right de-clicked output

**Parameters:**
- `Sensitivity` (Low/Medium/High) - How aggressively to detect and reduce clicks

**How to Use:**
1. Insert after modules that produce discontinuous signals
2. Adjust sensitivity based on the severity of clicks
3. Essential for smoothing abrupt parameter changes
4. Use on CV signals as well as audio

---

## 3. MODULATOR NODES

Modulator nodes generate control voltages for animating parameters over time.

### LFO
**Low-Frequency Oscillator**

A versatile LFO for modulating parameters with periodic waveforms.

**Inputs:**
- `Rate Mod` (CV) - Rate modulation input
- `Depth Mod` (CV) - Depth modulation input
- `Wave Mod` (CV) - Waveform selection modulation

**Outputs:**
- `Out` (CV) - CV modulation output

**Parameters:**
- `Rate` (0.05-20 Hz) - LFO frequency
- `Depth` (0-1) - Modulation amount
- `Bipolar` (Bool) - Output range: On = -1 to +1, Off = 0 to 1
- `Wave` (Choice) - Sine, Triangle, or Sawtooth
- `Sync` (Bool) - Sync to global tempo
- `Division` (Choice) - Note division when synced (1/32 to 8 bars)
- `Relative Mod` (Bool) - When enabled, rate CV is additive around slider position

**How to Use:**
1. Set rate to desired modulation speed
2. Choose bipolar for modulation around a center point, unipolar for one-directional
3. Select waveform based on desired modulation shape
4. Enable Sync and set Division for tempo-locked modulation
5. Connect output to any CV modulation input
6. Use multiple LFOs at different rates for complex modulation

---

### ADSR
**Attack-Decay-Sustain-Release Envelope Generator**

A classic ADSR envelope generator for shaping sounds over time.

**Inputs:**
- `Gate In` (Gate) - Gate signal to trigger and hold envelope
- `Trigger In` (Gate) - Trigger signal to retrigger envelope
- `Attack Mod` (CV) - Attack time modulation
- `Decay Mod` (CV) - Decay time modulation
- `Sustain Mod` (CV) - Sustain level modulation
- `Release Mod` (CV) - Release time modulation

**Outputs:**
- `Env Out` (CV) - Main envelope output (0-1)
- `Inv Out` (CV) - Inverted envelope output (1-0)
- `EOR Gate` (Gate) - End of Release gate
- `EOC Gate` (Gate) - End of Cycle gate

**Parameters:**
- `Attack` (0.001-5 seconds) - Rise time from 0 to 1
- `Decay` (0.001-5 seconds) - Fall time from 1 to sustain level
- `Sustain` (0-1) - Held level while gate is high
- `Release` (0.001-5 seconds) - Fall time from sustain to 0 after gate goes low
- `Relative Attack/Decay/Sustain/Release Mod` (Bool) - Enable relative modulation modes

**How to Use:**
1. Connect a gate source (sequencer, MIDI CV, etc.) to Gate In
2. Adjust Attack for how quickly sound reaches full volume
3. Set Decay for how quickly it falls to the sustain level
4. Sustain sets the held level while key/gate is pressed
5. Release controls fade-out time after gate is released
6. Connect Env Out to VCA gain for amplitude shaping
7. Use Inv Out for inverted modulation
8. EOR and EOC gates useful for triggering events at envelope completion

---

### Random
**Random Value Generator**

Generates random values at a specified rate with multiple output formats and tempo sync.

**Outputs:**
- `Norm Out` (CV) - Normalized random values (0-1 range)
- `Raw Out` (Raw) - Raw random values (custom range)
- `CV Out` (CV) - CV random values (custom CV range)
- `Bool Out` (Gate) - Random boolean (on/off)
- `Trig Out` (Gate) - Trigger pulse on each new random value

**Parameters:**
- `Rate` (0.1-50 Hz) - How often new random values are generated
- `Min` (-100 to 100) - Minimum value for Raw output
- `Max` (-100 to 100) - Maximum value for Raw output
- `CV Min` (0-1) - Minimum value for CV output
- `CV Max` (0-1) - Maximum value for CV output
- `Norm Min` (0-1) - Minimum value for Norm output
- `Norm Max` (0-1) - Maximum value for Norm output
- `Slew` (0-1) - Smoothing between random values
- `Trig Threshold` (0-1) - Threshold for Bool output
- `Sync` (Bool) - Sync to global tempo
- `Division` (Choice) - Note division when synced

**How to Use:**
1. Set rate for how often random values change
2. Adjust min/max ranges for each output type as needed
3. Use slew to smooth transitions between random values (0=stepped, 1=smooth)
4. Connect different outputs for different modulation needs
5. Use Bool Out for random gates/triggers
6. Enable sync for tempo-locked randomness
7. Great for adding unpredictability and variation to patches

---

### S&H
**Sample & Hold**

Samples and holds an input signal when triggered.

**Inputs:**
- `Signal In L` (Audio) - Left signal to sample
- `Signal In R` (Audio) - Right signal to sample
- `Trig In L` (Gate) - Trigger for left channel
- `Trig In R` (Gate) - Trigger for right channel
- `Threshold Mod` (CV) - Trigger threshold modulation
- `Edge Mod` (CV) - Trigger edge selection modulation
- `Slew Mod` (CV) - Slew limiting modulation

**Outputs:**
- `Out L` (Audio) - Left sampled & held output
- `Out R` (Audio) - Right sampled & held output

**Parameters:**
- `Threshold` (0-1) - Trigger threshold level
- `Edge` (Choice) - Rising, Falling, or Both edges
- `Slew` (0-1) - Slew limiting between sampled values

**How to Use:**
1. Connect a signal to sample (CV, audio, etc.)
2. Connect a trigger source (LFO, clock, gate)
3. Each trigger samples the current input value and holds it
4. Adjust threshold if using audio-rate triggers
5. Use slew to smooth transitions between held values
6. Classic for creating stepped random modulation (LFO → S&H → destination)

---

### Function Generator
**Drawable Envelope/LFO Generator**

A complex, drawable envelope and LFO generator with multiple curve slots and extensive modulation options.

**Inputs:**
- `Gate In` (Gate) - Gate input for envelope triggering
- `Trigger In` (Gate) - Trigger input for envelope
- `Sync In` (Gate) - Sync input for phase reset
- `Rate Mod` (CV) - Rate modulation
- `Slew Mod` (CV) - Slew limiting modulation
- `Gate Thresh Mod` (CV) - Gate threshold modulation
- `Trig Thresh Mod` (CV) - Trigger threshold modulation
- `Pitch Base Mod` (CV) - Pitch base modulation
- `Value Mult Mod` (CV) - Value multiplier modulation
- `Curve Select Mod` (CV) - Curve selection modulation

**Outputs:**
- `Value` (CV) - Main output value
- `Inverted` (CV) - Inverted output
- `Bipolar` (CV) - Bipolar output (-1 to +1)
- `Pitch` (CV) - Pitch CV output (V/Oct)
- `Gate` (Gate) - Gate output based on threshold
- `Trigger` (Gate) - Trigger output
- `End of Cycle` (Gate) - Trigger at cycle end
- `Blue/Red/Green Value` (CV) - Per-curve outputs
- `Blue/Red/Green Pitch` (CV) - Per-curve pitch outputs

**Parameters:**
- `Rate` (0.05-20 Hz) - Cycle speed
- `Slew` (0-1) - Smoothing between points
- `Gate Threshold` (0-1) - Threshold for gate output
- `Trig Threshold` (0-1) - Threshold for trigger output
- `Pitch Base` (-4 to +4 octaves) - Base pitch offset
- `Value Mult` (0-10) - Value scaling multiplier
- `Curve Select` (0-2) - Choose active curve (Blue, Red, or Green)
- Drawing Interface - Click and drag to draw curves

**How to Use:**
1. Click "Draw" to enter drawing mode
2. Draw up to 3 different curves (Blue, Red, Green tabs)
3. Set rate for cycle speed
4. Use as LFO (free-running) or envelope (gate-triggered)
5. Adjust slew for smooth or stepped transitions
6. Multiple outputs allow simultaneous different modulations
7. Per-curve outputs let you route different curves to different destinations
8. Extremely versatile for complex modulation shapes

---

### Shaping Oscillator
**Oscillator with Built-in Waveshaper**

An oscillator with integrated waveshaping for generating harmonically rich tones.

**Inputs:**
- `In L` (Audio) - External audio input (optional, can shape external audio)
- `In R` (Audio) - External audio input right channel
- `Freq Mod` (CV) - Frequency modulation
- `Wave Mod` (CV) - Waveform modulation
- `Drive Mod` (CV) - Drive modulation

**Outputs:**
- `Out` (Audio) - Shaped oscillator output

**Parameters:**
- `Frequency` (20 Hz - 20 kHz) - Oscillator frequency
- `Waveform` (Choice) - Base waveform
- `Drive` (0-10) - Waveshaping amount

**How to Use:**
1. Set frequency for desired pitch
2. Choose base waveform
3. Increase drive to add harmonics via waveshaping
4. Can also process external audio through the shaper
5. Great for thick, harmonically rich tones

---

## 4. UTILITY & LOGIC NODES

Utility nodes provide essential signal processing, routing, and logic operations.

### VCA
**Voltage-Controlled Amplifier**

A basic amp module for controlling audio levels with CV.

**Inputs:**
- `In L` (Audio) - Left audio input
- `In R` (Audio) - Right audio input
- `Gain Mod` (CV) - Gain modulation

**Outputs:**
- `Out L` (Audio) - Left amplified output
- `Out R` (Audio) - Right amplified output

**Parameters:**
- `Gain` (-60 to +12 dB) - Base gain level

**How to Use:**
1. Send audio through the VCA
2. Connect an envelope or LFO to Gain Mod for amplitude control
3. Essential for creating dynamic amplitude envelopes
4. The core of any subtractive synthesis voice

---

### Mixer
**Stereo Audio Mixer**

A two-input stereo mixer with gain, pan, and crossfade controls.

**Inputs:**
- `In A L/R` (Audio) - Input A stereo pair
- `In B L/R` (Audio) - Input B stereo pair
- `Gain Mod` (CV) - Gain modulation
- `Pan Mod` (CV) - Pan modulation
- `X-Fade Mod` (CV) - Crossfade modulation

**Outputs:**
- `Out L/R` (Audio) - Mixed stereo output

**Parameters:**
- `Gain` (-60 to +12 dB) - Overall output gain
- `Pan` (-1 to +1) - Stereo panning
- `Crossfade` (0-1) - Blend between input A (0) and input B (1)

**How to Use:**
1. Connect two stereo sources
2. Use crossfade to blend between them
3. Adjust gain and pan for final mix
4. Modulate crossfade with LFOs or envelopes for dynamic mixing

---

### CV Mixer
**Control Voltage Mixer**

A mixer specifically designed for mixing CV signals.

**Inputs:**
- `In A/B` (CV) - CV inputs
- `Gain Mod` (CV) - Gain modulation

**Outputs:**
- `Out` (CV) - Mixed CV output

**Parameters:**
- `Gain A/B` (-2 to +2) - Gain for each input

**How to Use:**
1. Mix multiple CV sources together
2. Use negative gain to invert signals
3. Create complex modulation by combining LFOs and envelopes
4. Essential for additive CV processing

---

### Track Mixer
**Multi-Channel Mixer**

A mixer for up to 8 monophonic tracks with individual gain and pan controls.

**Inputs:**
- `In 1-8` (Audio) - 8 mono audio inputs
- `Num Tracks Mod` (Raw) - Number of active tracks modulation
- `Gain 1-8 Mod` (CV) - Per-track gain modulation
- `Pan 1-8 Mod` (CV) - Per-track pan modulation

**Outputs:**
- `Out L/R` (Audio) - Stereo mixed output

**Parameters:**
- `Num Tracks` (1-8) - Number of active tracks
- `Gain 1-8` (-60 to +12 dB) - Per-track gain
- `Pan 1-8` (-1 to +1) - Per-track stereo panning

**How to Use:**
1. Connect multiple mono sources (great with PolyVCO outputs)
2. Adjust per-track gain and pan
3. Set Num Tracks to control how many inputs are active
4. Perfect for mixing polyphonic voices

---

### Attenuverter
**Attenuate/Invert Signal**

Attenuates (reduces) and/or inverts CV or audio signals.

**Inputs:**
- `In L/R` (Audio) - Stereo inputs
- `Amount Mod` (CV) - Amount modulation

**Outputs:**
- `Out L/R` (Audio) - Processed outputs

**Parameters:**
- `Amount` (-1 to +1) - Attenuation/inversion amount (0=silent, 0.5=half, 1=full, negative=inverted)

**How to Use:**
1. Use positive values (0-1) to reduce signal levels
2. Use negative values (-1-0) to invert and reduce
3. Set to 0 for silence
4. Essential for scaling modulation amounts
5. Use to create inverted versions of CV for opposite modulation

---

### Lag Processor
**Slew Limiter/Smoother**

Smooths abrupt changes in signals using independent rise and fall times.

**Inputs:**
- `Signal In` (CV) - CV input to smooth
- `Rise Mod` (CV) - Rise time modulation
- `Fall Mod` (CV) - Fall time modulation

**Outputs:**
- `Smoothed Out` (CV) - Smoothed CV output

**Parameters:**
- `Rise Time` (0.1-1000 ms) - Time to reach rising values
- `Fall Time` (0.1-1000 ms) - Time to reach falling values

**How to Use:**
1. Insert between a CV source and destination to smooth transitions
2. Use equal rise/fall times for symmetrical smoothing
3. Use different rise/fall for attack/release character
4. Great for portamento effects and smoothing stepped sequences
5. Can turn hard gates into smooth envelopes

---

### Math
**Mathematical Operations**

Performs mathematical operations on two input signals.

**Inputs:**
- `In A` (CV) - First operand
- `In B` (CV) - Second operand

**Outputs:**
- `Add` (CV) - A + B
- `Subtract` (CV) - A - B
- `Multiply` (CV) - A × B
- `Divide` (CV) - A ÷ B

**Parameters:**
- `Value A` (-100 to 100) - Default value for A (used if not patched)
- `Value B` (-100 to 100) - Default value for B (used if not patched)
- `Operation` (Choice) - Add, Subtract, Multiply, Divide, Min, Max, Power, Sqrt(A), Sin(A), Cos(A), Tan(A), Abs(A), Modulo, Fract(A), Int(A), A>B, A<B

**How to Use:**
1. Connect CV sources or use internal values
2. Choose operation
3. Use outputs for complex CV processing
4. Create custom modulation shapes by combining operations
5. Use comparison operations (A>B, A<B) for logic

---

### MapRange
**Value Range Mapper**

Remaps values from one range to another.

**Inputs:**
- `Raw In` (Raw) - Input value to remap

**Outputs:**
- `CV Out` (CV) - Remapped to 0-1 range
- `Audio Out` (Audio) - Remapped to audio-rate

**Parameters:**
- `Min In` (-1000 to 1000) - Input range minimum
- `Max In` (-1000 to 1000) - Input range maximum
- `Min Out` (-1000 to 1000) - Output range minimum
- `Max Out` (-1000 to 1000) - Output range maximum

**How to Use:**
1. Define your input range (min/max in)
2. Define your desired output range (min/max out)
3. Connect input signal
4. Output is linearly scaled to new range
5. Great for converting between different parameter ranges

---

### Quantizer
**Musical Scale Quantizer**

Snaps continuous CV to musical scales.

**Inputs:**
- `CV In` (CV) - Continuous pitch CV
- `Scale Mod` (CV) - Scale selection modulation
- `Root Mod` (CV) - Root note modulation

**Outputs:**
- `Out` (CV) - Quantized pitch CV

**Parameters:**
- `Scale` (Choice) - Musical scale (Major, Minor, Chromatic, Pentatonic, etc.)
- `Root` (Choice) - Root note (C, C#, D, etc.)

**How to Use:**
1. Connect a continuous CV source (LFO, random, etc.)
2. Choose a musical scale
3. Set the root note
4. Output will snap to nearest note in the scale
5. Great for creating melodic sequences from random sources

---

### Rate
**Rate Value Converter**

Converts raw values to normalized rate values for tempo-related modulation.

**Inputs:**
- `Rate Mod` (CV) - Rate modulation input

**Outputs:**
- `Out` (CV) - Normalized rate output

**Parameters:**
- `Rate` (0.1-20) - Rate multiplier

**How to Use:**
1. Use to convert between different rate representations
2. Useful for tempo-syncing external modulators
3. Provides standardized rate output for consistent timing

---

### Comparator
**Threshold Comparator**

Outputs a gate signal when input exceeds a threshold.

**Inputs:**
- `In` (CV) - CV input to compare

**Outputs:**
- `Out` (Gate) - Gate output (high when input > threshold)

**Parameters:**
- `Threshold` (0-1) - Comparison threshold

**How to Use:**
1. Connect a CV source
2. Set threshold
3. Output goes high when input exceeds threshold
4. Great for converting CV to gates
5. Create rhythm from slow LFOs

---

### Logic
**Boolean Logic Operations**

Performs boolean logic operations on gate signals.

**Inputs:**
- `In A` (Gate) - First input
- `In B` (Gate) - Second input

**Outputs:**
- `AND` (Gate) - High when both inputs are high
- `OR` (Gate) - High when either input is high
- `XOR` (Gate) - High when inputs differ
- `NOT A` (Gate) - Inverted A

**How to Use:**
1. Connect two gate sources
2. Use outputs for different logic combinations
3. AND: Both gates must be high (good for requiring multiple conditions)
4. OR: Either gate can be high (good for combining triggers)
5. XOR: Only one gate high (good for alternating patterns)
6. NOT A: Invert a gate signal

---

### Clock Divider
**Clock Division/Multiplication**

Divides and multiplies clock signals for polyrhythmic patterns.

**Inputs:**
- `Clock In` (Gate) - Clock input to divide/multiply
- `Reset` (Gate) - Reset all divisions to sync

**Outputs:**
- `/2, /4, /8` (Gate) - Divided clocks (half, quarter, eighth speed)
- `x2, x3, x4` (Gate) - Multiplied clocks (double, triple, quadruple speed)

**How to Use:**
1. Connect a clock source
2. Use divided outputs for slower rhythms
3. Use multiplied outputs for faster rhythms
4. Create polyrhythmic patterns by using multiple outputs
5. Use reset to synchronize all divisions

---

### Sequential Switch
**Signal Router**

Routes an input signal to one of four outputs based on CV thresholds.

**Inputs:**
- `Gate In` (Audio) - Signal to route
- `Thresh 1-4 CV` (CV) - Threshold values for each output

**Outputs:**
- `Out 1-4` (Audio) - Four possible output destinations

**Parameters:**
- `Threshold 1-4` (0-1) - Threshold levels

**How to Use:**
1. Connect a signal to Gate In
2. Set thresholds for each output
3. As input CV changes, signal routes to different outputs
4. Use with sequencers or LFOs for rhythmic switching
5. Great for creating evolving patterns

---

## 5. SEQUENCER NODES

Sequencer nodes generate rhythmic and melodic patterns.

### Sequencer
**16-Step CV/Gate Sequencer**

A classic 16-step sequencer for creating melodies and rhythms.

**Inputs:**
- Extensive per-step modulation inputs for values, triggers, and gates

**Outputs:**
- `Pitch` (CV) - Current step pitch value
- `Gate` (Gate) - Gate output
- `Gate Nuanced` (CV) - Gate with velocity
- `Velocity` (CV) - Velocity value
- `Mod` (CV) - Modulation output
- `Trigger` (Gate) - Trigger on each step

**Parameters:**
- `Rate` (0.1-20 Hz) - Sequence speed
- `Num Steps` (1-16) - Number of active steps
- `Gate Length` (0-1) - Duration of gates
- Per-step: Pitch, Gate, Velocity, Modulation values

**How to Use:**
1. Set number of steps and rate
2. Program pitch values for each step
3. Set gates on/off for rhythm
4. Adjust gate length for articulation
5. Can sync to Tempo Clock for musical timing

---

### Multi Sequencer
**Advanced Multi-Output Sequencer**

An advanced sequencer with parallel per-step outputs for polyphonic sequencing.

**Outputs:**
- Live outputs: Pitch, Gate, Trigger, Velocity, Mod
- Parallel outputs: Pitch 1-16, Gate 1-16, Trig 1-16 (all steps output simultaneously)

**How to Use:**
1. Similar to Sequencer but with simultaneous output of all 16 steps
2. Connect parallel outputs to PolyVCO for poly synth
3. Create complex polyphonic arrangements
4. Each step can trigger independently

---

### Tempo Clock
**Global Clock Generator**

Master tempo/clock source with transport controls.

**Inputs:**
- `BPM Mod` (CV) - BPM modulation
- `Tap` (Gate) - Tap tempo input
- `Nudge+/-` (Gate) - Fine tempo adjustment
- `Play/Stop/Reset` (Gate) - Transport controls
- `Swing Mod` (CV) - Swing amount modulation

**Outputs:**
- `Clock` (Gate) - Main clock pulse
- `Beat Trig` (Gate) - Trigger on each beat
- `Bar Trig` (Gate) - Trigger on each bar
- `Beat Gate` (Gate) - Gate for beat duration
- `Phase` (CV) - Clock phase (0-1)
- `BPM CV` (CV) - BPM as CV
- `Downbeat` (Gate) - First beat of bar

**Parameters:**
- `BPM` (20-300) - Tempo in beats per minute
- `Time Signature` (Choice) - 4/4, 3/4, 6/8, etc.
- `Swing` (0-100%) - Swing amount
- `Global Division` (Bool) - Override all synced modules' divisions

**How to Use:**
1. Set BPM for your project
2. Use clock outputs to drive sequencers and LFOs
3. Enable Global Division to control all synced modules at once
4. Use transport controls for performance

---

### Snapshot Sequencer
**Patch State Sequencer**

Sequences complete patch states, recalling all parameter values.

**How to Use:**
1. Create snapshots of your entire patch at different states
2. Sequence through snapshots for dramatic changes
3. Great for live performance and automation

---

### Stroke Sequencer
**Gesture-Based Sequencer**

Records and plays back drawn gestures as CV sequences.

**How to Use:**
1. Draw patterns with your mouse/tablet
2. Playback converts drawing to CV
3. Unique way to create expressive, human-feeling sequences

---

## 6. MIDI NODES

MIDI nodes handle MIDI input/output and conversion to CV.

### MIDI CV
**MIDI to CV Converter**

Converts incoming MIDI notes to CV/Gate signals (monophonic).

**Outputs:**
- `Pitch` (CV) - Note pitch as CV (V/Oct)
- `Gate` (Gate) - Note on/off gate
- `Velocity` (CV) - Note velocity
- `Mod Wheel` (CV) - CC1 modulation wheel
- `Pitch Bend` (CV) - Pitch bend wheel
- `Aftertouch` (CV) - Channel aftertouch

**How to Use:**
1. Connect MIDI controller or use virtual MIDI
2. Play notes → outputs CV/Gate
3. Use with VCO + VCA + ADSR for classic synth voice
4. Monophonic (last note priority)

---

### MIDI Player
**MIDI File Player**

Plays MIDI files with per-track CV/Gate outputs.

**How to Use:**
1. Load a MIDI file
2. Outputs CV/Gate for each MIDI track
3. Great for backing tracks or complex sequences

---

### MIDI Faders
**MIDI-Learnable Faders (1-16)**

1-16 MIDI-learnable faders with customizable output ranges.

**Outputs:**
- `Fader 1-16` (CV) - CV outputs (0-1 range)

**Parameters:**
- MIDI Learn for each fader
- Min/Max output range per fader

**How to Use:**
1. Click MIDI Learn
2. Move a fader on your controller
3. Fader is now linked
4. Adjust output ranges as needed

---

### MIDI Knobs
**MIDI-Learnable Knobs (1-16)**

Similar to MIDI Faders but optimized for rotary controls.

---

### MIDI Buttons
**MIDI-Learnable Buttons (1-32)**

1-32 MIDI-learnable buttons with Gate/Toggle/Trigger modes.

**Outputs:**
- `Button 1-32` (Gate) - Gate/trigger outputs

**Modes:**
- Gate: High while pressed
- Toggle: Alternates on/off each press
- Trigger: Brief pulse on press

---

### MIDI Jog Wheel
**MIDI Jog Wheel Control**

A single MIDI-learnable jog wheel for expressive modulation.

**Output:**
- `Value` (CV) - Wheel position/velocity

---

## 7. ANALYSIS NODES

Analysis nodes visualize and inspect signals.

### Scope
**Oscilloscope**

Visualizes audio or CV signals over time.

**Inputs/Outputs:**
- `In/Out` (Audio) - Pass-through with visualization

**Parameters:**
- `Window Size` (0.5-20 seconds) - Time window to display
- `Trigger Mode` (Choice) - Free-run, Rising Edge, Falling Edge
- `Trigger Level` (0-1) - Trigger threshold

**How to Use:**
1. Insert in signal path (pass-through)
2. Adjust window size to see desired time range
3. Use trigger modes for stable waveform display
4. Great for debugging and sound design

---

### Debug
**Signal Value Logger**

Logs signal value changes to the console.

**How to Use:**
1. Insert in CV path
2. Logs values to console when they change
3. Great for troubleshooting CV routing
4. No audio output (endpoint)

---

### Input Debug
**Passthrough Debug Logger**

Like Debug but with pass-through output.

---

### Frequency Graph
**Spectrum Analyzer**

High-resolution real-time spectrum analyzer with frequency-based gate outputs.

**Inputs:**
- `In` (Audio) - Mono audio to analyze

**Outputs:**
- `Out L/R` (Audio) - Stereo pass-through
- `Sub/Bass/Mid/High Gate` (Gate) - Per-band gate outputs
- `Sub/Bass/Mid/High Trig` (Gate) - Per-band trigger outputs

**Parameters:**
- `Gate Threshold` per band - Threshold for gate outputs

**How to Use:**
1. Send audio through for visualization
2. Displays frequency spectrum in real-time
3. Use gate/trigger outputs for frequency-reactive triggering
4. Great for kick drum detection, bass triggering, etc.

---

## 8. SPECIAL NODES

Special nodes provide unique functionality beyond traditional synthesis.

### TTS Performer
**Text-to-Speech Engine**

Advanced text-to-speech with word-level sequencing.

**Inputs:**
- Per-word trigger inputs (1-16)
- Rate, Gate, Speed, Pitch modulation

**Outputs:**
- `Audio` - Speech audio output
- `Word Gate` - Gate while speaking
- `EOP Gate` - End of phrase gate
- Per-word gates and triggers (1-16)

**Parameters:**
- Text input field
- Voice selection
- Rate, pitch, speed controls

**How to Use:**
1. Type text into text field
2. Choose voice
3. Trigger individual words or play entire phrase
4. Use per-word gates for word-synced events
5. Modulate pitch/speed for effects

---

### Vocal Tract Filter
**Formant Filter**

Simulates human vowel sounds through formant filtering.

**Parameters:**
- `Vowel Shape` - Continuous blend between vowels (A, E, I, O, U)
- `Formant Shift` - Shift formant frequencies up/down
- `Instability` - Add human-like variation
- `Gain` - Formant emphasis

**How to Use:**
1. Send audio through (great with sawtooth waves)
2. Adjust vowel shape to morph between vowels
3. Modulate with LFOs for talking/singing effects
4. Use with TTS Performer for enhanced vocal synthesis

---

### Physics
**2D Physics Simulation**

A 2D physics engine that outputs collision and contact data as CV.

**How to Use:**
1. Create physics objects in the UI
2. Set gravity, friction, elasticity
3. Objects collide and interact
4. Outputs include position, velocity, collision events
5. Use outputs to drive synthesis parameters
6. Experimental and creative

---

### Animation
**3D Animation Player**

Loads and plays 3D animations, outputs joint positions and velocities.

**How to Use:**
1. Load 3D animation file (FBX, etc.)
2. Play animation
3. Outputs joint positions as CV
4. Drive synthesis from motion capture data

---

## 9. COMPUTER VISION NODES

Computer vision nodes process video for audio/CV generation.

### Webcam Loader
**Webcam Video Source**

Captures video from webcam and publishes as a source for vision processing.

**Output:**
- `Source ID` (Raw) - Video source identifier

---

### Video File Loader
**Video File Source**

Loads and plays video files for vision processing.

---

### Movement Detector
**Motion Detection**

Analyzes video for motion via optical flow or background subtraction.

**Inputs:**
- `Source In` (Raw) - Video source ID

**Outputs:**
- `Motion X/Y` (CV) - Motion vector
- `Amount` (CV) - Total motion amount
- `Trigger` (Gate) - Trigger on significant motion

**How to Use:**
1. Connect webcam or video file loader
2. Outputs motion as CV
3. Use for interactive installations
4. Motion triggers synthesis events

---

### Human Detector
**Face/Body Detection**

Detects faces or bodies in video via Haar Cascades or HOG.

**Outputs:**
- `X, Y, Width, Height` (CV) - Detection bounding box
- `Gate` (Gate) - High when person detected

**How to Use:**
1. Connect video source
2. Detects faces/bodies
3. Outputs position and size as CV
4. Use for interactive performances

---

### Pose Estimator
**Body Keypoint Detection**

Uses OpenPose MPI model to detect 15 human body keypoints in real-time video, providing precise tracking of head, limbs, and torso positions.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `Head X/Y` (CV) - Head position
- `Neck X/Y` (CV) - Neck position
- `R Shoulder X/Y` (CV) - Right shoulder position
- `R Elbow X/Y` (CV) - Right elbow position
- `R Wrist X/Y` (CV) - Right wrist position
- `L Shoulder X/Y` (CV) - Left shoulder position
- `L Elbow X/Y` (CV) - Left elbow position
- `L Wrist X/Y` (CV) - Left wrist position
- `R Hip X/Y` (CV) - Right hip position
- `R Knee X/Y` (CV) - Right knee position
- `R Ankle X/Y` (CV) - Right ankle position
- `L Hip X/Y` (CV) - Left hip position
- `L Knee X/Y` (CV) - Left knee position
- `L Ankle X/Y` (CV) - Left ankle position
- `Chest X/Y` (CV) - Chest/torso center position

**Parameters:**
- `Confidence` (0.0-1.0) - Detection confidence threshold (default: 0.1). Lower values detect more keypoints but may include false positives
- `Draw Skeleton` (Bool) - Toggle skeleton overlay on video preview
- `Zoom` (+/-) - Toggle between normal (480px) and zoomed (960px) video preview

**How to Use:**
1. **Setup:** Download OpenPose MPI model files (see `guides/POSE_ESTIMATOR_SETUP.md`)
2. **Connect Video Source:** Connect a Webcam Loader or Video File Loader's `Source ID` output to `Source In`
3. **Adjust Confidence:** Lower threshold for more sensitive detection, higher for more reliable detection
4. **Map Keypoints:** Connect individual keypoint X/Y outputs to any CV modulation input
5. **Example Patches:**
   - **Hand-Controlled Oscillator:** Connect `R Wrist X` → VCO Frequency, `R Wrist Y` → VCF Cutoff
   - **Body-Driven Rhythm:** Connect `R Knee Y` → Sequencer Rate, `L Knee Y` → Gate threshold
   - **Dance Performance:** Map multiple keypoints to different parameters for full-body control
6. **Performance Tips:**
   - Good lighting improves detection accuracy
   - Stand 1-3 meters from camera for best results
   - Keep full body in frame for all keypoints to be detected
   - Simple backgrounds work best

**Technical Details:**
- Uses OpenPose MPI (faster) model with 15 keypoints
- Runs at ~15 FPS on CPU (computationally intensive)
- Outputs normalized coordinates (0-1 range)
- Real-time safe: Processing runs on separate thread, lock-free FIFO to audio thread
- Video preview shows skeleton overlay when enabled

**Creative Applications:**
- **Interactive Installations:** Create body-responsive soundscapes
- **Live Performance:** Control synthesis with gestures and movement
- **Accessibility:** Hands-free instrument control for performers with mobility constraints
- **Dance + Music:** Choreography-driven composition
- **Fitness Apps:** Exercise-triggered sound design
- **Game Controllers:** Full-body game audio integration

**Requirements:**
- OpenPose MPI model files (~200 MB download)
- Webcam or video file source
- OpenCV with DNN module (included in build)

---

### Hand Tracker
**Hand Keypoint Detection**

Uses OpenPose hand model to detect 21 hand keypoints in real-time video, providing precise finger and wrist tracking.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `Wrist X/Y` (CV) - Wrist position
- `Thumb 1-4 X/Y` (CV) - Thumb joint positions (4 points)
- `Index 1-4 X/Y` (CV) - Index finger joint positions (4 points)
- `Middle 1-4 X/Y` (CV) - Middle finger joint positions (4 points)
- `Ring 1-4 X/Y` (CV) - Ring finger joint positions (4 points)
- `Pinky 1-4 X/Y` (CV) - Pinky finger joint positions (4 points)

**Parameters:**
- `Confidence` (0.0-1.0) - Detection confidence threshold (default: 0.1)
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Setup:** Requires OpenPose hand model files in `assets/openpose_models/hand/`
2. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
3. **Adjust Confidence:** Lower values for sensitive detection, higher for reliable detection
4. **Map Gestures:** Connect finger joint positions to synthesis parameters
5. **Example Patches:**
   - **Gesture Control:** Map thumb position to filter cutoff, index position to VCO frequency
   - **Finger Tracking:** Use individual finger tips for multi-parameter control
   - **Hand Size:** Use wrist to fingertip distances for amplitude or volume control
6. **Performance Tips:**
   - Works best with hands clearly visible against contrasting background
   - Keep hands ~30-80cm from camera
   - Good lighting improves detection accuracy

**Technical Details:**
- Uses OpenPose hand detection model
- Detects 21 keypoints per hand
- Runs at ~15 FPS on CPU
- Outputs normalized coordinates (0-1 range)

---

### Face Tracker
**Facial Landmark Detection**

Uses OpenPose face model to detect 70 facial landmarks, providing detailed face and expression tracking.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `Pt 1-70 X/Y` (CV) - 70 facial landmark positions (eyes, nose, mouth, face outline)

**Parameters:**
- `Confidence` (0.0-1.0) - Detection confidence threshold (default: 0.1)
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Setup:** Requires OpenPose face model files in `assets/openpose_models/face/`
2. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
3. **Adjust Confidence:** Lower values for sensitive detection, higher for reliable detection
4. **Map Landmarks:** Connect facial landmark positions to synthesis parameters
5. **Example Patches:**
   - **Expression Control:** Map mouth width to effect parameters, eyebrow position to filter resonance
   - **Head Tracking:** Use face center for spatial panning
   - **Lip Sync:** Use mouth landmarks for vocoder or formant filtering
6. **Performance Tips:**
   - Face-front camera position works best
   - Keep face well-lit
   - Maintain 50-150cm distance from camera

**Technical Details:**
- Uses Haar Cascade for face detection, OpenPose DNN for landmark estimation
- Detects 70 landmarks per face
- Runs at ~15 FPS on CPU
- Outputs normalized coordinates (0-1 range)

---

### Object Detector
**YOLOv3 Object Detection**

Uses YOLOv3 deep learning model to detect objects from 80 COCO classes (person, car, bottle, etc.) in real-time video.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `X` (CV) - Center X position of detected object (normalized 0-1)
- `Y` (CV) - Center Y position of detected object (normalized 0-1)
- `Width` (CV) - Object width (normalized 0-1)
- `Height` (CV) - Object height (normalized 0-1)
- `Gate` (Gate) - High when object detected

**Parameters:**
- `Target Class` (Choice) - Object class to detect (person, car, bicycle, etc.)
- `Confidence` (0.0-1.0) - Detection confidence threshold (default: 0.5)
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Setup:** Requires YOLOv3 model files (`yolov3.cfg`, `yolov3.weights`, `coco.names`) in `assets/`
2. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
3. **Select Target Class:** Choose which object type to detect
4. **Adjust Confidence:** Lower for more detections (may include false positives), higher for reliable detection
5. **Map Coordinates:** Connect X/Y/Width/Height outputs to synthesis parameters
6. **Example Patches:**
   - **Person Tracking:** Use person bounding box to trigger events when person enters/exits frame
   - **Object Size Control:** Use Width × Height to control effect amount
   - **Position-Based Effects:** Map center X to panning, center Y to filter cutoff
7. **Performance Tips:**
   - YOLO is computationally intensive (~10 FPS on CPU)
   - Good lighting and contrast improve detection
   - Larger objects are detected more reliably

**Technical Details:**
- Uses YOLOv3 (You Only Look Once v3) detection model
- 80 COCO object classes supported
- Runs at ~10 FPS on CPU
- Outputs normalized bounding box coordinates
- Falls back to YOLOv3-tiny if standard model not available

---

### Color Tracker
**Multi-Color HSV Tracking**

Tracks multiple custom colors in video using HSV color space, outputting position and area for each tracked color.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- Dynamic outputs per tracked color:
  - `[Color Name] X` (CV) - Center X position (normalized 0-1)
  - `[Color Name] Y` (CV) - Center Y position (normalized 0-1)
  - `[Color Name] Area` (CV) - Area covered (normalized 0-1)

**Parameters:**
- `Add Color...` (Button) - Click to pick a color from the video preview
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
2. **Pick Colors:** Click "Add Color..." and click on the video preview to sample a color
3. **Track Multiple:** Add up to 8 different colors to track simultaneously
4. **Remove Colors:** Click "Remove" button next to each tracked color
5. **Map Coordinates:** Connect individual color outputs to synthesis parameters
6. **Example Patches:**
   - **Two-Object Control:** Track two colored objects for stereo panning or dual oscillator control
   - **Area-Based Effects:** Use Area output to control effect wet/dry mix
   - **Position Automation:** Map color X/Y to sequencer position or filter sweeps
7. **Performance Tips:**
   - Works best with saturated, distinct colors
   - Avoid overlapping colors in similar hues
   - Good lighting maintains consistent HSV values

**Technical Details:**
- Uses HSV color space for robust color detection
- Tracks up to 8 colors simultaneously
- Automatic morphological cleanup for noise reduction
- Runs at ~30 FPS on CPU
- Outputs normalized coordinates and area

---

### Contour Detector
**Shape Detection via Background Subtraction**

Detects shapes and their properties using background subtraction and contour analysis.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `Area` (CV) - Detected shape area (normalized 0-1)
- `Complexity` (CV) - Shape complexity based on polygon approximation (0-1)
- `Aspect Ratio` (CV) - Width/height ratio of bounding box

**Parameters:**
- `Threshold` (0-255) - Threshold for foreground/background separation (default: 128)
- `Noise Reduction` (Bool) - Enable morphological filtering to reduce noise (default: On)
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
2. **Adjust Threshold:** Set threshold to separate foreground from background
3. **Enable Noise Reduction:** Reduce detection of small, noisy artifacts
4. **Map Shape Properties:** Connect Area, Complexity, and Aspect Ratio to synthesis parameters
5. **Example Patches:**
   - **Size-Based Filtering:** Use Area to control low-pass filter cutoff
   - **Shape Recognition:** Use Complexity to detect simple vs complex shapes
   - **Orientation Control:** Use Aspect Ratio to determine if object is horizontal or vertical
6. **Performance Tips:**
   - Requires relatively static background for best results
   - Good contrast between foreground and background
   - Use noise reduction for clean signal

**Technical Details:**
- Uses MOG2 background subtraction
- Polygon approximation for complexity calculation
- Runs at ~25 FPS on CPU
- Outputs normalized shape properties

---

### Semantic Segmentation
**Scene Segmentation via Deep Learning**

Uses semantic segmentation networks (ENet or DeepLabV3) to identify and track objects by semantic class in video.

**Inputs:**
- `Source In` (Raw) - Video source ID from webcam or video file loader

**Outputs:**
- `Area` (CV) - Percentage of frame covered by target class (normalized 0-1)
- `Center X` (CV) - Center X position of detected region (normalized 0-1)
- `Center Y` (CV) - Center Y position of detected region (normalized 0-1)
- `Gate` (Gate) - High when target class detected

**Parameters:**
- `Target Class` (Choice) - Semantic class to detect (person, road, car, etc.)
- `Zoom` (+/-) - Adjust preview size: Small (240px), Normal (480px), Large (960px)

**How to Use:**
1. **Setup:** Requires ENet or DeepLabV3 model files in `assets/`
2. **Connect Video Source:** Connect Webcam or Video File Loader's `Source ID` to `Source In`
3. **Select Target Class:** Choose which semantic class to track
4. **Map Region Properties:** Connect Area and Center outputs to synthesis parameters
5. **Example Patches:**
   - **Presence Detection:** Use Gate output to trigger events when person enters frame
   - **Coverage-Based Effects:** Use Area to control reverb size or delay feedback
   - **Center Tracking:** Use Center X/Y for spatial effects
6. **Performance Tips:**
   - Computationally intensive (~10 FPS on CPU)
   - Best results with scenes that match training data (Cityscapes, etc.)
   - Works well for large, distinct regions

**Technical Details:**
- Uses ENet (Efficient Neural Network) or DeepLabV3 segmentation models
- Supports Cityscapes dataset classes by default
- Runs at ~10 FPS on CPU
- Outputs normalized region properties with colored preview overlay

---

## 11. SYSTEM NODES

System nodes provide special functionality for patch organization.

### Meta
**Meta Module Container**

A container for creating custom reusable modules from sub-patches.

**How to Use:**
1. Create a patch inside the Meta module
2. Use Inlet/Outlet nodes to define interface
3. Save as reusable module
4. Collapse complex patches into single nodes

---

### Inlet
**Meta Module Input**

Defines an input for a Meta module.

---

### Outlet
**Meta Module Output**

Defines an output for a Meta module.

---

### Comment
**Documentation Node**

A text comment node for documenting patches.

**How to Use:**
1. Add comment node
2. Type documentation text
3. Helps explain complex patches
4. No audio/CV functionality

---

### Recorder
**Audio Recording to File**

Records incoming audio to WAV, AIFF, or FLAC files.

**Inputs:**
- `In L/R` (Audio) - Stereo audio to record

**Parameters:**
- File path/name
- Format (WAV, AIFF, FLAC)
- Bit depth (16/24/32)
- Record button

**How to Use:**
1. Set file path and format
2. Connect audio source
3. Click Record to start
4. Click Stop to finish and save

---

### VST Host
**VST Plugin Host**

Hosts VST2/VST3 plugins within the modular environment.

**How to Use:**
1. Load VST plugin
2. Audio routed through plugin
3. Use external effects and instruments
4. Combine modular with traditional plugins

---

## Glossary

**CV (Control Voltage):** A signal (typically 0-1 or -1 to +1) used to modulate parameters.

**Gate:** A binary signal (high/low, on/off) used for triggering and timing.

**Trigger:** A brief pulse signal, typically used to initiate events.

**Audio:** Full-rate audio signals (~44.1kHz or higher).

**Raw:** Unscaled numerical values for custom ranges.

**V/Oct:** Volt-per-octave pitch CV standard (1 semitone = 1/12 V).

**Relative Modulation:** CV modulates around a slider position (musical/proportional).

**Absolute Modulation:** CV directly maps to full parameter range.

**Bipolar:** Signal range from -1 to +1 (centered at 0).

**Unipolar:** Signal range from 0 to 1.

---

## Tips for Using the Dictionary

1. **Search by Function:** Use Ctrl+F to find nodes by keyword (e.g., "distortion", "filter", "envelope")
2. **Follow the Signal Flow:** Start with Sources → Effects → Output
3. **Modulation is Key:** Most parameters can be modulated - experiment!
4. **Save Your Patches:** Use the preset system to save and recall configurations
5. **Start Simple:** Build complexity gradually by adding one module at a time

---

**End of Nodes Dictionary**

*For more information, see the main user manual and individual module guides.*


