#pragma once

#include "ModuleProcessor.h"

class RandomModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs for APVTS
    static constexpr auto paramIdMin = "min";
    static constexpr auto paramIdMax = "max";
    static constexpr auto paramIdCvMin = "cvMin";
    static constexpr auto paramIdCvMax = "cvMax";
    static constexpr auto paramIdNormMin = "normMin";
    static constexpr auto paramIdNormMax = "normMax";
    static constexpr auto paramIdSlew = "slew";
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdTrigThreshold = "trigThreshold";
    
    // Virtual modulation and input IDs (no APVTS parameters required)
    static constexpr auto paramIdTriggerIn = "trigger_in";
    static constexpr auto paramIdRateMod = "rate_mod";
    static constexpr auto paramIdSlewMod = "slew_mod";

    RandomModuleProcessor();
    ~RandomModuleProcessor() override = default;

    const juce::String getName() const override { return "random"; }

    // --- Audio Processing ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- Required by ModuleProcessor ---
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- UI Display Helpers ---
    float getLastOutputValue() const;
    float getLastNormalizedOutputValue() const;
    float getLastCvOutputValue() const;
    float getLastBoolOutputValue() const;
    float getLastTrigOutputValue() const;

#if defined(PRESET_CREATOR_UI)
    // --- UI Drawing and Pin Definitions ---
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    
    // --- Parameter Modulation Routing ---
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    juce::Random rng;
    
    // --- Parameter Pointers ---
    std::atomic<float>* minParam{ nullptr };
    std::atomic<float>* maxParam{ nullptr };
    std::atomic<float>* cvMinParam{ nullptr };
    std::atomic<float>* cvMaxParam{ nullptr };
    std::atomic<float>* normMinParam{ nullptr };
    std::atomic<float>* normMaxParam{ nullptr };
    std::atomic<float>* slewParam{ nullptr };
    std::atomic<float>* rateParam{ nullptr };
    std::atomic<float>* trigThresholdParam{ nullptr };
    
    // --- DSP State ---
    float currentValue{ 0.0f };
    float targetValue{ 0.0f };
    float currentValueCV{ 0.0f };
    float targetValueCV{ 0.0f };
    double sampleRate{ 44100.0 };
    double phase{ 0.0 }; // Phase accumulator for internal clock
    bool lastTriggerState { false }; // For external trigger edge detection
    int trigPulseRemaining { 0 }; // For the trigger output
    
    // --- Smoothed values for modulated parameters ---
    juce::SmoothedValue<float> smoothedRate;
    juce::SmoothedValue<float> smoothedSlew;
    
    // --- Telemetry for UI ---
    std::atomic<float> lastOutputValue{ 0.0f };
    std::atomic<float> lastNormalizedOutputValue{ 0.0f };
    std::atomic<float> lastCvOutputValue{ 0.0f };
    std::atomic<float> lastBoolOutputValue{ 0.0f };
    std::atomic<float> lastTrigOutputValue { 0.0f };
};