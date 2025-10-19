#pragma once

#include "ModuleProcessor.h"

class MixerModuleProcessor : public ModuleProcessor
{
public:
    MixerModuleProcessor();
    ~MixerModuleProcessor() override = default;

    const juce::String getName() const override { return "mixer"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    void drawIoPins(const NodePinHelpers& helpers) override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In A L";
            case 1: return "In A R";
            case 2: return "In B L";
            case 3: return "In B R";
            case 4: return "Gain Mod";
            case 5: return "Pan Mod";
            case 6: return "X-Fade Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out L";
            case 1: return "Out R";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
    
    // Parameter bus contract implementation

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* gainParam { nullptr };       // dB
    std::atomic<float>* panParam { nullptr };        // -1..1
    std::atomic<float>* crossfadeParam { nullptr };  // <<< ADD THIS LINE
};