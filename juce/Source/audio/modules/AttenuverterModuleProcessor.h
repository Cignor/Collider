#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class AttenuverterModuleProcessor : public ModuleProcessor
{
public:
    AttenuverterModuleProcessor();
    ~AttenuverterModuleProcessor() override = default;

    const juce::String getName() const override { return "attenuverter"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        float amt = amountParam != nullptr ? amountParam->load() : 1.0f;
        ImGui::PushItemWidth (itemWidth);
        
        bool isAmountModulated = isParamModulated("amount");
        if (isAmountModulated) {
            amt = getLiveParamValueFor("amount", "amount_live", amt);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat ("Amount", &amt, -10.0f, 10.0f)) {
            if (!isAmountModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("amount"))) *p = amt;
            }
        }
        if (!isAmountModulated) adjustParamOnWheel (ap.getParameter ("amount"), "amount", amt);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isAmountModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);
        helpers.drawAudioInputPin("Amount Mod", 2);
        helpers.drawAudioOutputPin("Out L", 0);
        helpers.drawAudioOutputPin("Out R", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
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
    std::atomic<float>* amountParam { nullptr }; // -1..1
    juce::AudioParameterBool* rectifyParam { nullptr }; // Rectify mode for Audio-to-CV conversion
};


