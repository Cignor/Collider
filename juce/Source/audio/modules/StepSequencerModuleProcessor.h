#pragma once

#include "ModuleProcessor.h"

class StepSequencerModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_STEPS = 16;
    StepSequencerModuleProcessor();
    ~StepSequencerModuleProcessor() override = default;

    const juce::String getName() const override { return "sequencer"; }

    void prepareToPlay (double newSampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // State management for transport settings
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree&) override;

    // Pin label overrides
    juce::String getAudioOutputLabel(int channel) const override;
    juce::String getAudioInputLabel(int channel) const override;
    
    // Parameter bus contract implementation
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
    // Rhythm reporting for BPM Monitor
    std::optional<RhythmInfo> getRhythmInfo() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void setTimingInfo(const TransportState& state) override;
    void forceStop() override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<int> currentStep { 0 };
    double phase { 0.0 };
    double sampleRate { 44100.0 };

    TransportState m_currentTransport;
    bool wasPlaying = false;

    std::atomic<float>* rateParam { nullptr };
    std::atomic<float>* gateLengthParam { nullptr };
    std::atomic<float>* gateThresholdParam { nullptr };
    std::atomic<float>* rateModParam { nullptr };
    std::atomic<float>* gateLengthModParam { nullptr };
    std::atomic<float>* numStepsModParam { nullptr };
    std::atomic<float>* stepsModMaxParam { nullptr };
    std::vector<std::atomic<float>*> pitchParams; // size MAX_STEPS
    std::vector<std::atomic<float>*> stepModParams; // size MAX_STEPS
    // Per-step trigger base (checkbox) and modulation
    std::vector<juce::AudioParameterBool*> stepTrigParams; // size MAX_STEPS
    std::vector<std::atomic<float>*> stepTrigModParams;    // size MAX_STEPS (0..1)
    // Per-step gate level parameters
    std::vector<std::atomic<float>*> stepGateParams;       // size MAX_STEPS (0..1)
    std::atomic<float>* numStepsParam { nullptr };

    // Pulse generator state for Trigger Out
    int pendingTriggerSamples { 0 };
    
    // Gate fade-in state
    bool previousGateOn { false };
    float gateFadeProgress { 0.0f };
    static constexpr float GATE_FADE_TIME_MS = 5.0f; // 5ms fade-in time
};

// Pin label overrides
inline juce::String StepSequencerModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Pitch";
        case 1: return "Gate";
        case 2: return "Gate Nuanced";
        case 3: return "Velocity";
        case 4: return "Mod";
        case 5: return "Trigger";
        default: return juce::String("Out ") + juce::String(channel + 1);
    }
}

inline juce::String StepSequencerModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Mod In L";
        case 1: return "Mod In R";
        case 2: return "Rate Mod";
        case 3: return "Gate Mod";
        case 4: return "Steps Mod";
        default: return juce::String("In ") + juce::String(channel + 1);
    }
}


