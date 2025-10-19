#pragma once

#include "ModuleProcessor.h"

class SequentialSwitchModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdThreshold1 = "threshold1";
    static constexpr auto paramIdThreshold2 = "threshold2";
    static constexpr auto paramIdThreshold3 = "threshold3";
    static constexpr auto paramIdThreshold4 = "threshold4";
    
    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdThreshold1Mod = "threshold1_mod";
    static constexpr auto paramIdThreshold2Mod = "threshold2_mod";
    static constexpr auto paramIdThreshold3Mod = "threshold3_mod";
    static constexpr auto paramIdThreshold4Mod = "threshold4_mod";

    SequentialSwitchModuleProcessor();
    ~SequentialSwitchModuleProcessor() override = default;

    const juce::String getName() const override { return "sequentialswitch"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Cached parameter pointers
    std::atomic<float>* threshold1Param { nullptr };
    std::atomic<float>* threshold2Param { nullptr };
    std::atomic<float>* threshold3Param { nullptr };
    std::atomic<float>* threshold4Param { nullptr };
};
