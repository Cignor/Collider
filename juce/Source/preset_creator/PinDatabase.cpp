#include "PinDatabase.h"

// Module Descriptions - populated on first use
void populateModuleDescriptions()
{
    auto& descriptions = getModuleDescriptions();
    if (!descriptions.empty()) return; // Only run once
    
    // Sources
    descriptions["audio input"] = "Brings hardware audio into the patch.";
    descriptions["VCO"] = "A standard Voltage-Controlled Oscillator.";
    descriptions["polyvco"] = "A multi-voice oscillator bank for polyphony.";
    descriptions["Noise"] = "Generates white, pink, or brown noise.";
    descriptions["Sequencer"] = "A classic 16-step CV and Gate sequencer.";
    descriptions["multi sequencer"] = "Advanced sequencer with parallel per-step outputs.";
    descriptions["midi player"] = "Plays MIDI files and outputs CV/Gate for each track.";
    descriptions["Value"] = "Outputs a constant, adjustable numerical value.";
    descriptions["sample loader"] = "Loads and plays audio samples with pitch/time control.";
    descriptions["best practice"] = "A template and example node demonstrating best practices.";
    // TTS Family
    descriptions["TTS Performer"] = "Advanced Text-to-Speech engine with word-level sequencing.";
    descriptions["Vocal Tract Filter"] = "A formant filter that simulates human vowel sounds.";
    // Effects
    descriptions["VCF"] = "A Voltage-Controlled Filter (LP, HP, BP).";
    descriptions["Delay"] = "A stereo delay effect with modulation.";
    descriptions["Reverb"] = "A stereo reverb effect.";
    descriptions["chorus"] = "A stereo chorus effect.";
    descriptions["phaser"] = "A stereo phaser effect.";
    descriptions["compressor"] = "Reduces the dynamic range of a signal.";
    descriptions["limiter"] = "Prevents a signal from exceeding a set level.";
    descriptions["gate"] = "A stereo noise gate to silence signals below a threshold.";
    descriptions["drive"] = "A waveshaping distortion effect.";
    descriptions["graphic eq"] = "An 8-band graphic equalizer.";
    descriptions["Frequency Graph"] = "A high-resolution, real-time spectrum analyzer.";
    descriptions["Waveshaper"] = "A distortion effect with multiple shaping algorithms.";
    descriptions["8bandshaper"] = "A multi-band waveshaper for frequency-specific distortion.";
    descriptions["Granulator"] = "A granular synthesizer/effect that plays small grains of a sample.";
    descriptions["harmonic shaper"] = "Shapes the harmonic content of a signal.";
    descriptions["timepitch"] = "Real-time pitch and time manipulation using RubberBand.";
    descriptions["De-Crackle"] = "A utility to reduce clicks from discontinuous signals.";
    descriptions["recorder"] = "Records incoming audio to a WAV, AIFF, or FLAC file.";
    // Modulators
    descriptions["LFO"] = "A Low-Frequency Oscillator for modulation.";
    descriptions["ADSR"] = "An Attack-Decay-Sustain-Release envelope generator.";
    descriptions["Random"] = "A random value generator with internal sample & hold.";
    descriptions["S&H"] = "A classic Sample and Hold module.";
    descriptions["Function Generator"] = "A complex, drawable envelope/LFO generator.";
    descriptions["shaping oscillator"] = "An oscillator with a built-in waveshaper.";
    // Utilities & Logic
    descriptions["VCA"] = "A Voltage-Controlled Amplifier to control signal level.";
    descriptions["Mixer"] = "A stereo audio mixer with crossfading and panning.";
    descriptions["cv mixer"] = "A mixer specifically for control voltage signals.";
    descriptions["trackmixer"] = "A multi-channel mixer for polyphonic sources.";
    descriptions["Attenuverter"] = "Attenuates (reduces) and/or inverts signals.";
    descriptions["Lag Processor"] = "Smooths out abrupt changes in a signal (slew limiter).";
    descriptions["Math"] = "Performs mathematical operations on signals.";
    descriptions["MapRange"] = "Remaps a signal from one numerical range to another.";
    descriptions["Quantizer"] = "Snaps a continuous signal to a musical scale.";
    descriptions["Rate"] = "Converts a control signal into a normalized rate value.";
    descriptions["Comparator"] = "Outputs a high signal if an input is above a threshold.";
    descriptions["Logic"] = "Performs boolean logic (AND, OR, XOR, NOT) on gate signals.";
    descriptions["ClockDivider"] = "Divides and multiplies clock signals.";
    descriptions["SequentialSwitch"] = "A signal router with multiple thresholds.";
    // Analysis
    descriptions["Scope"] = "Visualizes an audio or CV signal.";
    descriptions["debug"] = "A tool for logging signal value changes.";
    descriptions["input debug"] = "A passthrough version of the Debug node for inspecting signals on a cable.";
}

