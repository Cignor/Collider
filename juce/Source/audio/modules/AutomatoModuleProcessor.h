#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

// --- Data Model ---

// A single chunk of automation data (stores X,Y pairs)
struct AutomatoChunk
{
    using Ptr = std::shared_ptr<AutomatoChunk>;

    std::vector<std::pair<float, float>> samples; // X, Y pairs
    double startBeat;
    int numBeats;
    int samplesPerBeat;

    AutomatoChunk(double start, int lengthBeats, int resolution)
        : startBeat(start), numBeats(lengthBeats), samplesPerBeat(resolution)
    {
        samples.resize(static_cast<size_t>(numBeats * samplesPerBeat), std::make_pair(0.5f, 0.5f));
    }
};

// Immutable state container for thread-safe access
struct AutomatoState
{
    using Ptr = std::shared_ptr<AutomatoState>;

    std::vector<AutomatoChunk::Ptr> chunks; // Sorted by time
    double totalDurationBeats;

    AutomatoState() : totalDurationBeats(0.0) {}

    // Helper to find chunk at time
    AutomatoChunk::Ptr findChunkAt(double beat) const
    {
        // Simple linear search is fine for < 50 chunks
        for (const auto& chunk : chunks)
        {
            if (beat >= chunk->startBeat && beat < chunk->startBeat + chunk->numBeats)
                return chunk;
        }
        return nullptr;
    }
};

class AutomatoModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdRecordMode = "recordMode"; // Record vs Edit
    static constexpr auto paramIdSync = "sync"; // Sync to transport
    static constexpr auto paramIdDivision = "division"; // Speed division
    static constexpr auto paramIdLoop = "loop"; // Loop playback
    static constexpr auto paramIdRate = "rate"; // Free-running rate in Hz
    static constexpr auto paramIdXMod = "x_mod"; // X CV modulation input
    static constexpr auto paramIdYMod = "y_mod"; // Y CV modulation input

    // Output IDs
    enum
    {
        OUTPUT_X = 0,
        OUTPUT_Y,
        OUTPUT_COMBINED,
        OUTPUT_VALUE,
        OUTPUT_INVERTED,
        OUTPUT_BIPOLAR,
        OUTPUT_PITCH
    };

    AutomatoModuleProcessor();
    ~AutomatoModuleProcessor() override = default;

    const juce::String getName() const override { return "automato"; }

    // --- Core functions ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    void setTimingInfo(const TransportState& state) override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection
    std::optional<RhythmInfo> getRhythmInfo() const override;

    // --- State management ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

    // --- UI drawing functions ---
#if defined(PRESET_CREATOR_UI)
    ImVec2 getCustomNodeSize() const override { return ImVec2(280.0f, 0.0f); } // Increased for larger canvas

    void drawParametersInNode(
        float itemWidth,
        const std::function<bool(const juce::String&)>& isParamModulated,
        const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawParallelPins("X Mod", 0, "X", OUTPUT_X);
        helpers.drawParallelPins("Y Mod", 1, "Y", OUTPUT_Y);
        helpers.drawAudioOutputPin("Combined", OUTPUT_COMBINED);
        helpers.drawAudioOutputPin("Value", OUTPUT_VALUE);
        helpers.drawAudioOutputPin("Inverted", OUTPUT_INVERTED);
        helpers.drawAudioOutputPin("Bipolar", OUTPUT_BIPOLAR);
        helpers.drawAudioOutputPin("Pitch", OUTPUT_PITCH);
    }
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        if (channel == 0) return "X Mod";
        if (channel == 1) return "Y Mod";
        return {};
    }
    juce::String getAudioOutputLabel(int channel) const override;
    bool usesCustomPinLayout() const override { return true; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    void forceStop() override;

    // --- Internal Helpers ---
    double getDivisionSpeed(int divisionIndex) const;
    void ensureChunkExistsAt(double beat);
    void modifyChunkSamplesThreadSafe(
        AutomatoChunk::Ptr chunk,
        int startSampleIndex,
        int endSampleIndex,
        float startX, float startY,
        float endX, float endY);

    // Thread-safe state access
    void updateState(AutomatoState::Ptr newState);
    AutomatoState::Ptr getState() const;

    // Recording helpers
    void startRecording();
    void stopRecording();
    bool isRecording() const { return isCurrentlyRecording.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Use std::atomic<std::shared_ptr> for thread-safe state
    std::atomic<std::shared_ptr<AutomatoState>> activeState{nullptr};

    // --- DSP State ---
    double currentPhase{0.0}; // Current playback position in beats
    double sampleRate{44100.0};
    double recordingStartTime{0.0}; // Time when recording started (in beats or seconds)

    TransportState m_currentTransport;
    TransportCommand lastTransportCommand{TransportCommand::Stop};

    // --- Parameters ---
    std::atomic<float>* recordModeParam{nullptr};
    std::atomic<float>* syncParam{nullptr};
    std::atomic<float>* divisionParam{nullptr};
    std::atomic<float>* loopParam{nullptr};
    std::atomic<float>* rateParam{nullptr};

    // --- Recording State ---
    std::atomic<bool> isCurrentlyRecording{false};
    std::vector<std::pair<float, float>> recordingBuffer; // Temporary buffer during recording
    std::mutex recordingMutex; // Protect recording buffer
    double cvRecordingPhase{0.0}; // Phase accumulator for CV recording rate

#if defined(PRESET_CREATOR_UI)
    // UI State
    ImVec2 lastMousePosInGrid; // For smooth drawing interpolation
    bool isDrawing{false};
#endif
};

