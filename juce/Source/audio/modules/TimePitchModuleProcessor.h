#pragma once

#include "ModuleProcessor.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/TimePitchProcessor.h"

class TimePitchModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr const char* paramIdSpeed     = "speed";
    static constexpr const char* paramIdPitch     = "pitch";
    static constexpr const char* paramIdEngine    = "engine";
    static constexpr const char* paramIdSpeedMod  = "speed_mod";
    static constexpr const char* paramIdPitchMod  = "pitch_mod";

    TimePitchModuleProcessor();
    ~TimePitchModuleProcessor() override = default;

    const juce::String getName() const override { return "timepitch"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
            case 2: return "Speed Mod";
            case 3: return "Pitch Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }
    
    // Parameter bus contract implementation
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override;
    void drawIoPins (const NodePinHelpers& helpers) override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    TimePitchProcessor timePitch;
    juce::HeapBlock<float> interleavedInput;
    juce::HeapBlock<float> interleavedOutput;
    int interleavedInputCapacityFrames { 0 };
    int interleavedOutputCapacityFrames { 0 };

    // Parameter pointers
    std::atomic<float>* speedParam { nullptr };
    std::atomic<float>* pitchParam { nullptr };
    std::atomic<float>* speedModParam { nullptr };
    std::atomic<float>* pitchModParam { nullptr };
    juce::AudioParameterChoice* engineParam { nullptr };
    double sr { 48000.0 };

    // --- Streaming FIFO for live input buffering ---
    juce::AudioBuffer<float> inputFifo; // stereo FIFO storage
    juce::AbstractFifo abstractFifo { 0 }; // manages read/write indices
    int fifoSize { 0 };
    
    // Smoothed parameters for zipper-free modulation
    juce::SmoothedValue<float> speedSm;
    juce::SmoothedValue<float> pitchSm;
};


