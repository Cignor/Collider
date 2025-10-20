#include "ImGuiNodeEditorComponent.h"

#include <imgui.h>
#include <imnodes.h>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <algorithm>
#include "../audio/graph/ModularSynthProcessor.h"
#include "../audio/modules/ModuleProcessor.h"
#include "../audio/modules/AudioInputModuleProcessor.h"
#include "../audio/modules/AttenuverterModuleProcessor.h"
#include "../audio/modules/MapRangeModuleProcessor.h"
#include "../audio/modules/RandomModuleProcessor.h"
#include "../audio/modules/ValueModuleProcessor.h"
#include "../audio/modules/SampleLoaderModuleProcessor.h"
#include "../audio/modules/MIDIPlayerModuleProcessor.h"
#include "../audio/modules/PolyVCOModuleProcessor.h"
#include "../audio/modules/TrackMixerModuleProcessor.h"
#include "../audio/modules/MathModuleProcessor.h"
#include "../audio/modules/StepSequencerModuleProcessor.h"
#include "../audio/modules/MultiSequencerModuleProcessor.h"
#include "../audio/modules/MapRangeModuleProcessor.h"
#include "../audio/modules/LagProcessorModuleProcessor.h"
#include "../audio/modules/DeCrackleModuleProcessor.h"
#include "../audio/modules/GraphicEQModuleProcessor.h"
#include "../audio/modules/FrequencyGraphModuleProcessor.h"
#include "../audio/modules/ChorusModuleProcessor.h"
#include "../audio/modules/PhaserModuleProcessor.h"
#include "../audio/modules/CompressorModuleProcessor.h"
#include "../audio/modules/RecordModuleProcessor.h"
#include "../audio/modules/LimiterModuleProcessor.h"
#include "../audio/modules/GateModuleProcessor.h"
#include "../audio/modules/DriveModuleProcessor.h"
#include <imgui_impl_juce/imgui_impl_juce.h>
#include <backends/imgui_impl_opengl2.h>
#include <juce_opengl/juce_opengl.h>

#define NODE_DEBUG 1

// --- Module Descriptions for Tooltips ---
static const std::map<juce::String, const char*> moduleDescriptions = {
    // Sources
    {"audio input", "Brings hardware audio into the patch."},
    {"VCO", "A standard Voltage-Controlled Oscillator."},
    {"polyvco", "A multi-voice oscillator bank for polyphony."},
    {"Noise", "Generates white, pink, or brown noise."},
    {"Sequencer", "A classic 16-step CV and Gate sequencer."},
    {"multi sequencer", "Advanced sequencer with parallel per-step outputs."},
    {"midi player", "Plays MIDI files and outputs CV/Gate for each track."},
    {"Value", "Outputs a constant, adjustable numerical value."},
    {"sample loader", "Loads and plays audio samples with pitch/time control."},
    {"best practice", "A template and example node demonstrating best practices."},
    // TTS Family
    {"TTS Performer", "Advanced Text-to-Speech engine with word-level sequencing."},
    {"Vocal Tract Filter", "A formant filter that simulates human vowel sounds."},
    // Effects
    {"VCF", "A Voltage-Controlled Filter (LP, HP, BP)."},
    {"Delay", "A stereo delay effect with modulation."},
    {"Reverb", "A stereo reverb effect."},
    {"chorus", "A stereo chorus effect."},
    {"phaser", "A stereo phaser effect."},
    {"compressor", "Reduces the dynamic range of a signal."},
    {"limiter", "Prevents a signal from exceeding a set level."},
    {"gate", "A stereo noise gate to silence signals below a threshold."},
    {"drive", "A waveshaping distortion effect."},
    {"graphic eq", "An 8-band graphic equalizer."},
    {"Frequency Graph", "A high-resolution, real-time spectrum analyzer."},
    {"Waveshaper", "A distortion effect with multiple shaping algorithms."},
    {"8bandshaper", "A multi-band waveshaper for frequency-specific distortion."},
    {"Granulator", "A granular synthesizer/effect that plays small grains of a sample."},
    {"harmonic shaper", "Shapes the harmonic content of a signal."},
    {"timepitch", "Real-time pitch and time manipulation using RubberBand."},
    {"De-Crackle", "A utility to reduce clicks from discontinuous signals."},
    {"recorder", "Records incoming audio to a WAV, AIFF, or FLAC file."},
    // Modulators
    {"LFO", "A Low-Frequency Oscillator for modulation."},
    {"ADSR", "An Attack-Decay-Sustain-Release envelope generator."},
    {"Random", "A random value generator with internal sample & hold."},
    {"S&H", "A classic Sample and Hold module."},
    {"Function Generator", "A complex, drawable envelope/LFO generator."},
    {"shaping oscillator", "An oscillator with a built-in waveshaper."},
    // Utilities & Logic
    {"VCA", "A Voltage-Controlled Amplifier to control signal level."},
    {"Mixer", "A stereo audio mixer with crossfading and panning."},
    {"cv mixer", "A mixer specifically for control voltage signals."},
    {"trackmixer", "A multi-channel mixer for polyphonic sources."},
    {"Attenuverter", "Attenuates (reduces) and/or inverts signals."},
    {"Lag Processor", "Smooths out abrupt changes in a signal (slew limiter)."},
    {"Math", "Performs mathematical operations on signals."},
    {"MapRange", "Remaps a signal from one numerical range to another."},
    {"Quantizer", "Snaps a continuous signal to a musical scale."},
    {"Rate", "Converts a control signal into a normalized rate value."},
    {"Comparator", "Outputs a high signal if an input is above a threshold."},
    {"Logic", "Performs boolean logic (AND, OR, XOR, NOT) on gate signals."},
    {"ClockDivider", "Divides and multiplies clock signals."},
    {"SequentialSwitch", "A signal router with multiple thresholds."},
    // Analysis
    {"Scope", "Visualizes an audio or CV signal."},
    {"debug", "A tool for logging signal value changes."},
    {"input debug", "A passthrough version of the Debug node for inspecting signals on a cable."}
};

static const char* toString(PinDataType t)
{
    switch (t)
    {
        case PinDataType::Audio: return "Audio";
        case PinDataType::CV: return "CV";
        case PinDataType::Gate: return "Gate";
        case PinDataType::Raw: return "Raw";
        default: return "Unknown";
    }
}

#define LOG_LINK(msg) do { if (NODE_DEBUG) juce::Logger::writeToLog("[LINK] " + juce::String(msg)); } while(0)

struct Range { float min; float max; };

// Forward declarations
class ModularSynthProcessor;
class RandomModuleProcessor;
class ValueModuleProcessor;
class StepSequencerModuleProcessor;
class MapRangeModuleProcessor;

// Helper methods for MapRange configuration
ImGuiNodeEditorComponent::Range getSourceRange(const ImGuiNodeEditorComponent::PinID& srcPin, ModularSynthProcessor* synth)
{
    if (synth == nullptr) return {0.0f, 1.0f};
    
    auto* module = synth->getModuleForLogical(srcPin.logicalId);
    if (auto* random = dynamic_cast<RandomModuleProcessor*>(module))
    {
        auto& ap = random->getAPVTS();
        float min = 0.0f, max = 1.0f;
        if (auto* minParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("min")))
            min = minParam->get();
        if (auto* maxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("max")))
            max = maxParam->get();
        return {min, max};
    }
    else if (auto* value = dynamic_cast<ValueModuleProcessor*>(module))
    {
        auto& ap = value->getAPVTS();
        float min = 0.0f, max = 1.0f;
        if (auto* minParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("min")))
            min = minParam->get();
        if (auto* maxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("max")))
            max = maxParam->get();
        return {min, max};
    }
    else if (auto* stepSeq = dynamic_cast<StepSequencerModuleProcessor*>(module))
    {
        // StepSequencer outputs CV range
        return {0.0f, 1.0f};
    }
    // Fallback: estimate from source's lastOutputValues
    // TODO: implement fallback estimation
    return {0.0f, 1.0f};
}

