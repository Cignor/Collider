#pragma once

#include "ModuleProcessor.h"

/**
    OutletModuleProcessor - Acts as a signal outlet for Meta Modules
    
    This module has inputs but no outputs (inside the meta module).
    It collects signals from inside the meta module that should be
    sent to the outside.
    
    From the outside perspective, the MetaModule will have output pins that
    correspond to these Outlet modules inside.
*/
class OutletModuleProcessor : public ModuleProcessor
{
public:
    static constexpr auto paramIdLabel = "label";
    static constexpr auto paramIdChannelCount = "channelCount";
    
    OutletModuleProcessor();
    ~OutletModuleProcessor() override = default;

    const juce::String getName() const override { return "Outlet"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    int getPinIndex() const noexcept { return pinIndex; }
    void setPinIndex(int index) noexcept { pinIndex = index; }
    const juce::String& getCustomLabel() const noexcept { return customLabel; }

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Get the processed buffer to send to the parent MetaModule's output
    const juce::AudioBuffer<float>& getOutputBuffer() const { return cachedBuffer; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Outlets have inputs but no outputs
        auto& ap = getAPVTS();
        int channelCount = 2;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
            channelCount = p->get();
            
        for (int i = 0; i < channelCount; ++i)
            helpers.drawAudioInputPin(juce::String("In " + juce::String(i + 1)).toRawUTF8(), i);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        return juce::String("In ") + juce::String(channel + 1);
    }
#endif

    // Extra state for label
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioBuffer<float> cachedBuffer;
    juce::String customLabel;
    int pinIndex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutletModuleProcessor)
};

