#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_osc/juce_osc.h>

/**
 * @brief CV to OSC Sender Module
 * 
 * Converts CV/Audio/Gate signals from the modular synth into OSC messages.
 * - Accepts any number of input channels (up to 32)
 * - User can assign OSC addresses to each input
 * - Configurable send modes (per block, throttled, on change)
 * - Network configuration (target IP and port)
 */
class CVOSCSenderModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_INPUTS = 32;
    CVOSCSenderModuleProcessor();
    ~CVOSCSenderModuleProcessor() override;
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    const juce::String getName() const override { return "cv_osc_sender"; }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Dynamic inputs based on mappings
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    
    // Use dynamic pins system (explicit override like OSCCVModuleProcessor)
    bool usesCustomPinLayout() const override { return false; } // Use dynamic pins system
    
    // Bus layout support - allow up to 32 input channels
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    
#if defined(PRESET_CREATOR_UI)
    ImVec2 getCustomNodeSize() const override { return ImVec2(840.0f, 0.0f); } // ExtraWide size
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    // APVTS
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Parameters
    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* sendModeParam { nullptr };      // Per Block, Throttled, On Change
    juce::AudioParameterFloat* throttleRateParam { nullptr };   // Messages per second
    juce::AudioParameterFloat* changeThresholdParam { nullptr }; // Minimum change to trigger send
    
    // Network settings (stored in APVTS as strings/ints)
    juce::String targetHost;  // IP address or hostname
    int targetPort;
    
    // OSC Sender
    std::unique_ptr<juce::OSCSender> oscSender;
    juce::CriticalSection senderLock;
    bool isConnected { false };
    
    // Input mapping (channel -> OSC address)
    struct InputMapping {
        juce::String oscAddress;      // e.g., "/cv/pitch", "/gate/1"
        PinDataType inputType;        // CV, Gate, Audio (detected automatically)
        bool enabled;                 // Is this input active?
        float lastSentValue;          // Last value sent (for change detection)
        juce::uint64 lastSendTime;    // Timestamp for throttling
    };
    
    std::vector<InputMapping> inputMappings;
    mutable juce::CriticalSection mappingLock;
    
    // Activity tracking for UI
    std::atomic<int> messagesSentThisBlock { 0 };
    std::atomic<int> totalMessagesSent { 0 };
    juce::uint64 lastActivityResetTime { 0 };
    
    // Helper methods
    void updateConnection();
    void sendOSCMessage(int channel, float value);
    float computeOutputValue(int channel, const juce::AudioBuffer<float>& buffer);
    bool shouldSend(int channel, float currentValue, float lastValue);
    PinDataType detectPinType(int channel, const juce::AudioBuffer<float>& buffer) const;
    
    // Add/remove mappings (called from UI)
    void addInputMapping(const juce::String& address);
    void removeInputMapping(int index);
    void updateInputMappingAddress(int index, const juce::String& newAddress);
    void setInputMappingEnabled(int index, bool enabled);
    
    // Helper to find next available input number for default addresses
    int findNextAvailableInputNumber() const;
    
    // Save/load input mappings and network settings to/from APVTS state
    void saveInputMappingsToState();
    void loadInputMappingsFromState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CVOSCSenderModuleProcessor)
};


