#pragma once

#include "ModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include "InletModuleProcessor.h"
#include "OutletModuleProcessor.h"
#include <memory>

/**
    MetaModuleProcessor - A recursive container module for sub-patching
    
    This module contains a complete ModularSynthProcessor instance internally,
    allowing users to create complex patches and collapse them into single,
    reusable modules.
    
    Architecture:
    - Contains an internal ModularSynthProcessor graph
    - Uses InletModuleProcessor nodes inside to represent external inputs
    - Uses OutletModuleProcessor nodes inside to represent external outputs
    - Can expose selected internal parameters as its own parameters
*/
class MetaModuleProcessor : public ModuleProcessor
{
public:
    MetaModuleProcessor();
    ~MetaModuleProcessor() override = default;

    const juce::String getName() const override { return "Meta Module"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Access to the internal graph for UI editing
    ModularSynthProcessor* getInternalGraph() const { return internalGraph.get(); }
    
    // State management for the internal graph
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    
    // UI interaction flag - set when user clicks "Edit Internal Patch" button
    std::atomic<bool> editRequested { false };

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;

    void drawIoPins(const NodePinHelpers& helpers) override;

    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    // Helper to get inlet/outlet nodes
    std::vector<InletModuleProcessor*> getInletNodes() const;
    std::vector<OutletModuleProcessor*> getOutletNodes() const;

private:
    struct ChannelLayoutInfo
    {
        int channelCount = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<ModularSynthProcessor> internalGraph;
    
    // Temporary buffers for inlet/outlet communication
    std::vector<juce::AudioBuffer<float>> inletBuffers;
    std::vector<juce::AudioBuffer<float>> outletBuffers;
    std::vector<ChannelLayoutInfo> inletChannelLayouts;
    std::vector<ChannelLayoutInfo> outletChannelLayouts;
    
    // Cached inlet/outlet counts (updated when internal graph changes)
    int cachedInletCount { 0 };
    int cachedOutletCount { 0 };
    
    // Label for this meta module
    juce::String metaModuleLabel;
    
    // Rebuild bus layout based on inlets/outlets
    void rebuildBusLayout();
    
    // Update cached inlet/outlet information
    void updateInletOutletCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetaModuleProcessor)
};

