#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class VCFModuleProcessor : public ModuleProcessor
{
public:
    // Parameter ID constants
    static constexpr auto paramIdCutoff = "cutoff";
    static constexpr auto paramIdResonance = "resonance";
    static constexpr auto paramIdType = "type";
    static constexpr auto paramIdTypeMod = "type_mod";

    VCFModuleProcessor();
    ~VCFModuleProcessor() override = default;

    const juce::String getName() const override { return "vcf"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        
        // Helper for tooltips
        auto HelpMarkerVCF = [](const char* desc) {
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };
        
        float cutoff = cutoffParam != nullptr ? cutoffParam->load() : 1000.0f;
        float q = resonanceParam != nullptr ? resonanceParam->load() : 1.0f;
        int ftype = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdType))) ftype = p->getIndex();

        ImGui::PushItemWidth(itemWidth);

        // === FILTER PARAMETERS SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Filter Parameters");
        ImGui::Spacing();

        // Cutoff
        bool isCutoffModulated = isParamModulated(paramIdCutoff);
        if (isCutoffModulated) {
            cutoff = getLiveParamValueFor(paramIdCutoff, "cutoff_live", cutoff);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat("Cutoff", &cutoff, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic)) {
            if (!isCutoffModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCutoff))) *p = cutoff;
            }
        }
        if (!isCutoffModulated) adjustParamOnWheel(ap.getParameter(paramIdCutoff), "cutoffHz", cutoff);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isCutoffModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerVCF("Filter cutoff frequency in Hz (20-20000 Hz)\nLogarithmic scale for musical tuning");

        // Resonance
        bool isResoModulated = isParamModulated(paramIdResonance);
        if (isResoModulated) {
            q = getLiveParamValueFor(paramIdResonance, "resonance_live", q);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat("Resonance", &q, 0.1f, 10.0f)) {
            if (!isResoModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdResonance))) *p = q;
            }
        }
        if (!isResoModulated) adjustParamOnWheel(ap.getParameter(paramIdResonance), "resonance", q);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isResoModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerVCF("Filter resonance/Q factor (0.1-10)\nHigher values create a peak at cutoff frequency");
        
        // Type
        bool isTypeModulated = isParamModulated(paramIdTypeMod);
        if (isTypeModulated) {
            ftype = static_cast<int>(getLiveParamValueFor(paramIdTypeMod, "type_live", static_cast<float>(ftype)));
            ImGui::BeginDisabled();
        }
        if (ImGui::Combo("Type", &ftype, "Low-pass\0High-pass\0Band-pass\0\0")) {
            if (!isTypeModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdType))) *p = ftype;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isTypeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerVCF("Filter type:\nLow-pass = removes high frequencies\nHigh-pass = removes low frequencies\nBand-pass = keeps only mid frequencies");

        ImGui::Spacing();
        ImGui::Spacing();

        // === MODULATION MODE SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Modulation Mode");
        ImGui::Spacing();
        
        bool relativeCutoffMod = relativeCutoffModParam ? (relativeCutoffModParam->load() > 0.5f) : true;
        if (ImGui::Checkbox("Relative Cutoff Mod", &relativeCutoffMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeCutoffMod")))
            {
                *p = relativeCutoffMod;
                juce::Logger::writeToLog("[VCF UI] Relative Cutoff Mod changed to: " + juce::String(relativeCutoffMod ? "TRUE" : "FALSE"));
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::SameLine();
        HelpMarkerVCF("Relative: CV modulates around slider cutoff (±4 octaves)\nAbsolute: CV directly controls cutoff (20Hz-20kHz, ignores slider)");

        bool relativeResonanceMod = relativeResonanceModParam ? (relativeResonanceModParam->load() > 0.5f) : true;
        if (ImGui::Checkbox("Relative Resonance Mod", &relativeResonanceMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeResonanceMod")))
            {
                *p = relativeResonanceMod;
                juce::Logger::writeToLog("[VCF UI] Relative Resonance Mod changed to: " + juce::String(relativeResonanceMod ? "TRUE" : "FALSE"));
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::SameLine();
        HelpMarkerVCF("Relative: CV adds offset to slider resonance (±5 units)\nAbsolute: CV directly controls resonance (0.1-10.0, ignores slider)");

        ImGui::Spacing();
        ImGui::Spacing();

        // === FILTER RESPONSE SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Filter Response");
        ImGui::Spacing();

        // Visual frequency response curve
        float responseCurve[50];
        float logCutoff = std::log10(cutoff);
        
        for (int i = 0; i < 50; ++i)
        {
            float freq = 20.0f * std::pow(1000.0f, (float)i / 49.0f);  // 20 Hz to 20 kHz
            float logFreq = std::log10(freq);
            float delta = logFreq - logCutoff;
            
            // Simplified filter response simulation
            if (ftype == 0) {  // Low-pass
                responseCurve[i] = 1.0f / (1.0f + q * delta * delta * 4.0f);
            } else if (ftype == 1) {  // High-pass
                responseCurve[i] = 1.0f - (1.0f / (1.0f + q * delta * delta * 4.0f));
            } else {  // Band-pass
                responseCurve[i] = std::exp(-delta * delta * q);
            }
            responseCurve[i] = juce::jlimit(0.0f, 1.0f, responseCurve[i]);
        }

        // Color-code by filter type
        ImVec4 curveColor = (ftype == 0) ? ImVec4(1.0f, 0.5f, 0.3f, 1.0f) :  // Low-pass: orange
                            (ftype == 1) ? ImVec4(0.3f, 0.7f, 1.0f, 1.0f) :  // High-pass: blue
                                           ImVec4(0.5f, 1.0f, 0.5f, 1.0f);   // Band-pass: green
        
        ImGui::PushStyleColor(ImGuiCol_PlotLines, curveColor);
        ImGui::PlotLines("##response", responseCurve, 50, 0, nullptr, 0.0f, 1.0f, ImVec2(itemWidth, 60));
        ImGui::PopStyleColor();

        // Filter type badge
        const char* typeNames[] = { "LOW-PASS", "HIGH-PASS", "BAND-PASS" };
        ImGui::PushStyleColor(ImGuiCol_Text, curveColor);
        ImGui::Text("Active: %s", typeNames[ftype]);
        ImGui::PopStyleColor();

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);

        helpers.drawAudioInputPin("Cutoff Mod", 2);
        helpers.drawAudioInputPin("Resonance Mod", 3);
        helpers.drawAudioInputPin("Type Mod", 4);

        helpers.drawAudioOutputPin("Out L", 0);
        helpers.drawAudioOutputPin("Out R", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "In L";
            case 1: return "In R";
            case 2: return "Cutoff Mod";
            case 3: return "Resonance Mod";
            case 4: return "Type Mod";
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
    juce::dsp::StateVariableTPTFilter<float> filterA;
    juce::dsp::StateVariableTPTFilter<float> filterB;

    // Cached parameter pointers
    std::atomic<float>* cutoffParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* typeModParam = nullptr;
    std::atomic<float>* relativeCutoffModParam = nullptr;
    std::atomic<float>* relativeResonanceModParam = nullptr;
    
    // Smoothed values to prevent zipper noise
    juce::SmoothedValue<float> cutoffSm;
    juce::SmoothedValue<float> resonanceSm;

    // Type crossfade management
    bool activeIsA { true };
    int  activeType { 0 };
    int  pendingType { 0 };
    int  typeCrossfadeRemaining { 0 };
    static constexpr int TYPE_CROSSFADE_SAMPLES = 128; // short, click-free

    static inline void configureFilterForType(juce::dsp::StateVariableTPTFilter<float>& f, int type)
    {
        switch (type) {
            case 0: f.setType(juce::dsp::StateVariableTPTFilterType::lowpass); break;
            case 1: f.setType(juce::dsp::StateVariableTPTFilterType::highpass); break;
            default: f.setType(juce::dsp::StateVariableTPTFilterType::bandpass); break;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCFModuleProcessor)
};