void configureMapRangeFor(PinDataType srcType, PinDataType dstType, MapRangeModuleProcessor& m, ImGuiNodeEditorComponent::Range inRange)
{
    auto& ap = m.getAPVTS();
    
    // Set input range
    if (auto* inMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMin")))
        *inMinParam = inRange.min;
    if (auto* inMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMax")))
        *inMaxParam = inRange.max;
    
    // Set output range based on destination type
    if (dstType == PinDataType::Audio)
    {
        if (auto* outMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin")))
            *outMinParam = -1.0f;
        if (auto* outMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax")))
            *outMaxParam = 1.0f;
    }
    else // CV or Gate
    {
        if (auto* outMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin")))
            *outMinParam = 0.0f;
        if (auto* outMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax")))
            *outMaxParam = 1.0f;
    }
}

// ADD ALL OF THIS CODE:

// --- Structs to define a module's connection points ---
// (Structs are now defined in the header file)


// --- A map to hold the pin info for every module type ---
std::map<juce::String, ModulePinInfo> modulePinDatabase;

// --- A function to populate the database ---
void populatePinDatabase()
{
    if (!modulePinDatabase.empty()) return; // Only run once

    // --- Sources ---
    modulePinDatabase["audio input"] = ModulePinInfo(
        {},
        { AudioPin("Out 1", 0, PinDataType::Audio), AudioPin("Out 2", 1, PinDataType::Audio),
          AudioPin("Gate", 16, PinDataType::Gate), AudioPin("Trigger", 17, PinDataType::Gate), AudioPin("EOP", 18, PinDataType::Gate) },
        {}
    );
    modulePinDatabase["vco"] = ModulePinInfo(
        { AudioPin("Frequency", 0, PinDataType::CV), AudioPin("Waveform", 1, PinDataType::CV), AudioPin("Gate", 2, PinDataType::Gate) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["noise"] = ModulePinInfo(
        { AudioPin("Level Mod", 0, PinDataType::CV), AudioPin("Colour Mod", 1, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) }, // Stereo output to match actual implementation
        {}
    );
    modulePinDatabase["value"] = ModulePinInfo(
        {},
        { AudioPin("Raw", 0, PinDataType::Raw), AudioPin("Normalized", 1, PinDataType::CV), AudioPin("Inverted", 2, PinDataType::Raw),
          AudioPin("Integer", 3, PinDataType::Raw), AudioPin("CV Out", 4, PinDataType::CV) },
        {}
    );
    modulePinDatabase["sample loader"] = ModulePinInfo(
        { AudioPin("Pitch Mod", 0, PinDataType::CV), AudioPin("Speed Mod", 1, PinDataType::CV), AudioPin("Gate Mod", 2, PinDataType::CV),
          AudioPin("Trigger Mod", 3, PinDataType::Gate), AudioPin("Range Start Mod", 4, PinDataType::CV), AudioPin("Range End Mod", 5, PinDataType::CV),
          AudioPin("Randomize Trig", 6, PinDataType::Gate) },
        { AudioPin("Audio Output", 0, PinDataType::Audio) },
        {}
    );

    // --- Effects ---
    modulePinDatabase["vcf"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Cutoff Mod", 2, PinDataType::CV),
          AudioPin("Resonance Mod", 3, PinDataType::CV), AudioPin("Type Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["delay"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Time Mod", 2, PinDataType::CV),
          AudioPin("Feedback Mod", 3, PinDataType::CV), AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["reverb"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Size Mod", 2, PinDataType::CV),
          AudioPin("Damp Mod", 3, PinDataType::CV), AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["compressor"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Thresh Mod", 2, PinDataType::CV),
          AudioPin("Ratio Mod", 3, PinDataType::CV), AudioPin("Attack Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV),
          AudioPin("Makeup Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );

    // --- Modulators ---
    modulePinDatabase["lfo"] = ModulePinInfo(
        { AudioPin("Rate Mod", 0, PinDataType::CV), AudioPin("Depth Mod", 1, PinDataType::CV), AudioPin("Wave Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );
    modulePinDatabase["adsr"] = ModulePinInfo(
        { AudioPin("Gate In", 0, PinDataType::Gate), AudioPin("Trigger In", 1, PinDataType::Gate), AudioPin("Attack Mod", 2, PinDataType::CV),
          AudioPin("Decay Mod", 3, PinDataType::CV), AudioPin("Sustain Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV) },
        { AudioPin("Env Out", 0, PinDataType::CV), AudioPin("Inv Out", 1, PinDataType::CV), AudioPin("EOR Gate", 2, PinDataType::Gate),
          AudioPin("EOC Gate", 3, PinDataType::Gate) },
        {}
    );
    modulePinDatabase["random"] = ModulePinInfo(
        { AudioPin("Trigger In", 0, PinDataType::Gate), AudioPin("Rate Mod", 1, PinDataType::CV), AudioPin("Slew Mod", 2, PinDataType::CV) },
        { AudioPin("Norm Out", 0, PinDataType::CV), AudioPin("Raw Out", 1, PinDataType::Raw), AudioPin("CV Out", 2, PinDataType::CV),
          AudioPin("Bool Out", 3, PinDataType::Gate), AudioPin("Trig Out", 4, PinDataType::Gate) },
        {}
    );

    // --- Utilities ---
    modulePinDatabase["vca"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Gain Mod", 2, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["mixer"] = ModulePinInfo(
        { AudioPin("In A L", 0, PinDataType::Audio), AudioPin("In A R", 1, PinDataType::Audio), AudioPin("In B L", 2, PinDataType::Audio),
          AudioPin("In B R", 3, PinDataType::Audio), AudioPin("Gain Mod", 4, PinDataType::CV), AudioPin("Pan Mod", 5, PinDataType::CV),
          AudioPin("X-Fade Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["scope"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["graphic eq"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Band 1 Mod", 2, PinDataType::CV), AudioPin("Band 2 Mod", 3, PinDataType::CV),
          AudioPin("Band 3 Mod", 4, PinDataType::CV), AudioPin("Band 4 Mod", 5, PinDataType::CV),
          AudioPin("Band 5 Mod", 6, PinDataType::CV), AudioPin("Band 6 Mod", 7, PinDataType::CV),
          AudioPin("Band 7 Mod", 8, PinDataType::CV), AudioPin("Band 8 Mod", 9, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["frequency graph"] = ModulePinInfo(
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
    modulePinDatabase["chorus"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Depth Mod", 3, PinDataType::CV),
          AudioPin("Mix Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["phaser"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Depth Mod", 3, PinDataType::CV),
          AudioPin("Centre Mod", 4, PinDataType::CV), AudioPin("Feedback Mod", 5, PinDataType::CV),
          AudioPin("Mix Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["compressor"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Thresh Mod", 2, PinDataType::CV), AudioPin("Ratio Mod", 3, PinDataType::CV),
          AudioPin("Attack Mod", 4, PinDataType::CV), AudioPin("Release Mod", 5, PinDataType::CV),
          AudioPin("Makeup Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["Recorder"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        {}, // No outputs
        {}
    );
    modulePinDatabase["limiter"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio),
          AudioPin("Thresh Mod", 2, PinDataType::CV), AudioPin("Release Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["gate"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["drive"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["time/pitch"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Speed Mod", 2, PinDataType::CV), AudioPin("Pitch Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["waveshaper"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Drive Mod", 2, PinDataType::CV), AudioPin("Type Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["8bandshaper"] = ModulePinInfo(
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
    modulePinDatabase["granulator"] = ModulePinInfo(
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
    modulePinDatabase["granulator"] = modulePinDatabase["Granulator"];
    modulePinDatabase["mixer"] = ModulePinInfo(
        { AudioPin("In A L", 0, PinDataType::Audio), AudioPin("In A R", 1, PinDataType::Audio), AudioPin("In B L", 2, PinDataType::Audio), AudioPin("In B R", 3, PinDataType::Audio), AudioPin("Gain Mod", 4, PinDataType::CV), AudioPin("Pan Mod", 5, PinDataType::CV), AudioPin("X-Fade Mod", 6, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
    modulePinDatabase["sequencer"] = ModulePinInfo(
        { AudioPin("Mod In L", 0, PinDataType::Audio), AudioPin("Mod In R", 1, PinDataType::Audio), AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Gate Mod", 3, PinDataType::CV), AudioPin("Steps Mod", 4, PinDataType::CV), AudioPin("Steps Max", 5, PinDataType::CV),
          // Per-step value mods absolute 6..21 (Step1..Step16)
          AudioPin("Step 1 Mod", 6, PinDataType::CV), AudioPin("Step 2 Mod", 7, PinDataType::CV), AudioPin("Step 3 Mod", 8, PinDataType::CV), AudioPin("Step 4 Mod", 9, PinDataType::CV),
          AudioPin("Step 5 Mod", 10, PinDataType::CV), AudioPin("Step 6 Mod", 11, PinDataType::CV), AudioPin("Step 7 Mod", 12, PinDataType::CV), AudioPin("Step 8 Mod", 13, PinDataType::CV),
          AudioPin("Step 9 Mod", 14, PinDataType::CV), AudioPin("Step 10 Mod", 15, PinDataType::CV), AudioPin("Step 11 Mod", 16, PinDataType::CV), AudioPin("Step 12 Mod", 17, PinDataType::CV),
          AudioPin("Step 13 Mod", 18, PinDataType::CV), AudioPin("Step 14 Mod", 19, PinDataType::CV), AudioPin("Step 15 Mod", 20, PinDataType::CV), AudioPin("Step 16 Mod", 21, PinDataType::CV),
          // Per-step trig mods absolute 22..37 (Step1..Step16) — these are Gates
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

    modulePinDatabase["value"] = ModulePinInfo(
        {},
        { AudioPin("Raw", 0, PinDataType::Raw), AudioPin("Normalized", 1, PinDataType::CV), AudioPin("Inverted", 2, PinDataType::Raw), AudioPin("Integer", 3, PinDataType::Raw), AudioPin("CV Out", 4, PinDataType::CV) },
        {}
    );
// in populatePinDatabase()

// in populatePinDatabase()

// in populatePinDatabase()

modulePinDatabase["random"] = ModulePinInfo(
    { 
        AudioPin("Trigger In", 0, PinDataType::Gate), 
        AudioPin("Rate Mod", 1, PinDataType::CV), 
        AudioPin("Slew Mod", 2, PinDataType::CV) 
    },
    { 
        AudioPin("Norm Out", 0, PinDataType::CV), 
        AudioPin("Raw Out", 1, PinDataType::Raw), 
        AudioPin("CV Out", 2, PinDataType::CV),
        AudioPin("Bool Out", 3, PinDataType::Gate), 
        AudioPin("Trig Out", 4, PinDataType::Gate) 
    },
    { 
        ModPin("Rate", "rate_mod", PinDataType::CV),
        ModPin("Slew", "slew_mod", PinDataType::CV)
    }
);

    modulePinDatabase["tts performer"] = ModulePinInfo(
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
    modulePinDatabase["vocal tract filter"] = ModulePinInfo(
        { AudioPin("Audio In", 0, PinDataType::Audio) },
        { AudioPin("Audio Out", 0, PinDataType::Audio) },
        { ModPin("Vowel", "vowelShape", PinDataType::CV), ModPin("Formant", "formantShift", PinDataType::CV), ModPin("Instability", "instability", PinDataType::CV), ModPin("Gain", "formantGain", PinDataType::CV) }
    );
    modulePinDatabase["best practice"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Wave Mod", 3, PinDataType::CV), AudioPin("Drive Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Frequency", "frequency_mod", PinDataType::CV), ModPin("Waveform", "waveform_mod", PinDataType::CV), ModPin("Drive", "drive_mod", PinDataType::CV) }
    );
    modulePinDatabase["shaping oscillator"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Wave Mod", 3, PinDataType::CV), AudioPin("Drive Mod", 4, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Frequency", "frequency_mod", PinDataType::CV), ModPin("Waveform", "waveform_mod", PinDataType::CV), ModPin("Drive", "drive_mod", PinDataType::CV) }
    );
    modulePinDatabase["harmonic shaper"] = ModulePinInfo(
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Freq Mod", 2, PinDataType::CV), AudioPin("Drive Mod", 3, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        { ModPin("Master Frequency", "masterFrequency_mod", PinDataType::CV), ModPin("Master Drive", "masterDrive_mod", PinDataType::CV) }
    );
    modulePinDatabase["function generator"] = ModulePinInfo(
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

    modulePinDatabase["multi sequencer"] = ModulePinInfo(
        { // Inputs: Mod In L, Mod In R, Rate Mod, Gate Mod, Steps Mod, plus per-step mods and triggers
            AudioPin("Mod In L", 0, PinDataType::Audio), AudioPin("Mod In R", 1, PinDataType::Audio),
            AudioPin("Rate Mod", 2, PinDataType::CV), AudioPin("Gate Mod", 3, PinDataType::CV),
            AudioPin("Steps Mod", 4, PinDataType::CV),
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
        { // Outputs: Live outputs (0-5) + Parallel step outputs (6+)
            // Live Outputs
            AudioPin("Pitch", 0, PinDataType::CV), AudioPin("Gate", 1, PinDataType::Gate),
            AudioPin("Gate Nuanced", 2, PinDataType::CV), AudioPin("Velocity", 3, PinDataType::CV),
            AudioPin("Mod", 4, PinDataType::CV), AudioPin("Trigger", 5, PinDataType::Gate),
            // Parallel Step Outputs (Corrected Names and Channels)
            AudioPin("Pitch 1", 6, PinDataType::CV), AudioPin("Gate 1", 7, PinDataType::Gate), AudioPin("Trig 1", 8, PinDataType::Gate),
            AudioPin("Pitch 2", 9, PinDataType::CV), AudioPin("Gate 2", 10, PinDataType::Gate), AudioPin("Trig 2", 11, PinDataType::Gate),
            AudioPin("Pitch 3", 12, PinDataType::CV), AudioPin("Gate 3", 13, PinDataType::Gate), AudioPin("Trig 3", 14, PinDataType::Gate),
            AudioPin("Pitch 4", 15, PinDataType::CV), AudioPin("Gate 4", 16, PinDataType::Gate), AudioPin("Trig 4", 17, PinDataType::Gate),
            AudioPin("Pitch 5", 18, PinDataType::CV), AudioPin("Gate 5", 19, PinDataType::Gate), AudioPin("Trig 5", 20, PinDataType::Gate),
            AudioPin("Pitch 6", 21, PinDataType::CV), AudioPin("Gate 6", 22, PinDataType::Gate), AudioPin("Trig 6", 23, PinDataType::Gate),
            AudioPin("Pitch 7", 24, PinDataType::CV), AudioPin("Gate 7", 25, PinDataType::Gate), AudioPin("Trig 7", 26, PinDataType::Gate),
            AudioPin("Pitch 8", 27, PinDataType::CV), AudioPin("Gate 8", 28, PinDataType::Gate), AudioPin("Trig 8", 29, PinDataType::Gate),
            AudioPin("Pitch 9", 30, PinDataType::CV), AudioPin("Gate 9", 31, PinDataType::Gate), AudioPin("Trig 9", 32, PinDataType::Gate),
            AudioPin("Pitch 10", 33, PinDataType::CV), AudioPin("Gate 10", 34, PinDataType::Gate), AudioPin("Trig 10", 35, PinDataType::Gate),
            AudioPin("Pitch 11", 36, PinDataType::CV), AudioPin("Gate 11", 37, PinDataType::Gate), AudioPin("Trig 11", 38, PinDataType::Gate),
            AudioPin("Pitch 12", 39, PinDataType::CV), AudioPin("Gate 12", 40, PinDataType::Gate), AudioPin("Trig 12", 41, PinDataType::Gate),
            AudioPin("Pitch 13", 42, PinDataType::CV), AudioPin("Gate 13", 43, PinDataType::Gate), AudioPin("Trig 13", 44, PinDataType::Gate),
            AudioPin("Pitch 14", 45, PinDataType::CV), AudioPin("Gate 14", 46, PinDataType::Gate), AudioPin("Trig 14", 47, PinDataType::Gate),
            AudioPin("Pitch 15", 48, PinDataType::CV), AudioPin("Gate 15", 49, PinDataType::Gate), AudioPin("Trig 15", 50, PinDataType::Gate),
            AudioPin("Pitch 16", 51, PinDataType::CV), AudioPin("Gate 16", 52, PinDataType::Gate), AudioPin("Trig 16", 53, PinDataType::Gate)
        },
        {}
    );
    modulePinDatabase["comparator"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::Gate) },
        {}
    );

    modulePinDatabase["sample loader"] = ModulePinInfo(
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
    modulePinDatabase["track mixer"] = ModulePinInfo(
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
            AudioPin("Num Tracks Mod", 64, PinDataType::CV),

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
    modulePinDatabase["polyvco"] = ModulePinInfo(
        {
            // Num Voices modulation input
            AudioPin("Num Voices Mod", 0, PinDataType::CV),
            
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
    modulePinDatabase["Quantizer"] = ModulePinInfo(
        { AudioPin("CV In", 0, PinDataType::CV), AudioPin("Scale Mod", 1, PinDataType::CV), AudioPin("Root Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );
    
    modulePinDatabase["TimePitch"] = ModulePinInfo(
        { AudioPin("Audio In", 0, PinDataType::Audio), AudioPin("Speed Mod", 1, PinDataType::CV), AudioPin("Pitch Mod", 2, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );
    
    // Note: TTS Performer pin database is defined earlier in this function (around line 378)
    // Duplicate entry removed to avoid conflicts

    
    // Add TrackMixer module alias (main definition is "track mixer" above)
    modulePinDatabase["trackmixer"] = modulePinDatabase["track mixer"];
    
    
    // Add MIDI Player module
    modulePinDatabase["midiplayer"] = ModulePinInfo(
        {},
        {},
        {}
    );
    
    // Add converter modules
    modulePinDatabase["Attenuverter"] = {
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio), AudioPin("Amount Mod", 2, PinDataType::CV) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    };
    
    // Add lowercase alias for Attenuverter
    modulePinDatabase["attenuverter"] = modulePinDatabase["Attenuverter"];
    
    // Add Sample & Hold module
    modulePinDatabase["s&h"] = ModulePinInfo(
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
    
    modulePinDatabase["MapRange"] = {
        { AudioPin("Raw In", 0, PinDataType::Raw) },
        { AudioPin("CV Out", 0, PinDataType::CV), AudioPin("Audio Out", 1, PinDataType::Audio) },
        { ModPin("Min In", "minIn", PinDataType::Raw), ModPin("Max In", "maxIn", PinDataType::Raw), ModPin("Min Out", "minOut", PinDataType::Raw), ModPin("Max Out", "maxOut", PinDataType::Raw) }
    };
    
    modulePinDatabase["Lag Processor"] = {
        { AudioPin("Signal In", 0, PinDataType::CV), AudioPin("Rise Mod", 1, PinDataType::CV), AudioPin("Fall Mod", 2, PinDataType::CV) },
        { AudioPin("Smoothed Out", 0, PinDataType::CV) },
        {}
    };
    
    modulePinDatabase["De-Crackle"] = {
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    };

    // ADD MISSING MODULES FOR COLOR-CODED CHAINING

    modulePinDatabase["Scope"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );

    modulePinDatabase["Logic"] = ModulePinInfo(
        { AudioPin("In A", 0, PinDataType::Gate), AudioPin("In B", 1, PinDataType::Gate) },
        {
            AudioPin("AND", 0, PinDataType::Gate),
            AudioPin("OR", 1, PinDataType::Gate),
            AudioPin("XOR", 2, PinDataType::Gate),
            AudioPin("NOT A", 3, PinDataType::Gate)
        },
        {}
    );

    modulePinDatabase["ClockDivider"] = ModulePinInfo(
        { AudioPin("Clock In", 0, PinDataType::Gate), AudioPin("Reset", 1, PinDataType::Gate) },
        {
            AudioPin("/2", 0, PinDataType::Gate), AudioPin("/4", 1, PinDataType::Gate),
            AudioPin("/8", 2, PinDataType::Gate), AudioPin("x2", 3, PinDataType::Gate),
            AudioPin("x3", 4, PinDataType::Gate), AudioPin("x4", 5, PinDataType::Gate)
        },
        {}
    );

    modulePinDatabase["Rate"] = ModulePinInfo(
        { AudioPin("Rate Mod", 0, PinDataType::CV) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );

    // ADD REMAINING MISSING MODULES FROM CMAKE LISTS

    modulePinDatabase["Math"] = ModulePinInfo(
        { AudioPin("In A", 0, PinDataType::CV), AudioPin("In B", 1, PinDataType::CV) },
        { AudioPin("Add", 0, PinDataType::CV), AudioPin("Subtract", 1, PinDataType::CV),
          AudioPin("Multiply", 2, PinDataType::CV), AudioPin("Divide", 3, PinDataType::CV) },
        {}
    );

    modulePinDatabase["SequentialSwitch"] = ModulePinInfo(
        { AudioPin("CV In", 0, PinDataType::CV), AudioPin("Gate In", 1, PinDataType::Gate) },
        { AudioPin("Out", 0, PinDataType::CV) },
        {}
    );

    modulePinDatabase["Debug"] = ModulePinInfo(
        { AudioPin("In", 0, PinDataType::Audio) },
        {}, // No outputs
        {}
    );

    modulePinDatabase["InputDebug"] = ModulePinInfo(
        {}, // No inputs
        { AudioPin("Out", 0, PinDataType::Audio) },
        {}
    );

}

ImGuiNodeEditorComponent::ImGuiNodeEditorComponent(juce::AudioDeviceManager& dm)
    : deviceManager(dm)
{
    populatePinDatabase(); // Initialize the pin database for color coding
    
    glContext.setRenderer (this);
    glContext.setContinuousRepainting (true);
    glContext.setComponentPaintingEnabled (false);
    glContext.attachTo (*this);
    setWantsKeyboardFocus (true);
}

ImGuiNodeEditorComponent::~ImGuiNodeEditorComponent()
{
    glContext.detach();
}

void ImGuiNodeEditorComponent::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void ImGuiNodeEditorComponent::resized()
{
    juce::Logger::writeToLog ("resized: " + juce::String (getWidth()) + "x" + juce::String (getHeight()));
}

// Input handled by imgui_juce backend

void ImGuiNodeEditorComponent::newOpenGLContextCreated()
{
    juce::Logger::writeToLog("ImGuiNodeEditor: newOpenGLContextCreated()");
    // Create ImGui context
    imguiContext = ImGui::CreateContext();
    imguiIO = &ImGui::GetIO();
    ImGui::StyleColorsDark();

    // --- FONT LOADING FOR CHINESE CHARACTERS ---
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault(); // Load default English font

    // Define the path to your new font file
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto fontFile = appFile.getParentDirectory().getChildFile("../../Source/assets/NotoSansSC-VariableFont_wght.ttf");

    if (fontFile.existsAsFile())
    {
        ImFontConfig config;
        config.MergeMode = true; // IMPORTANT: This merges the new font into the default one
        config.PixelSnapH = true;

        // Define the character ranges to load for Chinese
        static const ImWchar ranges[] = { 0x4e00, 0x9fbf, 0, }; // Basic CJK Unified Ideographs

        io.Fonts->AddFontFromFileTTF(fontFile.getFullPathName().toRawUTF8(), 16.0f, &config, ranges);
        juce::Logger::writeToLog("ImGuiNodeEditor: Chinese font loaded successfully");
    }
    else
    {
        juce::Logger::writeToLog("ImGuiNodeEditor: WARNING - Chinese font not found at: " + fontFile.getFullPathName());
    }
    
    // --- END OF FONT LOADING ---

    // imgui_juce backend handles key mapping internally (new IO API)

    // Setup JUCE platform backend and OpenGL2 renderer backend
    ImGui_ImplJuce_Init (*this, glContext);
    ImGui_ImplOpenGL2_Init();
    
    // Build fonts after renderer is initialized
    io.Fonts->Build();

    // Setup imnodes
    ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
    editorContext = ImNodes::CreateContext();
    // Optional ergonomics: Alt = pan, Ctrl = detach link
    {
        auto& ioNodes = ImNodes::GetIO();
        auto& ioImgui = ImGui::GetIO();
        ioNodes.EmulateThreeButtonMouse.Modifier = &ioImgui.KeyAlt;
        ioNodes.LinkDetachWithModifierClick.Modifier = &ioImgui.KeyCtrl;
    }
    juce::Logger::writeToLog("ImGuiNodeEditor: ImNodes context created");
}

void ImGuiNodeEditorComponent::openGLContextClosing()
{
    juce::Logger::writeToLog("ImGuiNodeEditor: openGLContextClosing()");
    ImNodes::DestroyContext(editorContext);
    editorContext = nullptr;
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplJuce_Shutdown();
    ImGui::DestroyContext (imguiContext);
    imguiContext = nullptr; imguiIO = nullptr;
}

void ImGuiNodeEditorComponent::renderOpenGL()
{
    if (imguiContext == nullptr)
        return;

    ImGui::SetCurrentContext (imguiContext);

    // Clear background
    juce::OpenGLHelpers::clear (juce::Colours::darkgrey);

    // Ensure IO is valid and configured each frame (size, delta time, DPI scale, fonts)
    ImGuiIO& io = ImGui::GetIO();
    const float scale = (float) glContext.getRenderingScale();
    io.DisplaySize = ImVec2 ((float) getWidth(), (float) getHeight());
    io.DisplayFramebufferScale = ImVec2 (scale, scale);

    // imgui_juce will queue and apply key/mouse events; avoid manual KeysDown edits that break internal asserts
    io.MouseDrawCursor = false;

    // Mouse input comes via backend listeners; avoid overriding io.MousePos here

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastTime <= 0.0)
        lastTime = nowMs;
    const double dtMs = nowMs - lastTime;
    lastTime = nowMs;
    io.DeltaTime = (dtMs > 0.0 ? (float) (dtMs / 1000.0) : 1.0f / 60.0f);

    // Zoom/pan disabled: use default font scale and editor panning

    // Start a new frame for both backends
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplJuce_NewFrame();

    ImGui::NewFrame();
    // Demo is hidden by default; toggle can be added later if needed
    renderImGui();
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    // Render via OpenGL2 backend
    ImGui_ImplOpenGL2_RenderDrawData (dd);
}

void ImGuiNodeEditorComponent::renderImGui()
{
    // Handle F1 key for shortcuts window
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        showShortcutsWindow = !showShortcutsWindow;
    }

    // Basic docking-like two-panels layout
    ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize (ImVec2 ((float) getWidth(), (float) getHeight()), ImGuiCond_Always);
    ImGui::Begin ("Preset Creator", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    // --- DEFINITIVE STATUS OVERLAY ---
    // This code creates the small, semi-transparent window for the preset status.
    const float sidebarWidth = 260.0f;
    const float menuBarHeight = ImGui::GetFrameHeight();
    const float padding = 10.0f;

    ImGui::SetNextWindowPos(ImVec2(sidebarWidth + padding, menuBarHeight + padding));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("Preset Status Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

    // Display the preset name or "Unsaved Patch"
    if (currentPresetFile.isNotEmpty()) {
        ImGui::Text("Preset: %s", currentPresetFile.toRawUTF8());
    } else {
        ImGui::Text("Preset: Unsaved Patch");
    }

    // Display the "Saved" or "Edited" status
    if (isPatchDirty) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: EDITED");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: SAVED");
    }

    ImGui::End();
    // --- END OF OVERLAY ---

    // Clean up textures for deleted sample loaders
    if (synth != nullptr)
    {
        auto infos = synth->getModulesInfo();
        std::unordered_set<int> activeSampleLoaderIds;
        for (const auto& info : infos)
        {
            if (info.second.equalsIgnoreCase("sample loader"))
            {
                activeSampleLoaderIds.insert((int)info.first);
            }
        }

        for (auto it = sampleLoaderTextureIds.begin(); it != sampleLoaderTextureIds.end(); )
        {
            if (activeSampleLoaderIds.find(it->first) == activeSampleLoaderIds.end())
            {
                if (it->second)
                    it->second.reset();
                it = sampleLoaderTextureIds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // ADD THIS BLOCK:
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Preset", "Ctrl+S")) { startSaveDialog(); }
            if (ImGui::MenuItem("Load Preset", "Ctrl+O")) { startLoadDialog(); }
            
            // ADD: Audio Settings menu item
            if (ImGui::MenuItem("Audio Settings..."))
            {
                if (onShowAudioSettings)
                    onShowAudioSettings();
            }
            
            ImGui::EndMenu();
        }
        
        // <<< ADD THIS ENTIRE "Edit" MENU BLOCK >>>
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Clear Output Connections")) 
            {
                if (synth != nullptr)
                {
                    synth->clearOutputConnections();
                    pushSnapshot(); // Make the action undoable
                }
            }

            // <<< ADD THIS ENTIRE BLOCK >>>
            bool isNodeSelected = (ImNodes::NumSelectedNodes() > 0);
            if (ImGui::MenuItem("Clear Selected Node Connections", nullptr, false, isNodeSelected))
            {
                if (synth != nullptr)
                {
                    std::vector<int> selectedNodeIds(ImNodes::NumSelectedNodes());
                    ImNodes::GetSelectedNodes(selectedNodeIds.data());
                    if (!selectedNodeIds.empty())
                    {
                        // Act on the first selected node
                        juce::uint32 logicalId = (juce::uint32)selectedNodeIds[0];
                        auto nodeId = synth->getNodeIdForLogical(logicalId);
                        if (nodeId.uid != 0)
                        {
                            synth->clearConnectionsForNode(nodeId);
                            pushSnapshot(); // Make the action undoable
                        }
                    }
                }
            }
            // <<< END OF BLOCK >>>

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Actions"))
        {
            // This item should only be enabled if at least one node is selected
            bool anyNodesSelected = ImNodes::NumSelectedNodes() > 0;
            if (ImGui::MenuItem("Connect Selected to Track Mixer", nullptr, false, anyNodesSelected))
            {
                handleConnectSelectedToTrackMixer();
            }
            
            if (ImGui::MenuItem("Beautify Layout", "Ctrl+B"))
            {
                handleBeautifyLayout();
            }
            
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Recording"))
        {
            if (synth != nullptr)
            {
                bool isAnyRecording = synth->isAnyModuleRecording();
                const char* label = isAnyRecording ? "Stop All Recordings" : "Start All Recordings";
                if (ImGui::MenuItem(label))
                {
                    if (isAnyRecording)
                    {
                        synth->stopAllRecorders();
                    }
                    else
                    {
                        synth->startAllRecorders();
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Generate"))
        {
            if (ImGui::MenuItem("Randomize Patch", "Ctrl+P")) { handleRandomizePatch(); }
            if (ImGui::MenuItem("Randomize Connections", "Ctrl+M")) { handleRandomizeConnections(); }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Insert Node"))
        {
            bool isNodeSelected = (selectedLogicalId != 0);
            
            if (ImGui::BeginMenu("Audio Path", isNodeSelected))
            {
                if (ImGui::MenuItem("VCF")) { insertNodeBetween("VCF"); }
                if (ImGui::MenuItem("VCA")) { insertNodeBetween("VCA"); }
                if (ImGui::MenuItem("Delay")) { insertNodeBetween("Delay"); }
                if (ImGui::MenuItem("Reverb")) { insertNodeBetween("Reverb"); }
                if (ImGui::MenuItem("Chorus")) { insertNodeBetween("chorus"); }
                if (ImGui::MenuItem("Phaser")) { insertNodeBetween("phaser"); }
                if (ImGui::MenuItem("Compressor")) { insertNodeBetween("compressor"); }
                if (ImGui::MenuItem("Limiter")) { insertNodeBetween("limiter"); }
                if (ImGui::MenuItem("Gate")) { insertNodeBetween("gate"); }
                if (ImGui::MenuItem("Drive")) { insertNodeBetween("drive"); }
                if (ImGui::MenuItem("Graphic EQ")) { insertNodeBetween("graphic eq"); }
                if (ImGui::MenuItem("Waveshaper")) { insertNodeBetween("Waveshaper"); }
                if (ImGui::MenuItem("Time/Pitch Shifter")) { insertNodeBetween("timepitch"); }
                if (ImGui::MenuItem("De-Crackle")) { insertNodeBetween("De-Crackle"); }
                if (ImGui::MenuItem("Recorder")) { insertNodeBetween("recorder"); }
                if (ImGui::MenuItem("Mixer")) { insertNodeBetween("Mixer"); }
                if (ImGui::MenuItem("Shaping Oscillator")) { insertNodeBetween("shaping oscillator"); }
                if (ImGui::MenuItem("Function Generator")) { insertNodeBetween("Function Generator"); }
                if (ImGui::MenuItem("8-Band Shaper")) { insertNodeBetween("8bandshaper"); }
                if (ImGui::MenuItem("Granulator")) { insertNodeBetween("Granulator"); }
                if (ImGui::MenuItem("Harmonic Shaper")) { insertNodeBetween("harmonic shaper"); }
                if (ImGui::MenuItem("Vocal Tract Filter")) { insertNodeBetween("Vocal Tract Filter"); }
                if (ImGui::MenuItem("Scope")) { insertNodeBetween("Scope"); }
                if (ImGui::MenuItem("Frequency Graph")) { insertNodeBetween("Frequency Graph"); }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Modulation Path", isNodeSelected))
            {
                if (ImGui::MenuItem("Attenuverter")) { insertNodeBetween("Attenuverter"); }
                if (ImGui::MenuItem("Lag Processor")) { insertNodeBetween("Lag Processor"); }
                if (ImGui::MenuItem("Math")) { insertNodeBetween("Math"); }
                if (ImGui::MenuItem("MapRange")) { insertNodeBetween("MapRange"); }
                if (ImGui::MenuItem("Quantizer")) { insertNodeBetween("Quantizer"); }
                if (ImGui::MenuItem("S&H")) { insertNodeBetween("S&H"); }
                if (ImGui::MenuItem("Rate")) { insertNodeBetween("Rate"); }
                if (ImGui::MenuItem("Logic")) { insertNodeBetween("Logic"); }
                if (ImGui::MenuItem("Comparator")) { insertNodeBetween("Comparator"); }
                if (ImGui::MenuItem("CV Mixer")) { insertNodeBetween("CV Mixer"); }
                if (ImGui::MenuItem("Sequential Switch")) { insertNodeBetween("Sequential Switch"); }
                ImGui::EndMenu();
            }
            
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Inspector"))
        {
            ImGui::SliderFloat("Window (s)", &inspectorWindowSeconds, 0.5f, 20.0f, "%.1f s");
            ImGui::EndMenu();
        }
        
        // === DEBUG MENU ===
        if (ImGui::BeginMenu("Debug"))
        {
            if (ImGui::MenuItem("Show System Diagnostics", "Ctrl+Shift+D")) 
            {
                showDebugMenu = !showDebugMenu;
            }
            
            
            if (ImGui::MenuItem("Log System State"))
            {
                if (synth != nullptr)
                {
                    juce::Logger::writeToLog("=== SYSTEM DIAGNOSTICS ===");
                    juce::Logger::writeToLog(synth->getSystemDiagnostics());
                }
            }
            
            if (ImGui::MenuItem("Log Selected Module Diagnostics"))
            {
                if (synth != nullptr && selectedLogicalId != 0)
                {
                    juce::Logger::writeToLog("=== MODULE DIAGNOSTICS ===");
                    juce::Logger::writeToLog(synth->getModuleDiagnostics(selectedLogicalId));
                }
            }
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }

    ImGui::Columns (2, nullptr, true);
    ImGui::SetColumnWidth (0, 260.0f);

    // Zoom removed

    // ADD THIS BLOCK:
    ImGui::Text("Module Browser");
    auto addModuleButton = [this](const char* label, const char* type)
    {
        if (ImGui::Selectable(label, false))
        {
            if (synth != nullptr)
            {
                auto nodeId = synth->addModule(type);
                const ImVec2 mouse = ImGui::GetMousePos();
                // queue screen-space placement after node is drawn to avoid assertions
                const int logicalId = (int) synth->getLogicalIdForNode (nodeId);
                pendingNodeScreenPositions[logicalId] = mouse;
                // Defer snapshot until after EndNodeEditor so the node exists in this frame
                snapshotAfterEditor = true;
            }
        }
        
        // --- FIX: Show tooltip with module description on hover ---
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            
            // Find the description in our map using the module's internal 'type'
            auto it = moduleDescriptions.find(type);
            if (it != moduleDescriptions.end())
            {
                // If found, display it
                ImGui::TextUnformatted(it->second);
            }
            else
            {
                // Fallback text if a description is missing
                ImGui::TextUnformatted("No description available.");
            }
            
            ImGui::EndTooltip();
        }
    };
    if (ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen)) {
    addModuleButton("Audio Input", "audio input");
    addModuleButton("VCO", "VCO");
    addModuleButton("Polyphonic VCO", "polyvco");
    addModuleButton("Noise", "Noise");
        addModuleButton("Sequencer", "Sequencer");
        addModuleButton("Multi Sequencer", "multi sequencer");
        addModuleButton("MIDI Player", "midi player");
        addModuleButton("Value", "Value");
        addModuleButton("Sample Loader", "sample loader");
    }
    if (ImGui::CollapsingHeader("TTS Family", ImGuiTreeNodeFlags_DefaultOpen)) {

        addModuleButton("TTS Performer", "TTS Performer");
        addModuleButton("Vocal Tract Filter", "Vocal Tract Filter");
    }
    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        addModuleButton("VCF", "VCF");
        // addModuleButton("Vocal Tract Filter", "Vocal Tract Filter");
        addModuleButton("Delay", "Delay");
        addModuleButton("Reverb", "Reverb");
        addModuleButton("Chorus", "chorus");
        addModuleButton("Phaser", "phaser");
        addModuleButton("Compressor", "compressor");
        addModuleButton("Recorder", "recorder");
        addModuleButton("Limiter", "limiter");
        addModuleButton("Noise Gate", "gate");
        addModuleButton("Drive", "drive");
        addModuleButton("Graphic EQ", "graphic eq");
        addModuleButton("Time/Pitch Shifter", "timepitch");
        addModuleButton("Waveshaper", "Waveshaper");
        addModuleButton("8-Band Shaper", "8bandshaper");
        addModuleButton("Granulator", "granulator");
        addModuleButton("Harmonic Shaper", "harmonic shaper");
    }
    if (ImGui::CollapsingHeader("Modulators", ImGuiTreeNodeFlags_DefaultOpen)) {
        addModuleButton("LFO", "LFO");
        addModuleButton("ADSR", "ADSR");
        addModuleButton("Random", "Random");
    addModuleButton("S&H", "S&H");
            addModuleButton("Function Generator", "Function Generator");
        addModuleButton("Shaping Oscillator", "shaping oscillator");

    }
    if (ImGui::CollapsingHeader("Utilities & Logic", ImGuiTreeNodeFlags_DefaultOpen)) {
        addModuleButton("VCA", "VCA");
        addModuleButton("Mixer", "Mixer");
        addModuleButton("CV Mixer", "cv mixer");
        addModuleButton("Track Mixer", "trackmixer");
    addModuleButton("Attenuverter", "Attenuverter");
        addModuleButton("Lag Processor", "Lag Processor");
        addModuleButton("De-Crackle", "De-Crackle");
        addModuleButton("Math", "Math");
        addModuleButton("Map Range", "MapRange");
        addModuleButton("Quantizer", "Quantizer");
        addModuleButton("Rate", "Rate");
        addModuleButton("Comparator", "Comparator");
        addModuleButton("Logic", "Logic");
        addModuleButton("Clock Divider", "ClockDivider");
        addModuleButton("Sequential Switch", "SequentialSwitch");
        addModuleButton("Best Practice", "best practice");
    }
    if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
        addModuleButton("Scope", "Scope");
        addModuleButton("Debug", "debug");
        addModuleButton("Input Debug", "input debug");
        addModuleButton("Frequency Graph", "Frequency Graph");
    }


    ImGui::NextColumn();

    // <<< ADD THIS ENTIRE BLOCK TO CACHE CONNECTION STATUS >>>
    std::unordered_set<int> connectedInputAttrs;
    std::unordered_set<int> connectedOutputAttrs;
    if (synth != nullptr)
    {
        for (const auto& c : synth->getConnectionsInfo())
        {
            int srcAttr = getAttrId(c.srcLogicalId, c.srcChan, false, false);
            connectedOutputAttrs.insert(srcAttr);

            int dstAttr = c.dstIsOutput ? 
                getAttrId(0, c.dstChan, true, false) : 
                getAttrId(c.dstLogicalId, c.dstChan, true, false);
            connectedInputAttrs.insert(dstAttr);
        }
    }
    // <<< END OF BLOCK >>>

    // <<< ADD THIS BLOCK TO DEFINE COLORS >>>
    const ImU32 colPin = IM_COL32(150, 150, 150, 255); // Grey for disconnected
    const ImU32 colPinConnected = IM_COL32(120, 255, 120, 255); // Green for connected
    // <<< END OF BLOCK >>>

    // Pre-register attr IDs for all endpoints referenced by connections, so links can draw regardless of draw order
    if (synth != nullptr)
    {
        for (const auto& c : synth->getConnectionsInfo())
        {
            (void) getAttrId(c.srcLogicalId, c.srcChan, false, false);
            if (c.dstIsOutput) (void) getAttrId(0, c.dstChan, true, false);
            else               (void) getAttrId(c.dstLogicalId, c.dstChan, true, false);
        }
    }

    // Node canvas bound to the underlying model if available
    ImNodes::BeginNodeEditor();
    // Begin the editor

    linkIdToAttrs.clear();

    // +++ ADD THIS LINE AT THE START OF THE RENDER LOOP +++
    attrPositions.clear(); // Clear the cache at the beginning of each frame.
    // Rebuild mod attribute mapping from currently drawn nodes only
    // modAttrToParam.clear(); // TODO: Remove when fully migrated
    // Track which attribute IDs were actually registered this frame
    std::unordered_set<int> availableAttrs;
    // Track duplicates to diagnose disappearing pins
    std::unordered_set<int> seenAttrs;
    auto linkIdOf = [this] (int srcAttr, int dstAttr) -> int
    {
        return getLinkId(srcAttr, dstAttr);
    };

    if (synth != nullptr)
    {
        // Apply any pending UI state restore (first frame after load)
        if (uiPending.isValid())
        {
            // Cache target positions to ensure they stick even if nodes are created later this frame
            auto nodes = uiPending;
for (int i = 0; i < nodes.getNumChildren(); ++i)
            {
                auto n = nodes.getChild(i);
if (! n.hasType("node")) continue;
                const int nid = (int) n.getProperty("id", 0);
                const float x = (float) n.getProperty("x", 0.0f);
const float y = (float) n.getProperty("y", 0.0f);
                pendingNodePositions[nid] = ImVec2(x, y);
}
            uiPending = {};
}

        // Draw module nodes (exactly once per logical module)
        std::unordered_set<int> drawnNodes;
        for (const auto& mod : synth->getModulesInfo())
        {
            const juce::uint32 lid = mod.first;
const juce::String& type = mod.second;

            // Highlight nodes participating in the hovered link
            const bool isHoveredSource = (hoveredLinkSrcId != 0 && hoveredLinkSrcId == (juce::uint32) lid);
            const bool isHoveredDest   = (hoveredLinkDstId != 0 && hoveredLinkDstId == (juce::uint32) lid);
            if (isHoveredSource || isHoveredDest)
                ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(255, 220, 0, 255));

            // Visual feedback for muted nodes
            const bool isMuted = mutedNodeStates.count(lid) > 0;
            if (isMuted) {
                ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(80, 80, 80, 255));
            }

            ImNodes::BeginNode ((int) lid);
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted (type.toRawUTF8());
            ImNodes::EndNodeTitleBar();

            if (isHoveredSource || isHoveredDest)
                ImNodes::PopColorStyle();

            // Constrain node content width for compact layout and predictable label placement
            const float nodeContentWidth = 240.0f;

            // Inline parameter controls per module type
            if (synth != nullptr)
            {
if (auto* mp = synth->getModuleForLogical (lid))
{
    ImGui::PushID ((int) lid);

    // This new lambda function checks if a parameter is being modulated
    auto isParamModulated = [&](const juce::String& paramId) -> bool {
        if (!synth) return false;
        if (auto* mp = synth->getModuleForLogical(lid))
        {
            int busIdx = -1, chInBus = -1;
            // Use the new standardized routing API on the module itself
            if (!mp->getParamRouting(paramId, busIdx, chInBus)) 
                return false;

            // Calculate the absolute channel index that the graph uses for this bus/channel pair
            const int absoluteChannelIndex = mp->getChannelIndexInProcessBlockBuffer(true, busIdx, chInBus);
            if (absoluteChannelIndex < 0) return false;
            
            // Scan the simple graph connections for a match
            for (const auto& c : synth->getConnectionsInfo())
            {
                if (c.dstLogicalId == lid && c.dstChan == absoluteChannelIndex)
                    return true;
            }
        }
        return false;
    };

    // Helper to read a live, modulated value if available (respects _mod alias)
    auto getLiveValueOr = [&](const juce::String& paramId, float fallback) -> float
    {
        if (!synth) return fallback;
        if (auto* mp = synth->getModuleForLogical(lid))
            return mp->getLiveParamValueFor(paramId + "_mod", paramId + "_live", fallback);
        return fallback;
    };

    // Create a new function that calls pushSnapshot
    auto onModificationEnded = [&](){ this->pushSnapshot(); };

    // --- SPECIAL RENDERING FOR SAMPLE LOADER ---
    if (auto* sampleLoader = dynamic_cast<SampleLoaderModuleProcessor*>(mp))
    {
        // First, draw the standard parameters (buttons, sliders, etc.)
        // We pass a modified onModificationEnded to avoid creating undo states while dragging.
        sampleLoader->drawParametersInNode(nodeContentWidth, isParamModulated, onModificationEnded);

        // Now, handle the spectrogram texture and drawing
        juce::OpenGLTexture* texturePtr = nullptr;
        if (auto it = sampleLoaderTextureIds.find((int)lid); it != sampleLoaderTextureIds.end())
            texturePtr = it->second.get();

        juce::Image spectrogram = sampleLoader->getSpectrogramImage();
        if (spectrogram.isValid())
        {
            if (texturePtr == nullptr)
            {
                auto tex = std::make_unique<juce::OpenGLTexture>();
                texturePtr = tex.get();
                sampleLoaderTextureIds[(int)lid] = std::move(tex);
            }
            // Upload or update texture from JUCE image (handles format & parameters internally)
            texturePtr->loadImage(spectrogram);

            ImGui::Image((void*)(intptr_t) texturePtr->getTextureID(), ImVec2(nodeContentWidth, 100.0f));

            // Drag state is tracked per Sample Loader node to avoid cross-node interference
            static std::unordered_map<int,int> draggedHandleByNode; // lid -> -1,0,1
            int& draggedHandle = draggedHandleByNode[(int) lid];
            if (draggedHandle != 0 && draggedHandle != 1) draggedHandle = -1;
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
            ImGui::InvisibleButton("##spectrogram_interaction", ImVec2(nodeContentWidth, 100.0f));

            auto* drawList = ImGui::GetWindowDrawList();
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();

            float startNorm = sampleLoader->getAPVTS().getRawParameterValue("rangeStart")->load();
            float endNorm = sampleLoader->getAPVTS().getRawParameterValue("rangeEnd")->load();

            // Use live telemetry values when modulated
            startNorm = sampleLoader->getLiveParamValueFor("rangeStart_mod", "rangeStart_live", startNorm);
            endNorm = sampleLoader->getLiveParamValueFor("rangeEnd_mod", "rangeEnd_live", endNorm);

            // Visual guard even when modulated
            const float kMinGap = 0.001f;
            startNorm = juce::jlimit(0.0f, 1.0f, startNorm);
            endNorm   = juce::jlimit(0.0f, 1.0f, endNorm);
            if (startNorm >= endNorm)
            {
                if (startNorm <= 1.0f - kMinGap)
                    endNorm = juce::jmin(1.0f, startNorm + kMinGap);
                else
                    startNorm = juce::jmax(0.0f, endNorm - kMinGap);
            }

            // --- FIX FOR BUG 1: Separate modulation checks for each handle ---
            bool startIsModulated = isParamModulated("rangeStart_mod");
            bool endIsModulated = isParamModulated("rangeEnd_mod");

            const bool itemHovered = ImGui::IsItemHovered();
            const bool itemActive  = ImGui::IsItemActive();
            if (itemHovered)
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                float startHandleX = rectMin.x + startNorm * nodeContentWidth;
                float endHandleX = rectMin.x + endNorm * nodeContentWidth;

                bool canDragStart = !startIsModulated && (std::abs(mousePos.x - startHandleX) < 5);
                bool canDragEnd = !endIsModulated && (std::abs(mousePos.x - endHandleX) < 5);

                if (draggedHandle == -1 && (canDragStart || canDragEnd))
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }

                if (ImGui::IsItemClicked())
                {
                    // Only allow dragging to start if the corresponding handle is not modulated
                    if (canDragStart && canDragEnd)
                        draggedHandle = (std::abs(mousePos.x - startHandleX) < std::abs(mousePos.x - endHandleX)) ? 0 : 1;
                    else if (canDragStart)
                        draggedHandle = 0;
                    else if (canDragEnd)
                        draggedHandle = 1;
                }
            }

            if (itemActive && ImGui::IsMouseReleased(0))
            {
                if (draggedHandle != -1) onModificationEnded();
                draggedHandle = -1;
            }

            // Handle the drag update, checking the specific modulation flag for the active handle
            if (itemActive && draggedHandle != -1 && ImGui::IsMouseDragging(0))
            {
                float newNormX = juce::jlimit(0.0f, 1.0f, (ImGui::GetMousePos().x - rectMin.x) / nodeContentWidth);
                if (draggedHandle == 0 && !startIsModulated)
                {
                    // Guard: start cannot be >= end
                    startNorm = juce::jmin(newNormX, endNorm - 0.001f);
                    sampleLoader->getAPVTS().getParameter("rangeStart")->setValueNotifyingHost(startNorm);
                }
                else if (draggedHandle == 1 && !endIsModulated)
                {
                    // Guard: end cannot be <= start
                    endNorm = juce::jmax(newNormX, startNorm + 0.001f);
                    sampleLoader->getAPVTS().getParameter("rangeEnd")->setValueNotifyingHost(endNorm);
                }
            }

            float startX = rectMin.x + startNorm * nodeContentWidth;
            float endX = rectMin.x + endNorm * nodeContentWidth;
            drawList->AddRectFilled(rectMin, ImVec2(startX, rectMax.y), IM_COL32(0, 0, 0, 120));
            drawList->AddRectFilled(ImVec2(endX, rectMin.y), rectMax, IM_COL32(0, 0, 0, 120));
            drawList->AddLine(ImVec2(startX, rectMin.y), ImVec2(startX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);
            drawList->AddLine(ImVec2(endX, rectMin.y), ImVec2(endX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);
        }
    }
    // --- SPECIAL RENDERING FOR AUDIO INPUT (MULTI-CHANNEL) ---
    else if (auto* audioIn = dynamic_cast<AudioInputModuleProcessor*>(mp))
    {
        auto& apvts = audioIn->getAPVTS();

        // --- Device Selectors ---
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        
        // Input Device
        juce::StringArray availableInputDevices;
        if (auto* deviceType = deviceManager.getAvailableDeviceTypes().getFirst()) {
            availableInputDevices = deviceType->getDeviceNames(true);
        }
        std::vector<const char*> inputDeviceItems;
        for (const auto& name : availableInputDevices) inputDeviceItems.push_back(name.toRawUTF8());
        int currentInputDeviceIndex = availableInputDevices.indexOf(setup.inputDeviceName);
        if (currentInputDeviceIndex < 0) currentInputDeviceIndex = 0;

        ImGui::PushItemWidth(nodeContentWidth);
        if (ImGui::Combo("Input Device", &currentInputDeviceIndex, inputDeviceItems.data(), (int)inputDeviceItems.size())) {
            if (currentInputDeviceIndex < availableInputDevices.size()) {
                setup.inputDeviceName = availableInputDevices[currentInputDeviceIndex];
                deviceManager.setAudioDeviceSetup(setup, true);
                onModificationEnded();
            }
        }

        // Output Device
        juce::StringArray availableOutputDevices;
        if (auto* deviceType = deviceManager.getAvailableDeviceTypes().getFirst()) {
            availableOutputDevices = deviceType->getDeviceNames(false);
        }
        std::vector<const char*> outputDeviceItems;
        for (const auto& name : availableOutputDevices) outputDeviceItems.push_back(name.toRawUTF8());
        int currentOutputDeviceIndex = availableOutputDevices.indexOf(setup.outputDeviceName);
        if (currentOutputDeviceIndex < 0) currentOutputDeviceIndex = 0;
        
        if (ImGui::Combo("Output Device", &currentOutputDeviceIndex, outputDeviceItems.data(), (int)outputDeviceItems.size())) {
            if (currentOutputDeviceIndex < availableOutputDevices.size()) {
                setup.outputDeviceName = availableOutputDevices[currentOutputDeviceIndex];
                deviceManager.setAudioDeviceSetup(setup, true);
                onModificationEnded();
            }
        }
        
        // --- Channel Count ---
        auto* numChannelsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("numChannels"));
        int numChannels = numChannelsParam->get();
        if (ImGui::SliderInt("Channels", &numChannels, 1, AudioInputModuleProcessor::MAX_CHANNELS)) {
            *numChannelsParam = numChannels;
            onModificationEnded();
        }
        
        // --- Threshold Sliders ---
        auto* gateThreshParam = static_cast<juce::AudioParameterFloat*>(apvts.getParameter("gateThreshold"));
        float gateThresh = gateThreshParam->get();
        if (ImGui::SliderFloat("Gate Threshold", &gateThresh, 0.0f, 1.0f, "%.3f")) {
            *gateThreshParam = gateThresh;
            onModificationEnded();
        }
        
        auto* trigThreshParam = static_cast<juce::AudioParameterFloat*>(apvts.getParameter("triggerThreshold"));
        float trigThresh = trigThreshParam->get();
        if (ImGui::SliderFloat("Trigger Threshold", &trigThresh, 0.0f, 1.0f, "%.3f")) {
            *trigThreshParam = trigThresh;
            onModificationEnded();
        }
        
        ImGui::PopItemWidth();

        // --- Dynamic Channel Selectors & VU Meters ---
        auto hardwareChannels = deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getInputChannelNames() : juce::StringArray{};
        if (!hardwareChannels.isEmpty())
        {
            std::vector<const char*> hwChannelItems;
            for (const auto& name : hardwareChannels) hwChannelItems.push_back(name.toRawUTF8());
            
            for (int i = 0; i < numChannels; ++i) {
                auto* mappingParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("channelMap" + juce::String(i)));
                int selectedHwChannel = mappingParam->get();
                selectedHwChannel = juce::jlimit(0, (int)hwChannelItems.size() - 1, selectedHwChannel);

                ImGui::PushID(i);
                ImGui::PushItemWidth(nodeContentWidth * 0.6f);
                if (ImGui::Combo(("Input for Out " + juce::String(i + 1)).toRawUTF8(), &selectedHwChannel, hwChannelItems.data(), (int)hwChannelItems.size())) {
                    *mappingParam = selectedHwChannel;
                    std::vector<int> newMapping(numChannels);
                    for (int j = 0; j < numChannels; ++j) {
                        auto* p = static_cast<juce::AudioParameterInt*>(apvts.getParameter("channelMap" + juce::String(j)));
                        newMapping[j] = p->get();
                    }
                    synth->setAudioInputChannelMapping(synth->getNodeIdForLogical(lid), newMapping);
                    onModificationEnded();
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                
                // --- VU Meter with Threshold Lines ---
                float level = (i < (int)audioIn->channelLevels.size() && audioIn->channelLevels[i]) ? audioIn->channelLevels[i]->load() : 0.0f;
                ImVec2 meterSize(nodeContentWidth * 0.38f, ImGui::GetTextLineHeightWithSpacing() * 0.8f);
                ImGui::ProgressBar(level, meterSize, "");

                // Draw threshold lines on top of the progress bar
                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Gate Threshold (Yellow)
                float gateLineX = p_min.x + gateThresh * (p_max.x - p_min.x);
                draw_list->AddLine(ImVec2(gateLineX, p_min.y), ImVec2(gateLineX, p_max.y), IM_COL32(255, 255, 0, 200), 2.0f);

                // Trigger Threshold (Orange)
                float trigLineX = p_min.x + trigThresh * (p_max.x - p_min.x);
                draw_list->AddLine(ImVec2(trigLineX, p_min.y), ImVec2(trigLineX, p_max.y), IM_COL32(255, 165, 0, 200), 2.0f);
                
                ImGui::PopID();
            }
        }
    }
    else
    {
        mp->drawParametersInNode (nodeContentWidth, isParamModulated, onModificationEnded);
    }
    ImGui::Spacing();
    ImGui::PopID();
}
            }

            // IO per module type via helpers
            NodePinHelpers helpers;
                auto rightLabelWithinWidth = [&](const char* txt)
                {
                    const float startX = ImGui::GetCursorPosX();
                    const ImVec2 ts = ImGui::CalcTextSize(txt);
                    float x = startX + juce::jmax (0.0f, nodeContentWidth - ts.x - 8.0f);
                    ImGui::SetCursorPosX(x);
                    ImGui::TextUnformatted(txt);
                };
            helpers.drawAudioInputPin = [&](const char* label, int channel)
            {
                int attr = getAttrId((juce::uint32)lid, channel, true, false);
                seenAttrs.insert(attr); // Always insert, no warning needed - getAttrId is designed to return consistent IDs
                availableAttrs.insert(attr);

                // Get pin data type for color coding
                PinID pinId = { lid, channel, true, false, "" };
                PinDataType pinType = this->getPinDataTypeForPin(pinId);
                unsigned int pinColor = this->getImU32ForType(pinType);

                bool isConnected = connectedInputAttrs.count(attr) > 0;
                ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);

                ImNodes::BeginInputAttribute(attr); ImGui::TextUnformatted(label); ImNodes::EndInputAttribute();

                // +++ ADD THIS BLOCK TO CACHE THE PIN'S POSITION +++
                // Calculate the center of the pin's visual representation.
                ImVec2 pinMin = ImGui::GetItemRectMin();
                ImVec2 pinMax = ImGui::GetItemRectMax();
                attrPositions[attr] = ImVec2(pinMin.x, pinMin.y + (pinMax.y - pinMin.y) * 0.5f);
                // +++ END OF BLOCK +++

                ImNodes::PopColorStyle(); // Restore default color

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (isConnected) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                        // Find which output this input is connected to and show source info
                        for (const auto& c : synth->getConnectionsInfo())
                        {
                            bool isConnectedToThisPin = (!c.dstIsOutput && c.dstLogicalId == lid && c.dstChan == channel) || (c.dstIsOutput && lid == 0 && c.dstChan == channel);
                            if (isConnectedToThisPin)
                            {
                                if (auto* srcMod = synth->getModuleForLogical(c.srcLogicalId))
                                {
                                    float value = srcMod->getOutputChannelValue(c.srcChan);
                                    ImGui::Text("From %u:%d", c.srcLogicalId, c.srcChan);
                                    ImGui::Text("Value: %.3f", value);
                                }
                                break; 
                            }
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                    }
                    // Show pin data type
                    ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                    ImGui::EndTooltip();
                }
            };
            helpers.drawAudioOutputPin = [&](const char* label, int channel)
            {
                int attr = getAttrId((juce::uint32)lid, channel, false, false);
                seenAttrs.insert(attr); // Always insert, no warning needed - getAttrId is designed to return consistent IDs
                availableAttrs.insert(attr);

                PinID pinId = { lid, channel, false, false, "" };
                PinDataType pinType = this->getPinDataTypeForPin(pinId);
                unsigned int pinColor = this->getImU32ForType(pinType);
                bool isConnected = connectedOutputAttrs.count(attr) > 0;

                // CORRECTED LOGIC: Use colPinConnected for any connected output pin.
                ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
                ImNodes::BeginOutputAttribute(attr); rightLabelWithinWidth(label); ImNodes::EndOutputAttribute();

                // +++ ADD THIS BLOCK TO CACHE THE PIN'S POSITION +++
                // Calculate the center of the pin's visual representation.
                ImVec2 pinMin = ImGui::GetItemRectMin();
                ImVec2 pinMax = ImGui::GetItemRectMax();
                attrPositions[attr] = ImVec2(pinMax.x, pinMin.y + (pinMax.y - pinMin.y) * 0.5f);
                // +++ END OF BLOCK +++

                ImNodes::PopColorStyle();

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (isConnected) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                    }
                    // Show pin data type
                    ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                    // Show the existing value tooltip
                    if (auto* mp = synth->getModuleForLogical(lid))
                    {
                        float value = mp->getOutputChannelValue(channel);
                        ImGui::Text("Value: %.3f", value);
                    }
                    ImGui::EndTooltip();
                }
            };

            // ADD THE NEW drawParallelPins HELPER
            helpers.drawParallelPins = [&](const char* inLabel, int inChannel, const char* outLabel, int outChannel)
            {
                const float nodeContentWidth = 240.0f; // A fixed width for consistent layout

                // Draw Input Pin (Left Side) - only if inLabel is provided
                if (inLabel != nullptr)
                {
                    int attr = getAttrId((juce::uint32)lid, inChannel, true, false);
                    seenAttrs.insert(attr);
                    availableAttrs.insert(attr);

                    PinID pinId = { lid, inChannel, true, false, "" };
                    PinDataType pinType = this->getPinDataTypeForPin(pinId);
                    unsigned int pinColor = this->getImU32ForType(pinType);
                    bool isConnected = connectedInputAttrs.count(attr) > 0;

                    ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
                    ImNodes::BeginInputAttribute(attr);
                    ImGui::TextUnformatted(inLabel);
                    ImNodes::EndInputAttribute();

                    // +++ ADD THIS BLOCK TO CACHE THE PIN'S POSITION +++
                    // Calculate the center of the pin's visual representation.
                    ImVec2 pinMin = ImGui::GetItemRectMin();
                    ImVec2 pinMax = ImGui::GetItemRectMax();
                    attrPositions[attr] = ImVec2(pinMin.x, pinMin.y + (pinMax.y - pinMin.y) * 0.5f);
                    // +++ END OF BLOCK +++

                    ImNodes::PopColorStyle();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (isConnected) {
                            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                            
                            // Display the actual value from the connected source
                            for (const auto& c : synth->getConnectionsInfo())
                            {
                                if (!c.dstIsOutput && c.dstLogicalId == (juce::uint32)lid && c.dstChan == inChannel)
                                {
                                    if (auto* srcMod = synth->getModuleForLogical(c.srcLogicalId))
                                    {
                                        float value = srcMod->getOutputChannelValue(c.srcChan);
                                        ImGui::Text("From: %s (ID %u)", srcMod->getName().toRawUTF8(), (unsigned)c.srcLogicalId);
                                        ImGui::Text("Value: %.3f", value);
                                    }
                                    break;
                                }
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                        }
                        ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                        ImGui::EndTooltip();
                    }
                }

                // Draw Output Pin (Right Side) - only if outLabel is provided
                if (outLabel != nullptr)
                {
                    int attr = getAttrId((juce::uint32)lid, outChannel, false, false);
                    seenAttrs.insert(attr);
                    availableAttrs.insert(attr);

                    PinID pinId = { lid, outChannel, false, false, "" };
                    PinDataType pinType = this->getPinDataTypeForPin(pinId);
                    unsigned int pinColor = this->getImU32ForType(pinType);
                    bool isConnected = connectedOutputAttrs.count(attr) > 0;

                    // Position cursor to the right on the same line
                    float labelWidth = ImGui::CalcTextSize(outLabel).x;
                    ImGui::SameLine(nodeContentWidth - labelWidth);

                    ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
                    ImNodes::BeginOutputAttribute(attr);
                    ImGui::TextUnformatted(outLabel);
                    ImNodes::EndOutputAttribute();

                    // +++ ADD THIS BLOCK TO CACHE THE PIN'S POSITION +++
                    // Calculate the center of the pin's visual representation.
                    ImVec2 pinMin = ImGui::GetItemRectMin();
                    ImVec2 pinMax = ImGui::GetItemRectMax();
                    attrPositions[attr] = ImVec2(pinMax.x, pinMin.y + (pinMax.y - pinMin.y) * 0.5f);
                    // +++ END OF BLOCK +++

                    ImNodes::PopColorStyle();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (isConnected) {
                            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                        } else {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                        }
                        ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                        if (auto* mp = synth->getModuleForLogical(lid))
                        {
                            float value = mp->getOutputChannelValue(outChannel);
                            ImGui::Text("Value: %.3f", value);
                        }
                        ImGui::EndTooltip();
                    }
                }
                
                // --- THE FIX ---
                // Add a dummy item to advance the cursor to the next line.
                // This was missing, causing all rows to draw on top of each other.
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            };

            // Delegate per-module IO pin drawing
            if (synth != nullptr)
                if (auto* mp = synth->getModuleForLogical (lid))
                    mp->drawIoPins(helpers);

            // Optional per-node right-click popup
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                selectedLogicalId = (int) lid;
                ImGui::OpenPopup("NodeActionPopup");
            }

            // Legacy per-type IO drawing removed; delegated to module implementations via helpers

            ImNodes::EndNode();
            
            // Pop muted node styles
            if (isMuted) {
                ImNodes::PopColorStyle();
                ImGui::PopStyleVar();
                ImNodes::PopStyleVar();
            }
            
            // Apply pending placement if queued
            if (auto itS = pendingNodeScreenPositions.find((int) lid); itS != pendingNodeScreenPositions.end())
            {
                ImNodes::SetNodeScreenSpacePos((int) lid, itS->second);
                pendingNodeScreenPositions.erase(itS);
            }
            if (auto it = pendingNodePositions.find((int) lid); it != pendingNodePositions.end())
            {
                ImNodes::SetNodeGridSpacePos((int) lid, it->second);
                pendingNodePositions.erase(it);
            }
            drawnNodes.insert((int) lid);
        }

        // Node action popup (Delete / Duplicate)
        bool triggerInsertMixer = false;
        if (ImGui::BeginPopup("NodeActionPopup"))
        {
            if (ImGui::MenuItem("Delete") && selectedLogicalId != 0)
            {
                mutedNodeStates.erase((juce::uint32)selectedLogicalId); // Clean up muted state if exists
                synth->removeModule (synth->getNodeIdForLogical ((juce::uint32) selectedLogicalId));
                graphNeedsRebuild = true;
                // Post-state snapshot
                pushSnapshot();
                selectedLogicalId = 0;
            }
            if (ImGui::MenuItem("Duplicate") && selectedLogicalId != 0)
            {
                const juce::String type = getTypeForLogical ((juce::uint32) selectedLogicalId);
                if (! type.isEmpty())
                {
                    auto newNodeId = synth->addModule (type);
                    graphNeedsRebuild = true;
                    if (auto* src = synth->getModuleForLogical ((juce::uint32) selectedLogicalId))
                        if (auto* dst = synth->getModuleForLogical (synth->getLogicalIdForNode(newNodeId)))
                            dst->getAPVTS().replaceState (src->getAPVTS().copyState());
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos (selectedLogicalId);
                    ImNodes::SetNodeGridSpacePos ((int) synth->getLogicalIdForNode(newNodeId), ImVec2 (pos.x + 40.0f, pos.y + 40.0f));
                    // Post-state snapshot after duplication and position
                    pushSnapshot();
                }
            }
            if (ImGui::MenuItem("Insert Mixer", "Ctrl+T") && selectedLogicalId != 0) { triggerInsertMixer = true; }
            ImGui::EndPopup();
        }

        // Shortcut: Ctrl+T to insert a Mixer after selected node and reroute
        // Debounced Ctrl+T
        const bool ctrlDown = ImGui::GetIO().KeyCtrl;
        if (!ctrlDown) {
            mixerShortcutCooldown = false;
            insertNodeShortcutCooldown = false;
        }
        if ((triggerInsertMixer || (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_T))) && !mixerShortcutCooldown)
        {
            mixerShortcutCooldown = true; // Prevent re-triggering in the same frame
            const juce::uint32 srcLid = (juce::uint32) selectedLogicalId;

            juce::Logger::writeToLog("--- [InsertMixer] Start ---");
            juce::Logger::writeToLog("[InsertMixer] Selected Node Logical ID: " + juce::String(srcLid));

            auto srcNodeId = synth->getNodeIdForLogical(srcLid);
            if (srcNodeId.uid == 0) 
            {
                juce::Logger::writeToLog("[InsertMixer] ABORT: Source node with logical ID " + juce::String(srcLid) + " is invalid or could not be found.");
            } 
            else 
            {
                // 1. Collect all outgoing connections from the selected node
                std::vector<ModularSynthProcessor::ConnectionInfo> outgoingConnections;
                for (const auto& c : synth->getConnectionsInfo()) {
                    if (c.srcLogicalId == srcLid) {
                        outgoingConnections.push_back(c);
                    }
                }
                juce::Logger::writeToLog("[InsertMixer] Found " + juce::String(outgoingConnections.size()) + " outgoing connections to reroute.");
                for (const auto& c : outgoingConnections) {
                    juce::String destStr = c.dstIsOutput ? "Main Output" : "Node " + juce::String(c.dstLogicalId);
                    juce::Logger::writeToLog("  - Stored connection: [Src: " + juce::String(c.srcLogicalId) + ":" + juce::String(c.srcChan) + "] -> [Dst: " + destStr + ":" + juce::String(c.dstChan) + "]");
                }

                // 2. Create and position the new mixer node
                auto mixNodeIdGraph = synth->addModule("Mixer");
                const juce::uint32 mixLid = synth->getLogicalIdForNode(mixNodeIdGraph);
                ImVec2 pos = ImNodes::GetNodeGridSpacePos(selectedLogicalId);
                pendingNodePositions[(int)mixLid] = ImVec2(pos.x + 300.0f, pos.y);
                juce::Logger::writeToLog("[InsertMixer] Added new Mixer. Logical ID: " + juce::String(mixLid) + ", Node ID: " + juce::String(mixNodeIdGraph.uid));

                // 3. Disconnect all original outgoing links
                juce::Logger::writeToLog("[InsertMixer] Disconnecting original links...");
                for (const auto& c : outgoingConnections) {
                    auto currentSrcNodeId = synth->getNodeIdForLogical(c.srcLogicalId);
                    auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);

                    if (currentSrcNodeId.uid != 0 && dstNodeId.uid != 0) {
                        bool success = synth->disconnect(currentSrcNodeId, c.srcChan, dstNodeId, c.dstChan);
                        juce::Logger::writeToLog("  - Disconnecting [" + juce::String(currentSrcNodeId.uid) + ":" + juce::String(c.srcChan) + "] -> [" + juce::String(dstNodeId.uid) + ":" + juce::String(c.dstChan) + "]... " + (success ? "SUCCESS" : "FAILED"));
                    } else {
                        juce::Logger::writeToLog("  - SKIPPING Disconnect due to invalid node ID.");
                    }
                }

                // 4. Connect the source node to the new mixer's first input
                juce::Logger::writeToLog("[InsertMixer] Connecting source node to new mixer...");
                bool c1 = synth->connect(srcNodeId, 0, mixNodeIdGraph, 0); // L to In A L
                juce::Logger::writeToLog("  - Connecting [" + juce::String(srcNodeId.uid) + ":0] -> [" + juce::String(mixNodeIdGraph.uid) + ":0]... " + (c1 ? "SUCCESS" : "FAILED"));
                bool c2 = synth->connect(srcNodeId, 1, mixNodeIdGraph, 1); // R to In A R
                juce::Logger::writeToLog("  - Connecting [" + juce::String(srcNodeId.uid) + ":1] -> [" + juce::String(mixNodeIdGraph.uid) + ":1]... " + (c2 ? "SUCCESS" : "FAILED"));


                // 5. Connect the mixer's output to all the original destinations (maintaining the chain)
                juce::Logger::writeToLog("[InsertMixer] Connecting mixer to original destinations to maintain chain...");
                if (outgoingConnections.empty()) {
                    juce::Logger::writeToLog("  - No original outgoing connections. Connecting mixer to Main Output by default.");
                    auto outNode = synth->getOutputNodeID();
                    if (outNode.uid != 0) {
                        bool o1 = synth->connect(mixNodeIdGraph, 0, outNode, 0);
                        juce::Logger::writeToLog("  - Connecting [" + juce::String(mixNodeIdGraph.uid) + ":0] -> [Output:0]... " + (o1 ? "SUCCESS" : "FAILED"));
                        bool o2 = synth->connect(mixNodeIdGraph, 1, outNode, 1);
                        juce::Logger::writeToLog("  - Connecting [" + juce::String(mixNodeIdGraph.uid) + ":1] -> [Output:1]... " + (o2 ? "SUCCESS" : "FAILED"));
                    }
                } else {
                    for (const auto& c : outgoingConnections) {
                        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
                        if (dstNodeId.uid != 0) {
                            // Connect mixer output to the same destination the original node was connected to
                            // This maintains the chain: original -> mixer -> destination
                            bool success = synth->connect(mixNodeIdGraph, c.srcChan, dstNodeId, c.dstChan);
                            juce::String destStr = c.dstIsOutput ? "Main Output" : "Node " + juce::String(c.dstLogicalId);
                            juce::Logger::writeToLog("  - Maintaining chain: Mixer [" + juce::String(mixNodeIdGraph.uid) + ":" + juce::String(c.srcChan) + "] -> " + destStr + "[" + juce::String(dstNodeId.uid) + ":" + juce::String(c.dstChan) + "]... " + (success ? "SUCCESS" : "FAILED"));
                        } else {
                            juce::Logger::writeToLog("  - SKIPPING Reconnect due to invalid destination node ID for original logical ID " + juce::String(c.dstLogicalId));
                        }
                    }
                }

                graphNeedsRebuild = true;
                pushSnapshot(); // Make the entire operation undoable
                juce::Logger::writeToLog("[InsertMixer] Rerouting complete. Flagging for graph rebuild.");
            }
            juce::Logger::writeToLog("--- [InsertMixer] End ---");
        }

        // Shortcut: Ctrl+I to show Insert Node popup menu
        if (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_I) && !insertNodeShortcutCooldown)
        {
            insertNodeShortcutCooldown = true;
            showInsertNodePopup = true;
        }

        // Insert Node popup menu
        if (showInsertNodePopup)
        {
            ImGui::OpenPopup("InsertNodePopup");
            showInsertNodePopup = false;
        }

        if (ImGui::BeginPopup("InsertNodePopup"))
        {
            ImGui::Text("Insert Node Between Connections");
            
            // Audio Path
            if (ImGui::MenuItem("VCF")) { insertNodeBetween("VCF"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("VCA")) { insertNodeBetween("VCA"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Delay")) { insertNodeBetween("Delay"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Reverb")) { insertNodeBetween("Reverb"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Mixer")) { insertNodeBetween("Mixer"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Recorder")) { insertNodeBetween("recorder"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Shaping Oscillator")) { insertNodeBetween("shaping oscillator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("8-Band Shaper")) { insertNodeBetween("8bandshaper"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Granulator")) { insertNodeBetween("Granulator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Harmonic Shaper")) { insertNodeBetween("harmonic shaper"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Vocal Tract Filter")) { insertNodeBetween("Vocal Tract Filter"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Scope")) { insertNodeBetween("Scope"); ImGui::CloseCurrentPopup(); }
            
            ImGui::Separator();
            
            // Modulation Path
            if (ImGui::MenuItem("Attenuverter")) { insertNodeBetween("Attenuverter"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Math")) { insertNodeBetween("Math"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Comparator")) { insertNodeBetween("Comparator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("CV Mixer")) { insertNodeBetween("CV Mixer"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Sequential Switch")) { insertNodeBetween("Sequential Switch"); ImGui::CloseCurrentPopup(); }
            
            ImGui::EndPopup();
        }

        // Output sink node with stereo inputs (single, fixed ID 0)
        const bool isOutputHovered = (hoveredLinkDstId == kOutputHighlightId);
        if (isOutputHovered)
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(255, 220, 0, 255));
        ImNodes::BeginNode (0);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted ("Output");
        ImNodes::EndNodeTitleBar();
        if (isOutputHovered)
            ImNodes::PopColorStyle();
        { int a = getAttrId(0, 0, true, false); seenAttrs.insert(a); availableAttrs.insert(a); ImNodes::BeginInputAttribute (a);
        ImGui::Text ("In L");
        ImNodes::EndInputAttribute(); }
        { int a = getAttrId(0, 1, true, false); seenAttrs.insert(a); availableAttrs.insert(a); ImNodes::BeginInputAttribute (a);
        ImGui::Text ("In R");
        ImNodes::EndInputAttribute(); }
        ImNodes::EndNode();
        if (auto it = pendingNodePositions.find(0); it != pendingNodePositions.end())
        {
            ImNodes::SetNodeGridSpacePos(0, it->second);
            pendingNodePositions.erase(it);
        }
        drawnNodes.insert(0);

        // Use last frame's hovered node id for highlighting (queried after EndNodeEditor)
        int hoveredNodeId = lastHoveredNodeId;

        // Draw existing audio connections (IDs stable via registry)
        for (const auto& c : synth->getConnectionsInfo())
        {
            // Skip links whose nodes weren't drawn this frame (e.g., just deleted)
            if (c.srcLogicalId != 0 && ! drawnNodes.count((int) c.srcLogicalId)) continue;
            if (! c.dstIsOutput && c.dstLogicalId != 0 && ! drawnNodes.count((int) c.dstLogicalId)) continue;
            const int srcAttr = getAttrId(c.srcLogicalId, c.srcChan, false, false);
            const int dstAttr = c.dstIsOutput ? getAttrId(0, c.dstChan, true, false) : getAttrId(c.dstLogicalId, c.dstChan, true, false);
            if (! availableAttrs.count(srcAttr) || ! availableAttrs.count(dstAttr))
            {
                static std::unordered_set<std::string> skipOnce;
                const std::string key = std::to_string((int)c.srcLogicalId) + ":" + std::to_string(c.srcChan) + "->" +
                                         (c.dstIsOutput? std::string("0") : std::to_string((int)c.dstLogicalId)) + ":" + std::to_string(c.dstChan);
                if (skipOnce.insert(key).second)
                {
                    juce::Logger::writeToLog(
                        juce::String("[ImNodes][SKIP] missing attr: srcPresent=") + (availableAttrs.count(srcAttr)?"1":"0") +
                        " dstPresent=" + (availableAttrs.count(dstAttr)?"1":"0") +
                        " srcKey=(lid=" + juce::String((int)c.srcLogicalId) + ",ch=" + juce::String(c.srcChan) + ")" +
                        " dstKey=(lid=" + juce::String(c.dstIsOutput?0:(int)c.dstLogicalId) + ",ch=" + juce::String(c.dstChan) + ",in=1) id(s)=" +
                        juce::String(srcAttr) + "," + juce::String(dstAttr));
                }
                continue;
            }
            const int linkId = linkIdOf(srcAttr, dstAttr);
            linkIdToAttrs[linkId] = { srcAttr, dstAttr };
            
            // Determine the data type of the source pin for color coding
            auto srcPin = decodeAttr(srcAttr);
            PinDataType linkDataType = getPinDataTypeForPin(srcPin);
            ImU32 linkColor = getImU32ForType(linkDataType);
            
            // Push color styles for the link
            ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, IM_COL32(255, 255, 0, 255)); // Yellow on hover
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 255, 0, 255)); // Yellow when selected
            
            // Check if this link should be highlighted (node hover)
            const bool hl = (hoveredNodeId != -1) && ((int) c.srcLogicalId == hoveredNodeId || (! c.dstIsOutput && (int) c.dstLogicalId == hoveredNodeId) || (c.dstIsOutput && hoveredNodeId == 0));
            if (hl)
            {
                // Override with yellow for node highlighting
                ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(255, 255, 0, 255));
            }
            
            ImNodes::Link(linkId, srcAttr, dstAttr);
            
            // Pop all color styles
            if (hl)
            {
                ImNodes::PopColorStyle(); // Pop highlight override
            }
            ImNodes::PopColorStyle(); // Pop LinkSelected
            ImNodes::PopColorStyle(); // Pop LinkHovered
            ImNodes::PopColorStyle(); // Pop main link color
        }

        // Drag detection for node movement: snapshot once on mouse release (post-state)
        const bool hoveringNode = (lastHoveredNodeId != -1);
        if (hoveringNode && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            isDraggingNode = true;
        }
        if (isDraggingNode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingNode = false;
            // Capture positions after a move so subsequent operations (e.g. delete) undo to the moved location
            pushSnapshot();
        }
    }

    // --- Handle Auto-Connect Requests from MIDI Players ---
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(synth->getModuleForLogical(modInfo.first)))
        {
            if (midiPlayer->autoConnectTriggered.exchange(false)) // Check and reset the flag atomically
            {
                handleMidiPlayerAutoConnect(midiPlayer, modInfo.first);
                pushSnapshot(); // Make the entire operation undoable
            }
            
            if (midiPlayer->autoConnectVCOTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectVCO(midiPlayer, modInfo.first);
                pushSnapshot(); // Make the entire operation undoable
            }
            
            if (midiPlayer->autoConnectHybridTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectHybrid(midiPlayer, modInfo.first);
                pushSnapshot(); // Make the entire operation undoable
            }
        }
    }
    
    // --- Handle Auto-Connect Requests using new intelligent system ---
    handleAutoConnectionRequests();

    ImNodes::MiniMap (0.2f, ImNodesMiniMapLocation_BottomRight);

    ImNodes::EndNodeEditor();

    // --- CONSOLIDATED HOVERED LINK DETECTION ---
    // Declare these variables ONCE, immediately after the editor has ended.
    // All subsequent features that need to know about hovered links can now
    // safely reuse these results without causing redefinition or scope errors.
    int hoveredLinkId = -1;
    bool isLinkHovered = ImNodes::IsLinkHovered(&hoveredLinkId);
    // --- END OF CONSOLIDATED DECLARATION ---

    // --- CONTEXTUAL RIGHT-CLICK HANDLER ---
    // A cable was right-clicked. Store its info and open the insert popup.
    if (isLinkHovered && hoveredLinkId != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        juce::Logger::writeToLog("[InsertNode][RC] Hovered link id=" + juce::String(hoveredLinkId));
        // A cable was right-clicked. Store its info and open the insert popup.
        linkToInsertOn = {}; // Reset previous info
        linkToInsertOn.linkId = hoveredLinkId;

        bool captured = false;
        // TODO: Implement modulation link detection for new bus-based system
        // if (modLinkIdToRoute.count(hoveredLinkId))
        // {
        //     linkToInsertOn.isMod = true;
        //     auto& route = modLinkIdToRoute[hoveredLinkId];
        //     linkToInsertOn.srcLogicalId = std::get<0>(route);
        //     linkToInsertOn.srcChan = std::get<1>(route);
        //     linkToInsertOn.dstLogicalId = std::get<2>(route);
        //     linkToInsertOn.paramId = std::get<3>(route);
        //     juce::Logger::writeToLog("[InsertNode][RC] Mod link captured: srcLID=" + juce::String((int)linkToInsertOn.srcLogicalId) +
        //                               " srcChan=" + juce::String(linkToInsertOn.srcChan) +
        //                               " dstLID=" + juce::String((int)linkToInsertOn.dstLogicalId) +
        //                               " param='" + linkToInsertOn.paramId + "'");
        //     captured = true;
        // }
        if (linkIdToAttrs.count(hoveredLinkId))
        {
            linkToInsertOn.isMod = false;
            auto& attrs = linkIdToAttrs[hoveredLinkId];
            juce::Logger::writeToLog("[InsertNode][RC] Audio link attrs: srcAttr=" + juce::String(attrs.first) +
                                      " dstAttr=" + juce::String(attrs.second));
            linkToInsertOn.srcPin = decodeAttr(attrs.first);
            linkToInsertOn.dstPin = decodeAttr(attrs.second);
            juce::Logger::writeToLog("[InsertNode][RC] Audio pins: src(lid=" + juce::String((int)linkToInsertOn.srcPin.logicalId) +
                                      ",ch=" + juce::String(linkToInsertOn.srcPin.channel) +
                                      ",in=" + juce::String((int)linkToInsertOn.srcPin.isInput) + ") -> dst(lid=" +
                                      juce::String((int)linkToInsertOn.dstPin.logicalId) + ",ch=" +
                                      juce::String(linkToInsertOn.dstPin.channel) + ",in=" +
                                      juce::String((int)linkToInsertOn.dstPin.isInput) + ")");
            captured = true;
        }
        else
        {
            juce::Logger::writeToLog("[InsertNode][RC] Link id not found in maps");
        }

        if (captured)
        {
            showInsertNodePopup = true; // defer opening until after EndNodeEditor
            pendingInsertLinkId = hoveredLinkId;
            juce::Logger::writeToLog("[InsertNode][RC] Will open popup after EndNodeEditor");
        }
        else
        {
            linkToInsertOn.linkId = -1; // nothing recognized; do not open
        }
    }

    // --- Keyboard Shortcuts for Node Chaining ---
    // Check if multiple nodes are selected and no modifiers are held
    if (ImNodes::NumSelectedNodes() > 1 && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt)
    {
        // C: Standard stereo chaining (channels 0->0, 1->1)
        if (ImGui::IsKeyPressed(ImGuiKey_C))
        {
            handleNodeChaining();
        }
        // G: Audio type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_G))
        {
            handleColorCodedChaining(PinDataType::Audio);
        }
        // B: CV type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_B))
        {
            handleColorCodedChaining(PinDataType::CV);
        }
        // R: Raw type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            handleColorCodedChaining(PinDataType::Raw);
        }
        // Y: Gate type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            handleColorCodedChaining(PinDataType::Gate);
        }
    }
    // --- END OF KEYBOARD SHORTCUTS ---

    // --- Cable Splitting (Ctrl+Middle-Click) ---
    if (isLinkHovered && hoveredLinkId != -1)
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            // User initiated a split. Find the source pin of the hovered link.
            if (auto it = linkIdToAttrs.find(hoveredLinkId); it != linkIdToAttrs.end())
            {
                splittingFromAttrId = it->second.first; // The source attribute ID
                juce::Logger::writeToLog("[CableSplit] Starting split from attr ID: " + juce::String(splittingFromAttrId));
            }
        }
    }
    // --- END OF CABLE SPLITTING ---

    // 2. If a split-drag is active, handle drawing and completion.
    if (splittingFromAttrId != -1)
    {
        // Draw a line from the source pin to the mouse cursor for visual feedback.
        if (auto it = attrPositions.find(splittingFromAttrId); it != attrPositions.end())
        {
            ImVec2 sourcePos = it->second;
            ImVec2 mousePos = ImGui::GetMousePos();
            ImGui::GetForegroundDrawList()->AddLine(sourcePos, mousePos, IM_COL32(255, 255, 0, 200), 3.0f);
        }

        // 3. Handle completion or cancellation of the drag.
        // We use Left-click to complete the link, matching ImNodes' default behavior.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            int hoveredPinId = -1;
            if (ImNodes::IsPinHovered(&hoveredPinId) && hoveredPinId != -1)
            {
                // User dropped the link on a pin.
                auto srcPin = decodeAttr(splittingFromAttrId);
                auto dstPin = decodeAttr(hoveredPinId);

                // Ensure the connection is valid (Output -> Input).
                if (!srcPin.isInput && dstPin.isInput)
                {
                    auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                    auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                    synth->connect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                    graphNeedsRebuild = true;
                    pushSnapshot(); // Make it undoable
                }
            }

            // ALWAYS reset the state, whether the connection was successful or not.
            splittingFromAttrId = -1;
        }
        // Also allow cancellation with a right-click.
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            splittingFromAttrId = -1; // Cancel the operation.
        }
    }
    // --- END OF NEW LOGIC ---

    // Open popup now (outside editor) if requested this frame
    if (showInsertNodePopup)
    {
        showInsertNodePopup = false;
        // Validate the link still exists
        if (pendingInsertLinkId != -1)
        {
            bool stillValid = (/* modLinkIdToRoute.count(pendingInsertLinkId) || */ linkIdToAttrs.count(pendingInsertLinkId));
            if (!stillValid)
            {
                juce::Logger::writeToLog("[InsertNode] Skipping popup: link disappeared this frame");
                pendingInsertLinkId = -1;
            }
        }
        if (pendingInsertLinkId != -1)
        {
            ImGui::OpenPopup("InsertNodeOnLinkPopup");
            // Consume the mouse release/click so the popup stays open
            ImGui::GetIO().WantCaptureMouse = true;
            juce::Logger::writeToLog("[InsertNode] Opened popup (post-editor)");
        }
        else
        {
            linkToInsertOn = {}; // safety
        }
        pendingInsertLinkId = -1;
    }

    // Fallback: If user right-clicked and a link was hovered this frame, open popup using cached hover
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        && lastHoveredLinkId != -1
        && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup"))
    {
        int id = lastHoveredLinkId;
        linkToInsertOn = {}; linkToInsertOn.linkId = id;
        bool captured = false;
        // TODO: Handle modulation link deletion for new bus-based system
        // if (auto itM = modLinkIdToRoute.find(id); itM != modLinkIdToRoute.end())
        // {
        //     linkToInsertOn.isMod = true;
        //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
        //     linkToInsertOn.srcLogicalId = (juce::uint32) sL;
        //     linkToInsertOn.srcChan = sC;
        //     linkToInsertOn.dstLogicalId = (juce::uint32) dL;
        //     linkToInsertOn.paramId = paramId;
        //     captured = true;
        //     juce::Logger::writeToLog("[InsertNode][RC-Fallback] Mod link captured id=" + juce::String(id));
        // }
        // else 
        if (auto it = linkIdToAttrs.find(id); it != linkIdToAttrs.end())
        {
            linkToInsertOn.isMod = false;
            linkToInsertOn.srcPin = decodeAttr(it->second.first);
            linkToInsertOn.dstPin = decodeAttr(it->second.second);
            captured = true;
            juce::Logger::writeToLog("[InsertNode][RC-Fallback] Audio link captured id=" + juce::String(id));
        }
        if (captured)
        {
            ImGui::OpenPopup("InsertNodeOnLinkPopup");
            ImGui::GetIO().WantCaptureMouse = true;
            juce::Logger::writeToLog("[InsertNode][RC-Fallback] Opened popup");
        }
        else
        {
            linkToInsertOn.linkId = -1;
        }
    }
    // This function draws the popup if the popup is open.
    drawInsertNodeOnLinkPopup();

    // --- Cable Inspector: hovered link tooltip and highlight ---
    hoveredLinkSrcId = 0; hoveredLinkDstId = 0;
    // 3. Handle Cable Inspector Tooltip (only if no popup is open)
    if (!ImGui::IsPopupOpen("InsertNodeOnLinkPopup") && isLinkHovered && hoveredLinkId != -1 && synth != nullptr)
    {
        auto it = linkIdToAttrs.find(hoveredLinkId);
        if (it != linkIdToAttrs.end())
        {
            auto srcPin = decodeAttr(it->second.first);
            auto dstPin = decodeAttr(it->second.second);
            hoveredLinkSrcId = srcPin.logicalId;
            hoveredLinkDstId = (dstPin.logicalId == 0) ? kOutputHighlightId : dstPin.logicalId;

            if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId))
            {
                const float v = srcModule->getOutputChannelValue(srcPin.channel);
                // Update rolling history
                const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
                auto& hist = inspectorHistory[{ srcPin.logicalId, srcPin.channel }];
                hist.samples.emplace_back(nowSec, v);
                const double cutoff = nowSec - inspectorWindowSeconds;
                while (!hist.samples.empty() && hist.samples.front().first < cutoff) hist.samples.pop_front();
                float vmin = v, vmax = v;
                for (const auto& pr : hist.samples) { vmin = std::min(vmin, pr.second); vmax = std::max(vmax, pr.second); }
                ImGui::BeginTooltip();
                ImGui::Text("Value: %.3f", v);
                ImGui::Text("%.1fs Min: %.3f", inspectorWindowSeconds, vmin);
                ImGui::Text("%.1fs Max: %.3f", inspectorWindowSeconds, vmax);
                ImGui::Text("From: %s (ID %u)", srcModule->getName().toRawUTF8(), (unsigned) srcPin.logicalId);
                ImGui::Text("  Pin: %s", srcModule->getAudioOutputLabel(srcPin.channel).toRawUTF8());

                const bool hoveredIsModLink = false; // TODO: (modLinkIdToRoute.find(hoveredLinkId) != modLinkIdToRoute.end());
                if (!hoveredIsModLink)
                {
                    if (dstPin.logicalId == 0)
                    {
                        ImGui::Text("To:   Main Output");
                        ImGui::Text("  Pin: %s", (dstPin.channel == 0 ? "In L" : "In R"));
                    }
                    else if (auto* dstModule = synth->getModuleForLogical(dstPin.logicalId))
                    {
                        ImGui::Text("To:   %s (ID %u)", dstModule->getName().toRawUTF8(), (unsigned) dstPin.logicalId);
                        ImGui::Text("  Pin: %s", dstModule->getAudioInputLabel(dstPin.channel).toRawUTF8());
                    }
                }

                // If this is a modulation link, show destination parameter details
                // TODO: Handle modulation link inspection for new bus-based system
                // if (auto itM = modLinkIdToRoute.find(hoveredLinkId); itM != modLinkIdToRoute.end())
                // {
                //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
                //     if (auto* dstModule = synth->getModuleForLogical((juce::uint32) dL))
                //     {
                //         auto& ap = dstModule->getAPVTS();
                //         if (auto* p = ap.getParameter(paramId))
                //         {
                //             ImGui::Separator();
                //             ImGui::Text("To:   %s (ID %u)", dstModule->getName().toRawUTF8(), (unsigned) dL);
                //             ImGui::Text("Param: %s", paramId.toRawUTF8());
                //             if (auto* pf = dynamic_cast<juce::AudioParameterFloat*>(p))
                //             {
                //                 const auto& r = pf->range; const float pv = pf->get();
                //                 ImGui::Text("Type: float  Value: %.4f  Range: [%.4f .. %.4f]", pv, r.start, r.end);
                //             }
                //             else if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(p))
                //             {
                //                 const auto& r = pi->getNormalisableRange();
                //                 ImGui::Text("Type: int    Value: %d  Range: [%d .. %d]", (int) pi->get(), (int) r.start, (int) r.end);
                //             }
                //             else if (auto* pb = dynamic_cast<juce::AudioParameterBool*>(p))
                //             {
                //                 ImGui::Text("Type: bool   Value: %s", (*pb ? "true" : "false"));
                //             }
                //             else if (auto* pc = dynamic_cast<juce::AudioParameterChoice*>(p))
                //             {
                //                 ImGui::Text("Type: choice Value: %s (index %d)", pc->getCurrentChoiceName().toRawUTF8(), pc->getIndex());
                //             }
                //             else
                //             {
                //                 ImGui::Text("Type: unknown");
                //             }
                //         }
                //     }
                // }
                ImGui::EndTooltip();
            }
        }
    }

    // Deferred graph rebuild (once per frame)
    if (graphNeedsRebuild.load())
    {
        if (synth)
        {
            synth->commitChanges();
        }
        graphNeedsRebuild = false;
    }

    // Update hovered node/link id for next frame (must be called outside editor scope)
    {
        int hv = -1;
        if (ImNodes::IsNodeHovered(&hv)) lastHoveredNodeId = hv; else lastHoveredNodeId = -1;
    }
    {
        int hl = -1;
        if (ImNodes::IsLinkHovered(&hl)) lastHoveredLinkId = hl; else lastHoveredLinkId = -1;
    }

    // Shortcut: press 'I' while hovering a link to open Insert-on-Link popup (bypasses mouse handling)
    if (ImGui::IsKeyPressed(ImGuiKey_I) && lastHoveredLinkId != -1 && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup"))
    {
        linkToInsertOn = {}; // reset
        linkToInsertOn.linkId = lastHoveredLinkId;
        bool captured = false;
        // TODO: Handle modulation link hover end for new bus-based system
        // if (auto itM = modLinkIdToRoute.find(lastHoveredLinkId); itM != modLinkIdToRoute.end())
        // {
        //     linkToInsertOn.isMod = true;
        //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
        //     linkToInsertOn.srcLogicalId = (juce::uint32) sL;
        //     linkToInsertOn.srcChan = sC;
        //     linkToInsertOn.dstLogicalId = (juce::uint32) dL;
        //     linkToInsertOn.paramId = paramId;
        //     captured = true;
        //     juce::Logger::writeToLog("[InsertNode][KeyI] Mod link captured id=" + juce::String(lastHoveredLinkId));
        // }
        // else 
        if (auto it = linkIdToAttrs.find(lastHoveredLinkId); it != linkIdToAttrs.end())
        {
            linkToInsertOn.isMod = false;
            linkToInsertOn.srcPin = decodeAttr(it->second.first);
            linkToInsertOn.dstPin = decodeAttr(it->second.second);
            captured = true;
            juce::Logger::writeToLog("[InsertNode][KeyI] Audio link captured id=" + juce::String(lastHoveredLinkId));
        }
        if (captured)
        {
            pendingInsertLinkId = lastHoveredLinkId;
            showInsertNodePopup = true; // will open next lines
        }
        else
        {
            linkToInsertOn.linkId = -1;
            juce::Logger::writeToLog("[InsertNode][KeyI] No link data found for id=" + juce::String(lastHoveredLinkId));
        }
    }

    // After editor pass, if we added/duplicated a node, take snapshot now that nodes exist
    if (snapshotAfterEditor)
    {
        snapshotAfterEditor = false;
        pushSnapshot();
    }

    if (synth != nullptr)
    {
        // No persistent panning state when zoom is disabled

        // Right-click on empty canvas -> Add module popup
        // Avoid passing nullptr to ImNodes::IsLinkHovered; some builds may write to the pointer
        int dummyHoveredLinkId = -1;
        const bool anyLinkHovered = ImNodes::IsLinkHovered(&dummyHoveredLinkId);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
            && ! ImGui::IsAnyItemHovered()
            && !anyLinkHovered
            && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup")
            && linkToInsertOn.linkId == -1) // avoid conflict with insert-on-link popup
        {
                ImGui::OpenPopup("AddModulePopup");
        }

        if (ImGui::BeginPopup("AddModulePopup"))
        {
            auto addAtMouse = [this](const char* type) {
                auto nodeId = synth->addModule(type);
                const ImVec2 mouse = ImGui::GetMousePos();
                const int logicalId = (int) synth->getLogicalIdForNode (nodeId);
                pendingNodeScreenPositions[logicalId] = mouse;
                snapshotAfterEditor = true;
            };

            if (ImGui::BeginMenu("Sources")) {
                if (ImGui::MenuItem("Audio Input")) addAtMouse("audio input");
                if (ImGui::MenuItem("VCO")) addAtMouse("VCO");
                if (ImGui::MenuItem("Polyphonic VCO")) addAtMouse("polyvco");
                if (ImGui::MenuItem("Noise")) addAtMouse("Noise");
                if (ImGui::MenuItem("Sequencer")) addAtMouse("Sequencer");
                if (ImGui::MenuItem("Multi Sequencer")) addAtMouse("multi sequencer");
                if (ImGui::MenuItem("MIDI Player")) addAtMouse("midi player");
                if (ImGui::MenuItem("Value")) addAtMouse("Value");
                if (ImGui::MenuItem("Sample Loader")) addAtMouse("sample loader");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("TTS")) {
                if (ImGui::MenuItem("TTS Performer")) addAtMouse("TTS Performer");
                if (ImGui::MenuItem("Vocal Tract Filter")) addAtMouse("Vocal Tract Filter");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Effects")) {
                if (ImGui::MenuItem("VCF")) addAtMouse("VCF");
            if (ImGui::MenuItem("Delay")) addAtMouse("Delay");
            if (ImGui::MenuItem("Reverb")) addAtMouse("Reverb");
                if (ImGui::MenuItem("Chorus")) addAtMouse("chorus");
                if (ImGui::MenuItem("Phaser")) addAtMouse("phaser");
                if (ImGui::MenuItem("Compressor")) addAtMouse("compressor");
                if (ImGui::MenuItem("Recorder")) addAtMouse("recorder");
                if (ImGui::MenuItem("Limiter")) addAtMouse("limiter");
                if (ImGui::MenuItem("Noise Gate")) addAtMouse("gate");
                if (ImGui::MenuItem("Drive")) addAtMouse("drive");
                if (ImGui::MenuItem("Graphic EQ")) addAtMouse("graphic eq");
                if (ImGui::MenuItem("Waveshaper")) addAtMouse("Waveshaper");
                if (ImGui::MenuItem("8-Band Shaper")) addAtMouse("8bandshaper");
                if (ImGui::MenuItem("Granulator")) addAtMouse("granulator");
                if (ImGui::MenuItem("Harmonic Shaper")) addAtMouse("harmonic shaper");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Modulators")) {
                if (ImGui::MenuItem("LFO")) addAtMouse("LFO");
                if (ImGui::MenuItem("ADSR")) addAtMouse("ADSR");
                if (ImGui::MenuItem("Random")) addAtMouse("Random");
                if (ImGui::MenuItem("S&H")) addAtMouse("S&H");
                if (ImGui::MenuItem("Function Generator")) addAtMouse("Function Generator");
                if (ImGui::MenuItem("Shaping Oscillator")) addAtMouse("shaping oscillator");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Utilities & Logic")) {
                if (ImGui::MenuItem("VCA")) addAtMouse("VCA");
                if (ImGui::MenuItem("Mixer")) addAtMouse("Mixer");
                if (ImGui::MenuItem("CV Mixer")) addAtMouse("cv mixer");
                if (ImGui::MenuItem("Track Mixer")) addAtMouse("trackmixer");
                if (ImGui::MenuItem("Attenuverter")) addAtMouse("Attenuverter");
                if (ImGui::MenuItem("Lag Processor")) addAtMouse("Lag Processor");
                if (ImGui::MenuItem("De-Crackle")) addAtMouse("De-Crackle");
                if (ImGui::MenuItem("Math")) addAtMouse("Math");
                if (ImGui::MenuItem("Map Range")) addAtMouse("MapRange");
                if (ImGui::MenuItem("Quantizer")) addAtMouse("Quantizer");
                if (ImGui::MenuItem("Rate")) addAtMouse("Rate");
                if (ImGui::MenuItem("Comparator")) addAtMouse("Comparator");
                if (ImGui::MenuItem("Logic")) addAtMouse("Logic");
                if (ImGui::MenuItem("Clock Divider")) addAtMouse("ClockDivider");
                if (ImGui::MenuItem("Sequential Switch")) addAtMouse("SequentialSwitch");
                if (ImGui::MenuItem("Best Practice")) addAtMouse("best practice");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Analysis")) {
                if (ImGui::MenuItem("Scope")) addAtMouse("Scope");
                if (ImGui::MenuItem("Debug")) addAtMouse("debug");
                if (ImGui::MenuItem("Input Debug")) addAtMouse("input debug");
                if (ImGui::MenuItem("Frequency Graph")) addAtMouse("Frequency Graph");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // Helper functions are now class methods

        // Handle user-created links (must be called after EndNodeEditor)
        int startAttr = 0, endAttr = 0;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
        {
            auto startPin = decodeAttr(startAttr);
            auto endPin = decodeAttr(endAttr);
            auto srcPin = startPin.isInput ? endPin : startPin;
            auto dstPin = startPin.isInput ? startPin : endPin;

            // Ensure connection is always Output -> Input
            if (!srcPin.isInput && dstPin.isInput)
            {
                PinDataType srcType = getPinDataTypeForPin(srcPin);
                PinDataType dstType = getPinDataTypeForPin(dstPin);

                bool conversionHandled = false;

                // Determine if a converter is needed based on pin types
                if (srcType == PinDataType::Audio && dstType == PinDataType::CV)
                {
                    insertNodeBetween("Attenuverter", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::CV && dstType == PinDataType::Gate)
                {
                    insertNodeBetween("Comparator", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::Audio && dstType == PinDataType::Gate)
                {
                    insertNodeBetween("Comparator", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::Raw && dstType != PinDataType::Raw)
                {
                    insertNodeBetween("MapRange", srcPin, dstPin);
                    conversionHandled = true;
                }

                if (conversionHandled)
                {
                    graphNeedsRebuild = true;
                    pushSnapshot();
                }
                else
                {
                    // All other combinations are considered directly compatible.
                    auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                    auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                    synth->connect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                    synth->commitChanges();
                    graphNeedsRebuild = false;

                    if (auto* dstModule = synth->getModuleForLogical(dstPin.logicalId)) {
                        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(dstModule)) {
                            juce::String sourceName;
                            if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId)) {
                                sourceName = srcModule->getName();
                            }
                            recorder->updateSuggestedFilename(sourceName);
                        }
                    }

                    pushSnapshot();
                }
            }
        }

        // Handle link deletion (single)
        int linkId = 0;
        if (ImNodes::IsLinkDestroyed(&linkId))
        {
            if (auto it = linkIdToAttrs.find(linkId); it != linkIdToAttrs.end())
            {
                auto srcPin = decodeAttr(it->second.first);
                auto dstPin = decodeAttr(it->second.second);
                
                auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                // Debug log disconnect intent
                juce::Logger::writeToLog(
                    juce::String("[LinkDelete] src(lid=") + juce::String((int)srcPin.logicalId) + ",ch=" + juce::String(srcPin.channel) +
                    ") -> dst(lid=" + juce::String((int)dstPin.logicalId) + ",ch=" + juce::String(dstPin.channel) + ")");

                synth->disconnect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                
                // CORRECTED ORDER: Commit changes first
                synth->commitChanges();
                graphNeedsRebuild = false; // The rebuild is no longer pending
                
                // After disconnecting, tell the recorder to update (pass empty string for unconnected)
                if (auto* dstModule = synth->getModuleForLogical(dstPin.logicalId))
                {
                    if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(dstModule))
                    {
                        recorder->updateSuggestedFilename(""); // Empty = unconnected
                    }
                }
                
                pushSnapshot();
                linkIdToAttrs.erase (it);
            }
        }
        // Handle link deletion (multi-select via Delete)

        // Keyboard shortcuts
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;
        const bool alt = ImGui::GetIO().KeyAlt;
        
        if (ctrl && ImGui::IsKeyPressed (ImGuiKey_S)) { startSaveDialog(); }
        if (ctrl && ImGui::IsKeyPressed (ImGuiKey_O)) { startLoadDialog(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P)) { handleRandomizePatch(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_M)) { handleRandomizeConnections(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B)) { handleBeautifyLayout(); }
        
        // M: Mute/Bypass selected nodes (without Ctrl modifier)
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_M, false) && ImNodes::NumSelectedNodes() > 0)
        {
            handleMuteToggle();
        }
        
        // Ctrl + A: Select All
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            if (synth != nullptr)
            {
                const auto& modules = synth->getModulesInfo();
                std::vector<int> allNodeIds;
                allNodeIds.push_back(0); // Include output node
                for (const auto& mod : modules)
                {
                    allNodeIds.push_back((int)mod.first);
                }
                ImNodes::ClearNodeSelection();
                for (int id : allNodeIds)
                {
                    ImNodes::SelectNode(id);
                }
            }
        }
        
        // Ctrl + R: Reset selected node(s) to default parameters
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            const int numSelected = ImNodes::NumSelectedNodes();
            if (numSelected > 0 && synth != nullptr)
            {
                // Create a single undo state for the entire operation
                pushSnapshot();
                
                std::vector<int> selectedNodeIds(numSelected);
                ImNodes::GetSelectedNodes(selectedNodeIds.data());

                for (int lid : selectedNodeIds)
                {
                    if (auto* module = synth->getModuleForLogical((juce::uint32)lid))
                    {
                        // Get all parameters for this module
                        auto& params = module->getParameters();
                        for (auto* paramBase : params)
                        {
                            // Cast to a ranged parameter to access default values
                            if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(paramBase))
                            {
                                // Get the default value (normalized 0-1) and apply it
                                param->setValueNotifyingHost(param->getDefaultValue());
                            }
                        }
                        juce::Logger::writeToLog("[Reset] Reset parameters for node " + juce::String(lid));
                    }
                }
            }
        }
        
        // O: Connect selected to Output
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_O, false) && ImNodes::NumSelectedNodes() == 1)
        {
            if (synth != nullptr)
            {
                int selectedId;
                ImNodes::GetSelectedNodes(&selectedId);
                if (selectedId != 0)
                {
                    synth->connect(synth->getNodeIdForLogical(selectedId), 0, synth->getOutputNodeID(), 0);
                    synth->connect(synth->getNodeIdForLogical(selectedId), 1, synth->getOutputNodeID(), 1);
                    graphNeedsRebuild = true;
                    pushSnapshot();
                }
            }
        }
        
        // Alt + D: Disconnect selected nodes
        if (alt && ImGui::IsKeyPressed(ImGuiKey_D, false) && ImNodes::NumSelectedNodes() > 0)
        {
            if (synth != nullptr)
            {
                std::vector<int> selectedNodeIds(ImNodes::NumSelectedNodes());
                ImNodes::GetSelectedNodes(selectedNodeIds.data());
                for (int id : selectedNodeIds)
                {
                    synth->clearConnectionsForNode(synth->getNodeIdForLogical(id));
                }
                graphNeedsRebuild = true;
                pushSnapshot();
            }
        }
        
        // F: Frame Selected
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_F, false) && ImNodes::NumSelectedNodes() > 0)
        {
            std::vector<int> selectedNodeIds(ImNodes::NumSelectedNodes());
            ImNodes::GetSelectedNodes(selectedNodeIds.data());
            if (!selectedNodeIds.empty())
            {
                ImVec2 centerPos = ImNodes::GetNodeGridSpacePos(selectedNodeIds[0]);
                ImNodes::EditorContextResetPanning(centerPos);
            }
        }
        
        // Home: Frame All
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        {
            ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        }
        
        // Debug menu (Ctrl+Shift+D)
        if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_D)) { showDebugMenu = !showDebugMenu; }

        // Undo / Redo (Ctrl+Z / Ctrl+Y)
    if (ctrl && ImGui::IsKeyPressed (ImGuiKey_Z))
        {
            if (undoStack.size() > 1)
            {
                Snapshot current = undoStack.back();
                redoStack.push_back (current);
                undoStack.pop_back();
                restoreSnapshot (undoStack.back());
                // After a restore, clear transient link maps only; keep pending positions so they apply next frame
                linkIdToAttrs.clear();
                // modLinkIdToRoute.clear(); // TODO: Remove when fully migrated
            }
        }
        if (ctrl && ImGui::IsKeyPressed (ImGuiKey_Y))
        {
            if (! redoStack.empty())
            {
                Snapshot s = redoStack.back(); redoStack.pop_back();
                restoreSnapshot (s);
                undoStack.push_back (s);
                linkIdToAttrs.clear();
                // modLinkIdToRoute.clear(); // TODO: Remove when fully migrated
            }
        }

        // Duplicate selected nodes (Ctrl+D) and Duplicate with connections (Shift+D)
        if ((ctrl || ImGui::GetIO().KeyShift) && ImGui::IsKeyPressed (ImGuiKey_D))
        {
            const int n = ImNodes::NumSelectedNodes();
            if (n > 0)
            {
                std::vector<int> sel((size_t) n);
                ImNodes::GetSelectedNodes(sel.data());
                for (int oldId : sel)
                {
                    if (oldId == 0) continue;
                    const juce::String type = getTypeForLogical ((juce::uint32) oldId);
                    if (type.isEmpty()) continue;
                    auto newNodeId = synth->addModule (type);
                    graphNeedsRebuild = true;
                    const juce::uint32 newLogical = synth->getLogicalIdForNode (newNodeId);
                    if (newLogical != 0)
                    {
                        if (auto* src = synth->getModuleForLogical ((juce::uint32) oldId))
                            if (auto* dst = synth->getModuleForLogical (newLogical))
                                dst->getAPVTS().replaceState (src->getAPVTS().copyState());
                        // Position offset
                        ImVec2 pos = ImNodes::GetNodeGridSpacePos (oldId);
                        pendingNodePositions[(int) newLogical] = ImVec2 (pos.x + 40.0f, pos.y + 40.0f);

                        // If Shift is held: duplicate connections into and out of this node
                        if (!ctrl && ImGui::GetIO().KeyShift)
                        {
                            const auto oldNode = synth->getNodeIdForLogical ((juce::uint32) oldId);
                            const auto newNode = newNodeId;
                            // Duplicate audio/CV connections
                            for (const auto& c : synth->getConnectionsInfo())
                            {
                                // Outgoing from old -> someone
                                if ((int) c.srcLogicalId == oldId)
                                {
                                    auto dstNode = (c.dstLogicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical (c.dstLogicalId);
                                    synth->connect (newNode, c.srcChan, dstNode, c.dstChan);
                                }
                                // Incoming from someone -> old
                                if ((int) c.dstLogicalId == oldId)
                                {
                                    auto srcNode = synth->getNodeIdForLogical (c.srcLogicalId);
                                    synth->connect (srcNode, c.srcChan, newNode, c.dstChan);
                                }
                            }
                            // TODO: Implement modulation route duplication for new bus-based system
                        }
                    }
                }
                pushSnapshot();
            }
        }

        // Update selection for parameter panel
        {
            int selCount = ImNodes::NumSelectedNodes();
            if (selCount > 0)
            {
                std::vector<int> ids((size_t) selCount);
                ImNodes::GetSelectedNodes(ids.data());
                selectedLogicalId = ids.back();
            }
            else
            {
                selectedLogicalId = 0;
            }
        }

        handleDeletion();
    }

    // === DEBUG WINDOW ===
    if (showDebugMenu)
    {
        if (ImGui::Begin("System Diagnostics", &showDebugMenu))
        {
            if (synth != nullptr)
            {
                ImGui::Text("=== SYSTEM OVERVIEW ===");
                if (ImGui::Button("Refresh"))
                {
                    // Force refresh of diagnostics
                }
                
                
                // System diagnostics
                ImGui::Text("System State:");
                juce::String systemDiag = synth->getSystemDiagnostics();
                ImGui::TextWrapped("%s", systemDiag.toUTF8());
                
                
                // Module selector
                ImGui::Text("Module Diagnostics:");
                auto modules = synth->getModulesInfo();
                if (!modules.empty())
                {
                    static int selectedModuleIndex = 0;
                    if (selectedModuleIndex >= (int)modules.size()) selectedModuleIndex = 0;
                    
                    juce::String moduleList = "";
                    for (size_t i = 0; i < modules.size(); ++i)
                    {
                        if (i > 0) moduleList += "\0";
                        moduleList += "Logical " + juce::String((int)modules[i].first) + ": " + modules[i].second;
                    }
                    moduleList += "\0";
                    
                    if (ImGui::Combo("Select Module", &selectedModuleIndex, moduleList.toUTF8()))
                    {
                        if (selectedModuleIndex < (int)modules.size())
                        {
                            juce::String moduleDiag = synth->getModuleDiagnostics(modules[selectedModuleIndex].first);
                            ImGui::TextWrapped("%s", moduleDiag.toUTF8());
                        }
                    }
                }
                else
                {
                    ImGui::Text("No modules found.");
                }
            }
            else
            {
                ImGui::Text("No synth processor available.");
            }
        }
        ImGui::End();
    }

    // Keyboard Shortcuts Help Window (F1)
    if (showShortcutsWindow)
    {
        ImGui::Begin("Keyboard Shortcuts", &showShortcutsWindow, ImGuiWindowFlags_AlwaysAutoResize);
        
        ImGui::Text("Node & Patch Management");
        ImGui::Separator();
        ImGui::BulletText("M: Mute/Bypass selected node(s).");
        ImGui::BulletText("Ctrl + A: Select all nodes.");
        ImGui::BulletText("Ctrl + R: Reset selected node(s) to default parameters.");
        
        ImGui::Spacing();
        ImGui::Text("Connection & Signal Flow");
        ImGui::Separator();
        ImGui::BulletText("O: Connect selected node's first output to Main Output.");
        ImGui::BulletText("Alt + D: Disconnect all cables from selected node(s).");

        ImGui::Spacing();
        ImGui::Text("Navigation & View");
        ImGui::Separator();
        ImGui::BulletText("F: Frame selected nodes.");
        ImGui::BulletText("Home: Frame all nodes.");

        ImGui::Spacing();
        ImGui::Text("Patch Actions");
        ImGui::Separator();
        ImGui::BulletText("Ctrl + P: Randomize Patch.");
        ImGui::BulletText("Ctrl + M: Randomize Connections.");
        ImGui::BulletText("Ctrl + B: Beautify Layout.");

        ImGui::Spacing();
        ImGui::Text("Parameter & Data");
        ImGui::Separator();
        ImGui::BulletText("Ctrl + Click (on a slider): Instantly edit the value with the keyboard.");
        ImGui::BulletText("Ctrl + Shift + C: Copy selected node's settings.");
        ImGui::BulletText("Ctrl + Shift + V: Paste settings to selected node (of same type).");

        ImGui::Spacing();
        ImGui::Text("General");
        ImGui::Separator();
        ImGui::BulletText("Ctrl + S: Save Preset.");
        ImGui::BulletText("Ctrl + O: Load Preset.");
        ImGui::BulletText("Ctrl + Z: Undo.");
        ImGui::BulletText("Ctrl + Y: Redo.");
        ImGui::BulletText("Delete: Delete selected nodes/links.");
        ImGui::BulletText("F1: Toggle this help window.");
        
        ImGui::End();
    }

    ImGui::End();
    // drawPendingModPopup(); // TODO: Remove when fully migrated

    // No deferred snapshots; unified pre-state strategy
}

void ImGuiNodeEditorComponent::pushSnapshot()
{
    // Ensure any newly scheduled positions are flushed into the current UI state
    // by applying them immediately before capturing.
    if (! pendingNodePositions.empty())
    {
        juce::ValueTree applied = getUiValueTree();
        for (const auto& kv : pendingNodePositions)
        {
            // Overwrite the entry for this node if present
            for (int i = 0; i < applied.getNumChildren(); ++i)
            {
                auto n = applied.getChild(i);
                if (n.hasType("node") && (int) n.getProperty("id", -1) == kv.first)
                { n.setProperty("x", kv.second.x, nullptr); n.setProperty("y", kv.second.y, nullptr); break; }
            }
        }
        Snapshot s; s.uiState = applied; if (synth != nullptr) synth->getStateInformation (s.synthState);
        undoStack.push_back (std::move (s)); redoStack.clear();
        isPatchDirty = true; // Mark patch as dirty
        return;
    }
    Snapshot s; s.uiState = getUiValueTree();
    if (synth != nullptr) synth->getStateInformation (s.synthState);
    undoStack.push_back (std::move (s));
    redoStack.clear();
    
    // Mark patch as dirty whenever a change is made
    isPatchDirty = true;
}

void ImGuiNodeEditorComponent::restoreSnapshot (const Snapshot& s)
{
    if (synth != nullptr && s.synthState.getSize() > 0)
        synth->setStateInformation (s.synthState.getData(), (int) s.synthState.getSize());
    // Restore UI positions exactly as saved
    applyUiValueTreeNow (s.uiState);
}

juce::String ImGuiNodeEditorComponent::getTypeForLogical (juce::uint32 logicalId) const
{
    if (synth == nullptr) return {};
    for (const auto& p : synth->getModulesInfo())
        if (p.first == logicalId) return p.second;
    return {};
}

// Parameters are now drawn inline within each node; side panel removed


juce::ValueTree ImGuiNodeEditorComponent::getUiValueTree()
{
    juce::ValueTree ui ("NodeEditorUI");
    if (synth == nullptr) return ui;
    // Save node positions
    for (const auto& mod : synth->getModulesInfo())
    {
        const int nid = (int) mod.first;
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(nid);
        juce::ValueTree n ("node");
        n.setProperty ("id", nid, nullptr);
        n.setProperty ("x", pos.x, nullptr);
        n.setProperty ("y", pos.y, nullptr);
        
        // --- FIX: Save muted/bypassed state ---
        // If this node's ID is in our map of muted nodes, add the property to the XML
        if (mutedNodeStates.count(nid) > 0)
        {
            n.setProperty("muted", true, nullptr);
        }
        
        ui.addChild (n, -1, nullptr);
    }
    return ui;
}

void ImGuiNodeEditorComponent::applyUiValueTreeNow (const juce::ValueTree& uiState)
{
    if (! uiState.isValid()) return;
    
    // --- FIX: Clear stale UI state ---
    // The underlying synth graph has already been completely rebuilt by setStateInformation,
    // so we just need to clear our stale UI data before applying the new state from the preset.
    // Attempting to unmute old nodes would operate on invalid logical IDs from the previous graph.
    mutedNodeStates.clear();
    
    auto nodes = uiState; // expect tag NodeEditorUI
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        auto n = nodes.getChild(i);
        if (! n.hasType ("node")) continue;
        const int nid = (int) n.getProperty ("id", 0);
        const float x = (float) n.getProperty ("x", 0.0f);
        const float y = (float) n.getProperty ("y", 0.0f);
        pendingNodePositions[nid] = ImVec2(x, y);
        
        // --- FIX: Read and apply muted state from preset ---
        // When loading a preset, we need to mark nodes as muted and create bypass connections.
        if ((bool) n.getProperty("muted", false))
        {
            // Use muteNodeSilent to store the original connections first,
            // then apply the mute (which creates bypass connections)
            muteNodeSilent(nid);
            muteNode(nid);
        }
    }
    
    // --- FIX: Trigger graph rebuild after mute state changes ---
    // 3. Muting/unmuting modifies graph connections, so we must tell the
    //    synth to rebuild its processing order
    if (synth)
    {
        graphNeedsRebuild = true;
    }
}

void ImGuiNodeEditorComponent::applyUiValueTree (const juce::ValueTree& uiState)
{
    // Queue for next frame to avoid calling imnodes setters before editor is begun
    uiPending = uiState;
}

void ImGuiNodeEditorComponent::handleDeletion()
{
    if (synth == nullptr)
        return;

    // Shift+Delete => bypass delete (keep chain intact)
    if ((ImGui::GetIO().KeyShift) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        bypassDeleteSelectedNodes();
        return;
    }

    // Use new key query API (1.90+) for normal delete
    if (! ImGui::IsKeyPressed(ImGuiKey_Delete))
        return;

    // If a drag was in progress, capture positions before we mutate the graph
    if (isDraggingNode || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        isDraggingNode = false;
        pushSnapshot();
    }

    // Early out if nothing selected
    const int numSelLinks = ImNodes::NumSelectedLinks();
    const int numSelNodes = ImNodes::NumSelectedNodes();

    if (numSelLinks <= 0 && numSelNodes <= 0)
        return;

    // Perform batch delete; snapshot after commit

    // Disconnect selected links
        if (numSelLinks > 0)
        {
        std::vector<int> ids((size_t) numSelLinks);
        ImNodes::GetSelectedLinks(ids.data());
        for (int id : ids)
        {
            // TODO: Handle modulation link deletion for new bus-based system
            // if (auto itM = modLinkIdToRoute.find (id); itM != modLinkIdToRoute.end())
            // {
            //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
            //     // TODO: Handle modulation route removal
            //     // if (paramId.isNotEmpty())
            //     //     synth->removeModulationRoute (synth->getNodeIdForLogical ((juce::uint32) sL), sC, (juce::uint32) dL, paramId);
            //     // else
            //     //     synth->removeModulationRoute (synth->getNodeIdForLogical ((juce::uint32) sL), sC, (juce::uint32) dL);
            // }
            // else 
            if (auto it = linkIdToAttrs.find(id); it != linkIdToAttrs.end())
            {
                auto srcPin = decodeAttr(it->second.first);
                auto dstPin = decodeAttr(it->second.second);

                auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);
                synth->disconnect(srcNode, srcPin.channel, dstNode, dstPin.channel);
            }
            }
        }

        if (numSelNodes > 0)
        {
        std::vector<int> nodeIds((size_t) numSelNodes);
        ImNodes::GetSelectedNodes(nodeIds.data());
        // Build a set for quick lookup when removing connections
        std::unordered_map<int, bool> toDelete;
        for (int nid : nodeIds) toDelete[nid] = true;
        // Disconnect all connections touching any selected node
        for (const auto& c : synth->getConnectionsInfo())
        {
            if (toDelete.count((int) c.srcLogicalId) || (! c.dstIsOutput && toDelete.count((int) c.dstLogicalId)))
            {
                auto srcNode = synth->getNodeIdForLogical(c.srcLogicalId);
                auto dstNode = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
                synth->disconnect(srcNode, c.srcChan, dstNode, c.dstChan);
            }
        }
        // Remove nodes
        for (int nid : nodeIds)
        {
            if (nid == 0) continue; // don't delete output sink
            mutedNodeStates.erase((juce::uint32)nid); // Clean up muted state if exists
            synth->removeModule(synth->getNodeIdForLogical((juce::uint32) nid));
        }
    }
    graphNeedsRebuild = true;
    pushSnapshot();
}

void ImGuiNodeEditorComponent::bypassDeleteSelectedNodes()
{
    const int numSelNodes = ImNodes::NumSelectedNodes();
    if (numSelNodes <= 0 || synth == nullptr) return;

    // Snapshot positions first if dragging
    if (isDraggingNode || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        isDraggingNode = false;
        pushSnapshot();
    }

    std::vector<int> nodeIds((size_t) numSelNodes);
    ImNodes::GetSelectedNodes(nodeIds.data());

    for (int nid : nodeIds)
    {
        if (nid == 0) continue; // don't bypass-delete the output sink
        bypassDeleteNode((juce::uint32) nid);
    }
    graphNeedsRebuild = true;
    pushSnapshot();
}

void ImGuiNodeEditorComponent::bypassDeleteNode(juce::uint32 logicalId)
{
    // Collect all incoming/outgoing audio links for this node
    std::vector<decltype(synth->getConnectionsInfo())::value_type> inputs, outputs;
    for (const auto& c : synth->getConnectionsInfo())
    {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) inputs.push_back(c);
        if (c.srcLogicalId == logicalId) outputs.push_back(c);
    }

    // For each output channel, find matching input channel to splice
    for (const auto& out : outputs)
    {
        // Try to find input with same channel index, else fallback to first input
        const auto* inPtr = (const decltype(inputs)::value_type*) nullptr;
        for (const auto& in : inputs) { if (in.dstChan == out.srcChan) { inPtr = &in; break; } }
        if (inPtr == nullptr && !inputs.empty()) inPtr = &inputs.front();

        // Disconnect out link first
        auto srcNode = synth->getNodeIdForLogical(out.srcLogicalId);
        auto dstNode = out.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out.dstLogicalId);
        synth->disconnect(srcNode, out.srcChan, dstNode, out.dstChan);

        if (inPtr != nullptr)
        {
            // Disconnect incoming link from the node
            auto inSrcNode = synth->getNodeIdForLogical(inPtr->srcLogicalId);
            auto inDstNode = synth->getNodeIdForLogical(inPtr->dstLogicalId);
            synth->disconnect(inSrcNode, inPtr->srcChan, inDstNode, inPtr->dstChan);

            // Connect source of incoming directly to destination of outgoing
            auto finalDstNode = out.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out.dstLogicalId);
            synth->connect(inSrcNode, inPtr->srcChan, finalDstNode, out.dstChan);
        }
    }

    // TODO: Remove modulation routes targeting or originating this node using new bus-based system

    // Finally remove the node itself
    mutedNodeStates.erase(logicalId); // Clean up muted state if exists
    synth->removeModule(synth->getNodeIdForLogical(logicalId));
}

// === Non-Destructive Mute/Bypass Implementation ===

void ImGuiNodeEditorComponent::muteNodeSilent(juce::uint32 logicalId)
{
    // This function is used when loading presets. It records the connections that were
    // loaded from the XML without modifying the graph or creating bypass connections.
    // This preserves the original "unmuted" connections for later use.
    
    if (!synth) return;

    MutedNodeState state;
    auto allConnections = synth->getConnectionsInfo();

    // Store all connections attached to this node
    for (const auto& c : allConnections) {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) {
            state.incomingConnections.push_back(c);
        }
        if (c.srcLogicalId == logicalId) {
            state.outgoingConnections.push_back(c);
        }
    }

    // Store the state, but DON'T modify the graph or create bypass connections
    mutedNodeStates[logicalId] = state;
    juce::Logger::writeToLog("[MuteSilent] Node " + juce::String(logicalId) + 
                            " marked as muted, stored " + juce::String(state.incomingConnections.size()) + 
                            " incoming and " + juce::String(state.outgoingConnections.size()) + 
                            " outgoing connections.");
}

void ImGuiNodeEditorComponent::muteNode(juce::uint32 logicalId)
{
    if (!synth) return;

    MutedNodeState state;
    auto allConnections = synth->getConnectionsInfo();

    // 1. Find and store all connections attached to this node.
    for (const auto& c : allConnections) {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) {
            state.incomingConnections.push_back(c);
        }
        if (c.srcLogicalId == logicalId) {
            state.outgoingConnections.push_back(c);
        }
    }

    // 2. Disconnect all of them.
    for (const auto& c : state.incomingConnections) {
        synth->disconnect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, synth->getNodeIdForLogical(c.dstLogicalId), c.dstChan);
    }
    for (const auto& c : state.outgoingConnections) {
        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
        synth->disconnect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, dstNodeId, c.dstChan);
    }
    
    // --- FIX: More robust bypass splicing logic ---
    // 3. Splice the connections to bypass the node.
    // Connect the FIRST input source to ALL output destinations.
    // This correctly handles cases where input channel != output channel (e.g., Mixer input 3 → output 0).
    if (!state.incomingConnections.empty() && !state.outgoingConnections.empty())
    {
        const auto& primary_input = state.incomingConnections[0];
        auto srcNodeId = synth->getNodeIdForLogical(primary_input.srcLogicalId);

        for (const auto& out_conn : state.outgoingConnections)
        {
            auto dstNodeId = out_conn.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out_conn.dstLogicalId);
            // Connect the primary input's source directly to the original output's destination
            synth->connect(srcNodeId, primary_input.srcChan, dstNodeId, out_conn.dstChan);
            juce::Logger::writeToLog("[Mute] Splicing bypass: [" + juce::String(primary_input.srcLogicalId) + 
                                    ":" + juce::String(primary_input.srcChan) + "] -> [" + 
                                    (out_conn.dstIsOutput ? "Output" : juce::String(out_conn.dstLogicalId)) + 
                                    ":" + juce::String(out_conn.dstChan) + "]");
        }
    }

    // 4. Store the original state.
    mutedNodeStates[logicalId] = state;
    juce::Logger::writeToLog("[Mute] Node " + juce::String(logicalId) + " muted and bypassed.");
}

void ImGuiNodeEditorComponent::unmuteNode(juce::uint32 logicalId)
{
    if (!synth || mutedNodeStates.find(logicalId) == mutedNodeStates.end()) return;

    MutedNodeState state = mutedNodeStates[logicalId];

    // --- FIX: Remove bypass connections matching the new mute logic ---
    // 1. Find and remove the bypass connections.
    // The bypass connected the first input source to all output destinations.
    if (!state.incomingConnections.empty() && !state.outgoingConnections.empty())
    {
        const auto& primary_input = state.incomingConnections[0];
        auto srcNodeId = synth->getNodeIdForLogical(primary_input.srcLogicalId);

        for (const auto& out_conn : state.outgoingConnections)
        {
            auto dstNodeId = out_conn.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out_conn.dstLogicalId);
            // Disconnect the bypass connection
            synth->disconnect(srcNodeId, primary_input.srcChan, dstNodeId, out_conn.dstChan);
            juce::Logger::writeToLog("[Unmute] Removing bypass: [" + juce::String(primary_input.srcLogicalId) + 
                                    ":" + juce::String(primary_input.srcChan) + "] -> [" + 
                                    (out_conn.dstIsOutput ? "Output" : juce::String(out_conn.dstLogicalId)) + 
                                    ":" + juce::String(out_conn.dstChan) + "]");
        }
    }

    // 2. Restore the original connections.
    for (const auto& c : state.incomingConnections) {
        synth->connect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, synth->getNodeIdForLogical(c.dstLogicalId), c.dstChan);
    }
    for (const auto& c : state.outgoingConnections) {
        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
        synth->connect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, dstNodeId, c.dstChan);
    }

    // 3. Remove from muted state.
    mutedNodeStates.erase(logicalId);
    juce::Logger::writeToLog("[Mute] Node " + juce::String(logicalId) + " unmuted.");
}

