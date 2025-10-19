#pragma once

#include "ModuleProcessor.h"

class MathModuleProcessor : public ModuleProcessor
{
public:
    MathModuleProcessor();
    ~MathModuleProcessor() override = default;

    const juce::String getName() const override { return "math"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In A";
            case 1: return "In B";
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

    float getLastValue() const;
    float getLastValueA() const;
    float getLastValueB() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* valueAParam { nullptr };
    std::atomic<float>* valueBParam { nullptr };
    std::atomic<float>* operationParam { nullptr };

    std::atomic<float> lastValue { 0.0f };
    std::atomic<float> lastValueA { 0.0f };
    std::atomic<float> lastValueB { 0.0f };
};
