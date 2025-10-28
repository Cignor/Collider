#pragma once

#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

class MIDIPlayerModuleProcessor : public ModuleProcessor
{
public:
    // ADD THIS ENUM
    enum class AutoConnectState { None, Samplers, PolyVCO, Hybrid };
    
    static constexpr int kMaxTracks = 24;          // hard cap for output channels
    static constexpr int kOutputsPerTrack = 4;     // Pitch, Gate, Velocity, Trigger
    static constexpr int kClockChannelIndex = kMaxTracks * kOutputsPerTrack;        // 96
    static constexpr int kNumTracksChannelIndex = kClockChannelIndex + 1;           // 97
    static constexpr int kRawNumTracksChannelIndex = kNumTracksChannelIndex + 1;    // 98
    static constexpr int kTotalOutputs = kRawNumTracksChannelIndex + 1;             // 99
    static constexpr const char* SPEED_PARAM = "speed";
    static constexpr const char* PITCH_PARAM = "pitch";
    static constexpr const char* TEMPO_PARAM = "tempo";
    static constexpr const char* TRACK_PARAM = "track";
    static constexpr const char* LOOP_PARAM = "loop";
    static constexpr const char* SPEED_MOD_PARAM = "speed_mod";
    static constexpr const char* PITCH_MOD_PARAM = "pitch_mod";
    static constexpr const char* VELOCITY_MOD_PARAM = "velocity_mod";

    MIDIPlayerModuleProcessor();
    ~MIDIPlayerModuleProcessor() override = default;

    const juce::String getName() const override { return "midi_player"; }
    
    // Auto-connect trigger flags
    std::atomic<bool> autoConnectTriggered { false };
    std::atomic<bool> autoConnectVCOTriggered { false };
    std::atomic<bool> autoConnectHybridTriggered { false };

    // ADD THESE TWO LINES
    std::atomic<AutoConnectState> lastAutoConnectState { AutoConnectState::None };
    std::atomic<bool> connectionUpdateRequested { false };

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    
    // CRITICAL: Receive transport state (BPM, position, play state) from ModularSynthProcessor
    void setTimingInfo(const TransportState& state) override { m_currentTransport = state; }

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // POLYPHONIC OUTPUTS: Dynamic pins for multi-track playback
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    void drawIoPins(const NodePinHelpers& helpers) override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    // UI Methods
    #if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    #endif

    // Pin labeling
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;

    // MIDI File Management
    void loadMIDIFile(const juce::File& file);
    bool hasMIDIFileLoaded() const { return midiFile != nullptr && midiFile->getNumTracks() > 0; }
    juce::String getCurrentMIDIFileName() const { return currentMIDIFileName; }
    juce::String getCurrentMIDIFileFullPath() const { return currentMIDIFilePath; }

    // Piano Roll Data Access
    struct NoteData
    {
        double startTime;
        double endTime;
        int noteNumber;
        int velocity;
        int trackIndex;
    };
    
    struct TrackInfo
    {
        juce::String name;
        int noteCount;
        bool hasNotes;
    };
    
    const std::vector<std::vector<NoteData>>& getNotesByTrack() const { return notesByTrack; }
    const std::vector<TrackInfo>& getTrackInfos() const { return trackInfos; }
    const std::vector<int>& getActiveTrackIndices() const { return activeTrackIndices; }
    double getTotalDuration() const { return totalDuration; }
    
    // Quick Connect: Check and consume connection request
    // Returns: 0=none, 1=PolyVCO, 2=Samplers, 3=Both
    int getAndClearConnectionRequest() 
    { 
        int req = connectionRequestType.load(); 
        connectionRequestType = 0; 
        return req; 
    }
    int getNumTracks() const { return midiFile ? midiFile->getNumTracks() : 0; }
    int getTotalNoteCount() const;

private:
    // Protects cross-thread access to MIDI data structures
    juce::CriticalSection midiDataLock;

    juce::AudioProcessorValueTreeState apvts;
    
    // MIDI File Data
    std::unique_ptr<juce::MidiFile> midiFile;
    juce::String currentMIDIFileName;
    juce::String currentMIDIFilePath;
    