void ImGuiNodeEditorComponent::handleMuteToggle()
{
    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected == 0) return;

    pushSnapshot(); // Create a single undo state for the whole operation.

    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    for (int lid : selectedNodeIds) {
        if (mutedNodeStates.count(lid)) {
            unmuteNode(lid);
        } else {
            muteNode(lid);
        }
    }

    graphNeedsRebuild = true;
}

void ImGuiNodeEditorComponent::startSaveDialog()
{
    saveChooser = std::make_unique<juce::FileChooser> ("Save preset", findPresetsDirectory(), "*.xml");
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.exists() && ! f.getParentDirectory().exists()) return;
        if (synth == nullptr) return;
        
        // --- FIX: Temporarily unmute nodes to save original connections ---
        // Collect all currently muted nodes
        std::vector<juce::uint32> currentlyMutedNodes;
        for (const auto& pair : mutedNodeStates)
        {
            currentlyMutedNodes.push_back(pair.first);
        }
        
        // Temporarily UNMUTE all of them to restore the original connections
        for (juce::uint32 lid : currentlyMutedNodes)
        {
            unmuteNode(lid);
        }
        
        // Force the synth to apply these connection changes immediately
        if (synth)
        {
            synth->commitChanges();
        }
        // At this point, the synth graph is in its "true", unmuted state
        
        // NOW get the state - this will save the correct, original connections
        juce::MemoryBlock mb; synth->getStateInformation (mb);
        auto xml = juce::XmlDocument::parse (mb.toString());
        
        // IMMEDIATELY RE-MUTE the nodes to return the editor to its visible state
        for (juce::uint32 lid : currentlyMutedNodes)
        {
            muteNode(lid);
        }
        
        // Force the synth to apply the re-mute changes immediately
        if (synth)
        {
            synth->commitChanges();
        }
        // The synth graph is now back to its bypassed state for audio processing
        // --- END OF FIX ---
        
        if (! xml) return;
        juce::ValueTree presetVT = juce::ValueTree::fromXml (*xml);
        presetVT.addChild (getUiValueTree(), -1, nullptr);
        f.replaceWithText (presetVT.createXml()->toString());
        
        // Update preset status tracking
        isPatchDirty = false;
        currentPresetFile = f.getFileName();
    });
}

