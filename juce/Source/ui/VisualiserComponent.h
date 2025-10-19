// Rationale: VisualiserComponent renders a minimal debug dashboard of the
// listener and active voices using the VisualiserState from AudioEngine.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "DebugInfo.h"

class AudioEngine;

class VisualiserComponent : public juce::Component, private juce::Timer
{
public:
    explicit VisualiserComponent (AudioEngine& engineRef);
    ~VisualiserComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Point<int> worldToScreen (juce::Point<float> p) const;
    void drawLegend (juce::Graphics& g) const;

    AudioEngine& engine;
    // World coordinate bounds (from Python game): origin at (0,0), width=1920, height=1080
    juce::Rectangle<float> worldBounds { 0.0f, 0.0f, 1920.0f, 1080.0f };
};


