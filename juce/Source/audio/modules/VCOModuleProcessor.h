#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

class VCOModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdFrequency   = "frequency";
    static constexpr auto paramIdWaveform    = "waveform";
    // Virtual target only (no APVTS param needed) – used for routing to select waveform
    static constexpr auto paramIdWaveformMod = "waveform_mod";

    VCOModuleProcessor();
    ~VCOModuleProcessor() override = default;

    const juce::String getName() const override { return "vco"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override
    {
        auto& ap = getAPVTS();
        float freq = frequencyParam != nullptr ? getLiveParamValueFor(paramIdFrequency, paramIdFrequency, frequencyParam->load()) : 440.0f;
        int wave = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) wave = (int) getLiveParamValueFor(paramIdWaveformMod, paramIdWaveform, (float) p->getIndex());

        // Helper for tooltips (imgui_demo.cpp pattern)
        auto HelpMarker = [](const char* desc)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip())
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };

        ImGui::PushItemWidth (itemWidth);

        // === FREQUENCY SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Oscillator Control");
        ImGui::Spacing();
        
        const bool freqMod = isParamModulated(paramIdFrequency);
        
        // Color-coded modulation indicator
        if (freqMod)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)); // Cyan
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
        }
        
        if (freqMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat ("##freq", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
        {
            if (!freqMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = freq;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (!freqMod) adjustParamOnWheel (ap.getParameter(paramIdFrequency), "frequencyHz", freq);
        if (freqMod) ImGui::EndDisabled();
        
        ImGui::SameLine();
        if (freqMod)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Frequency (CV)");
            ImGui::PopStyleColor(3);
        }
        else
        {
            ImGui::Text("Frequency");
        }
        HelpMarker("Control voltage range: 0-1V = 20Hz to 20kHz (exponential)\nConnect LFO, Envelope, or Sequencer for modulation");

        // Note name display
        if (!freqMod)
        {
            auto getNoteFromFreq = [](float f) -> juce::String
            {
                if (f < 20.0f || f > 20000.0f) return "";
                float midiNote = 12.0f * std::log2(f / 440.0f) + 69.0f;
                int noteNum = (int)std::round(midiNote);
                const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                int octave = (noteNum / 12) - 1;
                return juce::String(notes[noteNum % 12]) + juce::String(octave);
            };
            ImGui::TextDisabled("Note: %s", getNoteFromFreq(freq).toRawUTF8());
        }

        // Quick preset buttons
        if (!freqMod)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
            float btnWidth = (itemWidth - 12) / 4.0f;
            
            if (ImGui::Button("A4", ImVec2(btnWidth, 0)))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = 440.0f;
                onModificationEnded();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("440 Hz (Concert A)");
            
            ImGui::SameLine();
            if (ImGui::Button("C4", ImVec2(btnWidth, 0)))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = 261.63f;
                onModificationEnded();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("261.63 Hz (Middle C)");
            
            ImGui::SameLine();
            if (ImGui::Button("A3", ImVec2(btnWidth, 0)))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = 220.0f;
                onModificationEnded();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("220 Hz");
            
            ImGui::SameLine();
            if (ImGui::Button("C3", ImVec2(btnWidth, 0)))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = 130.81f;
                onModificationEnded();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("130.81 Hz");
            
            ImGui::PopStyleVar();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // === WAVEFORM SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Waveform");
        ImGui::Spacing();
        
        const bool waveMod = isParamModulated(paramIdWaveformMod);
        
        // Color-coded modulation indicator
        if (waveMod)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f)); // Orange
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.4f, 0.2f, 0.5f));
        }
        
        if (waveMod) ImGui::BeginDisabled();
        if (ImGui::Combo ("##wave", &wave, "Sine\0Sawtooth\0Square\0\0"))
        {
            if (!waveMod) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) *p = wave;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (waveMod) ImGui::EndDisabled();
        
        ImGui::SameLine();
        if (waveMod)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Shape (CV)");
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::Text("Shape");
        }
        HelpMarker("Sine: Pure tone, no harmonics\nSawtooth: Bright, rich in harmonics\nSquare: Hollow, odd harmonics only\nCV modulation: 0V=Sine, 0.5V=Saw, 1V=Square");

        // Visual waveform preview (imgui_demo.cpp PlotLines pattern)
        float waveformPreview[128];
        for (int i = 0; i < 128; ++i)
        {
            float x = (float)i / 128.0f * 2.0f * juce::MathConstants<float>::pi;
            switch (wave)
            {
                case 0: waveformPreview[i] = std::sin(x); break; // Sine
                case 1: waveformPreview[i] = (x / juce::MathConstants<float>::pi) - 1.0f; break; // Sawtooth
                case 2: waveformPreview[i] = (x < juce::MathConstants<float>::pi) ? 1.0f : -1.0f; break; // Square
                default: waveformPreview[i] = 0.0f;
            }
        }
        ImGui::PlotLines("##wavepreview", waveformPreview, 128, 0, nullptr, -1.2f, 1.2f, ImVec2(itemWidth, 80));

        ImGui::Spacing();
        ImGui::Spacing();

        // === OUTPUT SECTION ===
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output");
        ImGui::Spacing();
        
        // Real-time output level meter
        float outputLevel = lastOutputValues[0]->load();
        float absLevel = std::abs(outputLevel);
        
        // Color-coded progress bar
        ImVec4 meterColor;
        if (absLevel < 0.7f)
            meterColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green
        else if (absLevel < 0.9f)
            meterColor = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Yellow
        else
            meterColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // Red
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, meterColor);
        ImGui::ProgressBar(absLevel, ImVec2(itemWidth, 0), "");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(0, 5);
        ImGui::Text("%.3f", outputLevel);
        HelpMarker("Live output signal level\nConnect to VCA, Filter, or Audio Out\nUse Gate input to control amplitude");

        ImGui::PopItemWidth();
    }

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Single input bus (0): ch0 Frequency Mod, ch1 Waveform Mod, ch2 Gate
        helpers.drawAudioInputPin("Frequency", 0);
        helpers.drawAudioInputPin("Waveform", 1);
        helpers.drawAudioInputPin("Gate", 2);
        helpers.drawAudioOutputPin("Out", 0);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Frequency Mod";
            case 1: return "Waveform Mod";
            case 2: return "Gate";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oscillator<float> oscillator;
    int currentWaveform = -1;

    // Cached parameter pointers
    std::atomic<float>* frequencyParam { nullptr };
    std::atomic<float>* waveformParam  { nullptr };

    // Click-free gating
    float smoothedGate { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCOModuleProcessor)
};


