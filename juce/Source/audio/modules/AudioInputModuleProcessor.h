#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <atomic>

class AudioInputModuleProcessor : public ModuleProcessor,
                                   public juce::AudioIODeviceCallback
{
public:
    static constexpr int MAX_CHANNELS = 16;
    
    // Parameter ID constants
    static constexpr auto paramIdNumChannels = "numChannels";
    static constexpr auto paramIdGateThreshold = "gateThreshold";
    static constexpr auto paramIdTriggerThreshold = "triggerThreshold";

    AudioInputModuleProcessor();
    ~AudioInputModuleProcessor() override;

    const juce::String getName() const override { return "audio_input"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    // AudioIODeviceCallback implementation
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Override labels for better pin naming
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;

    // Extra state for device name and parameters
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    // Device name access for UI
    juce::String getSelectedDeviceName() const { return selectedDeviceName; }
    void setSelectedDeviceName(const juce::String& name);
    
    // Device type access
    juce::String getSelectedDeviceType() const { return selectedDeviceType; }
    void setSelectedDeviceType(const juce::String& type) { selectedDeviceType = type; }
    
    // Set the AudioDeviceManager for device access
    void setAudioDeviceManager(juce::AudioDeviceManager* adm) { deviceManager = adm; updateDevice(); }

    // Real-time level metering for UI (one per channel)
    std::vector<std::unique_ptr<std::atomic<float>>> channelLevels;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Parameter pointers
    juce::AudioParameterInt* numChannelsParam { nullptr };
    std::vector<juce::AudioParameterInt*> channelMappingParams;
    juce::String selectedDeviceName;
    juce::String selectedDeviceType;
    
    juce::AudioParameterFloat* gateThresholdParam { nullptr };
    juce::AudioParameterFloat* triggerThresholdParam { nullptr };
    
    // Per-device audio capture
    juce::AudioDeviceManager* deviceManager { nullptr };
    std::unique_ptr<juce::AudioIODevice> audioDevice;
    juce::AudioSourcePlayer audioSourcePlayer;
    
    // Lock-free audio buffer transfer (ASIO-safe)
    // Increased size to handle timing mismatches between ASIO callback and main graph
    static constexpr int FIFO_SIZE = 8192;  // ~170ms at 48kHz, enough for any buffer size mismatch
    juce::AbstractFifo audioFifo { FIFO_SIZE };
    juce::AudioBuffer<float> fifoBuffer;
    juce::AudioBuffer<float> readBuffer;
    
    // Track callback timing to detect if ASIO is running properly
    std::atomic<juce::int64> lastCallbackTime { 0 };
    std::atomic<int> callbackCount { 0 };
    
    void updateDevice();
    void closeDevice();

    // State for signal analysis
    enum class PeakState { SILENT, PEAK };
    std::vector<PeakState> peakState;
    std::vector<bool> lastTriggerState;
    std::vector<int> silenceCounter;
    std::vector<int> eopPulseRemaining;
    std::vector<int> trigPulseRemaining;

    static constexpr int MIN_SILENCE_SAMPLES = 480; // ~10ms at 48kHz
};

