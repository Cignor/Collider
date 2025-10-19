#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class DeCrackleModuleProcessor : public ModuleProcessor
{
public:
    DeCrackleModuleProcessor();
    ~DeCrackleModuleProcessor() override = default;

    const juce::String getName() const override { return "de-crackle"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        
        float threshold = thresholdParam != nullptr ? thresholdParam->load() : 0.1f;
        float smoothingMs = smoothingTimeMsParam != nullptr ? smoothingTimeMsParam->load() : 5.0f;
        float amount = amountParam != nullptr ? amountParam->load() : 1.0f;
        
        ImGui::PushItemWidth(itemWidth);
        
        // Threshold slider
        if (ImGui::SliderFloat("Threshold", &threshold, 0.01f, 1.0f, "%.3f")) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("threshold"))) *p = threshold;
        }
        adjustParamOnWheel(ap.getParameter("threshold"), "threshold", threshold);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        // Smoothing time slider
        if (ImGui::SliderFloat("Smoothing (ms)", &smoothingMs, 0.1f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("smoothing_time"))) *p = smoothingMs;
        }
        adjustParamOnWheel(ap.getParameter("smoothing_time"), "smoothing_time", smoothingMs);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        // Amount (dry/wet) slider
        if (ImGui::SliderFloat("Amount", &amount, 0.0f, 1.0f, "%.2f")) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("amount"))) *p = amount;
        }
        adjustParamOnWheel(ap.getParameter("amount"), "amount", amount);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("In L", 0);
        helpers.drawAudioInputPin("In R", 1);
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

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* smoothingTimeMsParam { nullptr };
    std::atomic<float>* amountParam { nullptr };
    
    // State variables for discontinuity detection (per channel)
    float lastInputSample[2] { 0.0f, 0.0f };
    float lastOutputSample[2] { 0.0f, 0.0f };
    int smoothingSamplesRemaining[2] { 0, 0 };
    
    double currentSampleRate { 44100.0 };
};