void populatePinDatabase()
{
    // Populate both databases
    populateModuleDescriptions();
    
    auto& db = getModulePinDatabase();
    if (!db.empty()) return; // Only run once

    // --- Sources ---
    db["audio input"] = ModulePinInfo(
        {},
        { AudioPin("Out 1", 0, PinDataType::Audio), AudioPin("Out 2", 1, PinDataType::Audio),
          AudioPin("Gate", 16, PinDataType::Gate), AudioPin("Trigger", 17, PinDataType::Gate), AudioPin("EOP", 18, PinDataType::Gate) },
        {}
    );
    db["vco"] = ModulePinInfo(
        { AudioPin("Frequency", 0, PinDataType::CV), AudioPin("Waveform", 1, PinDataType::CV), AudioPin("Gate", 2, PinDataType::Gate) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    db["noise"] = ModulePinInfo(
        { AudioPin("Level Mod", 0, PinDataType::CV), AudioPin("Colour Mod", 1, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) }, // Stereo output to match actual implementation
        {}
    );
    db["value"] = ModulePinInfo(
        {},
        { AudioPin("Raw", 0, PinDataType::Raw), AudioPin("Normalized", 1, PinDataType::CV), AudioPin("Inverted", 2, PinDataType::Raw),
          AudioPin("Integer", 3, PinDataType::Raw), AudioPin("CV Out", 4, PinDataType::CV) },
        {}
    );
    db["sample loader"] = ModulePinInfo(
        { AudioPin("Pitch Mod", 0, PinDataType::CV), AudioPin("Speed Mod", 1, PinDataType::CV), AudioPin("Gate Mod", 2, PinDataType::CV),
          AudioPin("Trigger Mod", 3, PinDataType::Gate), AudioPin("Range Start Mod", 4, PinDataType::CV), AudioPin("Range End Mod", 5, PinDataType::CV),
          AudioPin("Randomize Trig", 6, PinDataType::Gate) },
        { AudioPin("Audio Output", 0, PinDataType::Audio) },
        {}
    );

    // --- Effects ---
    db["vcf"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Cutoff Mod", 2, PinDataType::CV),
          AudioPin("Resonance Mod", 3, PinDataType::CV), AudioPin("Type Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["delay"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Time Mod", 2, PinDataType::CV),
          AudioPin("Feedback Mod", 3, PinDataType::CV), AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["reverb"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Size Mod", 2, PinDataType::CV),
          AudioPin("Damp Mod", 3, PinDataType::CV), AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["compressor"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Thresh Mod", 2, PinDataType::CV),
          AudioPin("Ratio Mod", 3, PinDataType::CV), AudioPin("Attack Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV),
          AudioPin("Makeup Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );

    // --- Modulators ---
    db["lfo"] = ModulePinInfo(
        { AudioPin("Rate Mod", 0, PinDataType::CV), AudioPin("Depth Mod", 1, PinDataType::CV), AudioPin("Wave Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );
    db["adsr"] = ModulePinInfo(
        { AudioPin("Gate In", 0, PinDataType::Gate), AudioPin("Trigger In", 1, PinDataType::Gate), AudioPin("Attack Mod", 2, PinDataType::CV),
          AudioPin("Decay Mod", 3, PinDataType::CV), AudioPin("Sustain Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV) },
        { AudioPin("Env Out", 0, PinDataType::CV), AudioPin("Inv Out", 1, PinDataType::CV), AudioPin("EOR Gate", 2, PinDataType::Gate),
          AudioPin("EOC Gate", 3, PinDataType::Gate) },
        {}
    );
    db["random"] = ModulePinInfo(
        {}, // No inputs - self-contained random generator
        { AudioPin("Norm Out", 0, PinDataType::CV), AudioPin("Raw Out", 1, PinDataType::Raw), AudioPin("CV Out", 2, PinDataType::CV),
          AudioPin("Bool Out", 3, PinDataType::Gate), AudioPin("Trig Out", 4, PinDataType::Gate) },
        {}
    );

    // --- Utilities ---
    db["vca"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Gain Mod", 2, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["mixer"] = ModulePinInfo(
        { AudioPin("In A L", 0, PinDataType::Audio), AudioPin("In A R", 1, PinDataType::Audio), AudioPin("In B L", 2, PinDataType::Audio),
          AudioPin("In B R", 3, PinDataType::Audio), AudioPin("Gain Mod", 4, PinDataType::CV), AudioPin("Pan Mod", 5, PinDataType::CV),
          AudioPin("X-Fade Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["scope"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    db["graphic eq"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Band 1 Mod", 2, PinDataType::CV), AudioPin("Band 2 Mod", 3, PinDataType::CV),
          AudioPin("Band 3 Mod", 4, PinDataType::CV), AudioPin("Band 4 Mod", 5, PinDataType::CV),
          AudioPin("Band 5 Mod", 6, PinDataType::CV), AudioPin("Band 6 Mod", 7, PinDataType::CV),
          AudioPin("Band 7 Mod", 8, PinDataType::CV), AudioPin("Band 8 Mod", 9, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["frequency graph"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) }, // Mono Audio Input
        { // Outputs: Stereo audio pass-through + 8 Gate/Trigger outputs
            AudioPin("Out L", 0, PinDataType::Audio),
            AudioPin("Out R", 1, PinDataType::Audio),
            AudioPin("Sub Gate", 2, PinDataType::Gate),
            AudioPin("Sub Trig", 3, PinDataType::Gate),
            AudioPin("Bass Gate", 4, PinDataType::Gate),
            AudioPin("Bass Trig", 5, PinDataType::Gate),
            AudioPin("Mid Gate", 6, PinDataType::Gate),
            AudioPin("Mid Trig", 7, PinDataType::Gate),
            AudioPin("High Gate", 8, PinDataType::Gate),
            AudioPin("High Trig", 9, PinDataType::Gate)
        },
        {} // No modulation inputs
    );
    db["chorus"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Depth Mod", 3, PinDataType::CV),
          AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["phaser"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Depth Mod", 3, PinDataType::CV),
          AudioPin("Centre Mod", 4, PinDataType::CV), AudioPin("Feedback Mod", 5, PinDataType::CV),
          AudioPin("Mix Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["compressor"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Thresh Mod", 2, PinDataType::CV), AudioPin("Ratio Mod", 3, PinDataType::CV),
          AudioPin("Attack Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV),
          AudioPin("Makeup Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["Recorder"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        {}, // No outputs
        {}
    );
    db["limiter"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Thresh Mod", 2, PinDataType::CV), AudioPin("Release Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["gate"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["drive"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["time/pitch"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Speed Mod", 2, PinDataType::CV), AudioPin("Pitch Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["waveshaper"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Drive Mod", 2, PinDataType::CV), AudioPin("Type Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["8bandshaper"] = ModulePinInfo(
        {
            AudioPin("In L", 0, PinDataType::Audio),
            AudioPin("In R", 1, PinDataType::Audio),
            AudioPin("Drive 1 Mod", 2, PinDataType::CV),
            AudioPin("Drive 2 Mod", 3, PinDataType::CV),
            AudioPin("Drive 3 Mod", 4, PinDataType::CV),
            AudioPin("Drive 4 Mod", 5, PinDataType::CV),
            AudioPin("Drive 5 Mod", 6, PinDataType::CV),
            AudioPin("Drive 6 Mod", 7, PinDataType::CV),
            AudioPin("Drive 7 Mod", 8, PinDataType::CV),
            AudioPin("Drive 8 Mod", 9, PinDataType::CV),
            AudioPin("Gain Mod", 10, PinDataType::CV)
        },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["granulator"] = ModulePinInfo(
        {
            AudioPin("In L", 0, PinDataType::Audio),
            AudioPin("In R", 1, PinDataType::Audio),
            AudioPin("Trigger In", 2, PinDataType::Gate),
            AudioPin("Density Mod", 3, PinDataType::CV),
            AudioPin("Size Mod", 4, PinDataType::CV),
            AudioPin("Position Mod", 5, PinDataType::CV),
            AudioPin("Pitch Mod", 6, PinDataType::CV),
            AudioPin("Gate Mod", 7, PinDataType::CV)
        },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["mixer"] = ModulePinInfo(
        { AudioPin("In A L", 0, PinDataType::Audio), AudioPin("In A R", 1, PinDataType::Audio), AudioPin("In B L", 2, PinDataType::Audio), AudioPin("In B R", 3, PinDataType::Audio), AudioPin("Gain Mod", 4, PinDataType::CV), AudioPin("Pan Mod", 5, PinDataType::CV), AudioPin("X-Fade Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    db["sequencer"] = ModulePinInfo(
        { AudioPin("Mod In L", 0, PinDataType::Audio), AudioPin("Mod In R", 1, PinDataType::Audio), AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Gate Mod", 3, PinDataType::CV), AudioPin("Steps Mod", 4, PinDataType::CV), AudioPin("Gate Thr Mod", 5, PinDataType::CV),
          // Per-step value mods absolute 6..21 (Step1..Step16)
          AudioPin("Step 1 Mod", 6, PinDataType::CV), AudioPin("Step 2 Mod", 7, PinDataType::CV), AudioPin("Step 3 Mod", 8, PinDataType::CV), AudioPin("Step 4 Mod", 9, PinDataType::CV),
          AudioPin("Step 5 Mod", 10, PinDataType::CV), AudioPin("Step 6 Mod", 11, PinDataType::CV), AudioPin("Step 7 Mod", 12, PinDataType::CV), AudioPin("Step 8 Mod", 13, PinDataType::CV),
          AudioPin("Step 9 Mod", 14, PinDataType::CV), AudioPin("Step 10 Mod", 15, PinDataType::CV), AudioPin("Step 11 Mod", 16, PinDataType::CV), AudioPin("Step 12 Mod", 17, PinDataType::CV),
          AudioPin("Step 13 Mod", 18, PinDataType::CV), AudioPin("Step 14 Mod", 19, PinDataType::CV), AudioPin("Step 15 Mod", 20, PinDataType::CV), AudioPin("Step 16 Mod", 21, PinDataType::CV),
          // Per-step trig mods absolute 22..37 (Step1..Step16) â€” these are Gates
          AudioPin("Step 1 Trig Mod", 22, PinDataType::Gate), AudioPin("Step 2 Trig Mod", 23, PinDataType::Gate), AudioPin("Step 3 Trig Mod", 24, PinDataType::Gate), AudioPin("Step 4 Trig Mod", 25, PinDataType::Gate),
          AudioPin("Step 5 Trig Mod", 26, PinDataType::Gate), AudioPin("Step 6 Trig Mod", 27, PinDataType::Gate), AudioPin("Step 7 Trig Mod", 28, PinDataType::Gate), AudioPin("Step 8 Trig Mod", 29, PinDataType::Gate),
          AudioPin("Step 9 Trig Mod", 30, PinDataType::Gate), AudioPin("Step 10 Trig Mod", 31, PinDataType::Gate), AudioPin("Step 11 Trig Mod", 32, PinDataType::Gate), AudioPin("Step 12 Trig Mod", 33, PinDataType::Gate),
          AudioPin("Step 13 Trig Mod", 34, PinDataType::Gate), AudioPin("Step 14 Trig Mod", 35, PinDataType::Gate), AudioPin("Step 15 Trig Mod", 36, PinDataType::Gate), AudioPin("Step 16 Trig Mod", 37, PinDataType::Gate),
          // Per-step gate level mods absolute 38..53
          AudioPin("Step 1 Gate Mod", 38, PinDataType::CV), AudioPin("Step 2 Gate Mod", 39, PinDataType::CV), AudioPin("Step 3 Gate Mod", 40, PinDataType::CV), AudioPin("Step 4 Gate Mod", 41, PinDataType::CV),
          AudioPin("Step 5 Gate Mod", 42, PinDataType::CV), AudioPin("Step 6 Gate Mod", 43, PinDataType::CV), AudioPin("Step 7 Gate Mod", 44, PinDataType::CV), AudioPin("Step 8 Gate Mod", 45, PinDataType::CV),
          AudioPin("Step 9 Gate Mod", 46, PinDataType::CV), AudioPin("Step 10 Gate Mod", 47, PinDataType::CV), AudioPin("Step 11 Gate Mod", 48, PinDataType::CV), AudioPin("Step 12 Gate Mod", 49, PinDataType::CV),
          AudioPin("Step 13 Gate Mod", 50, PinDataType::CV), AudioPin("Step 14 Gate Mod", 51, PinDataType::CV), AudioPin("Step 15 Gate Mod", 52, PinDataType::CV), AudioPin("Step 16 Gate Mod", 53, PinDataType::CV) },
        { AudioPin("Pitch", 0, PinDataType::CV), AudioPin("Gate", 1, PinDataType::Gate), AudioPin("Gate Nuanced", 2, PinDataType::CV), AudioPin("Velocity", 3, PinDataType::CV), AudioPin("Mod", 4, PinDataType::CV), AudioPin("Trigger", 5, PinDataType::Gate) },
        {}
    );

    db["value"] = ModulePinInfo(
        {},
        { AudioPin("Raw", 0, PinDataType::Raw), AudioPin("Normalized", 1, PinDataType::CV), AudioPin("Inverted", 2, PinDataType::Raw), AudioPin("Integer", 3, PinDataType::Raw), AudioPin("CV Out", 4, PinDataType::CV) },
        {}
    );
// in populatePinDatabase()

// in populatePinDatabase()

// in populatePinDatabase()

db["random"] = ModulePinInfo(
    {}, // No inputs - self-contained random generator
    { 
        AudioPin("Norm Out", 0, PinDataType::CV), 
        AudioPin("Raw Out", 1, PinDataType::Raw), 
        AudioPin("CV Out", 2, PinDataType::CV),
        AudioPin("Bool Out", 3, PinDataType::Gate), 
        AudioPin("Trig Out", 4, PinDataType::Gate) 
    },
    {} // No modulation inputs
);

    db["tts performer"] = ModulePinInfo(
        { // Inputs (absolute channels based on bus structure)
            AudioPin("Rate Mod", 0, PinDataType::CV),
            AudioPin("Gate Mod", 1, PinDataType::CV),
            AudioPin("Trigger", 2, PinDataType::Gate),
            AudioPin("Reset", 3, PinDataType::Gate),
            AudioPin("Randomize Trig", 4, PinDataType::Gate),
            AudioPin("Trim Start Mod", 5, PinDataType::CV),
            AudioPin("Trim End Mod", 6, PinDataType::CV),
            AudioPin("Speed Mod", 7, PinDataType::CV),
            AudioPin("Pitch Mod", 8, PinDataType::CV),
            // Word Triggers (Channels 9-24)
            AudioPin("Word 1 Trig", 9, PinDataType::Gate), AudioPin("Word 2 Trig", 10, PinDataType::Gate),
            AudioPin("Word 3 Trig", 11, PinDataType::Gate), AudioPin("Word 4 Trig", 12, PinDataType::Gate),
            AudioPin("Word 5 Trig", 13, PinDataType::Gate), AudioPin("Word 6 Trig", 14, PinDataType::Gate),
            AudioPin("Word 7 Trig", 15, PinDataType::Gate), AudioPin("Word 8 Trig", 16, PinDataType::Gate),
            AudioPin("Word 9 Trig", 17, PinDataType::Gate), AudioPin("Word 10 Trig", 18, PinDataType::Gate),
            AudioPin("Word 11 Trig", 19, PinDataType::Gate), AudioPin("Word 12 Trig", 20, PinDataType::Gate),
            AudioPin("Word 13 Trig", 21, PinDataType::Gate), AudioPin("Word 14 Trig", 22, PinDataType::Gate),
            AudioPin("Word 15 Trig", 23, PinDataType::Gate), AudioPin("Word 16 Trig", 24, PinDataType::Gate)
        },
        { // Outputs
            AudioPin("Audio", 0, PinDataType::Audio),
            AudioPin("Word Gate", 1, PinDataType::Gate),
            AudioPin("EOP Gate", 2, PinDataType::Gate),
            // Per-Word Gates (Channels 3-18)
            AudioPin("Word 1 Gate", 3, PinDataType::Gate), AudioPin("Word 2 Gate", 4, PinDataType::Gate),
            AudioPin("Word 3 Gate", 5, PinDataType::Gate), AudioPin("Word 4 Gate", 6, PinDataType::Gate),
            AudioPin("Word 5 Gate", 7, PinDataType::Gate), AudioPin("Word 6 Gate", 8, PinDataType::Gate),
            AudioPin("Word 7 Gate", 9, PinDataType::Gate), AudioPin("Word 8 Gate", 10, PinDataType::Gate),
            AudioPin("Word 9 Gate", 11, PinDataType::Gate), AudioPin("Word 10 Gate", 12, PinDataType::Gate),
            AudioPin("Word 11 Gate", 13, PinDataType::Gate), AudioPin("Word 12 Gate", 14, PinDataType::Gate),
            AudioPin("Word 13 Gate", 15, PinDataType::Gate), AudioPin("Word 14 Gate", 16, PinDataType::Gate),
            AudioPin("Word 15 Gate", 17, PinDataType::Gate), AudioPin("Word 16 Gate", 18, PinDataType::Gate),
            // Per-Word Triggers (Channels 19-34)
            AudioPin("Word 1 Trig", 19, PinDataType::Gate), AudioPin("Word 2 Trig", 20, PinDataType::Gate),
            AudioPin("Word 3 Trig", 21, PinDataType::Gate), AudioPin("Word 4 Trig", 22, PinDataType::Gate),
            AudioPin("Word 5 Trig", 23, PinDataType::Gate), AudioPin("Word 6 Trig", 24, PinDataType::Gate),
            AudioPin("Word 7 Trig", 25, PinDataType::Gate), AudioPin("Word 8 Trig", 26, PinDataType::Gate),
            AudioPin("Word 9 Trig", 27, PinDataType::Gate), AudioPin("Word 10 Trig", 28, PinDataType::Gate),
            AudioPin("Word 11 Trig", 29, PinDataType::Gate), AudioPin("Word 12 Trig", 30, PinDataType::Gate),
            AudioPin("Word 13 Trig", 31, PinDataType::Gate), AudioPin("Word 14 Trig", 32, PinDataType::Gate),
            AudioPin("Word 15 Trig", 33, PinDataType::Gate), AudioPin("Word 16 Trig", 34, PinDataType::Gate)
        },
        { // Modulation Pins (for UI parameter disabling)
            ModPin("Rate", "rate_mod", PinDataType::CV),
            ModPin("Gate", "gate_mod", PinDataType::CV),
            ModPin("Trim Start", "trimStart_mod", PinDataType::CV),
            ModPin("Trim End", "trimEnd_mod", PinDataType::CV),
            ModPin("Speed", "speed_mod", PinDataType::CV),
            ModPin("Pitch", "pitch_mod", PinDataType::CV)
        }
    );
    db["vocal tract filter"] = ModulePinInfo(
        { AudioPin("Audio In", 0, PinDataType::Audio) },
        { AudioPin("Audio Out", 0, PinDataType::Audio) },
        { ModPin("Vowel", "vowelShape", PinDataType::CV), ModPin("Formant", "formantShift", PinDataType::CV), ModPin("Instability", "instability", PinDataType::CV), ModPin("Gain", "formantGain", PinDataType::CV) }
    );
    db["best practice"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Wave Mod", 3, PinDataType::CV), AudioPin("Drive Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Frequency", "frequency_mod", PinDataType::CV), ModPin("Waveform", "waveform_mod", PinDataType::CV), ModPin("Drive", "drive_mod", PinDataType::CV) }
    );
    db["shaping oscillator"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Wave Mod", 3, PinDataType::CV), AudioPin("Drive Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Frequency", "frequency_mod", PinDataType::CV), ModPin("Waveform", "waveform_mod", PinDataType::CV), ModPin("Drive", "drive_mod", PinDataType::CV) }
    );
    db["harmonic shaper"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Drive Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Master Frequency", "masterFrequency_mod", PinDataType::CV), ModPin("Master Drive", "masterDrive_mod", PinDataType::CV) }
    );
    db["function generator"] = ModulePinInfo(
        { 
            AudioPin("Gate In", 0, PinDataType::Gate),
            AudioPin("Trigger In", 1, PinDataType::Gate),
            AudioPin("Sync In", 2, PinDataType::Gate),
            AudioPin("Rate Mod", 3, PinDataType::CV),
            AudioPin("Slew Mod", 4, PinDataType::CV),
            AudioPin("Gate Thresh Mod", 5, PinDataType::CV),
            AudioPin("Trig Thresh Mod", 6, PinDataType::CV),
            AudioPin("Pitch Base Mod", 7, PinDataType::CV),
            AudioPin("Value Mult Mod", 8, PinDataType::CV),
            AudioPin("Curve Select Mod", 9, PinDataType::CV)
        },
        { 
            AudioPin("Value", 0, PinDataType::CV),
            AudioPin("Inverted", 1, PinDataType::CV),
            AudioPin("Bipolar", 2, PinDataType::CV),
            AudioPin("Pitch", 3, PinDataType::CV),
            AudioPin("Gate", 4, PinDataType::Gate),
            AudioPin("Trigger", 5, PinDataType::Gate),
            AudioPin("End of Cycle", 6, PinDataType::Gate),
            // New dedicated outputs
            AudioPin("Blue Value", 7, PinDataType::CV),
            AudioPin("Blue Pitch", 8, PinDataType::CV),
            AudioPin("Red Value", 9, PinDataType::CV),
            AudioPin("Red Pitch", 10, PinDataType::CV),
            AudioPin("Green Value", 11, PinDataType::CV),
            AudioPin("Green Pitch", 12, PinDataType::CV)
        },
        { 
            ModPin("Rate", "rate_mod", PinDataType::CV),
            ModPin("Slew", "slew_mod", PinDataType::CV),
            ModPin("Gate Thresh", "gateThresh_mod", PinDataType::CV),
            ModPin("Trig Thresh", "trigThresh_mod", PinDataType::CV),
            ModPin("Pitch Base", "pitchBase_mod", PinDataType::CV),
            ModPin("Value Mult", "valueMult_mod", PinDataType::CV),
            ModPin("Curve Select", "curveSelect_mod", PinDataType::CV)
        }
    );

    db["multi sequencer"] = ModulePinInfo(
        { // Inputs: Mod In L, Mod In R, Rate Mod, Gate Mod, Steps Mod, Gate Thr Mod, plus per-step mods and triggers
            AudioPin("Mod In L", 0, PinDataType::Audio), AudioPin("Mod In R", 1, PinDataType::Audio),
            AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Gate Mod", 3, PinDataType::CV),
            AudioPin("Steps Mod", 4, PinDataType::CV), AudioPin("Gate Thr Mod", 5, PinDataType::CV),
            // Per-step mods (channels 6-21)
            AudioPin("Step 1 Mod", 6, PinDataType::CV), AudioPin("Step 2 Mod", 7, PinDataType::CV),
            AudioPin("Step 3 Mod", 8, PinDataType::CV), AudioPin("Step 4 Mod", 9, PinDataType::CV),
            AudioPin("Step 5 Mod", 10, PinDataType::CV), AudioPin("Step 6 Mod", 11, PinDataType::CV),
            AudioPin("Step 7 Mod", 12, PinDataType::CV), AudioPin("Step 8 Mod", 13, PinDataType::CV),
            AudioPin("Step 9 Mod", 14, PinDataType::CV), AudioPin("Step 10 Mod", 15, PinDataType::CV),
            AudioPin("Step 11 Mod", 16, PinDataType::CV), AudioPin("Step 12 Mod", 17, PinDataType::CV),
            AudioPin("Step 13 Mod", 18, PinDataType::CV), AudioPin("Step 14 Mod", 19, PinDataType::CV),
            AudioPin("Step 15 Mod", 20, PinDataType::CV), AudioPin("Step 16 Mod", 21, PinDataType::CV),
            // Per-step trigger mods (channels 22-37)
            AudioPin("Step 1 Trig Mod", 22, PinDataType::Gate), AudioPin("Step 2 Trig Mod", 23, PinDataType::Gate),
            AudioPin("Step 3 Trig Mod", 24, PinDataType::Gate), AudioPin("Step 4 Trig Mod", 25, PinDataType::Gate),
            AudioPin("Step 5 Trig Mod", 26, PinDataType::Gate), AudioPin("Step 6 Trig Mod", 27, PinDataType::Gate),
            AudioPin("Step 7 Trig Mod", 28, PinDataType::Gate), AudioPin("Step 8 Trig Mod", 29, PinDataType::Gate),
            AudioPin("Step 9 Trig Mod", 30, PinDataType::Gate), AudioPin("Step 10 Trig Mod", 31, PinDataType::Gate),
            AudioPin("Step 11 Trig Mod", 32, PinDataType::Gate), AudioPin("Step 12 Trig Mod", 33, PinDataType::Gate),
            AudioPin("Step 13 Trig Mod", 34, PinDataType::Gate), AudioPin("Step 14 Trig Mod", 35, PinDataType::Gate),
            AudioPin("Step 15 Trig Mod", 36, PinDataType::Gate), AudioPin("Step 16 Trig Mod", 37, PinDataType::Gate)
        },
        { // Outputs: Live outputs (0-6) + Parallel step outputs (7+)
            // Live Outputs
            AudioPin("Pitch", 0, PinDataType::CV), AudioPin("Gate", 1, PinDataType::Gate),
            AudioPin("Gate Nuanced", 2, PinDataType::CV), AudioPin("Velocity", 3, PinDataType::CV),
            AudioPin("Mod", 4, PinDataType::CV), AudioPin("Trigger", 5, PinDataType::Gate),
            AudioPin("Num Steps", 6, PinDataType::Raw),
            // Parallel Step Outputs (Corrected Names and Channels, shifted by +1 after Num Steps)
            AudioPin("Pitch 1", 7, PinDataType::CV), AudioPin("Gate 1", 8, PinDataType::Gate), AudioPin("Trig 1", 9, PinDataType::Gate),
            AudioPin("Pitch 2", 10, PinDataType::CV), AudioPin("Gate 2", 11, PinDataType::Gate), AudioPin("Trig 2", 12, PinDataType::Gate),
            AudioPin("Pitch 3", 13, PinDataType::CV), AudioPin("Gate 3", 14, PinDataType::Gate), AudioPin("Trig 3", 15, PinDataType::Gate),
            AudioPin("Pitch 4", 16, PinDataType::CV), AudioPin("Gate 4", 17, PinDataType::Gate), AudioPin("Trig 4", 18, PinDataType::Gate),
            AudioPin("Pitch 5", 19, PinDataType::CV), AudioPin("Gate 5", 20, PinDataType::Gate), AudioPin("Trig 5", 21, PinDataType::Gate),
            AudioPin("Pitch 6", 22, PinDataType::CV), AudioPin("Gate 6", 23, PinDataType::Gate), AudioPin("Trig 6", 24, PinDataType::Gate),
            AudioPin("Pitch 7", 25, PinDataType::CV), AudioPin("Gate 7", 26, PinDataType::Gate), AudioPin("Trig 7", 27, PinDataType::Gate),
            AudioPin("Pitch 8", 28, PinDataType::CV), AudioPin("Gate 8", 29, PinDataType::Gate), AudioPin("Trig 8", 30, PinDataType::Gate),
            AudioPin("Pitch 9", 31, PinDataType::CV), AudioPin("Gate 9", 32, PinDataType::Gate), AudioPin("Trig 9", 33, PinDataType::Gate),
            AudioPin("Pitch 10", 34, PinDataType::CV), AudioPin("Gate 10", 35, PinDataType::Gate), AudioPin("Trig 10", 36, PinDataType::Gate),
            AudioPin("Pitch 11", 37, PinDataType::CV), AudioPin("Gate 11", 38, PinDataType::Gate), AudioPin("Trig 11", 39, PinDataType::Gate),
            AudioPin("Pitch 12", 40, PinDataType::CV), AudioPin("Gate 12", 41, PinDataType::Gate), AudioPin("Trig 12", 42, PinDataType::Gate),
            AudioPin("Pitch 13", 43, PinDataType::CV), AudioPin("Gate 13", 44, PinDataType::Gate), AudioPin("Trig 13", 45, PinDataType::Gate),
            AudioPin("Pitch 14", 46, PinDataType::CV), AudioPin("Gate 14", 47, PinDataType::Gate), AudioPin("Trig 14", 48, PinDataType::Gate),
            AudioPin("Pitch 15", 49, PinDataType::CV), AudioPin("Gate 15", 50, PinDataType::Gate), AudioPin("Trig 15", 51, PinDataType::Gate),
            AudioPin("Pitch 16", 52, PinDataType::CV), AudioPin("Gate 16", 53, PinDataType::Gate), AudioPin("Trig 16", 54, PinDataType::Gate)
        },
        {}
    );
    db["comparator"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::Gate) },
        {}
    );

    db["sample loader"] = ModulePinInfo(
        {
            AudioPin("Pitch Mod", 0, PinDataType::CV),
            AudioPin("Speed Mod", 1, PinDataType::CV),
            AudioPin("Gate Mod", 2, PinDataType::CV),
            AudioPin("Trigger Mod", 3, PinDataType::Gate),
            AudioPin("Range Start Mod", 4, PinDataType::CV),
            AudioPin("Range End Mod", 5, PinDataType::CV)
        },
        {
            AudioPin("Audio Output", 0, PinDataType::Audio)
        },
        {}
    );
    
    // Track Mixer - first 8 tracks UI definition (mono per track + gain/pan CV) and a Tracks Mod pin
    db["track mixer"] = ModulePinInfo(
        {
            // Mono audio inputs for first 8 tracks (absolute channels 0..7)
            AudioPin("In 1", 0, PinDataType::Audio),
            AudioPin("In 2", 1, PinDataType::Audio),
            AudioPin("In 3", 2, PinDataType::Audio),
            AudioPin("In 4", 3, PinDataType::Audio),
            AudioPin("In 5", 4, PinDataType::Audio),
            AudioPin("In 6", 5, PinDataType::Audio),
            AudioPin("In 7", 6, PinDataType::Audio),
            AudioPin("In 8", 7, PinDataType::Audio),

            // Num Tracks modulation CV at absolute channel 64 (start of Mod bus)
            AudioPin("Num Tracks Mod", 64, PinDataType::Raw),

            // Per-track CV inputs on Mod bus: Gain at 65,67,... Pan at 66,68,...
            AudioPin("Gain 1 Mod", 65, PinDataType::CV),  AudioPin("Pan 1 Mod", 66, PinDataType::CV),
            AudioPin("Gain 2 Mod", 67, PinDataType::CV),  AudioPin("Pan 2 Mod", 68, PinDataType::CV),
            AudioPin("Gain 3 Mod", 69, PinDataType::CV),  AudioPin("Pan 3 Mod", 70, PinDataType::CV),
            AudioPin("Gain 4 Mod", 71, PinDataType::CV),  AudioPin("Pan 4 Mod", 72, PinDataType::CV),
            AudioPin("Gain 5 Mod", 73, PinDataType::CV),  AudioPin("Pan 5 Mod", 74, PinDataType::CV),
            AudioPin("Gain 6 Mod", 75, PinDataType::CV),  AudioPin("Pan 6 Mod", 76, PinDataType::CV),
            AudioPin("Gain 7 Mod", 77, PinDataType::CV),  AudioPin("Pan 7 Mod", 78, PinDataType::CV),
            AudioPin("Gain 8 Mod", 79, PinDataType::CV),  AudioPin("Pan 8 Mod", 80, PinDataType::CV)
        },
        {
            AudioPin("Out L", 0, PinDataType::Audio),
            AudioPin("Out R", 1, PinDataType::Audio)
        },
        {}
    );
    
    // Add PolyVCO module - Build the pin lists directly in initializer list
    db["polyvco"] = ModulePinInfo(
        {
            // Num Voices modulation input
            AudioPin("Num Voices Mod", 0, PinDataType::Raw),
            
            // Frequency modulation inputs (channels 1-32)
            AudioPin("Freq 1 Mod", 1, PinDataType::CV), AudioPin("Freq 2 Mod", 2, PinDataType::CV),
            AudioPin("Freq 3 Mod", 3, PinDataType::CV), AudioPin("Freq 4 Mod", 4, PinDataType::CV),
            AudioPin("Freq 5 Mod", 5, PinDataType::CV), AudioPin("Freq 6 Mod", 6, PinDataType::CV),
            AudioPin("Freq 7 Mod", 7, PinDataType::CV), AudioPin("Freq 8 Mod", 8, PinDataType::CV),
            AudioPin("Freq 9 Mod", 9, PinDataType::CV), AudioPin("Freq 10 Mod", 10, PinDataType::CV),
            AudioPin("Freq 11 Mod", 11, PinDataType::CV), AudioPin("Freq 12 Mod", 12, PinDataType::CV),
            AudioPin("Freq 13 Mod", 13, PinDataType::CV), AudioPin("Freq 14 Mod", 14, PinDataType::CV),
            AudioPin("Freq 15 Mod", 15, PinDataType::CV), AudioPin("Freq 16 Mod", 16, PinDataType::CV),
            AudioPin("Freq 17 Mod", 17, PinDataType::CV), AudioPin("Freq 18 Mod", 18, PinDataType::CV),
            AudioPin("Freq 19 Mod", 19, PinDataType::CV), AudioPin("Freq 20 Mod", 20, PinDataType::CV),
            AudioPin("Freq 21 Mod", 21, PinDataType::CV), AudioPin("Freq 22 Mod", 22, PinDataType::CV),
            AudioPin("Freq 23 Mod", 23, PinDataType::CV), AudioPin("Freq 24 Mod", 24, PinDataType::CV),
            AudioPin("Freq 25 Mod", 25, PinDataType::CV), AudioPin("Freq 26 Mod", 26, PinDataType::CV),
            AudioPin("Freq 27 Mod", 27, PinDataType::CV), AudioPin("Freq 28 Mod", 28, PinDataType::CV),
            AudioPin("Freq 29 Mod", 29, PinDataType::CV), AudioPin("Freq 30 Mod", 30, PinDataType::CV),
            AudioPin("Freq 31 Mod", 31, PinDataType::CV), AudioPin("Freq 32 Mod", 32, PinDataType::CV),
            
            // Waveform modulation inputs (channels 33-64)
            AudioPin("Wave 1 Mod", 33, PinDataType::CV), AudioPin("Wave 2 Mod", 34, PinDataType::CV),
            AudioPin("Wave 3 Mod", 35, PinDataType::CV), AudioPin("Wave 4 Mod", 36, PinDataType::CV),
            AudioPin("Wave 5 Mod", 37, PinDataType::CV), AudioPin("Wave 6 Mod", 38, PinDataType::CV),
            AudioPin("Wave 7 Mod", 39, PinDataType::CV), AudioPin("Wave 8 Mod", 40, PinDataType::CV),
            AudioPin("Wave 9 Mod", 41, PinDataType::CV), AudioPin("Wave 10 Mod", 42, PinDataType::CV),
            AudioPin("Wave 11 Mod", 43, PinDataType::CV), AudioPin("Wave 12 Mod", 44, PinDataType::CV),
            AudioPin("Wave 13 Mod", 45, PinDataType::CV), AudioPin("Wave 14 Mod", 46, PinDataType::CV),
            AudioPin("Wave 15 Mod", 47, PinDataType::CV), AudioPin("Wave 16 Mod", 48, PinDataType::CV),
            AudioPin("Wave 17 Mod", 49, PinDataType::CV), AudioPin("Wave 18 Mod", 50, PinDataType::CV),
            AudioPin("Wave 19 Mod", 51, PinDataType::CV), AudioPin("Wave 20 Mod", 52, PinDataType::CV),
            AudioPin("Wave 21 Mod", 53, PinDataType::CV), AudioPin("Wave 22 Mod", 54, PinDataType::CV),
            AudioPin("Wave 23 Mod", 55, PinDataType::CV), AudioPin("Wave 24 Mod", 56, PinDataType::CV),
            AudioPin("Wave 25 Mod", 57, PinDataType::CV), AudioPin("Wave 26 Mod", 58, PinDataType::CV),
            AudioPin("Wave 27 Mod", 59, PinDataType::CV), AudioPin("Wave 28 Mod", 60, PinDataType::CV),
            AudioPin("Wave 29 Mod", 61, PinDataType::CV), AudioPin("Wave 30 Mod", 62, PinDataType::CV),
            AudioPin("Wave 31 Mod", 63, PinDataType::CV), AudioPin("Wave 32 Mod", 64, PinDataType::CV),
            
            // Gate modulation inputs (channels 65-96)
            AudioPin("Gate 1 Mod", 65, PinDataType::Gate), AudioPin("Gate 2 Mod", 66, PinDataType::Gate),
            AudioPin("Gate 3 Mod", 67, PinDataType::Gate), AudioPin("Gate 4 Mod", 68, PinDataType::Gate),
            AudioPin("Gate 5 Mod", 69, PinDataType::Gate), AudioPin("Gate 6 Mod", 70, PinDataType::Gate),
            AudioPin("Gate 7 Mod", 71, PinDataType::Gate), AudioPin("Gate 8 Mod", 72, PinDataType::Gate),
            AudioPin("Gate 9 Mod", 73, PinDataType::Gate), AudioPin("Gate 10 Mod", 74, PinDataType::Gate),
            AudioPin("Gate 11 Mod", 75, PinDataType::Gate), AudioPin("Gate 12 Mod", 76, PinDataType::Gate),
            AudioPin("Gate 13 Mod", 77, PinDataType::Gate), AudioPin("Gate 14 Mod", 78, PinDataType::Gate),
            AudioPin("Gate 15 Mod", 79, PinDataType::Gate), AudioPin("Gate 16 Mod", 80, PinDataType::Gate),
            AudioPin("Gate 17 Mod", 81, PinDataType::Gate), AudioPin("Gate 18 Mod", 82, PinDataType::Gate),
            AudioPin("Gate 19 Mod", 83, PinDataType::Gate), AudioPin("Gate 20 Mod", 84, PinDataType::Gate),
            AudioPin("Gate 21 Mod", 85, PinDataType::Gate), AudioPin("Gate 22 Mod", 86, PinDataType::Gate),
            AudioPin("Gate 23 Mod", 87, PinDataType::Gate), AudioPin("Gate 24 Mod", 88, PinDataType::Gate),
            AudioPin("Gate 25 Mod", 89, PinDataType::Gate), AudioPin("Gate 26 Mod", 90, PinDataType::Gate),
            AudioPin("Gate 27 Mod", 91, PinDataType::Gate), AudioPin("Gate 28 Mod", 92, PinDataType::Gate),
            AudioPin("Gate 29 Mod", 93, PinDataType::Gate), AudioPin("Gate 30 Mod", 94, PinDataType::Gate),
            AudioPin("Gate 31 Mod", 95, PinDataType::Gate), AudioPin("Gate 32 Mod", 96, PinDataType::Gate)
        },
        {
            // Audio outputs (channels 0-31)
            AudioPin("Out 1", 0, PinDataType::Audio), AudioPin("Out 2", 1, PinDataType::Audio),
            AudioPin("Out 3", 2, PinDataType::Audio), AudioPin("Out 4", 3, PinDataType::Audio),
            AudioPin("Out 5", 4, PinDataType::Audio), AudioPin("Out 6", 5, PinDataType::Audio),
            AudioPin("Out 7", 6, PinDataType::Audio), AudioPin("Out 8", 7, PinDataType::Audio),
            AudioPin("Out 9", 8, PinDataType::Audio), AudioPin("Out 10", 9, PinDataType::Audio),
            AudioPin("Out 11", 10, PinDataType::Audio), AudioPin("Out 12", 11, PinDataType::Audio),
            AudioPin("Out 13", 12, PinDataType::Audio), AudioPin("Out 14", 13, PinDataType::Audio),
            AudioPin("Out 15", 14, PinDataType::Audio), AudioPin("Out 16", 15, PinDataType::Audio),
            AudioPin("Out 17", 16, PinDataType::Audio), AudioPin("Out 18", 17, PinDataType::Audio),
            AudioPin("Out 19", 18, PinDataType::Audio), AudioPin("Out 20", 19, PinDataType::Audio),
            AudioPin("Out 21", 20, PinDataType::Audio), AudioPin("Out 22", 21, PinDataType::Audio),
            AudioPin("Out 23", 22, PinDataType::Audio), AudioPin("Out 24", 23, PinDataType::Audio),
            AudioPin("Out 25", 24, PinDataType::Audio), AudioPin("Out 26", 25, PinDataType::Audio),
            AudioPin("Out 27", 26, PinDataType::Audio), AudioPin("Out 28", 27, PinDataType::Audio),
            AudioPin("Out 29", 28, PinDataType::Audio), AudioPin("Out 30", 29, PinDataType::Audio),
            AudioPin("Out 31", 30, PinDataType::Audio), AudioPin("Out 32", 31, PinDataType::Audio)
        },
        {}
    );
    
    // Add missing modules
    db["Quantizer"] = ModulePinInfo(
        { AudioPin("CV In", 0, PinDataType::CV), AudioPin("Scale Mod", 1, PinDataType::CV), AudioPin("Root Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );
    
    db["TimePitch"] = ModulePinInfo(
        { AudioPin("Audio In", 0, PinDataType::Audio), AudioPin("Speed Mod", 1, PinDataType::CV), AudioPin("Pitch Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    
    // Note: TTS Performer pin database is defined earlier in this function (around line 378)
    // Duplicate entry removed to avoid conflicts

    
    // Add TrackMixer module alias (main definition is "track mixer" above)
    db["trackmixer"] = db["track mixer"];
    
    
    // Add MIDI Player module
    db["midiplayer"] = ModulePinInfo(
        {},
        {},
        {}
    );
    
    // Add converter modules
    db["Attenuverter"] = {
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Amount Mod", 2, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    };
    
    // Add lowercase alias for Attenuverter
    db["attenuverter"] = db["Attenuverter"];
    
    // Add Sample & Hold module
    db["s&h"] = ModulePinInfo(
        { 
            AudioPin("Signal In L", 0, PinDataType::Audio),
            AudioPin("Signal In R", 1, PinDataType::Audio),
            AudioPin("Trig In L", 2, PinDataType::Gate),
            AudioPin("Trig In R", 3, PinDataType::Gate),
            AudioPin("Threshold Mod", 4, PinDataType::CV),
            AudioPin("Edge Mod", 5, PinDataType::CV),
            AudioPin("Slew Mod", 6, PinDataType::CV)
        },
        { 
            AudioPin("Out L", 0, PinDataType::Audio),
            AudioPin("Out R", 1, PinDataType::Audio)
        },
        {}
    );
    
    db["MapRange"] = {
        { AudioPin("Raw In", 0, PinDataType::Raw) },
        { AudioPin("CV Out", 0, PinDataType::CV), AudioPin("Audio Out", 1, PinDataType::Audio) },
        { ModPin("Min In", "minIn", PinDataType::Raw), ModPin("Max In", "maxIn", PinDataType::Raw), ModPin("Min Out", "minOut", PinDataType::Raw), ModPin("Max Out", "maxOut", PinDataType::Raw) }
    };
    
    db["Lag Processor"] = {
        { AudioPin("Signal In", 0, PinDataType::CV), AudioPin("Rise Mod", 1, PinDataType::CV), AudioPin("Fall Mod", 2, PinDataType::CV) },
        { AudioPin("Smoothed Out", 0, PinDataType::CV) },
        {}
    };
    
    db["De-Crackle"] = {
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    };

    // ADD MISSING MODULES FOR COLOR-CODED CHAINING

    db["Scope"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );

    db["Logic"] = ModulePinInfo(
        { AudioPin("In A", 0, PinDataType::Gate), AudioPin("In B", 1, PinDataType::Gate) },
        {
            AudioPin("AND", 0, PinDataType::Gate),
            AudioPin("OR", 1, PinDataType::Gate),
            AudioPin("XOR", 2, PinDataType::Gate),
            AudioPin("NOT A", 3, PinDataType::Gate)
        },
        {}
    );

    db["ClockDivider"] = ModulePinInfo(
        { AudioPin("Clock In", 0, PinDataType::Gate), AudioPin("Reset", 1, PinDataType::Gate) },
        {
            AudioPin("/2", 0, PinDataType::Gate), AudioPin("/4", 1, PinDataType::Gate),
            AudioPin("/8", 2, PinDataType::Gate), AudioPin("x2", 3, PinDataType::Gate),
            AudioPin("x3", 4, PinDataType::Gate), AudioPin("x4", 5, PinDataType::Gate)
        },
        {}
    );

    db["Rate"] = ModulePinInfo(
        { AudioPin("Rate Mod", 0, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );

    // ADD REMAINING MISSING MODULES FROM CMAKE LISTS

    db["Math"] = ModulePinInfo(
        { AudioPin("In A", 0, PinDataType::CV), AudioPin("In B", 1, PinDataType::CV) },
        { AudioPin("Add", 0, PinDataType::CV), AudioPin("Subtract", 1, PinDataType::CV),
          AudioPin("Multiply", 2, PinDataType::CV), AudioPin("Divide", 3, PinDataType::CV) },
        {}
    );

    db["SequentialSwitch"] = ModulePinInfo(
        { AudioPin("CV In", 0, PinDataType::CV), AudioPin("Gate In", 1, PinDataType::Gate) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );

    db["snapshot sequencer"] = ModulePinInfo(
        { AudioPin("Clock In", 0, PinDataType::Gate), AudioPin("Reset In", 1, PinDataType::Gate) },
        {}, // No audio outputs
        {}
    );

    db["midi cv"] = ModulePinInfo(
        {}, // No inputs - receives MIDI messages
        {
            AudioPin("Pitch", 0, PinDataType::CV),
            AudioPin("Gate", 1, PinDataType::Gate),
            AudioPin("Velocity", 2, PinDataType::CV),
            AudioPin("Mod Wheel", 3, PinDataType::CV),
            AudioPin("Pitch Bend", 4, PinDataType::CV),
            AudioPin("Aftertouch", 5, PinDataType::CV)
        },
        {}
    );

    db["Debug"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        {}, // No outputs
        {}
    );

    db["InputDebug"] = ModulePinInfo(
        {}, // No inputs
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );

}



