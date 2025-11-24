#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

class VCAModuleProcessor : public ModuleProcessor
{
public:
    VCAModuleProcessor();
    ~VCAModuleProcessor() override = default;

    const juce::String getName() const override { return "vca"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Parameter bus contract implementation
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawParallelPins("In L", 0, "Out L", 0);
        helpers.drawParallelPins("In R", 1, "Out R", 1);

        int busIdx, chanInBus;
        if (getParamRouting("gain", busIdx, chanInBus))
            helpers.drawParallelPins("Gain Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus), nullptr, -1);
    }

    bool usesCustomPinLayout() const override { return true; }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
            case 2: return "Gain Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out L";
            case 1: return "Out R";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Gain<float> gain;

    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* relativeGainModParam = nullptr;

#if defined(PRESET_CREATOR_UI)
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> inputWaveformL;
        std::array<std::atomic<float>, waveformPoints> inputWaveformR;
        std::array<std::atomic<float>, waveformPoints> outputWaveformL;
        std::array<std::atomic<float>, waveformPoints> outputWaveformR;
        std::atomic<float> currentGainDb { 0.0f };
        std::atomic<float> inputLevelDb { -60.0f };
        std::atomic<float> outputLevelDb { -60.0f };

        VizData()
        {
            for (auto& v : inputWaveformL) v.store(0.0f);
            for (auto& v : inputWaveformR) v.store(0.0f);
            for (auto& v : outputWaveformL) v.store(0.0f);
            for (auto& v : outputWaveformR) v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffer for waveform capture
    juce::AudioBuffer<float> vizInputBuffer;
    juce::AudioBuffer<float> vizOutputBuffer;
    int vizWritePos { 0 };
    static constexpr int vizBufferSize = 2048; // ~43ms at 48kHz
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCAModuleProcessor)
};


