#pragma once
#include "ModuleProcessor.h"
#include <array>

class MIDIButtonsModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_BUTTONS = 32;
    
    MIDIButtonsModuleProcessor();
    ~MIDIButtonsModuleProcessor() override = default;
    
    const juce::String getName() const override { return "MIDI Buttons"; }
    
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
    enum class ButtonMode { Gate, Toggle, Trigger };
    enum class ViewMode { Visual, Compact, Table };
    
    struct ControlMapping
    {
        int midiCC = -1;  // -1 means unassigned
        float currentValue = 0.0f;
        ButtonMode mode = ButtonMode::Gate;
        bool toggleState = false;
        int triggerSamplesRemaining = 0;
    };
    
    std::array<ControlMapping, MAX_BUTTONS> mappings;
    int learningIndex = -1;  // Index of button currently in learn mode
    
#if defined(PRESET_CREATOR_UI)
    ViewMode viewMode = ViewMode::Visual;  // UI view mode
    
    void drawVisualButtons(int numActive, const std::function<void()>& onModificationEnded);
    void drawCompactList(int numActive, const std::function<void()>& onModificationEnded);
    void drawTableView(int numActive, const std::function<void()>& onModificationEnded);
    ImVec4 getModeColor(ButtonMode mode, float brightness) const;
#endif
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterInt* numButtonsParam { nullptr };
};

