#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class DelayModuleProcessor : public ModuleProcessor
{
public:
    DelayModuleProcessor();
    ~DelayModuleProcessor() override = default;

    const juce::String getName() const override { return "delay"; }

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
        
        // Helper for tooltips
        auto HelpMarkerDelay = [](const char* desc) {
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };
        
        // Get live modulated values for display
        bool isTimeModulated = isParamModulated("timeMs");
        bool isFbModulated = isParamModulated("feedback");
        bool isMixModulated = isParamModulated("mix");
        
        float timeMs = isTimeModulated ? getLiveParamValueFor("timeMs", "timeMs_live", timeMsParam->load()) : (timeMsParam != nullptr ? timeMsParam->load() : 400.0f);
        float fb = isFbModulated ? getLiveParamValueFor("feedback", "feedback_live", feedbackParam->load()) : (feedbackParam != nullptr ? feedbackParam->load() : 0.4f);
        float mix = isMixModulated ? getLiveParamValueFor("mix", "mix_live", mixParam->load()) : (mixParam != nullptr ? mixParam->load() : 0.3f);
        
        ImGui::PushItemWidth(itemWidth);

        // === DELAY PARAMETERS SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Delay Parameters");
        ImGui::Spacing();

        // Time
        if (isTimeModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Time (ms)", &timeMs, 1.0f, 2000.0f, "%.1f")) if (!isTimeModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("timeMs"))) *p = timeMs;
        if (!isTimeModulated) adjustParamOnWheel(ap.getParameter("timeMs"), "timeMs", timeMs);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isTimeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerDelay("Delay time in milliseconds (1-2000 ms)");

        // Feedback
        if (isFbModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Feedback", &fb, 0.0f, 0.95f)) if (!isFbModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("feedback"))) *p = fb;
        if (!isFbModulated) adjustParamOnWheel(ap.getParameter("feedback"), "feedback", fb);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isFbModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerDelay("Feedback amount (0-95%)\nCreates repeating echoes");

        // Mix
        if (isMixModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f)) if (!isMixModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("mix"))) *p = mix;
        if (!isMixModulated) adjustParamOnWheel(ap.getParameter("mix"), "mix", mix);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMixModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        ImGui::SameLine();
        HelpMarkerDelay("Dry/wet mix (0-100%)\n0% = dry signal only, 100% = delayed signal only");

        ImGui::Spacing();
        ImGui::Spacing();

        // === VISUAL DELAY TAPS SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Delay Taps");
        ImGui::Spacing();

        // Show first 5 delay taps as dots
        for (int i = 0; i < 5; ++i)
        {
            if (i > 0) ImGui::SameLine();
            
            float tapLevel = fb * std::pow(fb, (float)i);  // Exponential decay
            ImVec4 color = ImColor::HSV(0.15f, 0.7f, tapLevel).Value;
            
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::Button("•", ImVec2(itemWidth * 0.18f, 20));
            ImGui::PopStyleColor();
        }
        
        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);

        // CORRECTED MODULATION PINS - Use absolute channel indices
        int busIdx, chanInBus;
        if (getParamRouting("timeMs", busIdx, chanInBus))
            helpers.drawAudioInputPin("Time Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting("feedback", busIdx, chanInBus))
            helpers.drawAudioInputPin("Feedback Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
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
            case 2: return "Time Mod";
            case 3: return "Feedback Mod";
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
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> dlL { 48000 }, dlR { 48000 };
    std::atomic<float>* timeMsParam { nullptr };
    std::atomic<float>* feedbackParam { nullptr };
    std::atomic<float>* mixParam { nullptr };
    double sr { 48000.0 };
    int maxDelaySamples { 48000 };
    
    // Smoothed values to prevent clicks and zipper noise
    juce::SmoothedValue<float> timeSm;
    juce::SmoothedValue<float> feedbackSm;
    juce::SmoothedValue<float> mixSm;
};


