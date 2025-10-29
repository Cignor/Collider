#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class VCAModuleProcessor : public ModuleProcessor
{
public:
    VCAModuleProcessor();
    ~VCAModuleProcessor() override = default;

    const juce::String getName() const override { return "vca"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Parameter bus contract implementation
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        float gainDb = gainParam != nullptr ? gainParam->load() : 0.0f;
        ImGui::PushItemWidth (itemWidth);
        
        bool isGainModulated = isParamModulated("gain");
        if (isGainModulated) {
            gainDb = getLiveParamValueFor("gain", "gain_live", gainDb);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat ("Gain dB", &gainDb, -60.0f, 6.0f)) if (!isGainModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gain"))) *p = gainDb;
        if (!isGainModulated) adjustParamOnWheel(ap.getParameter("gain"), "gain", gainDb);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        ImGui::Spacing();
        ImGui::Spacing();

        // Modulation mode
        bool relativeGainMod = relativeGainModParam ? (relativeGainModParam->load() > 0.5f) : true;
        if (ImGui::Checkbox("Relative Gain Mod", &relativeGainMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeGainMod")))
            {
                *p = relativeGainMod;
                juce::Logger::writeToLog("[VCA UI] Relative Gain Mod changed to: " + juce::String(relativeGainMod ? "TRUE" : "FALSE"));
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relative: CV modulates around slider gain (±30dB)\nAbsolute: CV directly controls gain (-60dB to +6dB, ignores slider)");

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);
        
        int busIdx, chanInBus;
        if (getParamRouting("gain", busIdx, chanInBus))
            helpers.drawAudioInputPin("Gain Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
            
        helpers.drawAudioOutputPin("Out L", 0);
        helpers.drawAudioOutputPin("Out R", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
            case 2: return "Gain Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out L";
            case 1: return "Out R";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Gain<float> gain;

    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* relativeGainModParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCAModuleProcessor)
};


