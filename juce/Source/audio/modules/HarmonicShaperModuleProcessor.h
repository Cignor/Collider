#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

class HarmonicShaperModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int NUM_OSCILLATORS = 8;

    // --- Parameter IDs ---
    static constexpr auto paramIdMasterFreq   = "masterFrequency";
    static constexpr auto paramIdMasterDrive  = "masterDrive";
    static constexpr auto paramIdOutputGain   = "outputGain";
    // Modulation targets
    static constexpr auto paramIdMasterFreqMod = "masterFrequency_mod";
    static constexpr auto paramIdMasterDriveMod = "masterDrive_mod";

    HarmonicShaperModuleProcessor();
    ~HarmonicShaperModuleProcessor() override = default;

    const juce::String getName() const override { return "harmonic_shaper"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    std::array<juce::dsp::Oscillator<float>, NUM_OSCILLATORS> oscillators;
    std::array<int, NUM_OSCILLATORS> currentWaveforms;

    // --- Cached Parameter Pointers ---
    std::atomic<float>* masterFreqParam { nullptr };
    std::atomic<float>* masterDriveParam { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    std::array<std::atomic<float>*, NUM_OSCILLATORS> ratioParams;
    std::array<std::atomic<float>*, NUM_OSCILLATORS> detuneParams;
    std::array<std::atomic<float>*, NUM_OSCILLATORS> waveformParams;
    std::array<std::atomic<float>*, NUM_OSCILLATORS> driveParams;
    std::array<std::atomic<float>*, NUM_OSCILLATORS> levelParams;

    // --- Smoothed Values for Zipper-Free Modulation ---
    juce::SmoothedValue<float> smoothedMasterFreq;
    juce::SmoothedValue<float> smoothedMasterDrive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicShaperModuleProcessor);
};

