#pragma once

#include "ModuleProcessor.h"

class TempoClockModuleProcessor : public ModuleProcessor
{
public:
    TempoClockModuleProcessor();
    ~TempoClockModuleProcessor() override = default;

    const juce::String getName() const override { return "tempo_clock"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setTimingInfo(const TransportState& state) override { m_currentTransport = state; }

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    // Parameter bus contract (virtual modulation IDs)
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "BPM Mod";
            case 1: return "Tap";
            case 2: return "Nudge+";
            case 3: return "Nudge-";
            case 4: return "Play";
            case 5: return "Stop";
            case 6: return "Reset";
            case 7: return "Swing Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Clock";
            case 1: return "Beat Trig";
            case 2: return "Bar Trig";
            case 3: return "Beat Gate";
            case 4: return "Phase";
            case 5: return "BPM CV";
            case 6: return "Downbeat";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameters
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* bpmParam { nullptr };
    std::atomic<float>* swingParam { nullptr };
    std::atomic<float>* divisionParam { nullptr };
    std::atomic<float>* gateWidthParam { nullptr };
    std::atomic<float>* syncToHostParam { nullptr };
    std::atomic<float>* divisionOverrideParam { nullptr };

    // Transport cache for per-block start
    TransportState m_currentTransport;

    // Internal state for tap/nudge and trigger edge detection
    double sampleRateHz { 0.0 };
    int lastBeatIndex { 0 };
    int lastBarIndex { 0 };
    double lastScaledBeats { 0.0 };
    bool lastPlayHigh { false };
    bool lastStopHigh { false };
    bool lastResetHigh { false };
    bool lastTapHigh { false };
    bool lastNudgeUpHigh { false };
    bool lastNudgeDownHigh { false };
    double samplesSinceLastTap { 0.0 };
};


