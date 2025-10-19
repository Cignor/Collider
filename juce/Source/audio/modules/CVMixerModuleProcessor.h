#pragma once

#include "ModuleProcessor.h"

/**
 * CVMixerModuleProcessor - A dedicated mixer for control voltages (CV/modulation signals).
 * 
 * Features:
 * - Linear crossfading between two inputs (A and B) for precise morphing
 * - Additional summing inputs (C and D) with bipolar level controls
 * - Inverted output for signal polarity flipping
 * - Designed for CV signals with mathematically predictable linear operations
 */
class CVMixerModuleProcessor : public ModuleProcessor
{
public:
    CVMixerModuleProcessor();
    ~CVMixerModuleProcessor() override = default;

    const juce::String getName() const override { return "cv mixer"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    void drawIoPins(const NodePinHelpers& helpers) override;
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In A";
            case 1: return "In B";
            case 2: return "In C";
            case 3: return "In D";
            case 4: return "Crossfade Mod";
            case 5: return "Level A Mod";
            case 6: return "Level C Mod";
            case 7: return "Level D Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Mix Out";
            case 1: return "Inv Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* crossfadeParam { nullptr };  // -1..1 (A to B)
    std::atomic<float>* levelAParam { nullptr };     // 0..1 (master level for A/B crossfade)
    std::atomic<float>* levelCParam { nullptr };     // -1..1 (bipolar for C)
    std::atomic<float>* levelDParam { nullptr };     // -1..1 (bipolar for D)
};

