#pragma once

#include "ModuleProcessor.h"
#include <array>

class ClockDividerModuleProcessor : public ModuleProcessor
{
public:
    ClockDividerModuleProcessor();
    ~ClockDividerModuleProcessor() override = default;

    const juce::String getName() const override { return "clock_divider"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection for BPM Monitor
    std::optional<RhythmInfo> getRhythmInfo() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Clock In";
            case 1: return "Reset";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "/2";
            case 1: return "/4";
            case 2: return "/8";
            case 3: return "x2";
            case 4: return "x3";
            case 5: return "x4";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;

    // Parameters
    std::atomic<float>* gateThresholdParam { nullptr };
    std::atomic<float>* hysteresisParam { nullptr };
    std::atomic<float>* pulseWidthParam { nullptr }; // fraction of base period [0..1]

    // Division state
    int clockCount { 0 };
    bool div2State { false }, div4State { false }, div8State { false };

    // Multiplication state
    double sampleRate { 48000.0 };
    double currentClockInterval { 0.0 };
    int samplesSinceLastClock { 0 };
    double multiplierPhase[3] { 0.0, 0.0, 0.0 }; // For x2, x3, x4

    bool lastInputState { false };

    // Pulse generation state (samples remaining high)
    int pulseSamplesRemaining[6] { 0, 0, 0, 0, 0, 0 };
    
    // Schmitt trigger state for clock and reset
    bool schmittStateClock { false };
    bool schmittStateReset { false };

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> clockInputWaveform;
        std::array<std::atomic<float>, waveformPoints> resetInputWaveform;
        std::array<std::atomic<float>, waveformPoints> div2Waveform;
        std::array<std::atomic<float>, waveformPoints> div4Waveform;
        std::array<std::atomic<float>, waveformPoints> div8Waveform;
        std::array<std::atomic<float>, waveformPoints> mul2Waveform;
        std::array<std::atomic<float>, waveformPoints> mul3Waveform;
        std::array<std::atomic<float>, waveformPoints> mul4Waveform;
        std::atomic<int> writeIndex { 0 };
        std::atomic<double> currentBpm { 0.0 };
        std::atomic<double> clockInterval { 0.0 };
        std::atomic<int> clockCountLive { 0 };

        VizData()
        {
            for (auto& v : clockInputWaveform) v.store(0.0f);
            for (auto& v : resetInputWaveform) v.store(0.0f);
            for (auto& v : div2Waveform) v.store(0.0f);
            for (auto& v : div4Waveform) v.store(0.0f);
            for (auto& v : div8Waveform) v.store(0.0f);
            for (auto& v : mul2Waveform) v.store(0.0f);
            for (auto& v : mul3Waveform) v.store(0.0f);
            for (auto& v : mul4Waveform) v.store(0.0f);
        }
    };

    VizData vizData;
    juce::AudioBuffer<float> captureBuffer;
#endif
};
