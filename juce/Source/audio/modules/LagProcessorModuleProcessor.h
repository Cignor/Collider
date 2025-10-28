#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class LagProcessorModuleProcessor : public ModuleProcessor
{
public:
    LagProcessorModuleProcessor();
    ~LagProcessorModuleProcessor() override = default;

    const juce::String getName() const override { return "lag_processor"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        
        // Get current parameter values
        float riseMs = riseTimeParam != nullptr ? riseTimeParam->load() : 10.0f;
        float fallMs = fallTimeParam != nullptr ? fallTimeParam->load() : 10.0f;
        int modeIdx = modeParam != nullptr ? modeParam->getIndex() : 0;
        
        // Check for modulation
        bool isRiseModulated = isParamModulated("rise_time_mod");
        bool isFallModulated = isParamModulated("fall_time_mod");
        
        if (isRiseModulated) {
            riseMs = getLiveParamValueFor("rise_time_mod", "rise_time_live", riseMs);
        }
        if (isFallModulated) {
            fallMs = getLiveParamValueFor("fall_time_mod", "fall_time_live", fallMs);
        }
        
        ImGui::PushItemWidth(itemWidth);
        
        // Mode selector
        const char* modeNames[] = { "Slew Limiter", "Envelope Follower" };
        if (ImGui::Combo("Mode", &modeIdx, modeNames, 2)) {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("mode"))) {
                *p = modeIdx;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        // Rise Time (or Attack in Envelope Follower mode)
        const char* riseLabel = (modeIdx == 0) ? "Rise Time (ms)" : "Attack (ms)";
        if (isRiseModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(riseLabel, &riseMs, 0.1f, 4000.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            if (!isRiseModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("rise_time"))) *p = riseMs;
            }
        }
        if (!isRiseModulated) adjustParamOnWheel(ap.getParameter("rise_time"), "rise_time", riseMs);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isRiseModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        
        // Fall Time (or Release in Envelope Follower mode)
        const char* fallLabel = (modeIdx == 0) ? "Fall Time (ms)" : "Release (ms)";
        if (isFallModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(fallLabel, &fallMs, 0.1f, 4000.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            if (!isFallModulated) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("fall_time"))) *p = fallMs;
            }
        }
        if (!isFallModulated) adjustParamOnWheel(ap.getParameter("fall_time"), "fall_time", fallMs);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isFallModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        
        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("Signal In", 0);
        helpers.drawAudioInputPin("Rise Mod", 1);
        helpers.drawAudioInputPin("Fall Mod", 2);
        helpers.drawAudioOutputPin("Smoothed Out", 0);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Signal In";
            case 1: return "Rise Mod";
            case 2: return "Fall Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        if (channel == 0) return "Smoothed Out";
        return juce::String("Out ") + juce::String(channel + 1);
    }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* riseTimeParam { nullptr };  // milliseconds
    std::atomic<float>* fallTimeParam { nullptr };  // milliseconds
    juce::AudioParameterChoice* modeParam { nullptr };
    
    // State variables for smoothing algorithm
    float currentOutput { 0.0f };
    double currentSampleRate { 44100.0 };
};

