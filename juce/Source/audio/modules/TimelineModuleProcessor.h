#pragma once

#include "ModuleProcessor.h"

/**
 * @file TimelineModuleProcessor.h
 * @brief Automation recording and playback node synchronized with Tempo Clock
 * 
 * The Timeline Node serves as the single source of truth for temporal automation
 * in the modular synthesizer. It records precise CV, gate, trigger, and raw audio
 * data with sample-accurate timing synchronized to the global transport system.
 * 
 * Key Features:
 * - Transport-aware: Syncs with Tempo Clock start/stop/pause
 * - Dynamic I/O: Flexible input/output routing for automation
 * - Passthrough architecture: Zero-latency monitoring while recording
 * - XML persistence: Human-readable automation files
 * - Sample-accurate playback: Exact reproduction of recorded data
 */

// Signal type enumeration for automation tracking
enum class SignalType { CV, Gate, Trigger, Raw };

// Fundamental unit of recorded automation data
struct AutomationKeyframe
{
    double positionBeats; // Precise position in beats
    float value;          // The recorded value
};

// Holds all keyframes and metadata for a single automation channel
struct ChannelData
{
    std::string name;
    SignalType type = SignalType::CV;
    std::vector<AutomationKeyframe> keyframes;
};

class TimelineModuleProcessor : public ModuleProcessor
{
public:
    TimelineModuleProcessor();
    ~TimelineModuleProcessor() override = default;

    const juce::String getName() const override { return "timeline"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Rhythm introspection for BPM Monitor
    std::optional<RhythmInfo> getRhythmInfo() const override;

    // Dynamic I/O for flexible automation routing
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    bool usesCustomPinLayout() const override { return true; }
    ImVec2 getCustomNodeSize() const override { return ImVec2(320.0f, 0.0f); }

    // XML Persistence for automation data
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

private:
    void setTimingInfo(const TransportState& state) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    TransportState m_currentTransport;

    // Transport synchronization
    double m_internalPositionBeats { 0.0 };
    double m_sampleRate { 44100.0 };
    
    // Recording and playback state
    juce::AudioParameterBool* recordParam { nullptr };
    juce::AudioParameterBool* playParam { nullptr };
    
    // Playback optimization
    std::vector<int> m_lastKeyframeIndexHints;
    bool m_wasPlaying { false };
    double m_lastPositionBeats { 0.0 };
    
    // Automation data storage
    juce::CriticalSection automationLock;
    std::vector<ChannelData> m_automationChannels;
    int m_selectedChannelIndex { 0 };
};

