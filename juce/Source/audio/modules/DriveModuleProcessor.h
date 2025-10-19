#pragma once

#include "ModuleProcessor.h"

class DriveModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdDrive = "drive";
    static constexpr auto paramIdMix = "mix";

    DriveModuleProcessor();
    ~DriveModuleProcessor() override = default;

    const juce::String getName() const override { return "drive"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // A temporary buffer is needed to properly implement the dry/wet mix
    juce::AudioBuffer<float> tempBuffer;

    // Cached atomic pointers to parameters
    std::atomic<float>* driveParam { nullptr };
    std::atomic<float>* mixParam { nullptr };
};

