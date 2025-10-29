#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class CompressorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdThreshold = "threshold";
    static constexpr auto paramIdRatio = "ratio";
    static constexpr auto paramIdAttack = "attack";
    static constexpr auto paramIdRelease = "release";
    static constexpr auto paramIdMakeup = "makeup";

    // Virtual IDs for modulation inputs
    static constexpr auto paramIdThresholdMod = "threshold_mod";
    static constexpr auto paramIdRatioMod = "ratio_mod";
    static constexpr auto paramIdAttackMod = "attack_mod";
    static constexpr auto paramIdReleaseMod = "release_mod";
    static constexpr auto paramIdMakeupMod = "makeup_mod";

    CompressorModuleProcessor();
    ~CompressorModuleProcessor() override = default;

    const juce::String getName() const override { return "compressor"; }

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

    // The core JUCE DSP Compressor object
    juce::dsp::Compressor<float> compressor;

    // Cached atomic pointers to parameters
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* ratioParam { nullptr };
    std::atomic<float>* attackParam { nullptr };
    std::atomic<float>* releaseParam { nullptr };
    std::atomic<float>* makeupParam { nullptr };
    
    // Relative modulation parameters
    std::atomic<float>* relativeThresholdModParam { nullptr };
    std::atomic<float>* relativeRatioModParam { nullptr };
    std::atomic<float>* relativeAttackModParam { nullptr };
    std::atomic<float>* relativeReleaseModParam { nullptr };
    std::atomic<float>* relativeMakeupModParam { nullptr };
};

