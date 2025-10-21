#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

class PolyVCOModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_VOICES = 32;

    PolyVCOModuleProcessor();
    ~PolyVCOModuleProcessor() override = default;

    const juce::String getName() const override { return "polyvco"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    // Parameter bus contract implementation (must be available in Collider too)
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int getEffectiveNumVoices() const;
    
    juce::String getAudioInputLabel(int channel) const override;

    juce::AudioProcessorValueTreeState apvts;

    // Global control
    juce::AudioParameterInt* numVoicesParam { nullptr };

    // Per-voice parameters
    std::vector<juce::AudioParameterFloat*> voiceFreqParams;
    std::vector<juce::AudioParameterChoice*> voiceWaveParams;
    std::vector<juce::AudioParameterFloat*> voiceGateParams;

    // DSP engines - pre-initialized for each waveform to avoid audio thread allocations
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> sineOscillators;
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> sawOscillators;
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> squareOscillators;
    
    // Track the current waveform for each voice
    std::array<int, MAX_VOICES> currentWaveforms;
    
    // --- ADD GATE SMOOTHING ---
    std::array<float, MAX_VOICES> smoothedGateLevels {};

    // Clickless gate: per-voice envelope and hysteresis state
    std::array<float, MAX_VOICES> gateEnvelope {};
    std::array<uint8_t, MAX_VOICES> gateOnState {}; // 0/1 with hysteresis
};
