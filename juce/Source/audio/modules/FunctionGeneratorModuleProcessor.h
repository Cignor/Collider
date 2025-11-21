#pragma once
#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class FunctionGeneratorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdRate = "rate";
    static constexpr auto paramIdMode = "mode";
    static constexpr auto paramIdLoop = "loop";
    static constexpr auto paramIdSlew = "slew";
    static constexpr auto paramIdGateThresh = "gateThresh";
    static constexpr auto paramIdTrigThresh = "trigThresh";
    static constexpr auto paramIdPitchBase = "pitchBase";
    static constexpr auto paramIdValueMult = "valueMult";
    static constexpr auto paramIdCurveSelect = "curveSelect";

    // Virtual ID for direct input connection checking
    static constexpr auto paramIdGateIn = "gate_in";

    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdRateMod = "rate_mod";
    static constexpr auto paramIdSlewMod = "slew_mod";
    static constexpr auto paramIdGateThreshMod = "gateThresh_mod";
    static constexpr auto paramIdTrigThreshMod = "trigThresh_mod";
    static constexpr auto paramIdPitchBaseMod = "pitchBase_mod";
    static constexpr auto paramIdValueMultMod = "valueMult_mod";
    static constexpr auto paramIdCurveSelectMod = "curveSelect_mod";

    FunctionGeneratorModuleProcessor();
    ~FunctionGeneratorModuleProcessor() override = default;

    const juce::String getName() const override { return "function_generator"; }

    // --- Core functions ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    
    void setTimingInfo(const TransportState& state) override;
    
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection for BPM Monitor
    std::optional<RhythmInfo> getRhythmInfo() const override;

    // --- State management for saving/loading the drawn curves ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

    // --- UI drawing functions ---
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    
    // --- REFACTORED drawIoPins ---
    void drawIoPins(const NodePinHelpers& helpers) override
    {
        // Primary signal inputs/outputs
        helpers.drawParallelPins("Gate In", 0, "Value", 0);
        helpers.drawParallelPins("Trigger In", 1, "Inverted", 1);
        helpers.drawParallelPins("Sync In", 2, "Bipolar", 2);
        
        ImGui::Spacing(); // Small gap between signal and modulation groups
        
        // Modulation inputs with corresponding outputs
        helpers.drawParallelPins("Rate Mod", 3, "Pitch", 3);
        helpers.drawParallelPins("Slew Mod", 4, "Gate", 4);
        helpers.drawParallelPins("Gate Thresh Mod", 5, "Trigger", 5);
        helpers.drawParallelPins("Trig Thresh Mod", 6, "End of Cycle", 6);
        
        ImGui::Spacing(); // Small gap before remaining modulation inputs
        
        // Remaining modulation inputs
        helpers.drawAudioInputPin("Pitch Base Mod", 7);
        helpers.drawAudioInputPin("Value Mult Mod", 8);
        helpers.drawAudioInputPin("Curve Select Mod", 9);

        ImGui::Spacing(); // Visual separator before curve outputs
        ImGui::Spacing();

        // Dedicated curve outputs (grouped by color for clarity)
        helpers.drawAudioOutputPin("Blue Value", 7);
        helpers.drawAudioOutputPin("Blue Pitch", 8);
        helpers.drawAudioOutputPin("Red Value", 9);
        helpers.drawAudioOutputPin("Red Pitch", 10);
        helpers.drawAudioOutputPin("Green Value", 11);
        helpers.drawAudioOutputPin("Green Pitch", 12);
    }

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- Core state for the curves ---
    std::array<std::vector<float>, 3> curves;
    static constexpr int CURVE_RESOLUTION = 256;

    // --- DSP State ---
    double phase{ 0.0 };
    double lastPhase{ 0.0 };
    double sampleRate{ 44100.0 };
    bool lastTriggerState{ false };
    bool lastGateState{ false };
    bool lastSyncState{ false };
    float currentValue{ 0.0f };
    float targetValue{ 0.0f };
    bool isRunning{ false };
    bool lastGateOut{ false };
    int eocPulseRemaining{ 0 };

    TransportState m_currentTransport;
    
    // --- Smoothed values ---
    juce::SmoothedValue<float> smoothedSlew;
    juce::SmoothedValue<float> smoothedRate;
    juce::SmoothedValue<float> smoothedGateThresh;
    juce::SmoothedValue<float> smoothedTrigThresh;
    juce::SmoothedValue<float> smoothedPitchBase;
    juce::SmoothedValue<float> smoothedValueMult;

    // --- Parameter Pointers ---
    std::atomic<float>* rateParam{ nullptr };
    std::atomic<float>* modeParam{ nullptr };
    std::atomic<float>* loopParam{ nullptr };
    std::atomic<float>* slewParam{ nullptr };
    std::atomic<float>* gateThreshParam{ nullptr };
    std::atomic<float>* trigThreshParam{ nullptr };
    std::atomic<float>* pitchBaseParam{ nullptr };
    std::atomic<float>* valueMultParam{ nullptr };
    std::atomic<float>* curveSelectParam{ nullptr };

    // --- Helper functions ---
    float interpolateCurve(int curveIndex, float phase);
    void generateOutputs(float selectedValue, float blueValue, float redValue, float greenValue, bool endOfCycle, float* outputs, float gateThresh, float pitchBase, float valueMult);

#if defined(PRESET_CREATOR_UI)
    // State for the interactive drawing canvas
    bool isDragging{ false };
    ImVec2 lastMousePosInCanvas{ -1, -1 };
#endif
};