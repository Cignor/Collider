#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

/**
 * @class NoiseModuleProcessor
 * @brief Generates white, pink, or brown noise with controllable level.
 *
 * This module acts as a sound source, providing different colors of noise. Both the
 * noise color and the output level can be modulated via CV inputs.
 */
class NoiseModuleProcessor : public ModuleProcessor
{
public:
    // Parameter ID constants for APVTS and modulation routing
    static constexpr auto paramIdLevel = "level";
    static constexpr auto paramIdColour = "colour";
    // ADDED: Virtual parameter IDs for modulation inputs
    static constexpr auto paramIdLevelMod = "level_mod";
    static constexpr auto paramIdColourMod = "colour_mod";

    NoiseModuleProcessor();
    ~NoiseModuleProcessor() override = default;

    const juce::String getName() const override { return "noise"; }

    // --- Audio Processing ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- Required by ModuleProcessor ---
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // --- Parameter Modulation Routing ---
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    // --- UI Drawing and Pin Definitions ---
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;

    // --- Parameters ---
    std::atomic<float>* levelDbParam{ nullptr };
    juce::AudioParameterChoice* colourParam{ nullptr };

    // --- DSP State ---
    juce::Random random;
    juce::dsp::IIR::Filter<float> pinkFilter;  // Simple filter to approximate pink noise
    juce::dsp::IIR::Filter<float> brownFilter; // Simple filter to approximate brown noise

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseModuleProcessor)
};