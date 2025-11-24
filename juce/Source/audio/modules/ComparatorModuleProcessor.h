#pragma once

#include "ModuleProcessor.h"

class ComparatorModuleProcessor : public ModuleProcessor
{
public:
    ComparatorModuleProcessor();
    ~ComparatorModuleProcessor() override = default;

    const juce::String getName() const override { return "comparator"; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    bool usesCustomPinLayout() const override { return true; }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* thresholdParam { nullptr };
};