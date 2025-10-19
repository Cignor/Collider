#pragma once

#include "ModuleProcessor.h"

/**
    A logic utility module that performs boolean operations on Gate signals.

    This module takes two Gate inputs and provides outputs for various logical
    operations: AND, OR, XOR, and NOT. Useful for creating complex gate patterns
    and conditional triggers in modular patches.
*/
class LogicModuleProcessor : public ModuleProcessor
{
public:
    LogicModuleProcessor();
    ~LogicModuleProcessor() override = default;

    const juce::String getName() const override { return "logic"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    /**
        Maps parameter IDs to their corresponding modulation bus and channel indices.
        
        @param paramId              The parameter ID to query.
        @param outBusIndex          Receives the bus index for modulation.
        @param outChannelIndexInBus Receives the channel index within the bus.
        @returns                    True if the parameter supports modulation.
    */
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Parameters
    std::atomic<float>* operationParam { nullptr };
    std::atomic<float>* gateThresholdParam { nullptr };
};