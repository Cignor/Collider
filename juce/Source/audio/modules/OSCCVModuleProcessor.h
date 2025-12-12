#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief OSC to CV/Gate Converter Module
 * 
 * Converts incoming OSC messages to CV and Gate signals:
 * - Monophonic operation (single note at a time)
 * - Outputs: Gate, Pitch CV (1V/octave), Velocity
 * - Address pattern matching for flexible routing
 * - Source filtering (by OSC device/port)
 * 
 * Supported OSC patterns:
 * - /synth/note/on {note: int32, velocity: float32}
 * - /synth/note/off {note: int32}
 * - /cv/pitch float32
 * - /cv/velocity float32
 * - /gate float32
 * - /trigger (no args or float32)
 */
class OSCCVModuleProcessor : public ModuleProcessor
{
public:
    OSCCVModuleProcessor();
    ~OSCCVModuleProcessor() override = default;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    // OSC signal processing
    void handleOscSignal(const std::vector<OscDeviceManager::OscMessageWithSource>& oscMessages) override;

    const juce::String getName() const override { return "osc_cv"; }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Dynamic outputs based on mapped addresses
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
    // Use dynamic pins (override usesCustomPinLayout)
    bool usesCustomPinLayout() const override { return false; } // Use dynamic pins system

#if defined(PRESET_CREATOR_UI)
    ImVec2 getCustomNodeSize() const override { return ImVec2(480.0f, 0.0f); } // Big size for monitor section
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    // APVTS with source/pattern filtering parameters
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // OSC filtering parameters
    juce::AudioParameterChoice* sourceFilterParam { nullptr };  // "All Sources" or specific device
    
    // Address-to-output mapping (address -> output channel)
    struct AddressMapping
    {
        juce::String oscAddress;      // OSC address pattern (e.g., "/data/motion/accelerometer/x")
        int outputChannel;            // Output channel index (0-based)
        PinDataType pinType;          // CV or Gate
        float lastValue;              // Last received value
        juce::uint64 lastUpdateTime;  // Timestamp of last update
    };
    
    std::vector<AddressMapping> addressMappings;
    mutable juce::CriticalSection mappingLock;  // Thread-safe access to mappings
    
    // Add a new output mapping for an address (called from UI)
    void addAddressMapping(const juce::String& address);
    
    // Remove an address mapping
    void removeAddressMapping(int outputChannel);
    
    // Check if an address is already mapped
    bool isAddressMapped(const juce::String& address) const;
    
    // Pattern matching helper
    bool matchesPattern(const juce::String& address, const juce::String& pattern);
    
    // Convert MIDI note number to CV (1V/octave, where C4 = 60 = 0V)
    float midiNoteToCv(int noteNumber) const;
    
    // Convert CV to MIDI note number (for note off matching)
    int cvToMidiNote(float cv) const;
    
    // Value storage per address (thread-safe)
    mutable juce::CriticalSection valueLock;
    std::map<juce::String, float> addressValues; // address -> current CV value
    
    // Address monitoring for UI (thread-safe)
    mutable juce::CriticalSection addressMonitorLock;
    std::map<juce::String, juce::uint64> lastSeenAddresses; // address -> timestamp (milliseconds)
    
    // Get list of recently seen addresses (for UI display)
    std::vector<juce::String> getRecentAddresses() const;
    
    // Helper to get output value for a mapped address
    float getOutputValueForAddress(const juce::String& address) const;
    
    // Save/load address mappings to/from APVTS state
    void saveAddressMappingsToState();
    void loadAddressMappingsFromState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OSCCVModuleProcessor)
};

