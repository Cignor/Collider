#pragma once

#include <functional>
#include <juce_core/juce_core.h>

struct NodePinHelpers
{
    // Draws a standard audio input pin.
    std::function<void(const char* label, int channelIndex)> drawAudioInputPin;

    // Draws a standard audio output pin.
    std::function<void(const char* label, int channelIndex)> drawAudioOutputPin;

    // Draws input and output pins on the same row (for compact layouts).
    std::function<void(const char* inLabel, int inChannel, const char* outLabel, int outChannel)> drawParallelPins;
};


