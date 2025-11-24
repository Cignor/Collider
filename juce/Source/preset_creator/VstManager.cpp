#include "VstManager.h"
#include "PresetCreatorApplication.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

void VstManager::scanDirectory(const juce::File& directory,
                               juce::AudioPluginFormatManager& formatManager,
                               juce::KnownPluginList& knownPluginList)
{
    if (!directory.exists() || !directory.isDirectory())
    {
        juce::Logger::writeToLog("[VstManager] Invalid directory: " + directory.getFullPathName());
        return;
    }

    // 1. Find VST3 format
    juce::VST3PluginFormat* vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        if (auto* format = formatManager.getFormat(i); format->getName() == "VST3")
        {
            vst3Format = dynamic_cast<juce::VST3PluginFormat*>(format);
            break;
        }
    }

    if (vst3Format == nullptr)
    {
        juce::Logger::writeToLog("[VstManager] ERROR: VST3 format not found in format manager.");
        return;
    }

    // 2. Setup scanner
    juce::FileSearchPath searchPath;
    searchPath.add(directory);

    auto& app = PresetCreatorApplication::getApp();
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile(app.getApplicationName());

    juce::PluginDirectoryScanner scanner(knownPluginList, *vst3Format, searchPath, true,
                                         appDataDir.getChildFile("dead_plugins.txt"), true);

    // 3. Perform scan
    juce::String pluginBeingScanned;
    int numFound = 0;
    juce::Logger::writeToLog("[VstManager] Starting scan in: " + directory.getFullPathName());
    
    while (scanner.scanNextFile(true, pluginBeingScanned))
    {
        juce::Logger::writeToLog("[VstManager] Scanning: " + pluginBeingScanned);
        ++numFound;
    }

    juce::Logger::writeToLog("[VstManager] Scan complete. Found " + juce::String(numFound) + " plugin(s).");
    juce::Logger::writeToLog("[VstManager] Total plugins in list: " + juce::String(knownPluginList.getNumTypes()));

    // 4. Save the updated plugin list
    auto pluginListFile = appDataDir.getChildFile("known_plugins.xml");
    if (auto pluginListXml = knownPluginList.createXml())
    {
        if (pluginListXml->writeTo(pluginListFile))
        {
            juce::Logger::writeToLog("[VstManager] Saved plugin list to: " + pluginListFile.getFullPathName());
        }
    }

    // 5. Build tree from knownPluginList
    buildTreeFromPluginList(directory, knownPluginList);
}

void VstManager::buildTreeFromPluginList(const juce::File& scanDirectory,
                                         juce::KnownPluginList& knownPluginList)
{
    rootNode = std::make_unique<DirectoryNode>();
    rootNode->name = scanDirectory.getFileName();
    rootNode->directory = scanDirectory;

    // Group plugins by manufacturer
    std::map<juce::String, std::vector<juce::PluginDescription>> byManufacturer;
    std::set<juce::String> seenPlugins; // For deduplication (name + manufacturer)

    for (const auto& desc : knownPluginList.getTypes())
    {
        juce::File pluginFile(desc.fileOrIdentifier);
        if (!pluginFile.existsAsFile())
            continue;

        // Check if plugin is in the scan directory
        juce::File pluginDir = pluginFile.getParentDirectory();
        if (!pluginDir.isAChildOf(scanDirectory) && pluginDir != scanDirectory)
            continue;

        // Deduplication: Use name + manufacturer as unique key
        juce::String uniqueKey = desc.name + "|" + desc.manufacturerName;
        if (seenPlugins.find(uniqueKey) != seenPlugins.end())
            continue; // Skip duplicate

        seenPlugins.insert(uniqueKey);

        juce::String manufacturer = desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
        byManufacturer[manufacturer].push_back(desc);
    }

    // Build tree structure
    for (const auto& [manufacturer, plugins] : byManufacturer)
    {
        auto manufacturerNode = std::make_unique<DirectoryNode>();
        manufacturerNode->name = manufacturer;

        for (const auto& desc : plugins)
        {
            VstInfo info;
            info.description = desc;
            info.name = desc.name;
            info.manufacturer = desc.manufacturerName;
            info.version = desc.version;
            info.pluginFile = juce::File(desc.fileOrIdentifier);
            info.isInstrument = desc.isInstrument;
            info.numInputs = desc.numInputChannels;
            info.numOutputs = desc.numOutputChannels;
            manufacturerNode->plugins.push_back(info);
        }

        rootNode->subdirectories.push_back(std::move(manufacturerNode));
    }

    juce::Logger::writeToLog("[VstManager] Built tree with " + juce::String((int)rootNode->subdirectories.size()) + " manufacturer(s).");
}

std::vector<VstManager::VstInfo> VstManager::searchPlugins(const juce::String& searchTerm) const
{
    std::vector<VstInfo> results;
    
    if (searchTerm.isEmpty())
        return results;

    juce::String searchLower = searchTerm.toLowerCase();

    std::function<void(const DirectoryNode*)> searchRecursive = 
        [&](const DirectoryNode* node)
    {
        if (!node) return;

        for (const auto& plugin : node->plugins)
        {
            if (plugin.name.toLowerCase().contains(searchLower) ||
                plugin.manufacturer.toLowerCase().contains(searchLower))
            {
                results.push_back(plugin);
            }
        }

        for (const auto& subdir : node->subdirectories)
        {
            searchRecursive(subdir.get());
        }
    };

    searchRecursive(rootNode.get());
    return results;
}

