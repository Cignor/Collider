#pragma once

#include "ModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

class SAndHModuleProcessor : public ModuleProcessor
{
public:
    SAndHModuleProcessor();
    ~SAndHModuleProcessor() override = default;

    const juce::String getName() const override { return "s_and_h"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        const auto& theme = ThemeManager::getInstance().getCurrentTheme();
        float thr=0.5f, slew=0.0f; int edge=0;
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("threshold"))) thr = *p;
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("edge"))) edge = p->getIndex();
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("slewMs"))) slew = *p;
        
        ImGui::PushItemWidth (itemWidth);

        // === SECTION: Sample Settings ===
        ThemeText("SAMPLE SETTINGS", theme.modulation.frequency);

        // Threshold
        bool isThreshModulated = isParamModulated("threshold_mod");
        if (isThreshModulated) {
            thr = getLiveParamValueFor("threshold_mod", "threshold_live", thr);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat ("Threshold", &thr, 0.0f, 1.0f)) if (!isThreshModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("threshold"))) *p = thr;
        if (!isThreshModulated) adjustParamOnWheel (ap.getParameter ("threshold"), "threshold", thr);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isThreshModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trigger threshold for sampling signal");

        // Edge
        bool isEdgeModulated = isParamModulated("edge_mod");
        if (isEdgeModulated) {
            edge = static_cast<int>(getLiveParamValueFor("edge_mod", "edge_live", static_cast<float>(edge)));
            ImGui::BeginDisabled();
        }
        const char* items = "Rising\0Falling\0Both\0\0";
        if (ImGui::Combo ("Edge", &edge, items)) if (!isEdgeModulated) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("edge"))) *p = edge;
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isEdgeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which edge triggers sampling");

        // Slew
        bool isSlewModulated = isParamModulated("slewMs_mod");
        if (isSlewModulated) {
            slew = getLiveParamValueFor("slewMs_mod", "slewMs_live", slew);
            ImGui::BeginDisabled();
        }
        if (ImGui::SliderFloat ("Slew", &slew, 0.0f, 2000.0f, "%.1f ms", ImGuiSliderFlags_Logarithmic)) if (!isSlewModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("slewMs"))) *p = slew;
        if (!isSlewModulated) adjustParamOnWheel (ap.getParameter ("slewMs"), "slewMs", slew);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isSlewModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smooth transitions between held values");

        ImGui::Spacing();
        ImGui::Spacing();

        // === SECTION: Held Values ===
        ThemeText("HELD VALUES", theme.modulation.frequency);
        
        // Get current held values from output tracking
        float heldL = 0.0f, heldR = 0.0f;
        if (lastOutputValues.size() >= 2) {
            if (lastOutputValues[0]) heldL = lastOutputValues[0]->load();
            if (lastOutputValues[1]) heldR = lastOutputValues[1]->load();
        }
        
        // Calculate fixed width for progress bars using actual text measurements
        const float labelTextWidth = ImGui::CalcTextSize("R:").x;  // R is wider than L
        const float valueTextWidth = ImGui::CalcTextSize("-0.999").x;  // Max expected width
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float barWidth = itemWidth - labelTextWidth - valueTextWidth - (spacing * 2.0f);
        
        // Display held values with progress bars (bidirectional for -1 to +1)
        ImGui::Text("L:");
        ImGui::SameLine();
        ImGui::ProgressBar((heldL + 1.0f) / 2.0f, ImVec2(barWidth, 0), "");
        ImGui::SameLine();
        ImGui::Text("%.3f", heldL);
        
        ImGui::Text("R:");
        ImGui::SameLine();
        ImGui::ProgressBar((heldR + 1.0f) / 2.0f, ImVec2(barWidth, 0), "");
        ImGui::SameLine();
        ImGui::Text("%.3f", heldR);

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Two stereo input pairs: signal (0,1) and trig (2,3)
        helpers.drawAudioInputPin("Signal In L", 0);
        helpers.drawAudioInputPin("Signal In R", 1);
        helpers.drawAudioInputPin("Trig In L", 2);
        helpers.drawAudioInputPin("Trig In R", 3);

        // CORRECTED MODULATION PINS - Use absolute channel indices
        int busIdx, chanInBus;
        if (getParamRouting("threshold_mod", busIdx, chanInBus))
            helpers.drawAudioInputPin("Threshold Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting("slewMs_mod", busIdx, chanInBus))
            helpers.drawAudioInputPin("Slew Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting("edge_mod", busIdx, chanInBus))
            helpers.drawAudioInputPin("Edge Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));

        helpers.drawAudioOutputPin("Out L", 0);
        helpers.drawAudioOutputPin("Out R", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Signal In L";
            case 1: return "Signal In R";
            case 2: return "Trig In L";
            case 3: return "Trig In R";
            case 4: return "Threshold Mod";
            case 5: return "Edge Mod";
            case 6: return "Slew Mod";
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

    // State
    float lastTrigL { 0.0f };
    float lastTrigR { 0.0f };
    float heldL { 0.0f };
    float heldR { 0.0f };
    float outL  { 0.0f };
    float outR  { 0.0f };
    double sr { 44100.0 };

    // Parameters
    std::atomic<float>* thresholdParam { nullptr }; // 0..1
    juce::AudioParameterChoice* edgeParam { nullptr }; // 0 rising, 1 falling, 2 both
    std::atomic<float>* slewMsParam { nullptr }; // 0..2000 ms
    std::atomic<float>* thresholdModParam { nullptr };
    std::atomic<float>* slewMsModParam { nullptr };
    std::atomic<float>* edgeModParam { nullptr };
};


