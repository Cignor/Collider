#pragma once

#include "ModuleProcessor.h"

class ScopeModuleProcessor : public ModuleProcessor
{
public:
    ScopeModuleProcessor();
    ~ScopeModuleProcessor() override = default;

    const juce::String getName() const override { return "scope"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In", 0);
        helpers.drawAudioOutputPin("Out", 0);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

    const juce::AudioBuffer<float>& getScopeBuffer() const { return scopeBuffer; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    juce::AudioBuffer<float> scopeBuffer; // mono buffer for display
    int writePos { 0 };

    // --- Rolling min/max over ~5 seconds (decimated) ---
    double currentSampleRate { 44100.0 };
    int decimation { 48 };           // ~1 kHz at 48 kHz
    int decimCounter { 0 };
    std::vector<float> history;      // ring buffer of decimated samples
    int histWrite { 0 };
    int histCount { 0 };
    int histCapacity { 5000 };       // seconds * (sr/decimation)
    float rollMin { 0.0f };
    float rollMax { 0.0f };
    std::atomic<float>* monitorSecondsParam { nullptr };
};


