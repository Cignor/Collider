#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include "../audio/graph/ModularSynthProcessor.h"
#include "PinDatabase.h"

enum class PatchArchetype
{
    EastCoast,    // Subtractive
    WestCoast,    // Additive/FM
    AmbientDrone, // Texture
    TechnoBass,   // Acid
    Glitch,       // Chaos
    Ethereal,     // Super Saw Pad
    AcidLead,     // 303-style lead
    Pluck,        // Plucked string
    WarmPad,      // Warm pad sound
    DeepBass,      // Deep bass
    BrightLead,   // Bright lead synth
    Arpeggio,     // Arpeggiated sequence
    Percussion,   // Drum-like sounds
    ChordProg,    // Chord progression
    NoiseSweep,   // Noise with filter sweep
    FM,           // Frequency modulation
    Granular,     // Granular synthesis
    DelayLoop,    // Delay feedback loops
    ReverbWash,   // Reverb-heavy ambient
    Distorted,    // Heavy distortion
    WobbleBass,   // Wobble bass
    Stutter,      // Stuttering/glitchy
    Harmonic,     // Harmonic-rich
    Minimal,      // Minimalist
    Complex,      // Complex modulation
    Experimental, // Experimental/weird
    Random        // Surprise Me
};

class PatchGenerator
{
public:
    static void generate(
        ModularSynthProcessor* synth,
        PatchArchetype         type,
        float                  chaosAmount = 0.0f);

    // Get node positions that were set during patch generation
    // These can be applied to the UI component's pendingNodePositions
    static std::map<juce::uint32, juce::Point<float>> getNodePositions() { return nodePositions; }
    static void clearNodePositions() { nodePositions.clear(); }

private:
    // Static storage for node positions (logical ID -> position)
    static std::map<juce::uint32, juce::Point<float>> nodePositions;

    // --- Helper Functions ---
    static juce::uint32 addModule(
        ModularSynthProcessor* synth,
        const juce::String&    type,
        float                  x,
        float                  y);
    
    // Pin/Parameter Query Helpers
    static int findPinIndex(const juce::String& moduleType, const juce::String& pinName, bool isOutput);
    static bool paramExists(ModularSynthProcessor* synth, juce::uint32 moduleId, const juce::String& paramId);
    
    // Safe Connection/Parameter Setting (with validation and logging)
    static bool safeConnect(
        ModularSynthProcessor* synth,
        juce::uint32           sourceId,
        const juce::String&    sourcePinName,
        juce::uint32           destId,
        const juce::String&    destPinName);
    static bool safeConnect(
        ModularSynthProcessor* synth,
        juce::uint32           sourceId,
        int                    sourcePin,
        juce::uint32           destId,
        int                    destPin);
    static bool safeSetParam(
        ModularSynthProcessor* synth,
        juce::uint32           moduleId,
        const juce::String&    paramId,
        float                  value);
    
    // Legacy functions (kept for compatibility, but should use safe versions)
    static void connect(
        ModularSynthProcessor* synth,
        juce::uint32           sourceId,
        int                    sourcePin,
        juce::uint32           destId,
        int                    destPin);
    static void setParam(
        ModularSynthProcessor* synth,
        juce::uint32           moduleId,
        const juce::String&    paramId,
        float                  value);

    static float getRandomNote(float root, const std::vector<float>& scale, juce::Random& rng);

    static void connectComplexControl(
        ModularSynthProcessor* synth,
        juce::uint32           seqId,
        juce::uint32           compId,
        juce::uint32           funcGenId,
        int                    seqStep);

    // --- Recipes ---
    static void generateEastCoast(ModularSynthProcessor* synth, float chaos);
    static void generateWestCoast(ModularSynthProcessor* synth, float chaos);
    static void generateAmbient(ModularSynthProcessor* synth, float chaos);
    static void generateTechnoBass(ModularSynthProcessor* synth, float chaos);
    static void generateGlitch(ModularSynthProcessor* synth, float chaos);
    static void generateEthereal(ModularSynthProcessor* synth, float chaos);
    static void generateAcidLead(ModularSynthProcessor* synth, float chaos);
    static void generatePluck(ModularSynthProcessor* synth, float chaos);
    static void generateWarmPad(ModularSynthProcessor* synth, float chaos);
    static void generateDeepBass(ModularSynthProcessor* synth, float chaos);
    static void generateBrightLead(ModularSynthProcessor* synth, float chaos);
    static void generateArpeggio(ModularSynthProcessor* synth, float chaos);
    static void generatePercussion(ModularSynthProcessor* synth, float chaos);
    static void generateChordProg(ModularSynthProcessor* synth, float chaos);
    static void generateNoiseSweep(ModularSynthProcessor* synth, float chaos);
    static void generateFM(ModularSynthProcessor* synth, float chaos);
    static void generateGranular(ModularSynthProcessor* synth, float chaos);
    static void generateDelayLoop(ModularSynthProcessor* synth, float chaos);
    static void generateReverbWash(ModularSynthProcessor* synth, float chaos);
    static void generateDistorted(ModularSynthProcessor* synth, float chaos);
    static void generateWobbleBass(ModularSynthProcessor* synth, float chaos);
    static void generateStutter(ModularSynthProcessor* synth, float chaos);
    static void generateHarmonic(ModularSynthProcessor* synth, float chaos);
    static void generateMinimal(ModularSynthProcessor* synth, float chaos);
    static void generateComplex(ModularSynthProcessor* synth, float chaos);
    static void generateExperimental(ModularSynthProcessor* synth, float chaos);
};
