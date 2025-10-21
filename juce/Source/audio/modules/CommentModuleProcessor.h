#pragma once

#include "ModuleProcessor.h"

class CommentModuleProcessor : public ModuleProcessor
{
public:
    CommentModuleProcessor();
    ~CommentModuleProcessor() override = default;

    const juce::String getName() const override { return "comment"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers&) override {} // This node has no pins
#endif

    // Public buffers for UI interaction
    char titleBuffer[64];
    char textBuffer[2048];
    float nodeWidth = 250.0f;  // A more standard default width
    float nodeHeight = 150.0f; // A more standard default height

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

#if defined(PRESET_CREATOR_UI)
    bool wasBeingResizedLastFrame = false;
#endif
};
