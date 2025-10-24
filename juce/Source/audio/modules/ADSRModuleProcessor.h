#pragma once

#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

class ADSRModuleProcessor : public ModuleProcessor
{
public:
    // Parameter ID constants
    static constexpr auto paramIdAttack = "attack";
    static constexpr auto paramIdDecay = "decay";
    static constexpr auto paramIdSustain = "sustain";
    static constexpr auto paramIdRelease = "release";
    static constexpr auto paramIdAttackMod = "attack_mod";
    static constexpr auto paramIdDecayMod = "decay_mod";
    static constexpr auto paramIdSustainMod = "sustain_mod";
    static constexpr auto paramIdReleaseMod = "release_mod";

    ADSRModuleProcessor();
    ~ADSRModuleProcessor() override = default;

    const juce::String getName() const override { return "adsr"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
private:
    static void HelpMarkerADSR(const char* desc);
public:

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("Gate In", 0);
        helpers.drawAudioInputPin("Trigger In", 1);
        
        helpers.drawAudioInputPin("Attack Mod", 2);
        helpers.drawAudioInputPin("Decay Mod", 3);
        helpers.drawAudioInputPin("Sustain Mod", 4);
        helpers.drawAudioInputPin("Release Mod", 5);
        helpers.drawAudioOutputPin("Env Out", 0);
        helpers.drawAudioOutputPin("Inv Out", 1);
        helpers.drawAudioOutputPin("EOR Gate", 2);
        helpers.drawAudioOutputPin("EOC Gate", 3);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Gate In";
            case 1: return "Trigger In";
            case 2: return "Attack Mod";
            case 3: return "Decay Mod";
            case 4: return "Sustain Mod";
            case 5: return "Release Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Env Out";
            case 1: return "Inv Out";
            case 2: return "EOR Gate";
            case 3: return "EOC Gate";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* attackParam { nullptr };
    std::atomic<float>* decayParam { nullptr };
    std::atomic<float>* sustainParam { nullptr };
    std::atomic<float>* releaseParam { nullptr };
    std::atomic<float>* attackModParam { nullptr };
    std::atomic<float>* decayModParam { nullptr };
    std::atomic<float>* sustainModParam { nullptr };
    std::atomic<float>* releaseModParam { nullptr };
    // Simple RT-safe envelope state (custom, replaces juce::ADSR to avoid surprises)
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage { Stage::Idle };
    float envLevel { 0.0f };
    bool lastGate { false };
    bool lastTrigger { false };
    int eorPending { 0 };
    int eocPending { 0 };
    double sr { 44100.0 };
};


