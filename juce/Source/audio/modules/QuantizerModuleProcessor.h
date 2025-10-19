#pragma once

#include "ModuleProcessor.h"

class QuantizerModuleProcessor : public ModuleProcessor
{
public:
    QuantizerModuleProcessor();
    ~QuantizerModuleProcessor() override = default;

    const juce::String getName() const override { return "quantizer"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    void drawIoPins(const NodePinHelpers& helpers) override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In";
            case 1: return "Scale Mod";
            case 2: return "Root Mod";
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
    
    // Parameter bus contract implementation

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameter pointers
    juce::AudioParameterChoice* scaleParam { nullptr };
    juce::AudioParameterInt* rootNoteParam { nullptr };
    juce::AudioParameterFloat* scaleModParam { nullptr };
    juce::AudioParameterFloat* rootModParam { nullptr };

    // Scale definitions
    std::vector<std::vector<float>> scales;
};
