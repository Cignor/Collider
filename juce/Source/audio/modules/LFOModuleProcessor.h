#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class LFOModuleProcessor : public ModuleProcessor
{
public:
    // Parameter ID constants
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdDepth = "depth";
    static constexpr auto paramIdWave = "wave";
    static constexpr auto paramIdBipolar = "bipolar";
    static constexpr auto paramIdRateMod = "rate_mod";
    static constexpr auto paramIdDepthMod = "depth_mod";
    static constexpr auto paramIdWaveMod = "wave_mod";

    LFOModuleProcessor();
    ~LFOModuleProcessor() override = default;

    const juce::String getName() const override { return "lfo"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oscillator<float> osc;
    
    // Cached parameter pointers
    std::atomic<float>* rateParam{ nullptr };
    std::atomic<float>* depthParam{ nullptr };
    std::atomic<float>* bipolarParam{ nullptr };
    std::atomic<float>* waveParam{ nullptr };
    
    int currentWaveform = -1;
};