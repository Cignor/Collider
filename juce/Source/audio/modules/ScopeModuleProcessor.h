#pragma once

#include "ModuleProcessor.h"
#include <array>
#include <deque>
#include <cstdint>
#include <utility>

class ScopeModuleProcessor : public ModuleProcessor
{
public:
    ScopeModuleProcessor();
    ~ScopeModuleProcessor() override = default;

    const juce::String getName() const override { return "scope"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawParallelPins("In", 0, "Out", 0);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In";
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
#endif

    bool usesCustomPinLayout() const override { return true; }

    const juce::AudioBuffer<float>& getScopeBuffer() const { return scopeBuffer; }
    
    // Get rolling min/max statistics (computes on-demand)
    void getStatistics(float& outMin, float& outMax) const;

private:
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> waveform{};
        std::atomic<float> peakMin { 0.0f };
        std::atomic<float> peakMax { 0.0f };
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int computeHistoryCapacity() const;
    void resetHistoryState (int newCapacity);
    void pushDecimatedSample (float sample);
    void refreshVizWaveform();

    juce::AudioProcessorValueTreeState apvts;

    juce::AudioBuffer<float> scopeBuffer; // mono buffer for display
    int writePos { 0 };

    // --- Rolling min/max over ~5 seconds (decimated) ---
    double currentSampleRate { 44100.0 };
    int decimation { 48 };           // ~1 kHz at 48 kHz
    int decimCounter { 0 };
    int histCapacity { 5000 };       // seconds * (sr/decimation)
    std::int64_t histSampleCounter { -1 };
    std::deque<std::pair<std::int64_t, float>> historyMinDeque;
    std::deque<std::pair<std::int64_t, float>> historyMaxDeque;
    VizData vizData;
    std::atomic<float>* monitorSecondsParam { nullptr };
};