    // --- MODIFIED DATA STRUCTURES ---
    // Replace the single flat vector of notes with a per-track structure
    std::vector<std::vector<NoteData>> notesByTrack;
    std::vector<TrackInfo> trackInfos;
    double totalDuration { 0.0 };
    int numActiveTracks { 0 };
    std::vector<int> activeTrackIndices; // map active output group -> source track index
    
    // Playback State
    double currentPlaybackTime { 0.0 };
    double playbackSpeed { 1.0 };
    int currentTrackIndex { 0 };
    bool isLooping { true };
    
    // --- ADD NEW STATE VARIABLES FOR EFFICIENT SEARCH ---
    std::vector<int> lastNoteIndexHint; // Remembers the last search position for each track
    double previousPlaybackTime { -1.0 }; // Used to detect when playback loops or seeks
    
    // Output Values (legacy single-track summary retained for diagnostics, not routed)
    float pitchCV { 0.0f };
    float gateLevel { 0.0f };
    float velocityLevel { 0.0f };
    bool triggerPulse { false };
    float clockOutput { 0.0f };
    float numTracksOutput { 0.0f };
    std::atomic<double> pendingSeekTime { -1.0 };
    float lastResetCV { 0.0f }; // For reset modulation edge detection
    
    // Parameter Pointers
    std::atomic<float>* speedParam { nullptr };
    std::atomic<float>* pitchParam { nullptr };
    std::atomic<float>* tempoParam { nullptr };
    std::atomic<float>* trackParam { nullptr };
    std::atomic<float>* loopParam { nullptr };
    std::atomic<float>* speedModParam { nullptr };
    std::atomic<float>* pitchModParam { nullptr };
    std::atomic<float>* velocityModParam { nullptr };
    
    // File Chooser
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    // TEMPO HANDLING: Smart tempo system with file/host/multiplier hierarchy
    juce::AudioParameterBool* syncToHostParam { nullptr };
    juce::AudioParameterFloat* tempoMultiplierParam { nullptr };
    double fileBpm = 120.0; // Stores tempo parsed from loaded .mid file
    
    // CRITICAL: Custom transport state (replaces JUCE getPlayHead() for standalone app)
    TransportState m_currentTransport;
    
    // PHASE 1: UI State Variables for Piano Roll
    float nodeWidth = 600.0f;
    float zoomX = 50.0f; // Pixels per beat (matches MidiLogger default)
    
    // Quick Connect: Connection request flag (0=none, 1=PolyVCO, 2=Samplers, 3=Both)
    std::atomic<int> connectionRequestType { 0 };
    
    // Internal Methods
    void parseMIDIFile();
    void updatePlaybackTime(double deltaTime);
    void generateCVOutputs();
    double noteNumberToCV(int noteNumber) const;
    juce::ValueTree getExtraStateTree() const override
    {
        juce::ValueTree vt ("MIDIPlayerExtra");
        vt.setProperty ("fileName", currentMIDIFileName, nullptr);
        vt.setProperty ("filePath", currentMIDIFilePath, nullptr);
        vt.setProperty ("track", currentTrackIndex, nullptr);
        return vt;
    }
    void setExtraStateTree(const juce::ValueTree& vt) override
    {
        if (! vt.isValid() || ! vt.hasType ("MIDIPlayerExtra")) return;
        currentMIDIFileName = vt.getProperty ("fileName").toString();
        currentMIDIFilePath = vt.getProperty ("filePath").toString();
        currentTrackIndex = (int) vt.getProperty ("track", 0);
        if (currentMIDIFilePath.isNotEmpty())
        {
            juce::File f (currentMIDIFilePath);
            if (f.existsAsFile())
                loadMIDIFile (f);
        }
        if (auto* p = apvts.getParameter(TRACK_PARAM))
        {
            float norm = apvts.getParameterRange(TRACK_PARAM).convertTo0to1((float) currentTrackIndex);
            p->setValueNotifyingHost(norm);
        }
    }
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
};
