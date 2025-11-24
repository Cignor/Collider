#pragma once

#include "ModuleProcessor.h"
#include <array>
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

class RandomModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdMin = "min";
    static constexpr auto paramIdMax = "max";
    static constexpr auto paramIdCvMin = "cvMin";
    static constexpr auto paramIdCvMax = "cvMax";
    static constexpr auto paramIdNormMin = "normMin";
    static constexpr auto paramIdNormMax = "normMax";
    static constexpr auto paramIdSlew = "slew";
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdTrigThreshold = "trigThreshold";

    RandomModuleProcessor();
    ~RandomModuleProcessor() override = default;

    const juce::String getName() const override { return "random"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection for BPM Monitor
    std::optional<RhythmInfo> getRhythmInfo() const override;

    // State management for transport settings
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree&) override;

    // UI Display Helpers
    float getLastOutputValue() const;
    float getLastNormalizedOutputValue() const;
    float getLastCvOutputValue() const;
    float getLastBoolOutputValue() const;
    float getLastTrigOutputValue() const;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    bool usesCustomPinLayout() const override { return true; }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void setTimingInfo(const TransportState& state) override;
    void forceStop() override; // Force stop (used after patch load)
    
    juce::AudioProcessorValueTreeState apvts;
    juce::Random rng;

    TransportState m_currentTransport;
    
    // Parameter Pointers
    std::atomic<float>* minParam{ nullptr };
    std::atomic<float>* maxParam{ nullptr };
    std::atomic<float>* cvMinParam{ nullptr };
    std::atomic<float>* cvMaxParam{ nullptr };
    std::atomic<float>* normMinParam{ nullptr };
    std::atomic<float>* normMaxParam{ nullptr };
    std::atomic<float>* slewParam{ nullptr };
    std::atomic<float>* rateParam{ nullptr };
    std::atomic<float>* trigThresholdParam{ nullptr };
    
    // DSP State
    float currentValue{ 0.0f };
    float targetValue{ 0.0f };
    float currentValueCV{ 0.0f };
    float targetValueCV{ 0.0f };
    double sampleRate{ 44100.0 };
    double phase{ 0.0 };
    double lastScaledBeats{ 0.0 };
    int trigPulseRemaining { 0 };
    
    juce::SmoothedValue<float> smoothedSlew;
    
    // Telemetry for UI
    std::atomic<float> lastOutputValue{ 0.0f };
    std::atomic<float> lastNormalizedOutputValue{ 0.0f };
    std::atomic<float> lastCvOutputValue{ 0.0f };
    std::atomic<float> lastBoolOutputValue{ 0.0f };
    std::atomic<float> lastTrigOutputValue { 0.0f };

#if defined(PRESET_CREATOR_UI)
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> rawWaveform;
        std::array<std::atomic<float>, waveformPoints> cvWaveform;
        std::array<std::atomic<float>, waveformPoints> triggerMarkers; // 1.0 = trigger active, 0.0 = inactive
        std::atomic<float> currentMin { 0.0f };
        std::atomic<float> currentMax { 1.0f };
        std::atomic<float> currentCvMin { 0.0f };
        std::atomic<float> currentCvMax { 1.0f };
        std::atomic<float> currentTrigThreshold { 0.5f };
        std::atomic<float> currentRate { 1.0f };
        std::atomic<float> currentSlew { 0.0f };

        VizData()
        {
            for (auto& v : rawWaveform) v.store(0.0f);
            for (auto& v : cvWaveform) v.store(0.0f);
            for (auto& v : triggerMarkers) v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffer for waveform capture
    juce::AudioBuffer<float> vizRawBuffer;
    juce::AudioBuffer<float> vizCvBuffer;
    juce::AudioBuffer<float> vizTrigBuffer;
    int vizWritePos { 0 };
    static constexpr int vizBufferSize = 2048; // ~43ms at 48kHz
#endif
};