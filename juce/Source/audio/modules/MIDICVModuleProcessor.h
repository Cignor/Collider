#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/**
 * @brief MIDI to CV/Gate Converter Module (Polyphonic)
 * * Converts incoming MIDI messages to CV and Gate signals:
 * - 8-voice polyphony with voice stealing (lowest note priority)
 * - Per-voice outputs: Gate, Pitch CV (1V/octave), Velocity
 * - Global controllers: Mod Wheel, Pitch Bend, Aftertouch
 * - Compatible with MidiLoggerModuleProcessor for MIDI recording/export
 * * This module allows MIDI keyboards and controllers to drive the modular synth.
 */
class MIDICVModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int NUM_VOICES = 8;

    MIDICVModuleProcessor();
    ~MIDICVModuleProcessor() override = default;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    // Multi-MIDI device support
    void handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages) override;

    const juce::String getName() const override { return "midi_cv"; }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Quick Connect: Check and consume connection request to MidiLogger
    // Returns: 0=none, 1=MidiLogger
    int getAndClearConnectionRequest() 
    { 
        int req = connectionRequestType.load(); 
        connectionRequestType = 0; 
        return req; 
    }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    // Voice structure for polyphonic voice management
    struct Voice
    {
        bool active = false;           // Is this voice currently playing a note?
        int midiNote = -1;             // Current MIDI note number (0-127)
        float velocity = 0.0f;         // Current velocity (0.0-1.0)
        int midiChannel = 0;           // MIDI channel (1-16) this voice is on
        int64_t noteStartSample = 0;   // When this note started (for voice stealing)
    };

    // APVTS with device/channel filtering parameters
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Multi-MIDI device filtering parameters
    juce::AudioParameterChoice* deviceFilterParam { nullptr };
    juce::AudioParameterInt* midiChannelFilterParam { nullptr };

    // Polyphonic voice management
    std::array<Voice, NUM_VOICES> voices;
    juce::CriticalSection voiceLock;  // Thread-safe access to voices array

    // Global controllers (channel-wide, not per-voice)
    std::atomic<float> globalModWheel{0.0f};
    std::atomic<float> globalPitchBend{0.0f};
    std::atomic<float> globalAftertouch{0.0f};

    // Sample counter for voice stealing priority (oldest note = lowest priority)
    std::atomic<int64_t> currentSamplePosition{0};

    // Voice allocation functions
    int allocateVoice(int midiNote, int midiChannel, float velocity, int64_t currentSample);
    void releaseVoice(int voiceIndex, int midiNote);
    int findVoiceForNote(int midiNote);  // Find voice playing a specific note

    // Convert MIDI note number to CV (1V/octave, where C4 = 60 = 0V)
    float midiNoteToCv(int noteNumber) const;
    
    // Quick Connect: Connection request flag (0=none, 1=MidiLogger)
    std::atomic<int> connectionRequestType { 0 };
    
    // Telemetry for UI display
    std::vector<std::unique_ptr<std::atomic<float>>> lastOutputValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MIDICVModuleProcessor)
};