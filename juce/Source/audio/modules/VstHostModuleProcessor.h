#pragma once
#include "ModuleProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

/**
    VstHostModuleProcessor wraps a VST plugin instance to integrate it seamlessly
    into the modular synthesizer graph. It acts as a bridge, forwarding audio
    processing calls to the hosted plugin while managing its state and UI.
*/
class VstHostModuleProcessor : public ModuleProcessor
{
public:
    VstHostModuleProcessor(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::PluginDescription& desc);
    ~VstHostModuleProcessor() override;

    // AudioProcessor Overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void releaseResources() override;

    const juce::String getName() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    bool acceptsMidi() const override { return hostedPlugin != nullptr && hostedPlugin->acceptsMidi(); }
    bool producesMidi() const override { return hostedPlugin != nullptr && hostedPlugin->producesMidi(); }
    
    // ModuleProcessor Overrides
    juce::AudioProcessorValueTreeState& getAPVTS() override { return dummyApvts; }
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;

    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    
    // State Management through ExtraStateTree
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& vt) override;
    
    // Access to the plugin description for saving/loading
    const juce::PluginDescription& getPluginDescription() const { return pluginDescription; }
    
    // Access to the hosted plugin instance
    juce::AudioPluginInstance* getHostedPlugin() const { return hostedPlugin.get(); }

private:
    // Helper function to create correct bus properties from plugin
    static BusesProperties createBusesPropertiesForPlugin(juce::AudioPluginInstance& plugin);
    
    std::unique_ptr<juce::AudioPluginInstance> hostedPlugin;
    juce::PluginDescription pluginDescription;

    // A dummy APVTS to satisfy the base class requirements
    // VST parameters are managed by the plugin itself, not through APVTS
    juce::AudioProcessorValueTreeState dummyApvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstHostModuleProcessor)
};

