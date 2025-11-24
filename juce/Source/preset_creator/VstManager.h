#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>
#include <map>

// Forward declaration
class PresetCreatorApplication;

// Manages scanning and caching of VST plugins
class VstManager
{
public:
    struct VstInfo
    {
        juce::PluginDescription description;
        juce::String name;           // Plugin name
        juce::String manufacturer;   // Manufacturer name
        juce::String version;        // Plugin version
        juce::File pluginFile;       // Path to .vst3 file
        bool isInstrument;           // Instrument vs Effect
        int numInputs;               // Input channels
        int numOutputs;              // Output channels
    };

    struct DirectoryNode
    {
        juce::String name;           // Folder name or "Manufacturer Name"
        juce::File directory;        // Physical directory (if grouping by folder)
        std::vector<VstInfo> plugins;
        std::vector<std::unique_ptr<DirectoryNode>> subdirectories;
    };

    VstManager() : rootNode(std::make_unique<DirectoryNode>()) {}

    DirectoryNode* getRootNode() const { return rootNode.get(); }

    void scanDirectory(const juce::File& directory,
                      juce::AudioPluginFormatManager& formatManager,
                      juce::KnownPluginList& knownPluginList);

    void clearCache()
    {
        rootNode = std::make_unique<DirectoryNode>();
    }

    // Search functionality
    std::vector<VstInfo> searchPlugins(const juce::String& searchTerm) const;
    
    // Build tree from existing plugin list (without scanning)
    // Useful when plugins are already loaded from saved XML
    void buildTreeFromPluginList(const juce::File& scanDirectory,
                                 juce::KnownPluginList& knownPluginList);

private:

    std::unique_ptr<DirectoryNode> rootNode;
};

