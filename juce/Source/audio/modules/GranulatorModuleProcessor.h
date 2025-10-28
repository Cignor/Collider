#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

class GranulatorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs for APVTS
    static constexpr auto paramIdDensity      = "density";
    static constexpr auto paramIdSize         = "size";
    static constexpr auto paramIdPosition     = "position";
    static constexpr auto paramIdSpread       = "spread";
    static constexpr auto paramIdPitch        = "pitch";
    static constexpr auto paramIdPitchRandom  = "pitchRandom";
    static constexpr auto paramIdPanRandom    = "panRandom";
    static constexpr auto paramIdGate         = "gate";

    // Virtual IDs for modulation inputs, used for routing
    static constexpr auto paramIdTriggerIn    = "trigger_in_mod";
    static constexpr auto paramIdDensityMod   = "density_mod";
    static constexpr auto paramIdSizeMod      = "size_mod";
    static constexpr auto paramIdPositionMod  = "position_mod";
    static constexpr auto paramIdPitchMod     = "pitch_mod";
    static constexpr auto paramIdGateMod      = "gate_mod";

    GranulatorModuleProcessor();
    ~GranulatorModuleProcessor() override = default;

    const juce::String getName() const override { return "granulator"; }

    // --- JUCE AudioProcessor Overrides ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- UI & Routing Overrides ---
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

private:
    // --- Internal Implementation ---
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void launchGrain(int grainIndex, float density, float size, float position, float spread, float pitch, float pitchRandom, float panRandom);

    // --- APVTS & Parameters ---
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* densityParam      { nullptr };
    std::atomic<float>* sizeParam         { nullptr };
    std::atomic<float>* positionParam     { nullptr };
    std::atomic<float>* spreadParam       { nullptr };
    std::atomic<float>* pitchParam        { nullptr };
    std::atomic<float>* pitchRandomParam  { nullptr };
    std::atomic<float>* panRandomParam    { nullptr };
    std::atomic<float>* gateParam         { nullptr };

    // --- Grain State ---
    struct Grain
    {
        bool isActive { false };
        double readPosition { 0.0 };
        double increment { 1.0 };
        int samplesRemaining { 0 };
        int totalLifetime { 0 };
        float panL { 0.707f };
        float panR { 0.707f };
    };
    std::array<Grain, 64> grainPool;
    juce::Random random;

    // --- Audio Buffering ---
    juce::AudioBuffer<float> sourceBuffer;
    int sourceWritePos { 0 };
    int samplesUntilNextGrain { 0 };

    // --- Parameter Smoothing ---
    juce::SmoothedValue<float> smoothedDensity, smoothedSize, smoothedPosition, smoothedPitch, smoothedGate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranulatorModuleProcessor)
};
