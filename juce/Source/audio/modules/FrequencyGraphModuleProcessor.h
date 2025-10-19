#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

class FrequencyGraphModuleProcessor : public ModuleProcessor
{
public:
    static constexpr auto paramIdDecay = "decay";
    static constexpr auto paramIdSubThreshold = "subThreshold";
    static constexpr auto paramIdBassThreshold = "bassThreshold";
    static constexpr auto paramIdMidThreshold = "midThreshold";
    static constexpr auto paramIdHighThreshold = "highThreshold";
    
    // CV Output channels are on Bus 1
    enum CVOutputChannel
    {
        SubGate = 0, SubTrig,
        BassGate, BassTrig,
        MidGate, MidTrig,
        HighGate, HighTrig,
        TotalCVOutputs // This will be 8
    };

    FrequencyGraphModuleProcessor();
    ~FrequencyGraphModuleProcessor() override = default;

    const juce::String getName() const override { return "frequency graph"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    
    std::vector<float> fftInputBuffer;
    std::vector<float> fftData;
    int samplesAccumulated = 0;

    juce::AbstractFifo abstractFifo;
    std::vector<std::vector<float>> fifoBuffer;

    std::vector<float> latestFftData;
    std::vector<float> peakHoldData;
    bool isFrozen = false;

    struct BandAnalyser
    {
        float thresholdDb = -24.0f;
        bool lastGateState = false;
        int triggerSamplesRemaining = 0;
    };
    std::array<BandAnalyser, 4> bandAnalysers;

    std::atomic<float>* decayParam { nullptr };
    std::atomic<float>* subThresholdParam { nullptr };
    std::atomic<float>* bassThresholdParam { nullptr };
    std::atomic<float>* midThresholdParam { nullptr };
    std::atomic<float>* highThresholdParam { nullptr };
};
