#pragma once
#include "ModuleProcessor.h"
#include <vector>

class AudioInputModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_CHANNELS = 16;
    
    // Parameter ID constants
    static constexpr auto paramIdNumChannels = "numChannels";
    static constexpr auto paramIdGateThreshold = "gateThreshold";
    static constexpr auto paramIdTriggerThreshold = "triggerThreshold";

    AudioInputModuleProcessor();
    ~AudioInputModuleProcessor() override = default;

    const juce::String getName() const override { return "audio_input"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Override labels for better pin naming
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;

    // Extra state for device name and parameters
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    // Device name access for UI
    juce::String getSelectedDeviceName() const { return selectedDeviceName; }
    void setSelectedDeviceName(const juce::String& name) { selectedDeviceName = name; }

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
    
    juce::AudioParameterFloat* gateThresholdParam { nullptr };
    juce::AudioParameterFloat* triggerThresholdParam { nullptr };

    // State for signal analysis
    enum class PeakState { SILENT, PEAK };
    std::vector<PeakState> peakState;
    std::vector<bool> lastTriggerState;
    std::vector<int> silenceCounter;
    std::vector<int> eopPulseRemaining;
    std::vector<int> trigPulseRemaining;

    static constexpr int MIN_SILENCE_SAMPLES = 480; // ~10ms at 48kHz
};

