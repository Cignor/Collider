#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/**
 * @brief MIDI Pad Controller Module
 * 
 * Specialized MIDI-to-CV converter optimized for pad controllers (Akai MPD, Novation Launchpad, etc.).
 * Provides 16 independent trigger/gate outputs with velocity capture for drum programming,
 * sample triggering, and rhythmic modulation.
 * 
 * Outputs (33 channels):
 *   - Channels 0-15:  Pad 1-16 Gate outputs
 *   - Channels 16-31: Pad 1-16 Velocity outputs (0-1)
 *   - Channel 32:     Global velocity (last hit pad)
 * 
 * Features:
 *   - 4x4 visual pad grid with real-time animation
 *   - Multiple trigger modes (trigger, gate, toggle, latch)
 *   - Velocity curves (linear, exponential, logarithmic, fixed)
 *   - Device/channel filtering for multi-MIDI setups
 *   - Polyphonic operation (multiple simultaneous pad hits)
 */
class MIDIPadModuleProcessor : public ModuleProcessor
{
public:
    static constexpr int MAX_PADS = 16;
    
    MIDIPadModuleProcessor();
    ~MIDIPadModuleProcessor() override = default;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    // Multi-MIDI device support
    void handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages) override;

    const juce::String getName() const override { return "midi_pads"; }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Dynamic pins
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
    // State management
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, 
                              const std::function<bool(const juce::String&)>&, 
                              const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

private:
    // Pad mapping and state tracking
    struct PadMapping
    {
        int midiNote = -1;  // Learned MIDI note (-1 = unassigned)
        
        // Runtime state (thread-safe with atomics)
        std::atomic<bool> gateHigh{false};
        std::atomic<float> velocity{0.0f};
        std::atomic<double> triggerStartTime{0.0};
        std::atomic<bool> toggleState{false};  // For toggle mode
        
        // Check if pad is visually active (for UI animation)
        bool isActive() const {
            double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            return gateHigh.load() || (now - triggerStartTime.load() < 0.1);
        }
    };
    
    std::array<PadMapping, MAX_PADS> padMappings;
    
    // Global state
    std::atomic<float> lastGlobalVelocity{0.0f};
    std::atomic<int> lastHitPad{-1};
    std::atomic<int> activePadCount{0};
    
    // For latch mode - track which pad is currently latched
    std::atomic<int> latchedPad{-1};
    
    // APVTS with all parameters
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Parameter pointers for fast access
    juce::AudioParameterInt* numPadsParam { nullptr };
    juce::AudioParameterChoice* deviceFilterParam { nullptr };
    juce::AudioParameterInt* midiChannelFilterParam { nullptr };
    juce::AudioParameterChoice* triggerModeParam { nullptr };
    juce::AudioParameterFloat* triggerLengthParam { nullptr };
    juce::AudioParameterChoice* velocityCurveParam { nullptr };
    juce::AudioParameterChoice* colorModeParam { nullptr };
    
    // MIDI Learn state
    int learningIndex = -1;  // Which pad is learning (-1 = none)
    
    // Sample rate for timing
    double sampleRate = 44100.0;
    
    // Helper functions
    int midiNoteToPadIndex(int noteNumber) const;  // Find pad index for a MIDI note
    float applyVelocityCurve(float rawVelocity) const;
    void handlePadHit(int padIdx, float velocity);
    void handlePadRelease(int padIdx);
    void updateTriggerStates(juce::AudioBuffer<float>& buffer);
    
#if defined(PRESET_CREATOR_UI)
    ImVec4 getPadColor(int padIdx, float brightness) const;
#endif
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MIDIPadModuleProcessor)
};