void ImGuiNodeEditorComponent::startLoadDialog()
{
    loadChooser = std::make_unique<juce::FileChooser> ("Load preset", findPresetsDirectory(), "*.xml");
    loadChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::MemoryBlock mb; f.loadFileAsData (mb);
        if (synth != nullptr)
            synth->setStateInformation (mb.getData(), (int) mb.getSize());
            juce::ValueTree ui;
            if (auto xml = juce::XmlDocument::parse (mb.toString()))
            {
                auto vt = juce::ValueTree::fromXml (*xml);
                ui = vt.getChildWithName ("NodeEditorUI");
                if (ui.isValid())
                    applyUiValueTree (ui);
        }
        
        // Post-state snapshot: capture loaded synth + the UI positions from file
        Snapshot s;
        if (synth != nullptr) synth->getStateInformation (s.synthState);
        s.uiState = ui.isValid() ? ui : getUiValueTree();
        undoStack.push_back (std::move (s));
        redoStack.clear();
        
        // Update preset status tracking
        isPatchDirty = false;
        currentPresetFile = f.getFileName();
    });
}

void ImGuiNodeEditorComponent::handleRandomizePatch()
{
    if (synth == nullptr) return;
    
    populatePinDatabase();

    // 1. --- SETUP ---
    synth->clearAll();
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());
    
    // 2. --- ADD A "CLOUD" OF RANDOM MODULES ---
    std::vector<juce::String> modulePool = {
        "VCO", "Noise", "Sequencer", "VCF", "Delay", "Reverb", "Waveshaper",
        "LFO", "ADSR", "Random", "S&H", "Math", "MapRange", "Quantizer", "ClockDivider"
    };
    int numModules = 6 + rng.nextInt(7); // 6 to 12 modules
    std::vector<std::pair<juce::uint32, juce::String>> addedModules;

    for (int i = 0; i < numModules; ++i) {
        auto type = modulePool[rng.nextInt(modulePool.size())];
        auto newId = synth->getLogicalIdForNode(synth->addModule(type));
        addedModules.push_back({newId, type});
    }

    // 3. --- ESTABLISH AN OBSERVATION POINT ---
    // Always add a Mixer and Scope. This is our window into the chaos.
    auto mixerId = synth->getLogicalIdForNode(synth->addModule("Mixer"));
    addedModules.push_back({mixerId, "Mixer"});
    auto scopeId = synth->getLogicalIdForNode(synth->addModule("Scope"));
    addedModules.push_back({scopeId, "Scope"});
    
    // Connect the observation path: Mixer -> Scope -> Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(synth->getNodeIdForLogical(mixerId), 0, synth->getNodeIdForLogical(scopeId), 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 0, outputNodeId, 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 1, outputNodeId, 1);

    // 4. --- CREATE CHAOTIC CONNECTIONS ---
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioOuts;
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioIns;
    std::vector<std::pair<juce::uint32, ModPin>> allModIns;

    for (const auto& mod : addedModules) {
        auto it = modulePinDatabase.find(mod.second);
        if (it != modulePinDatabase.end()) {
            for(const auto& pin : it->second.audioOuts) allAudioOuts.push_back({mod.first, pin});
            for(const auto& pin : it->second.audioIns) allAudioIns.push_back({mod.first, pin});
            for(const auto& pin : it->second.modIns) allModIns.push_back({mod.first, pin});
        }
    }
    
    // Connect a few random audio sources to the Mixer to make sound likely
    int numMixerInputs = 2 + rng.nextInt(3); // 2 to 4 mixer inputs
    if (!allAudioOuts.empty()) {
        for (int i = 0; i < numMixerInputs; ++i) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            // Connect to mixer inputs 0, 1, 2, 3
            synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(mixerId), i);
        }
    }

    // Make a large number of fully random connections
    int numRandomConnections = numModules + rng.nextInt(numModules);
    for (int i = 0; i < numRandomConnections; ++i)
    {
        float choice = rng.nextFloat();
        // 70% chance of making a CV modulation connection
        if (choice < 0.7f && !allAudioOuts.empty() && !allModIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allModIns[rng.nextInt(allModIns.size())];
            // TODO: synth->addModulationRouteByLogical(source.first, source.second.channel, target.first, target.second.paramId);
        }
        // 30% chance of making an audio-path or gate connection
        else if (!allAudioOuts.empty() && !allAudioIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allAudioIns[rng.nextInt(allAudioIns.size())];
            // Allow self-connection for feedback
            if (source.first != target.first || rng.nextFloat() < 0.2f) {
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(target.first), target.second.channel);
            }
        }
    }

    // 5. --- LAYOUT AND FINALIZE ---
    // Arrange nodes in a neat grid to prevent overlap.
    const float startX = 50.0f;
    const float startY = 50.0f;
    const float cellWidth = 300.0f;
    const float cellHeight = 400.0f;
    const int numColumns = 4;
    int col = 0;
    int row = 0;

    juce::uint32 finalMixerId = 0, finalScopeId = 0;
    for (const auto& mod : addedModules) {
        if (mod.second == "Mixer") finalMixerId = mod.first;
        if (mod.second == "Scope") finalScopeId = mod.first;
    }

    for (const auto& mod : addedModules)
    {
        // Skip the special output-chain nodes; we will place them manually.
        if (mod.first == finalMixerId || mod.first == finalScopeId) continue;

        float x = startX + col * cellWidth;
        float y = startY + row * cellHeight;
        pendingNodePositions[(int)mod.first] = ImVec2(x, y);

        col++;
        if (col >= numColumns) {
            col = 0;
            row++;
        }
    }

    // Manually place the Mixer and Scope on the far right for a clean, readable signal flow.
    float finalX = startX + numColumns * cellWidth;
    if (finalMixerId != 0) pendingNodePositions[(int)finalMixerId] = ImVec2(finalX, startY);
    if (finalScopeId != 0) pendingNodePositions[(int)finalScopeId] = ImVec2(finalX, startY + cellHeight);
    
    synth->commitChanges();
    pushSnapshot();
}

