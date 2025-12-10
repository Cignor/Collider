#pragma once

#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>

// Forward declaration for the file chooser
namespace juce
{
class FileChooser;
}

class MidiLoggerModuleProcessor : public ModuleProcessor, public juce::Timer
{
public:
    MidiLoggerModuleProcessor();
    ~MidiLoggerModuleProcessor() override = default;

    //==============================================================================
    // Core Data Structures
    //==============================================================================

    struct MidiEvent
    {
        int   pitch = 60;      // MIDI Note Number (0-127)
        float velocity = 0.8f; // 0.0f to 1.0f

        // Timing is stored in samples for absolute precision
        int64_t startTimeInSamples = 0;
        int64_t durationInSamples = 0;
    };

    class MidiTrack
    {
    public:
        void addNoteOn(int pitch, float velocity, int64_t startTime);
        void addNoteOff(int pitch, int64_t endTime);

        // UI thread gets a safe copy to read/display
        std::vector<MidiEvent> getEventsCopy() const;

        // UI thread can set events after editing (e.g., for undo/redo)
        void setEvents(const std::vector<MidiEvent>& newEvents);

        juce::String name = "Track";
        juce::Colour color = juce::Colours::white;

        // Thread-safe active flag replacing dynamic vector resizing
        std::atomic<bool> active{false};

        bool isVisible = true;
        bool isMuted = false;
        bool isSoloed = false;

    private:
        std::vector<MidiEvent> events;
        // Helper map to track notes that are currently held down
        std::map<int, std::pair<float, int64_t>> activeNotes; // pitch -> {velocity, startTime}
        mutable juce::ReadWriteLock eventLock; // CRITICAL: Protects the 'events' vector
    };

    //==============================================================================
    // ModuleProcessor Overrides
    //==============================================================================
    const juce::String getName() const override { return "midi_logger"; }
    void               prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void               releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(
        float                                                   itemWidth,
        const std::function<bool(const juce::String& paramId)>& isParamModulated,
        const std::function<void()>&                            onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool usesCustomPinLayout() const override;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Track Management
    // Fixed pool of tracks to avoid allocation in audio thread
    static constexpr int                    MaxTracks = 12;
    std::vector<std::unique_ptr<MidiTrack>> tracks;

    // Helper to activate a track safely
    void activateTrack(int trackIndex);

    // Timer callback for thread-safe UI updates
    void timerCallback() override;

    // Transport
    enum class TransportState
    {
        Stopped,
        Playing,
        Recording
    };
    std::atomic<TransportState> transportState{TransportState::Stopped};
    std::atomic<int64_t>        playheadPositionSamples{0};

    // Timing Information
    double              currentSampleRate = 44100.0;
    std::atomic<double> currentBpm{120.0};

    // CRITICAL ADDITIONS FOR PHASE 5: MIDI File Export
    void                               exportToMidiFile();
    double                             samplesToMidiTicks(int64_t samples) const;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::AudioProcessorValueTreeState apvts;

    // CRITICAL ADDITION FOR PHASE 2:
    // State for playback outputs. We need to store the current CV values for each track.
    struct PlaybackState
    {
        float gate = 0.0f;
        float pitch = 0.0f;
        float velocity = 0.0f;
    };
    std::vector<PlaybackState> playbackStates;

    // Track the previous state of the gate input for each track to detect edges.
    std::vector<bool> previousGateState;
    // Track the previous recorded MIDI note to detect pitch changes during legato
    std::vector<int> previousMidiNote;

    // Flags to signal the message thread that a track needs naming/updates
    std::atomic<bool> trackNeedsNaming[MaxTracks];

    // CRITICAL ADDITIONS FOR PHASE 3: UI State Management
    float viewScrollX = 0.0f;
    float viewScrollY = 0.0f;
    float zoomX = 100.0f; // Pixels per beat

    // Parameters for UI control
    juce::AudioParameterInt* loopLengthParam{nullptr};

    // A configurable default width for our custom UI
    float nodeWidth = 600.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLoggerModuleProcessor)
};
