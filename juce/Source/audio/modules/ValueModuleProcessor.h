#pragma once

#include "ModuleProcessor.h"

class ValueModuleProcessor : public ModuleProcessor
{
public:
    ValueModuleProcessor();
    ~ValueModuleProcessor() override = default;

    const juce::String getName() const override { return "value"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Override to explicitly state this module has no modulatable inputs
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override
    {
        // This module has no modulatable inputs - it's a source-only module.
        // Always return false to prevent undefined behavior in the modulation system.
        juce::ignoreUnused(paramId, outBusIndex, outChannelIndexInBus);
        return false;
    }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("value"));
        if (!p) return;

        float currentValue = *p;

        ImGui::PushItemWidth(itemWidth);
        // Compact draggable number field without visible label
        if (ImGui::DragFloat("##value_drag", &currentValue, 0.01f, p->range.start, p->range.end, "%.4f"))
        {
            *p = currentValue;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            onModificationEnded();
        }
        ImGui::PopItemWidth();

        // New Time-Based, Exponential Mouse Wheel Logic
        if (ImGui::IsItemHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const double currentTime = ImGui::GetTime();
                const double timeDelta = currentTime - lastScrollTime;

                // 1. If user paused for > 0.2s, reset momentum
                if (timeDelta > 0.2)
                {
                    scrollMomentum = 1.0f;
                }

                // 2. Define the smallest step for precision
                const float baseStep = 0.01f;
                
                // 3. Calculate the final step using the momentum
                float finalStep = baseStep * scrollMomentum;

                // 4. Update the value
                float newValue = currentValue + (wheel > 0.0f ? finalStep : -finalStep);
                
                // Snap to the baseStep to keep numbers clean
                newValue = std::round(newValue / baseStep) * baseStep;

                // 5. Increase momentum for the *next* scroll event (exponential)
                // This makes continuous scrolling accelerate.
                scrollMomentum *= 1.08f;
                scrollMomentum = std::min(scrollMomentum, 2000.0f); // Cap momentum to prevent runaway

                // 6. Update the parameter and timestamp
                *p = juce::jlimit(p->range.start, p->range.end, newValue);
                lastScrollTime = currentTime;
            }
        }
        
        // CV Output Range Controls (compact layout)
        ImGui::Text("CV Out Range (0-1)");
        
        float cvMin = cvMinParam->load();
        float cvMax = cvMaxParam->load();
        
        ImGui::PushItemWidth(itemWidth * 0.45f); // Make sliders take up half the width each
        if (ImGui::SliderFloat("##cv_min", &cvMin, 0.0f, 1.0f, "Min: %.2f"))
        {
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("cvMin")) = cvMin;
            onModificationEnded();
        }
        ImGui::SameLine();
        if (ImGui::SliderFloat("##cv_max", &cvMax, 0.0f, 1.0f, "Max: %.2f"))
        {
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("cvMax")) = cvMax;
            onModificationEnded();
        }
        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Draw the five output pins for the Value module
        helpers.drawAudioOutputPin("Raw", 0);
        helpers.drawAudioOutputPin("Normalized", 1);
        helpers.drawAudioOutputPin("Inverted", 2);
        helpers.drawAudioOutputPin("Integer", 3);
        helpers.drawAudioOutputPin("CV Out", 4);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        // Value has no audio inputs
        return juce::String("In ") + juce::String(channel + 1);
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Raw";
            case 1: return "Normalized";
            case 2: return "Inverted";
            case 3: return "Integer";
            case 4: return "CV Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* valueParam { nullptr };
    std::atomic<float>* cvMinParam { nullptr };
    std::atomic<float>* cvMaxParam { nullptr };

    // Add these two state variables for the new scroll logic
    double lastScrollTime { 0.0 };
    float scrollMomentum { 1.0f };
};
