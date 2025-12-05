#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

class PolyVCOModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_VOICES = 32;

    PolyVCOModuleProcessor();
    ~PolyVCOModuleProcessor() override = default;

    const juce::String getName() const override { return "polyvco"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    // Dynamic pin interface for variable voice count
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void setTimingInfo(const TransportState& state) override;
    void forceStop() override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(
        float                                                   itemWidth,
        const std::function<bool(const juce::String& paramId)>& isParamModulated,
        const std::function<void()>&                            onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    // Parameter bus contract implementation (must be available in Collider too)
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus)
        const override;
    bool usesCustomPinLayout() const override { return true; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int                                                        getEffectiveNumVoices() const;

    juce::String getAudioInputLabel(int channel) const override;

    juce::AudioProcessorValueTreeState apvts;

    // Global control
    juce::AudioParameterInt*   numVoicesParam{nullptr};
    juce::AudioParameterBool*  relativeFreqModParam{nullptr};
    juce::AudioParameterFloat* portamentoParam{nullptr};

    // Per-voice parameters
    std::vector<juce::AudioParameterFloat*>  voiceFreqParams;
    std::vector<juce::AudioParameterChoice*> voiceWaveParams;
    std::vector<juce::AudioParameterFloat*>  voiceGateParams;

    // DSP engines - pre-initialized for each waveform to avoid audio thread allocations
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> sineOscillators;
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> sawOscillators;
    std::array<juce::dsp::Oscillator<float>, MAX_VOICES> squareOscillators;

    // Track the current waveform for each voice
    std::array<int, MAX_VOICES> currentWaveforms;

    // --- ADD GATE SMOOTHING ---
    std::array<float, MAX_VOICES> smoothedGateLevels{};

    // Clickless gate: per-voice envelope and hysteresis state
    std::array<float, MAX_VOICES>   gateEnvelope{};
    std::array<uint8_t, MAX_VOICES> gateOnState{}; // 0/1 with hysteresis

    // Portamento/glide: per-voice smoothed frequency
    std::array<float, MAX_VOICES> currentFrequencies{};
    double                        sampleRate{44100.0};

    // Transport state tracking
    TransportState m_currentTransport;

#if defined(PRESET_CREATOR_UI)
public:
    std::atomic<bool> autoConnectTrackMixerTriggered{false};

private:
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int                           waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> combinedWaveform;
        std::atomic<int>                               activeVoices{0};
        std::atomic<float>                             averageFrequency{440.0f};
        std::atomic<float>                             averageGateLevel{0.0f};

        VizData()
        {
            for (auto& v : combinedWaveform)
                v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffer for waveform capture (combined output from all voices)
    juce::AudioBuffer<float> vizOutputBuffer;
    int                      vizWritePos{0};
    static constexpr int     vizBufferSize = 2048; // ~43ms at 48kHz
#endif
};