void ImGuiNodeEditorComponent::handleRandomizeConnections()
{
    if (synth == nullptr) return;
    auto currentModules = synth->getModulesInfo();
    if (currentModules.empty()) return;

    // 1. --- SETUP AND CLEAR ---
    synth->clearAllConnections();
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());

    // 2. --- ESTABLISH AN OBSERVATION POINT ---
    juce::uint32 mixerId = 0, scopeId = 0;
    for (const auto& mod : currentModules) {
        if (mod.second == "Mixer") mixerId = mod.first;
        if (mod.second == "Scope") scopeId = mod.first;
    }
    // Add Mixer/Scope if they don't exist, as they are crucial for listening
    if (mixerId == 0) mixerId = synth->getLogicalIdForNode(synth->addModule("Mixer"));
    if (scopeId == 0) scopeId = synth->getLogicalIdForNode(synth->addModule("Scope"));

    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(synth->getNodeIdForLogical(mixerId), 0, synth->getNodeIdForLogical(scopeId), 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 0, outputNodeId, 0);

    // 3. --- CREATE CHAOTIC CONNECTIONS ---
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioOuts;
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioIns;
    std::vector<std::pair<juce::uint32, ModPin>> allModIns;
    
    // Refresh module list in case we added a Mixer/Scope
    auto updatedModules = synth->getModulesInfo();
    for (const auto& mod : updatedModules) {
        auto it = modulePinDatabase.find(mod.second);
        if (it != modulePinDatabase.end()) {
            for(const auto& pin : it->second.audioOuts) allAudioOuts.push_back({mod.first, pin});
            for(const auto& pin : it->second.audioIns) allAudioIns.push_back({mod.first, pin});
            for(const auto& pin : it->second.modIns) allModIns.push_back({mod.first, pin});
        }
    }
    
    // Connect random sources to the Mixer
    int numMixerInputs = 2 + rng.nextInt(3);
    if (!allAudioOuts.empty()) {
        for (int i = 0; i < numMixerInputs; ++i) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            if (source.first != mixerId) // Don't connect mixer to itself here
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(mixerId), i);
        }
    }
    
    // Make a large number of fully random connections
    int numRandomConnections = (int)updatedModules.size() + rng.nextInt((int)updatedModules.size());
    for (int i = 0; i < numRandomConnections; ++i)
    {
        float choice = rng.nextFloat();
        if (choice < 0.7f && !allAudioOuts.empty() && !allModIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allModIns[rng.nextInt(allModIns.size())];
            // TODO: synth->addModulationRouteByLogical(source.first, source.second.channel, target.first, target.second.paramId);
        }
        else if (!allAudioOuts.empty() && !allAudioIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allAudioIns[rng.nextInt(allAudioIns.size())];
            if (source.first != target.first || rng.nextFloat() < 0.2f) { // Allow feedback
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(target.first), target.second.channel);
            }
        }
    }

    // 4. --- FINALIZE ---
    synth->commitChanges();
    pushSnapshot();
}

