#pragma once

#include "ModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include <array>
#include <atomic>
#include <vector>
#endif

class SAndHModuleProcessor : public ModuleProcessor
{
public:
    SAndHModuleProcessor();
    ~SAndHModuleProcessor() override = default;

    const juce::String getName() const override { return "s_and_h"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawParallelPins("Signal In L", 0, "Out L", 0);
        helpers.drawParallelPins("Signal In R", 1, "Out R", 1);
        helpers.drawParallelPins("Gate In L", 2, nullptr, -1);
        helpers.drawParallelPins("Gate In R", 3, nullptr, -1);

        int busIdx, chanInBus;
        if (getParamRouting("threshold_mod", busIdx, chanInBus))
            helpers.drawParallelPins("Threshold Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus), nullptr, -1);
        if (getParamRouting("slewMs_mod", busIdx, chanInBus))
            helpers.drawParallelPins("Slew Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus), nullptr, -1);
        if (getParamRouting("edge_mod", busIdx, chanInBus))
            helpers.drawParallelPins("Edge Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus), nullptr, -1);
    }

    bool usesCustomPinLayout() const override { return true; }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Signal In L";
            case 1: return "Signal In R";
            case 2: return "Gate In L";
            case 3: return "Gate In R";
            case 4: return "Threshold Mod";
            case 5: return "Edge Mod";
            case 6: return "Slew Mod";
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

    // State
    float lastGateL { 0.0f };
    float lastGateR { 0.0f };
    float heldL { 0.0f };
    float heldR { 0.0f };
    float outL  { 0.0f };
    float outR  { 0.0f };
    double sr { 44100.0 };
    float lastGateLForNorm { 0.0f };  // For gate normalization state tracking
    float lastGateRForNorm { 0.0f };  // For gate normalization state tracking

    // Parameters
    std::atomic<float>* thresholdParam { nullptr }; // 0..1
    std::atomic<float>* hysteresisParam { nullptr }; // 0..0.1
    juce::AudioParameterChoice* edgeParam { nullptr }; // 0 rising, 1 falling, 2 both
    std::atomic<float>* slewMsParam { nullptr }; // 0..2000 ms
    std::atomic<float>* thresholdModParam { nullptr };
    std::atomic<float>* slewMsModParam { nullptr };
    std::atomic<float>* edgeModParam { nullptr };

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        static constexpr int waveformPoints = 192;
        std::array<std::atomic<float>, waveformPoints> signalL {};
        std::array<std::atomic<float>, waveformPoints> signalR {};
        std::array<std::atomic<float>, waveformPoints> gateL {};
        std::array<std::atomic<float>, waveformPoints> gateR {};
        std::array<std::atomic<float>, waveformPoints> heldWaveL {};
        std::array<std::atomic<float>, waveformPoints> heldWaveR {};
        std::array<std::atomic<uint8_t>, waveformPoints> gateMarkersL {};
        std::array<std::atomic<uint8_t>, waveformPoints> gateMarkersR {};
        std::atomic<float> liveThreshold { 0.5f };
        std::atomic<float> liveSlewMs { 0.0f };
        std::atomic<int> liveEdgeMode { 0 };
        std::atomic<float> lastLatchValueL { 0.0f };
        std::atomic<float> lastLatchValueR { 0.0f };
        std::atomic<float> latchAgeMsL { 0.0f };
        std::atomic<float> latchAgeMsR { 0.0f };
        std::atomic<float> gatePeakL { 0.0f };
        std::atomic<float> gatePeakR { 0.0f };
    };

    VizData vizData;
    juce::AudioBuffer<float> captureBuffer;
#endif
};


