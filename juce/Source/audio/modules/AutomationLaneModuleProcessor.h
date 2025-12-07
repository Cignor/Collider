#pragma once
#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <deque>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h> // Needed for ImVec2 initialization
#endif

// --- Data Model ---

// A single chunk of automation data
struct AutomationChunk
{
    using Ptr = std::shared_ptr<AutomationChunk>;

    std::vector<float> samples;
    double             startBeat;
    int                numBeats;
    int                samplesPerBeat;

    AutomationChunk(double start, int lengthBeats, int resolution)
        : startBeat(start), numBeats(lengthBeats), samplesPerBeat(resolution)
    {
        samples.resize(static_cast<size_t>(numBeats * samplesPerBeat), 0.5f);
    }
};

// Immutable state container for thread-safe access
struct AutomationState
{
    using Ptr = std::shared_ptr<AutomationState>;

    std::vector<AutomationChunk::Ptr> chunks; // Sorted by time
    double                            totalDurationBeats;

    AutomationState() : totalDurationBeats(0.0) {}

    // Helper to find chunk at time
    AutomationChunk::Ptr findChunkAt(double beat) const
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

class AutomationLaneModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdMode = "mode"; // Free (Hz) vs Sync (Beats)
    static constexpr auto paramIdLoop = "loop";
    static constexpr auto paramIdZoom = "zoom";      // UI only, pixels per beat
    static constexpr auto paramIdRecordMode = "rec"; // Record vs Edit
    static constexpr auto paramIdDivision = "div";   // Sync division
    static constexpr auto paramIdDurationMode =
        "durationMode"; // Duration mode: User Choice, 1 Bar, 4 Bars, etc.
    static constexpr auto paramIdCustomDuration =
        "customDuration"; // Custom duration in beats (for User Choice)
    static constexpr auto paramIdTriggerThreshold = "triggerThreshold"; // Trigger threshold (0.0-1.0)
    static constexpr auto paramIdTriggerEdge = "triggerEdge"; // Trigger edge mode (Rising/Falling/Both)

    // Output IDs
    enum
    {
        OUTPUT_VALUE = 0,
        OUTPUT_INVERTED,
        OUTPUT_BIPOLAR,
        OUTPUT_PITCH,
        OUTPUT_TRIGGER
    };

    AutomationLaneModuleProcessor();
    ~AutomationLaneModuleProcessor() override = default;

    const juce::String getName() const override { return "automation_lane"; }

    // --- Core functions ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    void setTimingInfo(const TransportState& state) override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection
    std::optional<RhythmInfo> getRhythmInfo() const override;

    // --- State management ---
    juce::ValueTree getExtraStateTree() const override;
    void            setExtraStateTree(const juce::ValueTree& vt) override;

    // --- UI drawing functions ---
#if defined(PRESET_CREATOR_UI)
    ImVec2 getCustomNodeSize() const override { return ImVec2(650.0f, 0.0f); }

    void drawParametersInNode(
        float itemWidth,
        const std::function<bool(const juce::String&)>&,
        const std::function<void()>&) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioOutputPin("Value", OUTPUT_VALUE);
        helpers.drawAudioOutputPin("Inverted", OUTPUT_INVERTED);
        helpers.drawAudioOutputPin("Bipolar", OUTPUT_BIPOLAR);
        helpers.drawAudioOutputPin("Pitch", OUTPUT_PITCH);
        helpers.drawAudioOutputPin("Trigger", OUTPUT_TRIGGER);
    }
#endif

    juce::String getAudioInputLabel(int channel) const override { return {}; }
    juce::String getAudioOutputLabel(int channel) const override;
    bool         usesCustomPinLayout() const override { return true; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus)
        const override;

    void forceStop() override;

    // --- DSP State ---
    double currentPhase{0.0}; // Current playback position in beats (or phase 0-1 for loop?)
    double sampleRate{44100.0};

    TransportState   m_currentTransport;
    TransportCommand lastTransportCommand{TransportCommand::Stop};

    // --- Trigger Detection State ---
    float previousValue{-1.0f}; // Previous automation value for edge detection (-1 = uninitialized)
    bool lastValueAboveThreshold{false}; // Previous threshold state
    int triggerPulseRemaining{0}; // Counter for trigger pulse duration

    // --- Parameters ---
    std::atomic<float>* rateParam{nullptr};
    std::atomic<float>* modeParam{nullptr};
    std::atomic<float>* loopParam{nullptr};
    std::atomic<float>* divisionParam{nullptr};
    std::atomic<float>* durationModeParam{nullptr};
    std::atomic<float>* customDurationParam{nullptr};
    std::atomic<float>* triggerThresholdParam{nullptr};
    std::atomic<float>* triggerEdgeParam{nullptr};

    // --- Internal Helpers ---
    float  getSampleAt(double beat);
    void   ensureChunkExistsAt(double beat);
    double getTargetDuration() const; // Get target duration based on duration mode
    void   modifyChunkSamplesThreadSafe(
          AutomationChunk::Ptr chunk,
          int                  startSampleIndex,
          int                  endSampleIndex,
          float                startValue,
          float                endValue);

    // Thread-safe state access
    void                 updateState(AutomationState::Ptr newState);
    AutomationState::Ptr getState() const;

    // --- Trigger Detection Helper ---
    bool lineSegmentCrossesThreshold(float prevValue, float currValue, float threshold, int edgeMode) const;

private:
    juce::AudioProcessorValueTreeState apvts;

    // Use std::atomic<std::shared_ptr> which is available in C++20
    // Initialize to nullptr explicitly
    std::atomic<std::shared_ptr<AutomationState>> activeState{nullptr};

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#if defined(PRESET_CREATOR_UI)
    // UI State (not saved in APVTS)
    float  scrollX{0.0f};
    bool   isDragging{false};
    bool   isPlaying{false};
    ImVec2 lastMousePosInCanvas; // For smooth drawing interpolation - initialized in constructor
#endif
};
