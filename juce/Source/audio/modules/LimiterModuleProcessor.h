#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class LimiterModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdThreshold = "threshold";
    static constexpr auto paramIdRelease = "release";

    // Virtual IDs for modulation inputs
    static constexpr auto paramIdThresholdMod = "threshold_mod";
    static constexpr auto paramIdReleaseMod = "release_mod";

    LimiterModuleProcessor();
    ~LimiterModuleProcessor() override = default;

    const juce::String getName() const override { return "limiter"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // The core JUCE DSP Limiter object
    juce::dsp::Limiter<float> limiter;

    // Cached atomic pointers to parameters
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* releaseParam { nullptr };
};

