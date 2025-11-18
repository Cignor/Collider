#pragma once

#include "ModuleProcessor.h"
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

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
        const auto& theme = ThemeManager::getInstance().getCurrentTheme();
        auto& ap = getAPVTS();
        ImGui::PushID(this);
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("value"));
        if (!p) { ImGui::PopID(); return; }

        float currentValue = *p;

        // Visualization section
        ImGui::Spacing();
        ImGui::Text("Output Values");
        ImGui::Spacing();

        auto* drawList = ImGui::GetWindowDrawList();
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        const ImU32 rawColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
        const ImU32 normColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
        const ImU32 invColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.amplitude);
        const ImU32 intColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.filter);
        const ImU32 cvColor = ImGui::ColorConvertFloat4ToU32(theme.accent);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float vizHeight = 120.0f;
        const float barWidth = (itemWidth - 20.0f) / 5.0f; // 5 bars with spacing
        const float barSpacing = 4.0f;
        const ImVec2 rectMax = ImVec2(origin.x + itemWidth, origin.y + vizHeight);
        
        drawList->AddRectFilled(origin, rectMax, bgColor, 4.0f);
        ImGui::PushClipRect(origin, rectMax, true);

        // Load values from atomics
        const float rawVal = vizData.rawValue.load();
        const float normVal = vizData.normalizedValue.load();
        const float invVal = vizData.invertedValue.load();
        const float intVal = vizData.integerValue.load();
        const float cvVal = vizData.cvValue.load();

        // Normalize values for display (0-1 range for bars)
        const float paramMin = p->range.start;
        const float paramMax = p->range.end;
        const float rawNorm = juce::jlimit(0.0f, 1.0f, (rawVal - paramMin) / (paramMax - paramMin));
        const float invNorm = juce::jlimit(0.0f, 1.0f, (-invVal - paramMin) / (paramMax - paramMin));

        // Draw bars for each output
        auto drawBar = [&](float normalizedValue, ImU32 color, float xOffset, const char* label)
        {
            const float barX = origin.x + xOffset;
            const float barHeight = normalizedValue * (vizHeight - 30.0f); // Leave space for labels
            const float barY = origin.y + (vizHeight - 30.0f) - barHeight;
            
            // Draw bar
            drawList->AddRectFilled(
                ImVec2(barX, barY),
                ImVec2(barX + barWidth - barSpacing, origin.y + vizHeight - 30.0f),
                color, 2.0f);
            
            // Draw label
            const ImVec2 textPos(barX + (barWidth - barSpacing) * 0.5f, origin.y + vizHeight - 25.0f);
            drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f)), label);
        };

        drawBar(rawNorm, rawColor, 0.0f, "Raw");
        drawBar(normVal, normColor, barWidth, "Norm");
        drawBar(invNorm, invColor, barWidth * 2.0f, "Inv");
        drawBar(juce::jlimit(0.0f, 1.0f, (intVal - paramMin) / (paramMax - paramMin)), intColor, barWidth * 3.0f, "Int");
        drawBar(cvVal, cvColor, barWidth * 4.0f, "CV");

        // Draw center line
        drawList->AddLine(
            ImVec2(origin.x, origin.y + (vizHeight - 30.0f) * 0.5f),
            ImVec2(rectMax.x, origin.y + (vizHeight - 30.0f) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 0.3f)), 1.0f);

        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, rectMax.y));
        ImGui::Dummy(ImVec2(itemWidth, 0));

        // Display current values
        ImGui::Spacing();
        ImGui::Text("Raw: %.2f  |  Norm: %.3f  |  Inv: %.2f  |  Int: %.0f  |  CV: %.3f",
            rawVal, normVal, invVal, intVal, cvVal);

        ImGui::Spacing();
        ThemeText("Value Parameters", theme.text.section_header);
        ImGui::Spacing();

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
        ImGui::PopID();
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

#if defined(PRESET_CREATOR_UI)
    struct VizData
    {
        std::atomic<float> rawValue { 0.0f };
        std::atomic<float> normalizedValue { 0.0f };
        std::atomic<float> invertedValue { 0.0f };
        std::atomic<float> integerValue { 0.0f };
        std::atomic<float> cvValue { 0.0f };
        std::atomic<float> currentValue { 0.0f };
        std::atomic<float> currentCvMin { 0.0f };
        std::atomic<float> currentCvMax { 1.0f };
    };

    VizData vizData;
#endif
};
