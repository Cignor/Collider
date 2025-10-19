#pragma once
#include "../graph/VoiceProcessor.h"
#include <juce_dsp/juce_dsp.h>

class NoiseVoiceProcessor : public VoiceProcessor
{
public:
    NoiseVoiceProcessor() = default;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void renderBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    juce::dsp::Oscillator<float> lfo { [] (float x) { return std::sin (x); } };
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::Random random;
};