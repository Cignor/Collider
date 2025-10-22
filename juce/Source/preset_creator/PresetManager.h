#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
 * PresetManager - Manages preset file scanning, caching, and loading
 * * This class provides preset browsing functionality for the node editor.
 * It scans directories for .xml preset files and provides search/filtering.
 */
class PresetManager
{
public:
    struct PresetInfo
    {
        juce::String name;
        juce::File file;
        juce::String description;
        juce::StringArray tags;
        juce::Time lastModified;
    };
    
    PresetManager() = default;
    ~PresetManager() = default;
    
    /**
     * Scan a directory for preset files (.xml)
     * @param directory The directory to scan
     * @param recursive Whether to scan subdirectories
     */
    void scanDirectory(const juce::File& directory, bool recursive = true)
    {
        if (!directory.exists() || !directory.isDirectory())
            return;
        
        auto files = directory.findChildFiles(
            recursive ? juce::File::findFiles : juce::File::findFilesAndDirectories,
            recursive,
            "*.xml"
        );
        
        for (const auto& file : files)
        {
            if (file.existsAsFile())
            {
                PresetInfo info;
                info.name = file.getFileNameWithoutExtension();
                info.file = file;
                info.lastModified = file.getLastModificationTime();
                
                // Try to extract description and tags from XML
                if (auto xml = juce::parseXML(file))
                {
                    info.description = xml->getStringAttribute("description", "");
                    info.tags = juce::StringArray::fromTokens(
                        xml->getStringAttribute("tags", ""),
                        ",",
                        ""
                    );
                }
                
                presets.add(info);
            }
        }
    }
    
    /**
     * Search for presets matching a query
     * @param query The search term (searches name, description, and tags)
     * @return Vector of matching presets
     */
    std::vector<PresetInfo> searchPresets(const juce::String& query) const
    {
        std::vector<PresetInfo> results;
        
        juce::String lowerQuery = query.toLowerCase();
        
        for (const auto& preset : presets)
        {
            if (query.isEmpty() ||
                preset.name.toLowerCase().contains(lowerQuery) ||
                preset.description.toLowerCase().contains(lowerQuery) ||
                containsTag(preset.tags, lowerQuery))
            {
                results.push_back(preset);
            }
        }
        
        return results;
    }
    
    /**
     * Load a preset file and return its XML
     * @param file The preset file to load
     * @return The XML element (caller must delete), or nullptr on failure
     */
    juce::XmlElement* loadPreset(const juce::File& file) const
    {
        if (!file.existsAsFile())
            return nullptr;
        
        return juce::parseXML(file).release();
    }
    
    /**
     * Clear the preset cache
     */
    void clearCache()
    {
        presets.clear();
    }
    
    /**
     * Get the number of cached presets
     */
    int getNumPresets() const { return presets.size(); }
    
private:
    bool containsTag(const juce::StringArray& tags, const juce::String& query) const
    {
        for (const auto& tag : tags)
        {
            if (tag.toLowerCase().contains(query))
                return true;
        }
        return false;
    }
    
    juce::Array<PresetInfo> presets;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};