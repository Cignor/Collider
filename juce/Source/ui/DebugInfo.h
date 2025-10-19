// Rationale: This header defines lightweight diagnostics containers we can pass
// from the audio engine to the UI without coupling UI code into the engine.
// We keep them simple, POD-like, and JUCE-friendly to allow lock-free updates
// (e.g., via copies or atomics) if needed.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

enum class VoiceDebugState
{
    Created,
    Prepared,
    Playing,
    Stopping,
    Silent,
    Clipping,
    Error
};

struct VoiceDebugInfo
{
    juce::Point<float> position { 0.0f, 0.0f };
    VoiceDebugState    state { VoiceDebugState::Created };
    float              gain { 0.0f };
    float              pan  { 0.0f };
};

struct VisualiserState
{
    juce::Point<float>              listenerPosition { 0.0f, 0.0f };
    juce::Array<VoiceDebugInfo>     voices;
};


