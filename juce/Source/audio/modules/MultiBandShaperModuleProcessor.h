#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

class MultiBandShaperModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int NUM_BANDS = 8;

    MultiBandShaperModuleProcessor();
    ~MultiBandShaperModuleProcessor() override = default;

    const juce::String getName() const override { return "8bandshaper"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;

    // A filter for each band (stereo)
    using IIRFilter = juce::dsp::IIR::Filter<float>;
    std::array<std::array<IIRFilter, 2>, NUM_BANDS> filters; // [band][channel]
    
    // A parameter pointer for each band's drive
    std::array<std::atomic<float>*, NUM_BANDS> driveParams;
    std::atomic<float>* outputGainParam { nullptr };
    
    // Relative modulation parameters (one for each band + one for output gain)
    std::array<std::atomic<float>*, NUM_BANDS> relativeDriveModParams;
    std::atomic<float>* relativeGainModParam { nullptr };

    // Pre-allocated working buffers to avoid real-time memory allocation
    juce::AudioBuffer<float> bandBuffer;
    juce::AudioBuffer<float> sumBuffer;
    
    // Center frequencies for the 8 bands
    static constexpr float centerFreqs[NUM_BANDS] = { 
        60.0f, 150.0f, 400.0f, 1000.0f, 2400.0f, 5000.0f, 10000.0f, 16000.0f 
    };
};

