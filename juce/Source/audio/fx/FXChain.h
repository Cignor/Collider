// Minimal stub to keep legacy references compiling. The project now uses per-voice FX.
#pragma once

#include <juce_dsp/juce_dsp.h>

struct FXParameters { };

class FXChain
{
public:
    void prepare (double sampleRate, int samplesPerBlock)
    {
        juce::ignoreUnused (sampleRate, samplesPerBlock);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        juce::ignoreUnused (buffer);
    }
};