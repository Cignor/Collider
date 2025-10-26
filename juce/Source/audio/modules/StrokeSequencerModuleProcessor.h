#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

// ==============================================================================
class StrokeSequencerModuleProcessor : public ModuleProcessor, private juce::Timer
{
public:
    StrokeSequencerModuleProcessor();
    ~StrokeSequencerModuleProcessor() override;

    // --- JUCE / Module Overrides ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    const juce::String getName() const override { return "Stroke Sequencer"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    // --- State Management ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

private:
    void timerCallback() override; // For UI updates
    void setTimingInfo(const TransportState& state) override;

    // --- Data Structures ---
    struct StrokePoint { float x, y; };
    std::vector<std::vector<StrokePoint>> userStrokes;      // For the UI thread to draw into
    std::vector<StrokePoint> audioStrokePoints;             // A flattened, sorted copy for the audio thread
    std::atomic<bool> strokeDataDirty { true };             // Flag to signal when the audio copy needs updating

    // --- Real-time State ---
    double playheadPosition = 0.0; // Range 0.0 to 1.0
    double phase = 0.0; // For free-running mode
    double sampleRate = 44100.0;
    std::array<bool, 3> wasAboveThreshold { false, false, false }; // State for edge detection
    std::array<std::atomic<float>, 3> triggerOutputValues; // Thread-safe triggers
    std::atomic<float> currentStrokeYValue { 0.0f }; // For UI telemetry
    
    // --- Transport State ---
    TransportState m_currentTransport;
    bool wasPlaying = false;

    // --- UI State ---
    bool isDrawing = false;
    bool isErasing = false;

    // --- Parameter Management ---
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Cached parameter pointers for efficient real-time access
    std::atomic<float>* rateParam { nullptr };
    std::atomic<float>* floorYParam { nullptr };
    std::atomic<float>* midYParam { nullptr };
    std::atomic<float>* ceilingYParam { nullptr };

    // Parameter IDs for APVTS
    static constexpr auto paramIdRate         = "rate";
    static constexpr auto paramIdSync         = "sync";
    static constexpr auto paramIdRateDivision = "rate_division";
    static constexpr auto paramIdFloorY       = "floorY";
    static constexpr auto paramIdMidY         = "midY";
    static constexpr auto paramIdCeilingY     = "ceilingY";

    // Virtual modulation target IDs
    static constexpr auto paramIdRateMod     = "rate_mod";
    static constexpr auto paramIdFloorYMod   = "floorY_mod";
    static constexpr auto paramIdMidYMod     = "midY_mod";
    static constexpr auto paramIdCeilingYMod = "ceilingY_mod";
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrokeSequencerModuleProcessor)
};

