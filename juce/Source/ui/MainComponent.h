#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "VisualiserComponent.h"
#include "TestHarnessComponent.h" // Add this include

// Forward declarations for audio stubs
class AudioEngine;
class CommandProcessor;

class MainComponent : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // AudioAppComponent hooks (pass-through to AudioEngine)
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    juce::Label statusLabel;
    juce::Slider masterVolumeSlider;
public:
    juce::Label connLabel;
    juce::Timer* timerHook { nullptr };
    std::unique_ptr<AudioEngine> audioEngine;
    TestHarnessComponent testHarness; // ADD THIS
    std::unique_ptr<VisualiserComponent> visualiser;
};


