#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

class ShapingOscillatorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdFrequency    = "frequency";
    static constexpr auto paramIdWaveform     = "waveform";
    static constexpr auto paramIdDrive        = "drive";
    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdFrequencyMod = "frequency_mod";
    static constexpr auto paramIdWaveformMod  = "waveform_mod";
    static constexpr auto paramIdDriveMod     = "drive_mod";

    ShapingOscillatorModuleProcessor();
    ~ShapingOscillatorModuleProcessor() override = default;

    const juce::String getName() const override { return "shaping oscillator"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oscillator<float> oscillator;

    // Cached parameter pointers
    std::atomic<float>* frequencyParam { nullptr };
    std::atomic<float>* waveformParam  { nullptr };
    std::atomic<float>* driveParam     { nullptr };

    // Smoothed values to prevent zipper noise
    juce::SmoothedValue<float> smoothedFrequency;
    juce::SmoothedValue<float> smoothedDrive;

    int currentWaveform = -1;
};

