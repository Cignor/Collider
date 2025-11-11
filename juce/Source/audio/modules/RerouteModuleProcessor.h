#pragma once

#include "ModuleProcessor.h"

/**
    A minimal pass-through utility module used to reroute cables.

    Provides a single audio/CV channel input and output with no parameters.
*/
class RerouteModuleProcessor : public ModuleProcessor
{
public:
    RerouteModuleProcessor();
    ~RerouteModuleProcessor() override = default;

    const juce::String getName() const override { return "reroute"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    void setPassthroughType(PinDataType newType);
    PinDataType getPassthroughType() const;

    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

#if defined(PRESET_CREATOR_UI)
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr PinDataType kDefaultType = PinDataType::Audio;

    std::atomic<PinDataType> currentType { kDefaultType };
    juce::AudioProcessorValueTreeState apvts;
};

