#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief MIDI to CV/Gate Converter Module
 * * Converts incoming MIDI messages to CV and Gate signals:
 * - Pitch: MIDI note number converted to 1V/octave standard
 * - Gate: High when note is held, low when released
 * - Velocity: MIDI velocity normalized to 0-1
 * - Mod Wheel: CC#1 normalized to 0-1
 * - Pitch Bend: Pitch bend wheel normalized to -1 to +1
 * - Aftertouch: Channel pressure normalized to 0-1
 * * This module allows MIDI keyboards and controllers to drive the modular synth.
 */
class MIDICVModuleProcessor : public ModuleProcessor
{
public:
    MIDICVModuleProcessor();
    ~MIDICVModuleProcessor() override = default;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    const juce::String getName() const override { return "MIDI CV"; }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return dummyApvts; }

#if defined(PRESET_CREATOR_UI)
    // ADD THESE TWO FUNCTION DECLARATIONS
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    // Current MIDI state
    struct MIDIState
    {
        int currentNote = -1;        // -1 = no note playing
        float currentVelocity = 0.0f;
        float modWheel = 0.0f;
        float pitchBend = 0.0f;      // -1 to +1
        float aftertouch = 0.0f;
        bool gateHigh = false;
    } midiState;

    // Dummy APVTS to satisfy base class requirement
    juce::AudioProcessorValueTreeState dummyApvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() { return {}; }

    // Convert MIDI note number to CV (1V/octave, where C4 = 60 = 0V)
    float midiNoteToCv(int noteNumber) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MIDICVModuleProcessor)
};