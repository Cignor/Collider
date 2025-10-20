#include "VstHostModuleProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Helper function to create correct bus properties from the plugin's layout
juce::AudioProcessor::BusesProperties VstHostModuleProcessor::createBusesPropertiesForPlugin(juce::AudioPluginInstance& plugin)
{
    BusesProperties properties;

    // Add all input buses
    for (int i = 0; i < plugin.getBusCount(true); ++i)
    {
        if (auto* bus = plugin.getBus(true, i))
        {
            properties.addBus(true, bus->getName(), bus->getDefaultLayout(), bus->isEnabledByDefault());
            juce::Logger::writeToLog("[VstHost] Input bus " + juce::String(i) + ": " + 
                                     bus->getName() + " (" + 
                                     juce::String(bus->getDefaultLayout().size()) + " channels)");
        }
    }

    // Add all output buses
    for (int i = 0; i < plugin.getBusCount(false); ++i)
    {
        if (auto* bus = plugin.getBus(false, i))
        {
            properties.addBus(false, bus->getName(), bus->getDefaultLayout(), bus->isEnabledByDefault());
            juce::Logger::writeToLog("[VstHost] Output bus " + juce::String(i) + ": " + 
                                     bus->getName() + " (" + 
                                     juce::String(bus->getDefaultLayout().size()) + " channels)");
        }
    }

    return properties;
}

VstHostModuleProcessor::VstHostModuleProcessor(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::PluginDescription& desc)
    : ModuleProcessor(createBusesPropertiesForPlugin(*plugin)),
      hostedPlugin(std::move(plugin)),
      pluginDescription(desc),
      dummyApvts(*this, nullptr, "DummyParams", {})
{
    jassert(hostedPlugin != nullptr);
    
    juce::Logger::writeToLog("[VstHost] Created wrapper for: " + getName() + 
                             " with " + juce::String(getTotalNumInputChannels()) + " inputs, " +
                             juce::String(getTotalNumOutputChannels()) + " outputs");
}

VstHostModuleProcessor::~VstHostModuleProcessor()
{
    juce::Logger::writeToLog("[VstHost] Destroying wrapper for: " + getName());
}

void VstHostModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (hostedPlugin != nullptr)
    {
        hostedPlugin->prepareToPlay(sampleRate, samplesPerBlock);
        juce::Logger::writeToLog("[VstHost] " + getName() + " prepared: " + 
                                 juce::String(sampleRate) + " Hz, " + 
                                 juce::String(samplesPerBlock) + " samples");
    }
}

void VstHostModuleProcessor::releaseResources()
{
    if (hostedPlugin != nullptr)
    {
        hostedPlugin->releaseResources();
    }
}

void VstHostModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (hostedPlugin != nullptr)
    {
        hostedPlugin->processBlock(buffer, midi);
    }
}

const juce::String VstHostModuleProcessor::getName() const
{
    return hostedPlugin != nullptr ? hostedPlugin->getName() : "VST Host";
}

juce::AudioProcessorEditor* VstHostModuleProcessor::createEditor()
{
    return hostedPlugin != nullptr ? hostedPlugin->createEditor() : nullptr;
}

bool VstHostModuleProcessor::hasEditor() const
{
    return hostedPlugin != nullptr ? hostedPlugin->hasEditor() : false;
}

// State management is crucial for saving presets
juce::ValueTree VstHostModuleProcessor::getExtraStateTree() const
{
    // This tree will be saved inside the <extra> block in your preset
    juce::ValueTree state("VstHostState");

    if (hostedPlugin == nullptr)
        return state;

    // 1. Store the unique plugin identifier so we know what to load
    state.setProperty("fileOrIdentifier", pluginDescription.fileOrIdentifier, nullptr);
    state.setProperty("name", pluginDescription.name, nullptr);
    state.setProperty("manufacturerName", pluginDescription.manufacturerName, nullptr);
    state.setProperty("version", pluginDescription.version, nullptr);
    state.setProperty("pluginFormatName", pluginDescription.pluginFormatName, nullptr);

    // 2. Get the plugin's internal state as binary data
    juce::MemoryBlock pluginState;
    hostedPlugin->getStateInformation(pluginState);

    // 3. Store the binary data as a Base64 string in our ValueTree
    if (pluginState.getSize() > 0)
    {
        state.setProperty("pluginState", pluginState.toBase64Encoding(), nullptr);
        juce::Logger::writeToLog("[VstHost] Saved state for: " + getName() + " (" + juce::String(pluginState.getSize()) + " bytes)");
    }

    return state;
}

void VstHostModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (hostedPlugin == nullptr)
        return;

    if (vt.hasType("VstHostState"))
    {
        // When loading, get the Base64 string from the ValueTree
        juce::String stateString = vt.getProperty("pluginState", "").toString();

        if (stateString.isNotEmpty())
        {
            // Decode it back into binary data
            juce::MemoryBlock pluginState;
            if (pluginState.fromBase64Encoding(stateString))
            {
                // Restore the plugin's state
                hostedPlugin->setStateInformation(pluginState.getData(), (int)pluginState.getSize());
                juce::Logger::writeToLog("[VstHost] Restored state for: " + getName() + " (" + juce::String(pluginState.getSize()) + " bytes)");
            }
        }
    }
}

// UI and Pin Drawing
juce::String VstHostModuleProcessor::getAudioInputLabel(int channel) const
{
    if (hostedPlugin == nullptr)
        return "In " + juce::String(channel + 1);
    
    // Iterate through all input buses to find which one contains this channel
    int channelOffset = 0;
    for (int busIndex = 0; busIndex < hostedPlugin->getBusCount(true); ++busIndex)
    {
        if (auto* bus = hostedPlugin->getBus(true, busIndex))
        {
            const int busChannels = bus->getNumberOfChannels();
            if (channel < channelOffset + busChannels)
            {
                // This channel belongs to this bus
                const int channelInBus = channel - channelOffset;
                return bus->getName() + " " + juce::String(channelInBus + 1);
            }
            channelOffset += busChannels;
        }
    }
    return "In " + juce::String(channel + 1);
}

juce::String VstHostModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (hostedPlugin == nullptr)
        return "Out " + juce::String(channel + 1);
    
    // Iterate through all output buses to find which one contains this channel
    int channelOffset = 0;
    for (int busIndex = 0; busIndex < hostedPlugin->getBusCount(false); ++busIndex)
    {
        if (auto* bus = hostedPlugin->getBus(false, busIndex))
        {
            const int busChannels = bus->getNumberOfChannels();
            if (channel < channelOffset + busChannels)
            {
                // This channel belongs to this bus
                const int channelInBus = channel - channelOffset;
                return bus->getName() + " " + juce::String(channelInBus + 1);
            }
            channelOffset += busChannels;
        }
    }
    return "Out " + juce::String(channel + 1);
}

#if defined(PRESET_CREATOR_UI)
// Helper class for self-deleting plugin window
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton)
    {
        setContentOwned(content, true);
        setResizable(true, true);
        setUsingNativeTitleBar(true);
        centreWithSize(content->getWidth(), content->getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        // This will be called when the user clicks the 'x' button.
        // The window will delete itself automatically.
        delete this;
    }
};
#endif

void VstHostModuleProcessor::drawParametersInNode(float itemWidth, 
                                                   const std::function<bool(const juce::String&)>&, 
                                                   const std::function<void()>&)
{
#if defined(PRESET_CREATOR_UI)
    if (ImGui::Button("Open Editor", ImVec2(itemWidth, 0)))
    {
        if (auto* editor = createEditor())
        {
            // Create a new self-deleting window to host the editor
            new PluginEditorWindow(getName(), editor);
            juce::Logger::writeToLog("[VstHost] Opened editor for: " + getName());
        }
        else
        {
            juce::Logger::writeToLog("[VstHost] Plugin has no editor: " + getName());
        }
    }
    
    // Display plugin info
    ImGui::TextDisabled("Manufacturer: %s", pluginDescription.manufacturerName.toRawUTF8());
    ImGui::TextDisabled("Version: %s", pluginDescription.version.toRawUTF8());
#else
    juce::ignoreUnused(itemWidth);
#endif
}

void VstHostModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    if (hostedPlugin == nullptr)
        return;
    
    // Draw input pins for all input channels using dynamic labels
    const int numInputs = hostedPlugin->getTotalNumInputChannels();
    for (int i = 0; i < numInputs; ++i)
    {
        helpers.drawAudioInputPin(getAudioInputLabel(i).toRawUTF8(), i);
    }
    
    // Draw output pins for all output channels using dynamic labels
    const int numOutputs = hostedPlugin->getTotalNumOutputChannels();
    for (int i = 0; i < numOutputs; ++i)
    {
        helpers.drawAudioOutputPin(getAudioOutputLabel(i).toRawUTF8(), i);
    }
}

