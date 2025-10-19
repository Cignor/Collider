#include "VisualiserComponent.h"
#include "DebugInfo.h"
#include "../audio/AudioEngine.h"

VisualiserComponent::VisualiserComponent (AudioEngine& engineRef)
    : engine (engineRef)
{
    startTimerHz (30);
}

VisualiserComponent::~VisualiserComponent()
{
    stopTimer();
}

void VisualiserComponent::timerCallback()
{
    repaint();
}

void VisualiserComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto state = engine.getVisualiserState();

    // Draw listener
    g.setColour (juce::Colours::white);
    auto lp = worldToScreen (state.listenerPosition);
    g.fillEllipse ((float) lp.x - 6.0f, (float) lp.y - 6.0f, 12.0f, 12.0f);

    // Draw voices
    for (auto& v : state.voices)
    {
        juce::Colour c = juce::Colours::grey;
        switch (v.state)
        {
            case VoiceDebugState::Playing:  c = juce::Colours::yellow; break;
            case VoiceDebugState::Stopping: c = juce::Colours::orange; break;
            case VoiceDebugState::Clipping: c = juce::Colours::red;    break;
            case VoiceDebugState::Prepared: c = juce::Colours::cornflowerblue; break;
            case VoiceDebugState::Silent:   c = juce::Colours::darkgrey; break;
            case VoiceDebugState::Error:    c = juce::Colours::deeppink; break;
            case VoiceDebugState::Created:  c = juce::Colours::lightblue; break;
        }
        g.setColour (c);
        auto p = worldToScreen (v.position);
        g.fillEllipse ((float) p.x - 4.0f, (float) p.y - 4.0f, 8.0f, 8.0f);
    }

    drawLegend (g);
}

void VisualiserComponent::resized()
{
}

juce::Point<int> VisualiserComponent::worldToScreen (juce::Point<float> p) const
{
    auto screen = getLocalBounds();
    const float wx0 = worldBounds.getX();
    const float wy0 = worldBounds.getY();
    const float wx1 = worldBounds.getRight();
    const float wy1 = worldBounds.getBottom();

    // Map X: [wx0, wx1] -> [screen.getX(), screen.getRight()]
    const float sx = juce::jmap (p.x, wx0, wx1, (float) screen.getX(), (float) screen.getRight());

    // Map Y (invert): game Y increases up, screen Y increases down
    // World Y=wy0 (bottom) should map to screen.getBottom()
    // World Y=wy1 (top)    should map to screen.getY()
    const float sy = juce::jmap (p.y, wy0, wy1, (float) screen.getBottom(), (float) screen.getY());

    const int ix = juce::jlimit (screen.getX(), screen.getRight(), (int) std::lround (sx));
    const int iy = juce::jlimit (screen.getY(), screen.getBottom(), (int) std::lround (sy));
    return { ix, iy };
}

void VisualiserComponent::drawLegend (juce::Graphics& g) const
{
    juce::Rectangle<int> panel (10, getHeight() - 110, 260, 100);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (panel.toFloat(), 6.0f);
    g.setColour (juce::Colours::white);
    g.drawText ("Legend:", panel.removeFromTop (18), juce::Justification::left);

    auto row = [&](juce::Colour col, const juce::String& text, int y) {
        g.setColour (col); g.fillEllipse (14.0f, (float) y + 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white); g.drawText (text, 30, y, 220, 16, juce::Justification::left);
    };
    int baseY = getHeight() - 88;
    row (juce::Colours::yellow, "Playing", baseY);
    row (juce::Colours::orange, "Stopping", baseY + 16);
    row (juce::Colours::red,    "Clipping", baseY + 32);
    row (juce::Colours::cornflowerblue, "Prepared", baseY + 48);
    row (juce::Colours::darkgrey, "Silent", baseY + 64);
}