void ImGuiNodeEditorComponent::handleBeautifyLayout()
{
    if (synth == nullptr) return;

    // Create an undo state so the action can be reversed
    pushSnapshot();
    juce::Logger::writeToLog("--- [Beautify Layout] Starting ---");

    // --- STEP 1: Build Graph Representation ---
    // Adjacency list: map<source_lid, vector<destination_lid>>
    std::map<juce::uint32, std::vector<juce::uint32>> adjacencyList;
    std::map<juce::uint32, int> inDegree; // Counts incoming connections for each node
    std::vector<juce::uint32> sourceNodes;

    auto modules = synth->getModulesInfo();
    for (const auto& mod : modules)
    {
        inDegree[mod.first] = 0;
        adjacencyList[mod.first] = {};
    }

    for (const auto& conn : synth->getConnectionsInfo())
    {
        if (!conn.dstIsOutput)
        {
            adjacencyList[conn.srcLogicalId].push_back(conn.dstLogicalId);
            inDegree[conn.dstLogicalId]++;
        }
    }

    for (const auto& mod : modules)
    {
        if (inDegree[mod.first] == 0)
        {
            sourceNodes.push_back(mod.first);
        }
    }

    juce::Logger::writeToLog("[Beautify] Found " + juce::String(sourceNodes.size()) + " source nodes");

    // --- STEP 2: Assign Nodes to Columns (Topological Sort) ---
    std::map<juce::uint32, int> nodeColumn;
    std::vector<std::vector<juce::uint32>> columns;
    int maxColumn = 0;

    // Initialize source nodes in column 0
    for (juce::uint32 nodeId : sourceNodes)
    {
        nodeColumn[nodeId] = 0;
    }
    columns.push_back(sourceNodes);

    // Process each column and assign children to appropriate columns
    std::queue<juce::uint32> processQueue;
    for (juce::uint32 srcNode : sourceNodes)
        processQueue.push(srcNode);

    while (!processQueue.empty())
    {
        juce::uint32 u = processQueue.front();
        processQueue.pop();

        for (juce::uint32 v : adjacencyList[u])
        {
            // The column for node 'v' is the maximum of its predecessors' columns + 1
            int newColumn = nodeColumn[u] + 1;
            if (nodeColumn.count(v) == 0 || newColumn > nodeColumn[v])
            {
                nodeColumn[v] = newColumn;
                maxColumn = std::max(maxColumn, newColumn);
                processQueue.push(v);
            }
        }
    }

    // Re-populate columns based on assignments
    columns.assign(maxColumn + 1, {});
    for (const auto& pair : nodeColumn)
    {
        columns[pair.second].push_back(pair.first);
    }

    juce::Logger::writeToLog("[Beautify] Arranged nodes into " + juce::String(maxColumn + 1) + " columns");

    // --- STEP 3: Optimize Node Ordering Within Columns ---
    // Sort nodes in each column based on median position of their parents
    for (int c = 1; c <= maxColumn; ++c)
    {
        std::map<juce::uint32, float> medianPositions;
        
        for (juce::uint32 nodeId : columns[c])
        {
            std::vector<float> parentPositions;
            
            // Find all parents in previous columns
            for (const auto& pair : adjacencyList)
            {
                for (juce::uint32 dest : pair.second)
                {
                    if (dest == nodeId)
                    {
                        // Find the vertical index of the parent node
                        int parentColumn = nodeColumn[pair.first];
                        auto& parentColVec = columns[parentColumn];
                        auto it = std::find(parentColVec.begin(), parentColVec.end(), pair.first);
                        if (it != parentColVec.end())
                        {
                            parentPositions.push_back((float)std::distance(parentColVec.begin(), it));
                        }
                    }
                }
            }
            
            if (!parentPositions.empty())
            {
                std::sort(parentPositions.begin(), parentPositions.end());
                medianPositions[nodeId] = parentPositions[parentPositions.size() / 2];
            }
            else
            {
                medianPositions[nodeId] = 0.0f;
            }
        }
        
        // Sort the column based on median positions
        std::sort(columns[c].begin(), columns[c].end(), [&](juce::uint32 a, juce::uint32 b) {
            return medianPositions[a] < medianPositions[b];
        });
    }

    // --- STEP 4: Calculate Final Coordinates ---
    const float COLUMN_WIDTH = 400.0f;
    const float NODE_VERTICAL_PADDING = 50.0f;

    // Find the tallest column to center shorter ones
    float tallestColumnHeight = 0.0f;
    for (const auto& col : columns)
    {
        float height = 0.0f;
        for (juce::uint32 lid : col)
        {
            ImVec2 nodeSize = ImNodes::GetNodeDimensions((int)lid);
            height += nodeSize.y + NODE_VERTICAL_PADDING;
        }
        tallestColumnHeight = std::max(tallestColumnHeight, height);
    }

    // --- STEP 5: Apply Positions ---
    for (int c = 0; c <= maxColumn; ++c)
    {
        // Calculate column height for centering
        float columnHeight = 0.0f;
        for (juce::uint32 lid : columns[c])
        {
            columnHeight += ImNodes::GetNodeDimensions((int)lid).y + NODE_VERTICAL_PADDING;
        }
        
        // Start Y position (centered vertically)
        float currentY = (tallestColumnHeight - columnHeight) / 2.0f;

        for (juce::uint32 lid : columns[c])
        {
            float x = c * COLUMN_WIDTH;
            pendingNodePositions[(int)lid] = ImVec2(x, currentY);
            
            ImVec2 nodeSize = ImNodes::GetNodeDimensions((int)lid);
            currentY += nodeSize.y + NODE_VERTICAL_PADDING;
        }
    }

    juce::Logger::writeToLog("[Beautify] Applied positions to " + juce::String(modules.size()) + " nodes");
    juce::Logger::writeToLog("--- [Beautify Layout] Complete ---");
}

