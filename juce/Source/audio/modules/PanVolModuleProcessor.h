#pragma once

#include "ModuleProcessor.h"

class PanVolModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdPan = "pan";
    static constexpr auto paramIdVolume = "volume";
    
    // CV modulation parameter IDs
    static constexpr auto paramIdPanMod = "pan_mod";
    static constexpr auto paramIdVolumeMod = "volume_mod";
    
    PanVolModuleProcessor();
    ~PanVolModuleProcessor() override = default;
    
    const juce::String getName() const override { return "panvol"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    void drawIoPins(const NodePinHelpers& helpers) override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    ImVec2 getCustomNodeSize() const override;
#endif
    
    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Pan Mod";
            case 1: return "Vol Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }
    
    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Pan Out";
            case 1: return "Vol Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
    
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* panParam { nullptr };      // -1.0 to +1.0
    std::atomic<float>* volumeParam { nullptr };    // -60.0 to +6.0 dB
};

