#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

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
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        const auto& theme = ThemeManager::getInstance().getCurrentTheme();
        
        // Helper for tooltips
        auto HelpMarkerReverb = [](const char* desc) {
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };
        
        // Get live modulated values for display
        bool isSizeModulated = isParamModulated("size");
        bool isDampModulated = isParamModulated("damp");
        bool isMixModulated = isParamModulated("mix");
        
        float size = isSizeModulated ? getLiveParamValueFor("size", "size_live", sizeParam->load()) : (sizeParam != nullptr ? sizeParam->load() : 0.5f);
        float damp = isDampModulated ? getLiveParamValueFor("damp", "damp_live", dampParam->load()) : (dampParam != nullptr ? dampParam->load() : 0.3f);
        float mix = isMixModulated ? getLiveParamValueFor("mix", "mix_live", mixParam->load()) : (mixParam != nullptr ? mixParam->load() : 0.3f);
        
        ImGui::PushItemWidth(itemWidth);

        // === REVERB PARAMETERS SECTION ===
        ThemeText("Reverb Parameters", theme.text.section_header);
        ImGui::Spacing();

        // Size
        if (isSizeModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Size", &size, 0.0f, 1.0f)) if (!isSizeModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("size"))) *p = size;
        if (!isSizeModulated) adjustParamOnWheel(ap.getParameter("size"), "size", size);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isSizeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerReverb("Room size (0-1)\n0 = small room, 1 = large hall");
        
        // Damp
        if (isDampModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Damp", &damp, 0.0f, 1.0f)) if (!isDampModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("damp"))) *p = damp;
        if (!isDampModulated) adjustParamOnWheel(ap.getParameter("damp"), "damp", damp);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isDampModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerReverb("High frequency damping (0-1)\n0 = bright, 1 = dark/muffled");
        
        // Mix
        if (isMixModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f)) if (!isMixModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("mix"))) *p = mix;
        if (!isMixModulated) adjustParamOnWheel(ap.getParameter("mix"), "mix", mix);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMixModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerReverb("Dry/wet mix (0-1)\n0 = dry only, 1 = wet only");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // === RELATIVE MODULATION SECTION ===
        ThemeText("CV Input Modes", theme.modulation.frequency);
        ImGui::Spacing();
        
        // Relative Size Mod checkbox
        bool relativeSizeMod = relativeSizeModParam != nullptr && relativeSizeModParam->load() > 0.5f;
        if (ImGui::Checkbox("Relative Size Mod", &relativeSizeMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeSizeMod")))
                *p = relativeSizeMod;
            juce::Logger::writeToLog("[Reverb UI] Relative Size Mod: " + juce::String(relativeSizeMod ? "ON" : "OFF"));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ON: CV modulates around slider value (±0.5)\nOFF: CV directly sets size (0-1)");
        }
        
        // Relative Damp Mod checkbox
        bool relativeDampMod = relativeDampModParam != nullptr && relativeDampModParam->load() > 0.5f;
        if (ImGui::Checkbox("Relative Damp Mod", &relativeDampMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDampMod")))
                *p = relativeDampMod;
            juce::Logger::writeToLog("[Reverb UI] Relative Damp Mod: " + juce::String(relativeDampMod ? "ON" : "OFF"));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ON: CV modulates around slider value (±0.5)\nOFF: CV directly sets damp (0-1)");
        }
        
        // Relative Mix Mod checkbox
        bool relativeMixMod = relativeMixModParam != nullptr && relativeMixModParam->load() > 0.5f;
        if (ImGui::Checkbox("Relative Mix Mod", &relativeMixMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeMixMod")))
                *p = relativeMixMod;
            juce::Logger::writeToLog("[Reverb UI] Relative Mix Mod: " + juce::String(relativeMixMod ? "ON" : "OFF"));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ON: CV modulates around slider value (±0.5)\nOFF: CV directly sets mix (0-1)");
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // === REVERB VISUALIZATION SECTION ===
        ThemeText("Decay Envelope", theme.text.section_header);
        ImGui::Spacing();

        // Visual decay curve based on size and damp
        float decayCurve[50];
        float rt60 = size * 3.0f + 0.5f;  // Decay time in seconds (0.5-3.5s)
        float dampFactor = 1.0f - (damp * 0.7f);  // Damping affects decay rate
        
        for (int i = 0; i < 50; ++i)
        {
            float t = (float)i / 49.0f;  // 0 to 1
            decayCurve[i] = std::exp(-t * 5.0f / (rt60 * dampFactor));
            decayCurve[i] = juce::jlimit(0.0f, 1.0f, decayCurve[i]);
        }

        ImGui::PushStyleColor(ImGuiCol_PlotLines, theme.modulation.frequency);
        ImGui::PlotLines("##decay", decayCurve, 50, 0, nullptr, 0.0f, 1.0f, ImVec2(itemWidth, 50));
        ImGui::PopStyleColor();

        // Room type indicator based on size
        const char* roomType = (size < 0.3f) ? "Small Room" : (size < 0.7f) ? "Medium Hall" : "Large Cathedral";
        ImGui::Text("Space: %s", roomType);
        ImGui::Text("RT60: %.2f s", rt60 * dampFactor);

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
    std::atomic<float>* relativeSizeModParam { nullptr };
    std::atomic<float>* relativeDampModParam { nullptr };
    std::atomic<float>* relativeMixModParam { nullptr };
};


