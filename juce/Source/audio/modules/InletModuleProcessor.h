#pragma once

#include "ModuleProcessor.h"

/**
    InletModuleProcessor - Acts as a signal inlet for Meta Modules
    
    This module has no inputs (inside the meta module) and provides outputs
    that represent signals coming from outside the meta module.
    
    From the outside perspective, the MetaModule will have input pins that
    correspond to these Inlet modules inside.
*/
class InletModuleProcessor : public ModuleProcessor
{
public:
    static constexpr auto paramIdLabel = "label";
    static constexpr auto paramIdChannelCount = "channelCount";
    
    InletModuleProcessor();
    ~InletModuleProcessor() override = default;

    const juce::String getName() const override { return "Inlet"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    int getPinIndex() const noexcept { return pinIndex; }
    void setPinIndex(int index) noexcept { pinIndex = index; }
    const juce::String& getCustomLabel() const noexcept { return customLabel; }

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Store the buffer passed from the parent MetaModule
    void setIncomingBuffer(const juce::AudioBuffer<float>* buffer) { incomingBuffer = buffer; }

    juce::uint32 getExternalLogicalId() const noexcept { return externalLogicalId; }
    int getExternalChannel() const noexcept { return externalChannel; }
    void setExternalMapping(juce::uint32 logicalId, int channel) noexcept
    {
        externalLogicalId = logicalId;
        externalChannel = channel;
    }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Inlets have no inputs, only outputs
        auto& ap = getAPVTS();
        int channelCount = 2;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
            channelCount = p->get();
            
        for (int i = 0; i < channelCount; ++i)
            helpers.drawAudioOutputPin(juce::String("Out " + juce::String(i + 1)).toRawUTF8(), i);
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        return juce::String("Out ") + juce::String(channel + 1);
    }
#endif

    // Extra state for label
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    const juce::AudioBuffer<float>* incomingBuffer { nullptr };
    juce::String customLabel;
    int pinIndex { 0 };
    juce::uint32 externalLogicalId { 0 };
    int externalChannel { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InletModuleProcessor)
};

