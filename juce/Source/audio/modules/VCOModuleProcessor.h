#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class VCOModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdFrequency   = "frequency";
    static constexpr auto paramIdWaveform    = "waveform";
    // Virtual target only (no APVTS param needed) – used for routing to select waveform
    static constexpr auto paramIdWaveformMod = "waveform_mod";

    VCOModuleProcessor();
    ~VCOModuleProcessor() override = default;

    const juce::String getName() const override { return "vco"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        float freq = frequencyParam != nullptr ? getLiveParamValueFor(paramIdFrequency, paramIdFrequency, frequencyParam->load()) : 440.0f;
        int wave = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) wave = (int) getLiveParamValueFor(paramIdWaveformMod, paramIdWaveform, (float) p->getIndex());

        ImGui::PushItemWidth (itemWidth);

        // Frequency (disabled when modulated)
        const bool freqMod = isParamModulated(paramIdFrequency);
        if (freqMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat ("Frequency", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
        {
            if (!freqMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = freq;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (!freqMod) adjustParamOnWheel (ap.getParameter(paramIdFrequency), "frequencyHz", freq);
        if (freqMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        // Waveform (disabled when modulated via waveform_mod)
        const bool waveMod = isParamModulated(paramIdWaveformMod);
        if (waveMod) ImGui::BeginDisabled();
        if (ImGui::Combo ("Waveform", &wave, "Sine\0Sawtooth\0Square\0\0"))
        {
            if (!waveMod) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) *p = wave;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (waveMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Single input bus (0): ch0 Frequency Mod, ch1 Waveform Mod, ch2 Gate
        helpers.drawAudioInputPin("Frequency", 0);
        helpers.drawAudioInputPin("Waveform", 1);
        helpers.drawAudioInputPin("Gate", 2);
        helpers.drawAudioOutputPin("Out", 0);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Frequency Mod";
            case 1: return "Waveform Mod";
            case 2: return "Gate";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oscillator<float> oscillator;
    int currentWaveform = -1;

    // Cached parameter pointers
    std::atomic<float>* frequencyParam { nullptr };
    std::atomic<float>* waveformParam  { nullptr };

    // Click-free gating
    float smoothedGate { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCOModuleProcessor)
};


