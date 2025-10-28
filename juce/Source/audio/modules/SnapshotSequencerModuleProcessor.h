#pragma once

#include "ModuleProcessor.h"
#include "../../ipc/CommandBus.h"
#include <array>

class SnapshotSequencerModuleProcessor : public ModuleProcessor
{
public:
    SnapshotSequencerModuleProcessor();
    ~SnapshotSequencerModuleProcessor() override = default;

    const juce::String getName() const override { return "snapshot_sequencer"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Public API for UI to capture and manage snapshots
    void setSnapshotForStep(int stepIndex, const juce::MemoryBlock& state);
    const juce::MemoryBlock& getSnapshotForStep(int stepIndex) const;
    void clearSnapshotForStep(int stepIndex);
    bool isSnapshotStored(int stepIndex) const;
    
    // Set the CommandBus pointer so we can enqueue LoadPatchState commands
    void setCommandBus(CommandBus* bus) { commandBus = bus; }
    
    // Get parent voice ID (needed for command routing)
    juce::uint64 getParentVoiceId() const { return parentVoiceId; }
    void setParentVoiceId(juce::uint64 id) { parentVoiceId = id; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override
    {
        helpers.drawAudioInputPin("Clock", 0);
        helpers.drawAudioInputPin("Reset", 1);
    }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Clock";
            case 1: return "Reset";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        return ""; // No audio outputs
    }
#endif

    // State persistence
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& tree) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Parameters
    std::atomic<float>* numStepsParam { nullptr };
    
    // Snapshot storage (16 steps maximum)
    static constexpr int MAX_STEPS = 16;
    std::array<juce::MemoryBlock, MAX_STEPS> snapshots;
    
    // Sequencer state
    std::atomic<int> currentStep { 0 };
    double sampleRate { 44100.0 };
    
    // Clock detection
    bool lastClockHigh { false };
    bool lastResetHigh { false };
    
    // Command bus for triggering patch loads
    CommandBus* commandBus { nullptr };
    juce::uint64 parentVoiceId { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotSequencerModuleProcessor)
};

