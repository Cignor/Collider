#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

class GraphicEQModuleProcessor : public ModuleProcessor
{
public:
    enum CVOutputChannel
    {
        GateOut = 0,
        TrigOut,
        TotalCVOutputs // = 2
    };

    GraphicEQModuleProcessor();
    ~GraphicEQModuleProcessor() override = default;

    const juce::String getName() const override { return "graphic_eq"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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

    // A chain of 8 stereo IIR filters, one for each band.
    using FilterBand = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    using EQChain = juce::dsp::ProcessorChain<FilterBand, FilterBand, FilterBand, FilterBand, FilterBand, FilterBand, FilterBand, FilterBand>;
    EQChain processorChain;

    // Store atomic pointers to all 8 band gain parameters and the output level
    std::array<std::atomic<float>*, 8> bandGainParams;
    std::array<std::atomic<float>*, 8> relativeBandModParams;
    std::atomic<float>* outputLevelParam { nullptr };
    std::atomic<float>* gateThresholdParam { nullptr };
    std::atomic<float>* triggerThresholdParam { nullptr };
    std::atomic<float>* relativeGateThresholdModParam { nullptr };
    std::atomic<float>* relativeTriggerThresholdModParam { nullptr };

    // Define the fixed center frequencies for our 8 bands
    static constexpr std::array<float, 8> centerFrequencies = { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f };

    // Gate/Trigger state tracking (audio-thread only)
    bool lastTriggerState = false;
    int triggerPulseSamplesRemaining = 0;

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        std::array<std::atomic<float>, 8> bandLevelDb;
        std::array<std::atomic<float>, 8> bandGainDb;
        std::atomic<float> gateThresholdDb { -30.0f };
        std::atomic<float> triggerThresholdDb { -6.0f };
        std::atomic<float> outputLevelDb { -60.0f };
        std::atomic<int> gateActive { 0 };
        std::atomic<int> triggerActive { 0 };

        VizData()
        {
            for (auto& v : bandLevelDb) v.store(-60.0f);
            for (auto& v : bandGainDb) v.store(0.0f);
        }
    };

    VizData vizData;
    std::array<juce::dsp::IIR::Filter<float>, 8> vizBandFilters;
    std::array<float, 8> vizBandAccum { { 0.0f } };
#endif
};

