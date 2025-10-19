#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

// Simple descriptor for one formant band
struct FormantData { float frequency; float gain; float q; };

class VocalTractFilterModuleProcessor : public ModuleProcessor
{
public:
    VocalTractFilterModuleProcessor();
    ~VocalTractFilterModuleProcessor() override = default;

    const juce::String getName() const override { return "vocal tract filter"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Audio In";
            case 1: return "Vowel Mod";
            case 2: return "Formant Mod";
            case 3: return "Instability Mod";
            case 4: return "Gain Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Audio Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

private:
    // Internal helpers
    void updateCoefficients(float vowelShape, float formantShift, float instability);
    void ensureWorkBuffers(int numChannels, int numSamples);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* vowelShapeParam { nullptr };
    std::atomic<float>* formantShiftParam { nullptr };
    std::atomic<float>* instabilityParam { nullptr };
    std::atomic<float>* outputGainParam { nullptr }; // dB

    // Formant tables
    static const FormantData VOWEL_A[4];
    static const FormantData VOWEL_E[4];
    static const FormantData VOWEL_I[4];
    static const FormantData VOWEL_O[4];
    static const FormantData VOWEL_U[4];

    // DSP - Use simple IIR filters directly for mono processing
    using IIRFilter = juce::dsp::IIR::Filter<float>;
    struct Bands { IIRFilter b0, b1, b2, b3; } bands;
    std::array<float, 4> bandGains { 1.0f, 0.5f, 0.2f, 0.15f };
    juce::dsp::Oscillator<float> wowOscillator, flutterOscillator;
    juce::dsp::ProcessSpec dspSpec { 0.0, 0, 0 };

    // Preallocated working buffers to avoid RT allocations
    juce::AudioBuffer<float> workBuffer;
    juce::AudioBuffer<float> sumBuffer;
    juce::AudioBuffer<float> tmpBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTractFilterModuleProcessor);
};
