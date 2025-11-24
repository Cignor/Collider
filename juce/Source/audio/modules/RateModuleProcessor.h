#pragma once

#include "ModuleProcessor.h"
#include <array>
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

class RateModuleProcessor : public ModuleProcessor
{
public:
    RateModuleProcessor();
    ~RateModuleProcessor() override = default;

    const juce::String getName() const override { return "rate"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    float getLastOutputValue() const;
    void drawIoPins(const NodePinHelpers& helpers) override;
    bool usesCustomPinLayout() const override { return true; }
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Rate Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* baseRateParam { nullptr };
    std::atomic<float>* multiplierParam { nullptr };
    
    std::atomic<float> lastOutputValue { 0.0f };

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> inputWaveform;
        std::array<std::atomic<float>, waveformPoints> outputWaveform;
        std::array<std::atomic<float>, waveformPoints> finalRateWaveform; // Final rate in Hz
        std::atomic<float> currentBaseRate { 1.0f };
        std::atomic<float> currentMultiplier { 1.0f };
        std::atomic<float> currentFinalRate { 1.0f };

        VizData()
        {
            for (auto& v : inputWaveform) v.store(0.0f);
            for (auto& v : outputWaveform) v.store(0.0f);
            for (auto& v : finalRateWaveform) v.store(1.0f);
        }
    };

    VizData vizData;
    juce::AudioBuffer<float> vizInputBuffer;
    juce::AudioBuffer<float> vizOutputBuffer;
#endif
};
