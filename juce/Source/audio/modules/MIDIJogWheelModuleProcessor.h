#pragma once
#include "ModuleProcessor.h"

class MIDIJogWheelModuleProcessor : public ModuleProcessor
{
public:
    MIDIJogWheelModuleProcessor();
    ~MIDIJogWheelModuleProcessor() override = default;
    
    const juce::String getName() const override { return "MIDI Jog Wheel"; }
    
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    // Multi-MIDI device support
    void handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    struct ControlMapping
    {
        int midiCC = -1;         // The main CC to track (learn mode)
        float currentValue = 0.0f;
        int lastRelativeValue = -1;  // For delta calculation
    };
    
    ControlMapping mapping;
    bool isLearning = false;
    
#if defined(PRESET_CREATOR_UI)
    int selectedPresetIndex = -1;
    char presetNameBuffer[128] = {};
    juce::String activeControllerPresetName;
#endif
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    juce::AudioParameterChoice* incrementParam { nullptr };
    juce::AudioParameterFloat* resetValueParam { nullptr };
    juce::AudioParameterInt* midiChannelParam { nullptr };
    juce::AudioParameterChoice* deviceFilterParam { nullptr };
};
