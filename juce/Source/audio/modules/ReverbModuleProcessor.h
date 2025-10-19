#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class ReverbModuleProcessor : public ModuleProcessor
{
public:
    ReverbModuleProcessor();
    ~ReverbModuleProcessor() override = default;

    const juce::String getName() const override { return "reverb"; }

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
        
        // Get live modulated values for display
        bool isSizeModulated = isParamModulated("size");
        bool isDampModulated = isParamModulated("damp");
        bool isMixModulated = isParamModulated("mix");
        
        // Use correct mod param IDs (same as parameter IDs)
        float size = isSizeModulated ? getLiveParamValueFor("size", "size_live", sizeParam->load()) : (sizeParam != nullptr ? sizeParam->load() : 0.5f);
        float damp = isDampModulated ? getLiveParamValueFor("damp", "damp_live", dampParam->load()) : (dampParam != nullptr ? dampParam->load() : 0.3f);
        float mix = isMixModulated ? getLiveParamValueFor("mix", "mix_live", mixParam->load()) : (mixParam != nullptr ? mixParam->load() : 0.3f);
        ImGui::PushItemWidth (itemWidth);

        // Size
        if (isSizeModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat ("Size", &size, 0.0f, 1.0f)) if (!isSizeModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("size"))) *p = size;
        if (!isSizeModulated) adjustParamOnWheel (ap.getParameter ("size"), "size", size);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isSizeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        
        // Damp
        if (isDampModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat ("Damp", &damp, 0.0f, 1.0f)) if (!isDampModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("damp"))) *p = damp;
        if (!isDampModulated) adjustParamOnWheel (ap.getParameter ("damp"), "damp", damp);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isDampModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        
        // Mix
        if (isMixModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat ("Mix", &mix, 0.0f, 1.0f)) if (!isMixModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("mix")))  *p = mix;
        if (!isMixModulated) adjustParamOnWheel (ap.getParameter ("mix"), "mix", mix);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMixModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);
        
        int busIdx, chanInBus;
        if (getParamRouting("size", busIdx, chanInBus))
            helpers.drawAudioInputPin("Size Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting("damp", busIdx, chanInBus))
            helpers.drawAudioInputPin("Damp Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting("mix", busIdx, chanInBus))
            helpers.drawAudioInputPin("Mix Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
            
        helpers.drawAudioOutputPin("Out L", 0);
        helpers.drawAudioOutputPin("Out R", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
            case 2: return "Size Mod";
            case 3: return "Damp Mod";
            case 4: return "Mix Mod";
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
    juce::dsp::Reverb reverb;
    std::atomic<float>* sizeParam { nullptr };
    std::atomic<float>* dampParam { nullptr };
    std::atomic<float>* mixParam { nullptr };
};


