#pragma once

#include "ModuleProcessor.h"

class MultiSequencerModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_STEPS = 16;
    MultiSequencerModuleProcessor();
    ~MultiSequencerModuleProcessor() override = default;

    const juce::String getName() const override { return "multi sequencer"; }

    void prepareToPlay (double newSampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    juce::String getAudioOutputLabel(int channel) const override;
    juce::String getAudioInputLabel(int channel) const override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    std::atomic<bool> autoConnectSamplersTriggered { false };
    std::atomic<bool> autoConnectVCOTriggered { false };

    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> currentStep { 0 };
    double phase { 0.0 };
    double sampleRate { 44100.0 };
    std::atomic<float>* rateParam { nullptr };
    std::atomic<float>* gateLengthParam { nullptr };
    std::atomic<float>* gateThresholdParam { nullptr };
    std::atomic<float>* rateModParam { nullptr };
    std::atomic<float>* gateLengthModParam { nullptr };
    std::atomic<float>* numStepsModParam { nullptr };
    std::atomic<float>* stepsModMaxParam { nullptr };
    std::vector<std::atomic<float>*> pitchParams;
    std::vector<std::atomic<float>*> stepModParams;
    std::vector<juce::AudioParameterBool*> stepTrigParams;
    std::vector<std::atomic<float>*> stepTrigModParams;
    std::vector<std::atomic<float>*> stepGateParams;
    std::atomic<float>* numStepsParam { nullptr };
    int pendingTriggerSamples { 0 };
    bool previousGateOn { false };
    float gateFadeProgress { 0.0f };
    static constexpr float GATE_FADE_TIME_MS = 5.0f;
};