void ImGuiNodeEditorComponent::handleConnectSelectedToTrackMixer()
{
    if (synth == nullptr || ImNodes::NumSelectedNodes() <= 0)
    {
        juce::Logger::writeToLog("[AutoConnect] Aborted: No synth or no nodes selected.");
        return;
    }

    // This is a significant action, so create an undo state first.
    pushSnapshot();
    juce::Logger::writeToLog("--- [Connect to Mixer] Starting routine ---");

    // 1. Get all selected node IDs.
    const int numSelectedNodes = ImNodes::NumSelectedNodes();
    std::vector<int> selectedNodeLids(numSelectedNodes);
    ImNodes::GetSelectedNodes(selectedNodeLids.data());

    // 2. Find the geometric center of the selected nodes to position our new modules.
    float totalX = 0.0f, maxX = 0.0f, totalY = 0.0f;
    for (int lid : selectedNodeLids)
    {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(lid);
        totalX += pos.x;
        totalY += pos.y;
        if (pos.x > maxX) {
            maxX = pos.x;
        }
    }
    ImVec2 centerPos = ImVec2(totalX / numSelectedNodes, totalY / numSelectedNodes);
    
    // 3. Create the Value node and set its value to the number of selected nodes.
    auto valueNodeId = synth->addModule("Value");
    auto valueLid = synth->getLogicalIdForNode(valueNodeId);
    if (auto* valueProc = dynamic_cast<ValueModuleProcessor*>(synth->getModuleForLogical(valueLid)))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(valueProc->getAPVTS().getParameter("value")))
        {
            *p = (float)numSelectedNodes;
            juce::Logger::writeToLog("[AutoConnect] Created Value node " + juce::String(valueLid) + " and set its value to " + juce::String(numSelectedNodes));
        }
    }
    // Position it slightly to the right of the center of the selection.
    pendingNodePositions[(int)valueLid] = ImVec2(centerPos.x + 400.0f, centerPos.y);

    // 4. Create the Track Mixer node.
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    // Position it to the right of the right-most selected node for a clean signal flow.
    pendingNodePositions[(int)mixerLid] = ImVec2(maxX + 800.0f, centerPos.y);
    juce::Logger::writeToLog("[AutoConnect] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // 5. Connect the Value node to the Track Mixer's "Num Tracks Mod" input.
    // The Value module's "Raw" output is channel 0 (provides the exact value entered by the user).
    // The Track Mixer's "Num Tracks Mod" is on Bus 1, Channel 0, which is absolute channel 64.
    synth->connect(valueNodeId, 0, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
    juce::Logger::writeToLog("[AutoConnect] Connected Value node 'Raw' output to Track Mixer's Num Tracks Mod input.");

    // 6. Connect the primary audio output of each selected node to a unique input on the Track Mixer.
    int mixerInputChannel = 0;
    for (int lid : selectedNodeLids)
    {
        if (mixerInputChannel >= TrackMixerModuleProcessor::MAX_TRACKS) break;

        auto sourceNodeId = synth->getNodeIdForLogical((juce::uint32)lid);
        
        // We will connect the first audio output (channel 0) of the source to the next available mixer input.
        synth->connect(sourceNodeId, 0, mixerNodeId, mixerInputChannel);
        juce::Logger::writeToLog("[AutoConnect] Connected node " + juce::String(lid) + " (Out 0) to Track Mixer (In " + juce::String(mixerInputChannel + 1) + ")");
        
        mixerInputChannel++;
    }

    // 7. Flag the graph for a rebuild to apply all changes.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [Connect to Mixer] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnect(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer || midiPlayerLid == 0 || !midiPlayer->hasMIDIFileLoaded())
    {
        juce::Logger::writeToLog("[AutoConnect] Aborted: MIDI Player not ready.");
        return;
    }

    juce::Logger::writeToLog("--- [AutoConnect to Samplers] Starting routine for MIDI Player " + juce::String(midiPlayerLid) + " ---");

    // 1. Get initial positions and clear existing connections from the MIDI Player.
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPlayerPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);
    synth->clearConnectionsForNode(midiPlayerNodeId);

    // --- FIX: Create and position the Track Mixer first ---
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(midiPlayerPos.x + 1200.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnect] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // --- FIX: Connect MIDI Player "Num Tracks" output to Track Mixer "Num Tracks Mod" input ---
    // This ensures the Track Mixer automatically adjusts its track count based on the MIDI file content
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kNumTracksChannelIndex, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
    juce::Logger::writeToLog("[AutoConnect] Connected MIDI Player Num Tracks to Track Mixer Num Tracks Mod");

    // 2. Create and connect a Sample Loader for each active MIDI track.
    const auto& activeTrackIndices = midiPlayer->getActiveTrackIndices();
    juce::Logger::writeToLog("[AutoConnect] MIDI file has " + juce::String(activeTrackIndices.size()) + " active tracks.");

    for (int i = 0; i < (int)activeTrackIndices.size(); ++i)
    {
        if (i >= MIDIPlayerModuleProcessor::kMaxTracks) break;

        // A. Create and position the new modules.
        auto samplerNodeId = synth->addModule("sample loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        pendingNodePositions[(int)samplerLid] = ImVec2(midiPlayerPos.x + 800.0f, midiPlayerPos.y + (i * 350.0f));

        auto mapRangeNodeId = synth->addModule("MapRange");
        auto mapRangeLid = synth->getLogicalIdForNode(mapRangeNodeId);
        pendingNodePositions[(int)mapRangeLid] = ImVec2(midiPlayerPos.x + 400.0f, midiPlayerPos.y + (i * 350.0f));
        
        // B. Configure the MapRange module for Pitch CV conversion.
        if (auto* mapRangeProc = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(mapRangeLid)))
        {
            auto& ap = mapRangeProc->getAPVTS();
            // MIDI Player Pitch Out (0..1) -> Sample Loader Pitch Mod (-24..+24 semitones)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMin"))) *p = 0.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMax"))) *p = 1.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin"))) *p = -24.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax"))) *p = 24.0f;
        }

        // C. Connect the outputs for this track.
        const int pitchChan = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 0;
        const int gateChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 1;
        const int trigChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 3;

        // Pitch: MIDI Player -> MapRange -> Sample Loader
        synth->connect(midiPlayerNodeId, pitchChan, mapRangeNodeId, 0); // Pitch Out -> MapRange In
        synth->connect(mapRangeNodeId, 1, samplerNodeId, 0);             // MapRange Raw Out -> SampleLoader Pitch Mod In

        // Gate: MIDI Player -> Sample Loader
        synth->connect(midiPlayerNodeId, gateChan, samplerNodeId, 2);    // Gate Out -> SampleLoader Gate Mod In

        // Trigger: MIDI Player -> Sample Loader
        synth->connect(midiPlayerNodeId, trigChan, samplerNodeId, 3);    // Trigger Out -> SampleLoader Trigger Mod In

        // --- FIX: Connect the Sample Loader's audio output to the Track Mixer ---
        // The Sample Loader's main audio output is channel 0.
        // The Track Mixer's inputs are mono channels 0, 1, 2...
        synth->connect(samplerNodeId, 0, mixerNodeId, i);
    }

    // --- FIX: Connect the mixer to the main output so you can hear it! ---
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Main Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Main Out R

    // 3. Flag the graph for a rebuild to apply all changes.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [AutoConnect to Samplers] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnectVCO(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer || midiPlayerLid == 0 || !midiPlayer->hasMIDIFileLoaded())
    {
        juce::Logger::writeToLog("[AutoConnectVCO] Aborted: MIDI Player not ready.");
        return;
    }
    
    juce::Logger::writeToLog("--- [AutoConnectVCO] Starting routine for MIDI Player " + juce::String(midiPlayerLid) + " ---");

    // 1. Get initial positions and clear all existing connections from the MIDI Player.
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPlayerPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);
    synth->clearConnectionsForNode(midiPlayerNodeId);
    
    // 2. Create and position the PolyVCO and Track Mixer.
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(midiPlayerPos.x + 400.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnectVCO] Created PolyVCO with logical ID " + juce::String(polyVcoLid));

    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(midiPlayerPos.x + 800.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnectVCO] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // 3. Connect the track count outputs to control both new modules.
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, polyVcoNodeId, 0); // Raw Num Tracks -> PolyVCO Num Voices Mod
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS); // Raw Num Tracks -> Mixer Num Tracks Mod
    juce::Logger::writeToLog("[AutoConnectVCO] Connected MIDI Player raw track counts to PolyVCO and Track Mixer modulation inputs.");
    
    // 4. Loop through active MIDI tracks to connect CV routes and audio.
    const auto& activeTrackIndices = midiPlayer->getActiveTrackIndices();
    juce::Logger::writeToLog("[AutoConnectVCO] MIDI file has " + juce::String(activeTrackIndices.size()) + " active tracks. Patching voices...");

    for (int i = 0; i < (int)activeTrackIndices.size(); ++i)
    {
        if (i >= PolyVCOModuleProcessor::MAX_VOICES) break; // Don't try to connect more voices than the PolyVCO has

        int sourceTrackIndex = activeTrackIndices[i];

        // A. Connect CV modulation routes from MIDI Player to the corresponding PolyVCO voice.
        int pitchChan = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 0;
        int velChan   = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 2;
        
        // Connect MIDI CV to the corresponding PolyVCO voice inputs
        synth->connect(midiPlayerNodeId, pitchChan, polyVcoNodeId, 1 + i); // Pitch -> Freq Mod
        synth->connect(midiPlayerNodeId, velChan,   polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i); // Velocity -> Gate Mod

        // B. Connect the PolyVCO voice's audio output to the Track Mixer's input.
        synth->connect(polyVcoNodeId, i, mixerNodeId, i);
    }
    
    // 5. Connect the Track Mixer to the main audio output.
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Main Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Main Out R
    
    // 6. Flag the graph for a rebuild.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [AutoConnectVCO] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnectHybrid(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer || midiPlayerLid == 0 || !midiPlayer->hasMIDIFileLoaded())
    {
        juce::Logger::writeToLog("[AutoConnectHybrid] Aborted: MIDI Player not ready.");
        return;
    }
    
    juce::Logger::writeToLog("--- [AutoConnectHybrid] Starting routine for MIDI Player " + juce::String(midiPlayerLid) + " ---");

    // 1. Get positions, clear existing connections, and get track count.
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPlayerPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);
    synth->clearConnectionsForNode(midiPlayerNodeId);
    
    const auto& activeTrackIndices = midiPlayer->getActiveTrackIndices();
    const int numActiveTracks = (int)activeTrackIndices.size();
    if (numActiveTracks == 0) return;

    // 2. Create all necessary modules.
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(midiPlayerPos.x + 400.0f, midiPlayerPos.y);

    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(midiPlayerPos.x + 1200.0f, midiPlayerPos.y);
    
    // Create a Math setup to double the track count for the mixer
    auto valueNodeId = synth->addModule("Value");
    auto valueLid = synth->getLogicalIdForNode(valueNodeId);
    if(auto* valueProc = dynamic_cast<ValueModuleProcessor*>(synth->getModuleForLogical(valueLid)))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(valueProc->getAPVTS().getParameter("value"))) *p = 2.0f;
    pendingNodePositions[(int)valueLid] = ImVec2(midiPlayerPos.x, midiPlayerPos.y + 200.0f);
    
    auto mathNodeId = synth->addModule("Math");
    auto mathLid = synth->getLogicalIdForNode(mathNodeId);
    if(auto* mathProc = dynamic_cast<MathModuleProcessor*>(synth->getModuleForLogical(mathLid)))
        *dynamic_cast<juce::AudioParameterChoice*>(mathProc->getAPVTS().getParameter("operation")) = 2; // Set to Multiply
    pendingNodePositions[(int)mathLid] = ImVec2(midiPlayerPos.x + 200.0f, midiPlayerPos.y + 200.0f);

    // 3. Connect the master control signals for voice/track counts.
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, mathNodeId, 0); // Raw Num Tracks -> Math In A
    synth->connect(valueNodeId, 0, mathNodeId, 1); // Value (2.0) -> Math In B
    synth->connect(mathNodeId, 0, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS); // Math Out -> Mixer Num Tracks Mod
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, polyVcoNodeId, 0); // Raw Num Tracks -> PolyVCO Num Voices Mod

    // 4. Create and connect a Sample Loader for each active MIDI track, and wire up CV.
    std::vector<juce::uint32> samplerLids;
    for (int i = 0; i < numActiveTracks; ++i)
    {
        auto samplerNodeId = synth->addModule("sample loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        samplerLids.push_back(samplerLid);
        pendingNodePositions[(int)samplerLid] = ImVec2(midiPlayerPos.x + 800.0f, midiPlayerPos.y + (i * 350.0f));

        int pitchChan = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 0;
        int gateChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 1;
        int velChan   = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 2;
        int trigChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 3;

        // Patch CV to PolyVCO Voice
        synth->connect(midiPlayerNodeId, pitchChan, polyVcoNodeId, 1 + i);
        synth->connect(midiPlayerNodeId, velChan,   polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i);

        // Patch CV to Sample Loader
        auto currentSamplerNodeId = synth->getNodeIdForLogical(samplerLids.back());
        synth->connect(midiPlayerNodeId, pitchChan, currentSamplerNodeId, 0);
        synth->connect(midiPlayerNodeId, gateChan,  currentSamplerNodeId, 2);
        synth->connect(midiPlayerNodeId, trigChan,  currentSamplerNodeId, 3);
        synth->connect(midiPlayerNodeId, velChan,   currentSamplerNodeId, 1); // Velocity -> Speed Mod
    }

    // 5. Connect all audio routes to the mixer.
    for (int i = 0; i < numActiveTracks; ++i)
    {
        // PolyVCO audio outputs -> first half of mixer
        synth->connect(polyVcoNodeId, i, mixerNodeId, i);

        // Sample Loader audio outputs -> second half of mixer
        auto samplerNodeId = synth->getNodeIdForLogical(samplerLids[i]);
        synth->connect(samplerNodeId, 0, mixerNodeId, i + numActiveTracks);
    }
    
    // 6. Connect the main mixer to the audio output.
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0);
    synth->connect(mixerNodeId, 1, outputNodeId, 1);
    
    // 7. Flag the graph for a rebuild.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [AutoConnectHybrid] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMultiSequencerAutoConnectSamplers(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid)
{
    if (!synth || !sequencer) return;

    // 1. Get Sequencer info and clear its old connections
    auto seqNodeId = synth->getNodeIdForLogical(sequencerLid);
    ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)sequencerLid);
    const int numSteps = static_cast<int>(sequencer->getAPVTS().getRawParameterValue("numSteps")->load());
    synth->clearConnectionsForNode(seqNodeId);

    // 2. Create the necessary Mixer
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y + 100.0f);
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")) = numSteps;
    }

    // 3. CREATE a Sample Loader for each step and connect its audio to the mixer
    for (int i = 0; i < numSteps; ++i)
    {
        auto samplerNodeId = synth->addModule("sample loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        pendingNodePositions[(int)samplerLid] = ImVec2(seqPos.x + 400.0f, seqPos.y + (i * 220.0f));

        // Connect this sampler's audio output to the mixer's input
        synth->connect(samplerNodeId, 0 /*Audio Output*/, mixerNodeId, i);
        
        // Connect the Sequencer's CV/Trig for this step directly to the new sampler
        synth->connect(seqNodeId, 6 + i * 3 + 0, samplerNodeId, 0); // Pitch N -> Pitch Mod
        synth->connect(seqNodeId, 6 + i * 3 + 1, samplerNodeId, 2); // Gate N -> Gate Mod
        synth->connect(seqNodeId, 6 + i * 3 + 2, samplerNodeId, 3); // Trig N  -> Trigger Mod
    }

    // 4. Connect the mixer to the main output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
}

void ImGuiNodeEditorComponent::handleMultiSequencerAutoConnectVCO(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid)
{
    if (!synth || !sequencer) return;

    // 1. Get Sequencer info and clear its old connections
    auto seqNodeId = synth->getNodeIdForLogical(sequencerLid);
    ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)sequencerLid);
    const int numSteps = static_cast<int>(sequencer->getAPVTS().getRawParameterValue("numSteps")->load());
    synth->clearConnectionsForNode(seqNodeId);

    // 2. CREATE the PolyVCO and Track Mixer
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(seqPos.x + 400.0f, seqPos.y);
    if (auto* vco = dynamic_cast<PolyVCOModuleProcessor*>(synth->getModuleForLogical(polyVcoLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(vco->getAPVTS().getParameter("numVoices")) = numSteps;
    }
    
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y);
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")) = numSteps;
    }

    // 3. Connect CV, Audio, and Main Output
    for (int i = 0; i < numSteps; ++i)
    {
        // Connect CV: Sequencer -> PolyVCO
        synth->connect(seqNodeId, 6 + i * 3 + 0, polyVcoNodeId, 1 + i);                                  // Pitch N -> Freq N Mod
        synth->connect(seqNodeId, 6 + i * 3 + 1, polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i); // Gate N  -> Gate N Mod

        // Connect Audio: PolyVCO -> Mixer
        synth->connect(polyVcoNodeId, i, mixerNodeId, i);
    }
    
    // Connect Mixer -> Main Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
}

// Add this exact helper function to the class
void ImGuiNodeEditorComponent::parsePinName(const juce::String& fullName, juce::String& outType, int& outIndex)
{
    outIndex = -1; // Default to no index
    outType = fullName;

    if (fullName.contains(" "))
    {
        const juce::String lastWord = fullName.substring(fullName.lastIndexOfChar(' ') + 1);
        if (lastWord.containsOnly("0123456789"))
        {
            outIndex = lastWord.getIntValue();
            outType = fullName.substring(0, fullName.lastIndexOfChar(' '));
        }
    }
}

// Helper functions to get pins from modules
std::vector<AudioPin> ImGuiNodeEditorComponent::getOutputPins(const juce::String& moduleType)
{
    auto it = modulePinDatabase.find(moduleType);
    if (it != modulePinDatabase.end())
        return it->second.audioOuts;
    return {};
}

std::vector<AudioPin> ImGuiNodeEditorComponent::getInputPins(const juce::String& moduleType)
{
    auto it = modulePinDatabase.find(moduleType);
    if (it != modulePinDatabase.end())
        return it->second.audioIns;
    return {};
}

AudioPin* ImGuiNodeEditorComponent::findInputPin(const juce::String& moduleType, const juce::String& pinName)
{
    auto pins = getInputPins(moduleType);
    for (auto& pin : pins)
    {
        if (pin.name == pinName)
            return &pin;
    }
    return nullptr;
}

AudioPin* ImGuiNodeEditorComponent::findOutputPin(const juce::String& moduleType, const juce::String& pinName)
{
    auto pins = getOutputPins(moduleType);
    for (auto& pin : pins)
    {
        if (pin.name == pinName)
            return &pin;
    }
    return nullptr;
}

std::vector<juce::uint32> ImGuiNodeEditorComponent::findNodesOfType(const juce::String& moduleType)
{
    std::vector<juce::uint32> result;
    if (!synth) return result;
    
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleTypeForLogical(modInfo.first) == moduleType)
        {
            result.push_back(modInfo.first);
        }
    }
    return result;
}

// New dynamic pin-fetching helper
std::vector<PinInfo> ImGuiNodeEditorComponent::getDynamicOutputPins(ModuleProcessor* module)
{
    std::vector<PinInfo> pins;
    if (!module) return pins;

    const int numOutputChannels = module->getBus(false, 0)->getNumberOfChannels();
    for (int i = 0; i < numOutputChannels; ++i)
    {
        juce::String pinName = module->getAudioOutputLabel(i);
        if (pinName.isNotEmpty())
        {
            pins.push_back({(uint32_t)i, pinName}); // Store the full pin name in the type field
        }
    }
    return pins;
}

