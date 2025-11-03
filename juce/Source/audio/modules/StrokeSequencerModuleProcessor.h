#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <box2d/box2d.h>

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
    std::atomic<bool> autoBuildDrumKitTriggered { false };
    
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    // --- State Management ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

private:
    void timerCallback() override; // For UI updates
    void setTimingInfo(const TransportState& state) override;
    void clearStrokes(); // Helper to reset all stroke data
    
    // --- Line Segment Intersection Detection ---
    bool lineSegmentCrossesHorizontalLine(float x1, float y1, float x2, float y2, float lineY) const;

    // --- Data Structures ---
    struct StrokePoint {
        float x, y;
    };
    std::vector<std::vector<StrokePoint>> userStrokes;      // For the UI thread to draw into
    std::vector<StrokePoint> audioStrokePoints;             // A flattened, sorted copy for the audio thread
    std::atomic<bool> strokeDataDirty { true };             // Flag to signal when the audio copy needs updating

    // --- Real-time State ---
    double playheadPosition = 0.0; // Range 0.0 to 1.0
    double phase = 0.0; // For free-running mode
    double sampleRate = 44100.0;
    std::atomic<float> currentStrokeYValue { 0.0f }; // For UI telemetry
    float previousStrokeY = 0.5f; // Track previous Y for intersection detection
    double previousPlayheadPos = 0.0; // Track previous X for intersection detection
    std::atomic<bool> isUnderManualControl { false }; // True when user is actively dragging the playhead slider
    std::atomic<double> livePlayheadPosition { 0.0 }; // Live playhead position for UI tracking
    
    // --- Continuous Pitch CV (live, interpolated under playhead) ---
    std::atomic<float> m_continuousPitchCV { 0.0f };

    // --- Captured Pitch CV per trigger line (Floor, Mid, Ceiling) ---
    std::array<std::atomic<float>, 3> m_currentPitchCV { 0.0f, 0.0f, 0.0f };

    // --- Strict trigger gating: one trigger per line per stroke per loop ---
    int m_activeStrokeIndex = -1; // Index into userStrokes for the currently active stroke
    std::array<bool, 3> m_hasTriggeredThisSegment { false, false, false };
    bool m_isPrimed { false }; // Require one stable sample on-stroke before allowing triggers

    // Mapping from flattened audio points to their parent stroke index (kept in lockstep with audioStrokePoints)
    std::vector<int> audioPointToStrokeIndex;
    
    // --- Transport State ---
    TransportState m_currentTransport;
    bool wasPlaying = false;

    // --- UI State ---
    bool isDrawing = false;
    juce::String activeStrokePresetName; // Name of the currently loaded preset
    int selectedStrokePresetIndex = -1; // Index in the preset dropdown
    char strokePresetNameBuffer[128] = ""; // Buffer for new preset name input

    // --- Parameter Management ---
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Cached parameter pointers for efficient real-time access
    std::atomic<float>* rateParam { nullptr };
    std::atomic<float>* floorYParam { nullptr };
    std::atomic<float>* midYParam { nullptr };
    std::atomic<float>* ceilingYParam { nullptr };
    std::atomic<float>* playheadParam { nullptr };

    // Parameter IDs for APVTS
    static constexpr auto paramIdRate         = "rate";
    static constexpr auto paramIdSync         = "sync";
    static constexpr auto paramIdRateDivision = "rate_division";
    static constexpr auto paramIdFloorY       = "floorY";
    static constexpr auto paramIdMidY         = "midY";
    static constexpr auto paramIdCeilingY     = "ceilingY";
    static constexpr auto paramIdPlayhead     = "playhead";

    // Virtual modulation target IDs
    static constexpr auto paramIdRateMod     = "rate_mod";
    static constexpr auto paramIdFloorYMod   = "floorY_mod";
    static constexpr auto paramIdMidYMod     = "midY_mod";
    static constexpr auto paramIdCeilingYMod = "ceilingY_mod";
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrokeSequencerModuleProcessor)
};

