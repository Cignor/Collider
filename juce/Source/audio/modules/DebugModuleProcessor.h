#pragma once

#include "ModuleProcessor.h"
#include <juce_core/juce_core.h>
#include <array>

struct DebugEvent
{
    juce::uint8 pinIndex;       // 0..7
    float value;                // captured value (e.g., peak)
    juce::uint64 sampleCounter; // sample-accurate timestamp
};

class DebugModuleProcessor : public ModuleProcessor
{
public:
    DebugModuleProcessor();
    ~DebugModuleProcessor() override = default;

    const juce::String getName() const override { return "debug"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Audio-thread state
    double currentSampleRate { 44100.0 };
    juce::uint64 totalSamples { 0 };
    std::array<float, 8> lastReported { { 0,0,0,0,0,0,0,0 } };
    std::array<uint8_t, 8> pinEnabled { { 1,1,1,1,1,1,1,1 } };
    float threshold { 0.001f };
    int maxEventsPerBlock { 64 };

    // Lock-free SPSC queue
    juce::AbstractFifo fifo { 2048 };
    std::vector<DebugEvent> fifoBuffer; // size == fifo.getTotalSize()
    std::atomic<uint32_t> droppedEvents { 0 };

    // UI-thread state
    struct PinStats { float last { 0.0f }; float min { 1e9f }; float max { -1e9f }; float rmsAcc { 0.0f }; int rmsCount { 0 }; };
    std::array<PinStats, 8> stats;
    std::vector<DebugEvent> uiEvents; // bounded list
    bool uiPaused { false };

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        static constexpr int maxEventMarkers = 64;
        std::array<std::array<std::atomic<float>, waveformPoints>, 8> inputWaveforms;
        std::array<std::atomic<int>, 8> eventMarkerCounts;
        std::array<std::array<std::atomic<int>, maxEventMarkers>, 8> eventMarkerPositions; // Position in waveform (0-255)
        std::array<std::atomic<float>, 8> inputRms;
        std::atomic<int> writeIndex { 0 };

        VizData()
        {
            for (auto& ch : inputWaveforms)
                for (auto& v : ch) v.store(0.0f);
            for (auto& c : eventMarkerCounts) c.store(0);
            for (auto& ch : eventMarkerPositions)
                for (auto& p : ch) p.store(-1);
            for (auto& r : inputRms) r.store(0.0f);
        }
    };

    VizData vizData;
    juce::AudioBuffer<float> captureBuffer;
#endif
};


