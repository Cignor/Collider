#pragma once

#include "ModuleProcessor.h"
#include "TapTempo.h"
#include <vector>
#include <array>
#include <atomic>

/**
 * BPM Monitor Node - Hybrid Smart System
 * 
 * This node automatically detects and reports BPM from rhythm-producing modules
 * using two complementary approaches:
 * 
 * 1. INTROSPECTION (Fast Path): Directly queries modules that implement getRhythmInfo()
 *    - Instant, accurate BPM reporting
 *    - Works with sequencers, animations, etc.
 * 
 * 2. BEAT DETECTION (Universal Fallback): Analyzes audio inputs for beat patterns
 *    - Tap tempo algorithm with rolling average
 *    - Works with any rhythmic signal (including external audio/VSTs)
 * 
 * The node dynamically generates output pins for each detected rhythm source:
 * - [Name] BPM (Raw) - Absolute BPM value
 * - [Name] CV - Normalized 0-1 for modulation
 * - [Name] Active/Confidence - Gate or confidence level
 * 
 * This node can be added via the Analysis menu and behaves like a normal module.
 */
class BPMMonitorModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_DETECTION_INPUTS = 16;  // Max beat detection inputs
    
    /**
     * Operation modes for the BPM Monitor
     */
    enum class OperationMode
    {
        Auto = 0,           // Use both introspection + beat detection
        IntrospectionOnly,  // Only scan modules with getRhythmInfo()
        DetectionOnly       // Only analyze audio inputs
    };
    
    BPMMonitorModuleProcessor();
    ~BPMMonitorModuleProcessor() override = default;

    // === JUCE AudioProcessor Interface ===
    const juce::String getName() const override { return "bpm_monitor"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // === Dynamic Pin Interface ===
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    
    // === Pin Labels ===
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    
#if defined(PRESET_CREATOR_UI)
    // === UI Drawing ===
    void drawParametersInNode(float itemWidth, 
                             const std::function<bool(const juce::String&)>& isParamModulated,
                             const std::function<void()>& onModificationEnded) override;
#endif

private:
    // === Parameter Layout ===
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    double m_sampleRate = 44100.0;
    
    // === INTROSPECTION ENGINE ===
    
    /**
     * Cached information about a rhythm source discovered via introspection
     */
    struct IntrospectedSource {
        juce::String name;      // Display name (e.g., "Sequencer #3")
        juce::String type;      // Source type (e.g., "sequencer", "animation")
        float bpm;              // Current BPM
        bool isActive;          // Is currently running?
        bool isSynced;          // Synced to global transport?
    };
    
    std::vector<IntrospectedSource> m_introspectedSources;
    mutable juce::CriticalSection m_sourcesLock;  // Thread safety for dynamic pin queries
    
    /**
     * Scan the parent graph for modules with getRhythmInfo()
     * Updates m_introspectedSources
     */
    void scanGraphForRhythmSources();
    
    // === BEAT DETECTION ENGINE ===
    
    std::array<TapTempo, MAX_DETECTION_INPUTS> m_tapAnalyzers;
    std::array<double, MAX_DETECTION_INPUTS> m_channelTime {};  // Current time per channel
    std::vector<DetectedRhythmSource> m_detectedSources;
    
    /**
     * Process beat detection on all active input channels
     * Simple: detect edges, measure intervals, calculate median BPM
     */
    void processDetection(const juce::AudioBuffer<float>& buffer);
    
    // === OUTPUT MANAGEMENT ===
    
    /**
     * Normalize BPM to 0-1 range for CV output
     */
    float normalizeBPM(float bpm, float minBPM, float maxBPM) const;
    
    // === PERFORMANCE OPTIMIZATION ===
    
    int m_scanCounter { 0 };  // Counter to reduce graph scan frequency

#if defined(PRESET_CREATOR_UI)
    // Simple visualization data - no atomic overhead needed
    struct VizSource
    {
        juce::String name;
        float bpm { 0.0f };
        float confidence { 0.0f };
        bool isActive { false };
    };
    
    mutable juce::CriticalSection m_vizLock;
    std::vector<VizSource> m_vizIntrospected;
    std::vector<VizSource> m_vizDetected;
#endif
};

