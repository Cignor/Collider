#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class GainProcessor : public juce::AudioProcessor
{
public:
    GainProcessor();
    ~GainProcessor() override = default;

    const juce::String getName() const override { return "GainProcessor"; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void setLinearGain (float newGain);
    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }

private:
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* gainParam { nullptr };
    juce::dsp::Gain<float> gain;
};