// Template function implementations
template<typename TargetProcessorType>
void ImGuiNodeEditorComponent::connectToMonophonicTargets(
    ModuleProcessor* sourceNode,
    const std::map<juce::String, juce::String>& pinNameMapping,
    const std::vector<juce::uint32>& targetLids)
{
    if (!synth || !sourceNode || targetLids.empty()) return;
    
    juce::Logger::writeToLog("[AutoConnect] connectToMonophonicTargets called for " + sourceNode->getName());
    
    // Get the source module type
    juce::String sourceModuleType;
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
        {
            sourceModuleType = synth->getModuleTypeForLogical(modInfo.first);
            break;
        }
    }
    
    if (sourceModuleType.isEmpty()) return;
    
    // Use provided target logical IDs explicitly
    auto targetNodes = targetLids;

    int currentTargetIndex = 0;

    // First, group all of the source node's output pins by their index number.
    // For example, "Pitch 1" and "Trig 1" will both be in the group for index 1.
    std::map<int, std::vector<PinInfo>> pinsByIndex;
    
    // THE FIX: Get pins directly from the module instance.
    auto outputPins = getDynamicOutputPins(sourceNode);
    
    for (const auto& pin : outputPins)
    {
        juce::String type;
        int index = -1;
        parsePinName(pin.type, type, index); // Use pin.type instead of pin.name
        if (index != -1) {
            // Store channel ID as the pin's ID
            pinsByIndex[index].push_back({(uint32_t)pin.id, type}); 
        }
    }

    // Now, loop through each group of pins (each voice).
    for (auto const& [index, pinsInGroup] : pinsByIndex)
    {
        if (currentTargetIndex >= (int)targetNodes.size()) break; // Stop if we run out of targets
        auto targetNodeId = targetNodes[currentTargetIndex];

        // For each pin in the group (e.g., for "Pitch 1" and "Trig 1")...
        for (const auto& pinInfo : pinsInGroup)
        {
            // Check if we have a connection rule for this pin type (e.g., "Pitch").
            if (pinNameMapping.count(pinInfo.type))
            {
                juce::String targetPinName = pinNameMapping.at(pinInfo.type);
                auto* targetPin = findInputPin("sample loader", targetPinName);

                // If the target pin exists, create the connection.
                if (targetPin)
                {
                    juce::uint32 sourceLogicalId = 0;
                    for (const auto& modInfo : synth->getModulesInfo())
                    {
                        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
                        {
                            sourceLogicalId = modInfo.first;
                            break;
                        }
                    }
                    auto sourceNodeId = synth->getNodeIdForLogical(sourceLogicalId);
                    synth->connect(sourceNodeId, pinInfo.id, synth->getNodeIdForLogical(targetNodeId), targetPin->channel);
                }
            }
        }
        // IMPORTANT: Move to the next target module for the next voice.
        currentTargetIndex++;
    }
}

template<typename TargetProcessorType>
void ImGuiNodeEditorComponent::connectToPolyphonicTarget(
    ModuleProcessor* sourceNode,
    const std::map<juce::String, juce::String>& pinNameMapping)
{
    if (!synth || !sourceNode) return;
    
    juce::Logger::writeToLog("[AutoConnect] connectToPolyphonicTarget called for " + sourceNode->getName());
    
    // Get the source module type
    juce::String sourceModuleType;
    juce::uint32 sourceLogicalId = 0;
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
        {
            sourceModuleType = synth->getModuleTypeForLogical(modInfo.first);
            sourceLogicalId = modInfo.first;
            break;
        }
    }
    
    if (sourceModuleType.isEmpty()) return;
    
    auto targetNodes = findNodesOfType("polyvco");
    if (targetNodes.empty()) return;
    auto targetNodeId = targetNodes[0]; // Use the first available PolyVCO

    auto sourceNodeId = synth->getNodeIdForLogical(sourceLogicalId);

    // THE FIX: Get pins directly from the module instance, not the database.
    auto outputPins = getDynamicOutputPins(sourceNode);

    // Loop through every output pin on the source module.
    for (const auto& sourcePin : outputPins)
    {
        // Parse the source pin's name to get its type and index.
        juce::String sourceType;
        int sourceIndex = -1;
        parsePinName(sourcePin.type, sourceType, sourceIndex); // Use pin.type instead of pin.name

        if (sourceIndex == -1) continue; // Skip pins that aren't numbered.

        // Check if we have a rule for this pin type (e.g., "Pitch" maps to "Freq").
        if (pinNameMapping.count(sourceType))
        {
            juce::String targetType = pinNameMapping.at(sourceType);
            // PolyVCO inputs use the format "Freq 1 Mod", "Gate 1 Mod", etc.
            juce::String targetPinName = targetType + " " + juce::String(sourceIndex) + " Mod";

            // Find that pin on the target and connect it if available.
            auto* targetPin = findInputPin("polyvco", targetPinName);
            if (targetPin)
            {
                synth->connect(sourceNodeId, sourcePin.id, synth->getNodeIdForLogical(targetNodeId), targetPin->channel);
            }
        }
    }
}

void ImGuiNodeEditorComponent::handleAutoConnectionRequests()
{
    if (!synth) return;
    
    for (const auto& modInfo : synth->getModulesInfo())
    {
        auto* module = synth->getModuleForLogical(modInfo.first);
        if (!module) continue;

        // --- Check MultiSequencer Flags ---
        if (auto* multiSeq = dynamic_cast<MultiSequencerModuleProcessor*>(module))
        {
            if (multiSeq->autoConnectSamplersTriggered.exchange(false))
            {
                handleMultiSequencerAutoConnectSamplers(multiSeq, modInfo.first); // Call the new specific handler
                pushSnapshot();
                return;
            }
            if (multiSeq->autoConnectVCOTriggered.exchange(false))
            {
                handleMultiSequencerAutoConnectVCO(multiSeq, modInfo.first); // Call the new specific handler
                pushSnapshot();
                return;
            }
        }
        
        // --- Check MIDIPlayer Flags ---
        if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(module))
        {
            if (midiPlayer->autoConnectTriggered.exchange(false)) // Samplers
            {
                handleMidiPlayerAutoConnect(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
            if (midiPlayer->autoConnectVCOTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectVCO(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
            if (midiPlayer->autoConnectHybridTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectHybrid(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
        }
    }
}

void ImGuiNodeEditorComponent::drawInsertNodeOnLinkPopup()
{
    if (ImGui::BeginPopup("InsertNodeOnLinkPopup"))
    {
        const int numSelected = ImNodes::NumSelectedLinks();
        const bool isMultiInsert = numSelected > 1;

        // --- FIX: Use map to separate display names from internal type names ---
        // Map format: {Display Name, Internal Type}
        const std::map<const char*, const char*> audioInsertable = {
            {"VCF", "VCF"}, {"VCA", "VCA"}, {"Delay", "Delay"}, {"Reverb", "Reverb"},
            {"Chorus", "chorus"}, {"Phaser", "phaser"}, {"Compressor", "compressor"},
            {"Recorder", "recorder"}, {"Limiter", "limiter"}, {"Gate", "gate"}, {"Drive", "drive"},
            {"Graphic EQ", "graphic eq"}, {"Waveshaper", "Waveshaper"}, {"Time/Pitch Shifter", "timepitch"},
            {"Attenuverter", "Attenuverter"}, {"De-Crackle", "De-Crackle"}, {"Mixer", "Mixer"},
            {"Shaping Oscillator", "shaping oscillator"}, {"Function Generator", "Function Generator"},
            {"8-Band Shaper", "8bandshaper"}, // <<< THE FIX: Internal type is "8bandshaper"
            {"Granulator", "Granulator"}, {"Harmonic Shaper", "harmonic shaper"},
            {"Vocal Tract Filter", "Vocal Tract Filter"}, {"Scope", "Scope"}
        };
        const std::map<const char*, const char*> modInsertable = {
            {"Attenuverter", "Attenuverter"}, {"Lag Processor", "Lag Processor"}, {"Math", "Math"},
            {"MapRange", "MapRange"}, {"Quantizer", "Quantizer"}, {"S&H", "S&H"},
            {"Rate", "Rate"}, {"Logic", "Logic"}, {"Comparator", "Comparator"},
            {"CV Mixer", "CV Mixer"}, {"Sequential Switch", "Sequential Switch"}
        };
        const auto& listToShow = linkToInsertOn.isMod ? modInsertable : audioInsertable;

        if (isMultiInsert)
            ImGui::Text("Insert Node on %d Cables", numSelected);
        else
            ImGui::Text("Insert Node on Cable");

        // --- FIX: Iterate over map pairs instead of simple strings ---
        for (const auto& pair : listToShow)
        {
            // pair.first = display label, pair.second = internal type
            if (ImGui::MenuItem(pair.first))
            {
                if (isMultiInsert)
                {
                    handleInsertNodeOnSelectedLinks(pair.second);
                }
                else
                {
                    insertNodeBetween(pair.second);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    else
    {
        // --- FIX: Reset state when popup is closed ---
        // If the popup is not open (i.e., it was closed or the user clicked away),
        // we must reset the state variable. This ensures that the application
        // is no longer "stuck" in the insert-on-link mode and right-click on
        // empty canvas will work again.
        linkToInsertOn.linkId = -1;
    }
}

// --- NEW HELPER FUNCTION ---
void ImGuiNodeEditorComponent::insertNodeOnLink(const juce::String& nodeType, const LinkInfo& linkInfo, const ImVec2& position)
{
    if (synth == nullptr) return;

    PinDataType srcType = getPinDataTypeForPin(linkInfo.srcPin);
    PinDataType dstType = getPinDataTypeForPin(linkInfo.dstPin);

    // 1. Create and Position the New Node
    auto newNodeId = synth->addModule(nodeType);
    auto newNodeLid = synth->getLogicalIdForNode(newNodeId);
    pendingNodeScreenPositions[(int)newNodeLid] = position;

    // 2. Get Original Connection Points
    auto originalSrcNodeId = synth->getNodeIdForLogical(linkInfo.srcPin.logicalId);
    auto originalDstNodeId = (linkInfo.dstPin.logicalId == 0) 
        ? synth->getOutputNodeID() 
        : synth->getNodeIdForLogical(linkInfo.dstPin.logicalId);

    // 3. Disconnect the Original Link
    synth->disconnect(originalSrcNodeId, linkInfo.srcPin.channel, originalDstNodeId, linkInfo.dstPin.channel);

    // 4. Configure newly inserted node if necessary (e.g., MapRange)
    int newNodeOutputChannel = 0;
    if (nodeType == "MapRange")
    {
        if (auto* mapRange = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(newNodeLid)))
        {
            Range inRange = getSourceRange(linkInfo.srcPin, synth);
            configureMapRangeFor(srcType, dstType, *mapRange, inRange);
            newNodeOutputChannel = (dstType == PinDataType::Audio) ? 1 : 0;
        }
    }

    // 5. Reconnect Through the New Node
    synth->connect(originalSrcNodeId, linkInfo.srcPin.channel, newNodeId, 0);
    synth->connect(newNodeId, newNodeOutputChannel, originalDstNodeId, linkInfo.dstPin.channel);
}

// --- REFACTORED OLD FUNCTION ---
void ImGuiNodeEditorComponent::insertNodeBetween(const juce::String& nodeType, const PinID& srcPin, const PinID& dstPin)
{
    if (synth == nullptr) return;

    // 1. Get positions to place the new node between the source and destination
    ImVec2 srcPos = ImNodes::GetNodeGridSpacePos(srcPin.logicalId);
    ImVec2 dstPos = ImNodes::GetNodeGridSpacePos(dstPin.logicalId == 0 ? 0 : dstPin.logicalId);
    ImVec2 newNodePos = ImVec2((srcPos.x + dstPos.x) * 0.5f, (srcPos.y + dstPos.y) * 0.5f);

    // 2. Create and position the new converter node
    auto newNodeId = synth->addModule(nodeType);
    auto newNodeLid = synth->getLogicalIdForNode(newNodeId);
    pendingNodePositions[(int)newNodeLid] = newNodePos;

    // 3. Get original node IDs
    auto originalSrcNodeId = synth->getNodeIdForLogical(srcPin.logicalId);
    auto originalDstNodeId = (dstPin.logicalId == 0)
        ? synth->getOutputNodeID()
        : synth->getNodeIdForLogical(dstPin.logicalId);

    // 4. Configure the new node if it's a MapRange or Attenuverter
    int newNodeOutputChannel = 0;
    if (nodeType == "MapRange") {
        if (auto* mapRange = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(newNodeLid))) {
            PinDataType srcType = getPinDataTypeForPin(srcPin);
            PinDataType dstType = getPinDataTypeForPin(dstPin);
            Range inRange = getSourceRange(srcPin, synth);
            configureMapRangeFor(srcType, dstType, *mapRange, inRange);
            newNodeOutputChannel = (dstType == PinDataType::Audio) ? 1 : 0; // Use Raw Out for Audio, Norm Out for CV
        }
    } else if (nodeType == "Attenuverter") {
        // You might want to pre-configure the Attenuverter here if needed
    }

    // 5. Connect the signal chain: Original Source -> New Node -> Original Destination
    synth->connect(originalSrcNodeId, srcPin.channel, newNodeId, 0); // Source -> New Node's first input
    synth->connect(newNodeId, newNodeOutputChannel, originalDstNodeId, dstPin.channel); // New Node -> Destination

    juce::Logger::writeToLog("[AutoConvert] Inserted '" + nodeType + "' between " + juce::String(srcPin.logicalId) + " and " + juce::String(dstPin.logicalId));
}

void ImGuiNodeEditorComponent::insertNodeBetween(const juce::String& nodeType)
{
    // This function is now just a wrapper that calls the helper
    // with the stored link info and the current mouse position.
    if (linkToInsertOn.linkId != -1)
    {
        insertNodeOnLink(nodeType, linkToInsertOn, ImGui::GetMousePos());
        graphNeedsRebuild = true;
        pushSnapshot();
        linkToInsertOn.linkId = -1; // Reset state
    }
}

void ImGuiNodeEditorComponent::handleInsertNodeOnSelectedLinks(const juce::String& nodeType)
{
    if (synth == nullptr || ImNodes::NumSelectedLinks() == 0) return;

    pushSnapshot(); // Create one undo state for the entire batch operation.

    const int numSelectedLinks = ImNodes::NumSelectedLinks();
    std::vector<int> selectedLinkIds(numSelectedLinks);
    ImNodes::GetSelectedLinks(selectedLinkIds.data());

    ImVec2 basePosition = ImGui::GetMousePos();
    float x_offset = 0.0f;

    for (int linkId : selectedLinkIds)
    {
        auto it = linkIdToAttrs.find(linkId);
        if (it != linkIdToAttrs.end())
        {
            // Decode the link and create a LinkInfo struct for it
            LinkInfo currentLink;
            currentLink.linkId = linkId;
            currentLink.isMod = false; // Assuming audio links for now
            currentLink.srcPin = decodeAttr(it->second.first);
            currentLink.dstPin = decodeAttr(it->second.second);
            
            // Calculate a staggered position for the new node
            ImVec2 newPosition = ImVec2(basePosition.x + x_offset, basePosition.y);
            
            // Call our reusable helper function
            insertNodeOnLink(nodeType, currentLink, newPosition);
            
            // Increment the offset for the next node
            x_offset += 40.0f; 
        }
    }

    graphNeedsRebuild = true;
    // The single pushSnapshot at the beginning handles the undo state.
}

juce::File ImGuiNodeEditorComponent::findPresetsDirectory()
{
    // Search upwards from the executable's location for a sibling directory
    // named "Synth_presets". This is robust to different build configurations.
    juce::File dir = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    for (int i = 0; i < 8; ++i) // Limit search depth to 8 levels
    {
        dir = dir.getParentDirectory();
        if (!dir.exists()) break;

        juce::File candidate = dir.getSiblingFile("Synth_presets");
        if (candidate.isDirectory())
        {
            return candidate;
        }
    }
    
    // Fallback to an empty file (system default) if not found
    return {};
}

// Helper function implementations
PinDataType ImGuiNodeEditorComponent::getPinDataTypeForPin(const PinID& pin)
{
    if (synth == nullptr) return PinDataType::Raw;

    // Handle the main output node as a special case
    if (pin.logicalId == 0)
    {
        return PinDataType::Audio;
    }

    juce::String moduleType = getTypeForLogical(pin.logicalId);
    if (moduleType.isEmpty()) return PinDataType::Raw;

    auto it = modulePinDatabase.find(moduleType);
    if (it == modulePinDatabase.end())
    {
        // Fallback: case-insensitive lookup (module registry may use different casing)
        juce::String moduleTypeLower = moduleType.toLowerCase();
        for (const auto& kv : modulePinDatabase)
        {
            if (kv.first.compareIgnoreCase(moduleType) == 0 || kv.first.toLowerCase() == moduleTypeLower)
            {
                it = modulePinDatabase.find(kv.first);
                break;
            }
        }
        if (it == modulePinDatabase.end())
            return PinDataType::Raw;
    }

    const auto& pinInfo = it->second;

    if (pin.isMod)
    {
        for (const auto& modPin : pinInfo.modIns)
        {
            if (modPin.paramId == pin.paramId)
            {
                return modPin.type;
            }
        }
    }
    else // It's an audio pin
    {
        const auto& pins = pin.isInput ? pinInfo.audioIns : pinInfo.audioOuts;
        for (const auto& audioPin : pins)
        {
            if (audioPin.channel == pin.channel)
            {
                return audioPin.type;
            }
        }
    }
    return PinDataType::Raw; // Fallback
}

unsigned int ImGuiNodeEditorComponent::getImU32ForType(PinDataType type)
{
    switch (type)
    {
        case PinDataType::CV:    return IM_COL32(100, 150, 255, 255); // Blue
        case PinDataType::Audio: return IM_COL32(100, 255, 150, 255); // Green
        case PinDataType::Gate:  return IM_COL32(255, 220, 100, 255); // Yellow
        case PinDataType::Raw:   return IM_COL32(255, 100, 100, 255); // Red
        default:                 return IM_COL32(150, 150, 150, 255); // Grey
    }
}

const char* ImGuiNodeEditorComponent::pinDataTypeToString(PinDataType type)
{
    switch (type)
    {
        case PinDataType::CV:    return "CV (0 to 1)";
        case PinDataType::Audio: return "Audio (-1 to 1)";
        case PinDataType::Gate:  return "Gate/Trigger";
        case PinDataType::Raw:   return "Raw";
        default:                 return "Unknown";
    }
}

// Add this new function implementation to the .cpp file.

void ImGuiNodeEditorComponent::handleNodeChaining()
{
    if (synth == nullptr) return;

    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected <= 1) return;

    juce::Logger::writeToLog("[Node Chaining] Initiated for " + juce::String(numSelected) + " nodes.");

    // 1. Get all selected nodes and their horizontal positions.
    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    std::vector<std::pair<float, int>> sortedNodes;
    for (int nodeId : selectedNodeIds)
    {
        // Don't include the main output node in the chaining logic.
        if (nodeId == 0) continue;
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeId);
        sortedNodes.push_back({pos.x, nodeId});
    }

    // 2. Sort the nodes from left to right based on their X position.
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Create a single undo action for the entire operation.
    pushSnapshot();

    // 3. Connect the nodes in sequence.
    for (size_t i = 0; i < sortedNodes.size() - 1; ++i)
    {
        juce::uint32 sourceLid = sortedNodes[i].second;
        juce::uint32 destLid   = sortedNodes[i + 1].second;

        auto sourceNodeId = synth->getNodeIdForLogical(sourceLid);
        auto destNodeId   = synth->getNodeIdForLogical(destLid);

        if (sourceNodeId.uid != 0 && destNodeId.uid != 0)
        {
            // Standard stereo connection: Out L -> In L, Out R -> In R
            synth->connect(sourceNodeId, 0, destNodeId, 0); // Connect channel 0
            synth->connect(sourceNodeId, 1, destNodeId, 1); // Connect channel 1

            juce::Logger::writeToLog("[Node Chaining] Connected " + getTypeForLogical(sourceLid) + " (" + juce::String(sourceLid) + ") to " + getTypeForLogical(destLid) + " (" + juce::String(destLid) + ")");
        }
    }

    // 4. Apply all the new connections to the audio graph.
    graphNeedsRebuild = true;
}

// Add this new helper function implementation.

std::vector<AudioPin> ImGuiNodeEditorComponent::getPinsOfType(juce::uint32 logicalId, bool isInput, PinDataType targetType)
{
    std::vector<AudioPin> matchingPins;
    juce::String moduleType = getTypeForLogical(logicalId);

    juce::Logger::writeToLog("[getPinsOfType] Looking for " + juce::String(toString(targetType)) + " " + juce::String(isInput ? "input" : "output") + " pins on logicalId=" + juce::String(logicalId) + " (type='" + moduleType + "')");

    if (moduleType.isEmpty())
    {
        juce::Logger::writeToLog("[getPinsOfType] ERROR: moduleType is empty");
        return matchingPins;
    }

    auto it = modulePinDatabase.find(moduleType);

    // --- CASE-INSENSITIVE LOOKUP FIX ---
    // If the direct lookup fails, try a case-insensitive search.
    if (it == modulePinDatabase.end())
    {
        juce::String moduleTypeLower = moduleType.toLowerCase();
        for (const auto& kv : modulePinDatabase)
        {
            if (kv.first.compareIgnoreCase(moduleType) == 0 || kv.first.toLowerCase() == moduleTypeLower)
            {
                it = modulePinDatabase.find(kv.first);
                juce::Logger::writeToLog("[getPinsOfType] Found case-insensitive match: '" + moduleType + "' -> '" + kv.first + "'");
                break;
            }
        }
    }
    // --- END OF CASE-INSENSITIVE LOOKUP FIX ---

    if (it == modulePinDatabase.end())
    {
        juce::Logger::writeToLog("[getPinsOfType] ERROR: Module '" + moduleType + "' not in database");
        return matchingPins;
    }

    const auto& pins = isInput ? it->second.audioIns : it->second.audioOuts;
    juce::Logger::writeToLog("[getPinsOfType] Found " + juce::String(pins.size()) + " " + juce::String(isInput ? "input" : "output") + " pins total");

    for (const auto& pin : pins)
    {
        juce::Logger::writeToLog("[getPinsOfType] Checking pin '" + pin.name + "' (type=" + juce::String(toString(pin.type)) + ", channel=" + juce::String(pin.channel) + ")");
        if (pin.type == targetType)
        {
            matchingPins.push_back(pin);
            juce::Logger::writeToLog("[getPinsOfType] MATCH! Added pin '" + pin.name + "'");
        }
    }

    juce::Logger::writeToLog("[getPinsOfType] Returning " + juce::String(matchingPins.size()) + " matching pins");
    return matchingPins;
}

// Add this new function implementation to the .cpp file.

void ImGuiNodeEditorComponent::handleColorCodedChaining(PinDataType targetType)
{
    if (synth == nullptr)
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: synth is nullptr");
        return;
    }

    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected <= 1)
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: numSelected <= 1 (" + juce::String(numSelected) + ")");
        return;
    }

    juce::Logger::writeToLog("[Color Chaining] Started for " + juce::String(toString(targetType)) + " with " + juce::String(numSelected) + " nodes");

    // 1. Get and sort selected nodes by their horizontal position.
    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    std::vector<std::pair<float, int>> sortedNodes;
    for (int nodeId : selectedNodeIds)
    {
        if (nodeId == 0) continue; // Exclude the output node.
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeId);
        sortedNodes.push_back({pos.x, nodeId});
    }

    if (sortedNodes.empty())
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: No valid nodes after filtering");
        return;
    }

    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Create a single undo action for the entire operation.
    pushSnapshot();

    int totalConnectionsMade = 0;
    int totalConnectionAttempts = 0;

    // 2. Iterate through sorted nodes and connect matching pins.
    for (size_t i = 0; i < sortedNodes.size() - 1; ++i)
    {
        juce::uint32 sourceLid = sortedNodes[i].second;
        juce::uint32 destLid   = sortedNodes[i + 1].second;

        auto sourceNodeId = synth->getNodeIdForLogical(sourceLid);
        auto destNodeId   = synth->getNodeIdForLogical(destLid);

        if (sourceNodeId.uid == 0 || destNodeId.uid == 0)
        {
            juce::Logger::writeToLog("[Color Chaining] Skipping invalid node pair: " + juce::String(sourceLid) + " -> " + juce::String(destLid));
            continue;
        }

        // Find all matching output pins on the source and input pins on the destination.
        auto sourcePins = getPinsOfType(sourceLid, false, targetType);
        auto destPins   = getPinsOfType(destLid, true, targetType);

        if (sourcePins.empty() || destPins.empty())
        {
            juce::Logger::writeToLog("[Color Chaining] No matching pins: " + juce::String(sourcePins.size()) + " src, " + juce::String(destPins.size()) + " dst");
            continue;
        }

        // Connect them one-to-one until we run out of available pins on either side.
        int connectionsToMake = std::min((int)sourcePins.size(), (int)destPins.size());

        for (int j = 0; j < connectionsToMake; ++j)
        {
            totalConnectionAttempts++;
            bool connectResult = synth->connect(sourceNodeId, sourcePins[j].channel, destNodeId, destPins[j].channel);
            if (connectResult)
            {
                totalConnectionsMade++;
                juce::Logger::writeToLog("[Color Chaining] Connected " + getTypeForLogical(sourceLid) + " -> " + getTypeForLogical(destLid));
            }
        }
    }

    juce::Logger::writeToLog("[Color Chaining] Completed: " + juce::String(totalConnectionsMade) + "/" + juce::String(totalConnectionAttempts) + " connections made");

    // 3. Apply all new connections to the audio graph.
    graphNeedsRebuild = true;
}

