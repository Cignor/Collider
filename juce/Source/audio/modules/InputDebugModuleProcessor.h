#pragma once

#include "ModuleProcessor.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <atomic>

struct InputDebugEvent
{
    juce::uint64 sampleCounter; // Sample-accurate timestamp
    int pinIndex;               // Which input pin (0-indexed)
    float value;                // The signal's value (magnitude)
};

class InputDebugModuleProcessor : public ModuleProcessor
{
public:
    InputDebugModuleProcessor();
    ~InputDebugModuleProcessor() override = default;

    const juce::String getName() const override { return "input_debug"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool usesCustomPinLayout() const override { return true; }

    // Labels for pins for Debug CSV enrichment
    juce::String getAudioInputLabel(int channel) const override
    {
        return juce::String("Tap In ") + juce::String(channel + 1);
    }
    juce::String getAudioOutputLabel(int channel) const override
    {
        return juce::String("Tap Out ") + juce::String(channel + 1);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- Thread-safe communication ---
    juce::AbstractFifo abstractFifo;
    std::vector<InputDebugEvent> fifoBackingStore;
    std::atomic<uint32_t> droppedEvents { 0 };

    // --- State for the audio thread ---
    double currentSampleRate { 44100.0 };
    juce::uint64 totalSamplesProcessed { 0 };
    std::array<float, 8> lastValues{};          // Last measured per input
    std::array<float, 8> lastReportedValues{};  // Last reported/logged
    static constexpr float CHANGE_THRESHOLD = 0.001f;
    static constexpr float HYSTERESIS = 0.0001f;

    // --- State for the UI thread ---
    std::vector<InputDebugEvent> displayedEvents;
    static constexpr size_t MAX_DISPLAYED_EVENTS = 500;
    bool isPaused { false };

#if defined(PRESET_CREATOR_UI)
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        static constexpr int numChannels = 8;
        std::array<std::array<std::atomic<float>, waveformPoints>, numChannels> waveforms;
        std::array<std::atomic<float>, numChannels> currentValues {};

        VizData()
        {
            for (auto& ch : waveforms)
                for (auto& v : ch)
                    v.store(0.0f);
            for (auto& v : currentValues)
                v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffers for waveform capture
    std::array<juce::AudioBuffer<float>, 8> vizBuffers;
    std::array<int, 8> vizWritePositions {};
#endif
};


