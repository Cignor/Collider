#pragma once
#include "ModuleProcessor.h"

class MIDIJogWheelModuleProcessor : public ModuleProcessor
{
public:
    MIDIJogWheelModuleProcessor();
    ~MIDIJogWheelModuleProcessor() override = default;
    
    const juce::String getName() const override { return "MIDI Jog Wheel"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    int midiCC = -1;  // -1 means unassigned
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float currentValue = 0.0f;
    bool isLearning = false;
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
};

