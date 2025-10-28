#pragma once
#include "ModuleProcessor.h"
#include <array>

class MIDIFadersModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_FADERS = 16;
    
    MIDIFadersModuleProcessor();
    ~MIDIFadersModuleProcessor() override = default;
    
    const juce::String getName() const override { return "midi_faders"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    
    // Multi-MIDI device support
    void handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    enum class ViewMode { Visual, Compact, Table };
    
    struct ControlMapping
    {
        int midiCC = -1;  // -1 means unassigned
        float minVal = 0.0f;
        float maxVal = 1.0f;
        float currentValue = 0.0f;
    };
    
    std::array<ControlMapping, MAX_FADERS> mappings;
    int learningIndex = -1;  // Index of fader currently in learn mode
    
#if defined(PRESET_CREATOR_UI)
    ViewMode viewMode = ViewMode::Visual;  // UI view mode
    
    // Preset UI state
    int selectedPresetIndex = -1;
    char presetNameBuffer[128] = {};
    juce::String activeControllerPresetName;  // Name of currently active preset
    
    void drawVisualFaders(int numActive, const std::function<void()>& onModificationEnded);
    void drawCompactList(int numActive, const std::function<void()>& onModificationEnded);
    void drawTableView(int numActive, const std::function<void()>& onModificationEnded);
#endif
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterInt* numFadersParam { nullptr };
    juce::AudioParameterInt* midiChannelParam { nullptr };
    juce::AudioParameterChoice* deviceFilterParam { nullptr };
};

