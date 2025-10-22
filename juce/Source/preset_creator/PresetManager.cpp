#include "PresetManager.h"

PresetManager::PresetManager()
{
}

void PresetManager::scanDirectory(const juce::File& directory)
{
    if (!directory.exists() || !directory.isDirectory())
        return;
    
    // Find all .xml preset files
    juce::Array<juce::File> presetFiles;
    directory.findChildFiles(presetFiles, juce::File::findFiles, false, "*.xml");
    
    for (const auto& file : presetFiles)
    {
        PresetInfo info = extractMetadata(file);
        m_presets.push_back(info);
    }
    
    juce::Logger::writeToLog("[PresetManager] Scanned " + juce::String(m_presets.size()) + 
                            " presets from: " + directory.getFullPathName());
}

std::vector<PresetManager::PresetInfo> PresetManager::searchPresets(const juce::String& searchTerm) const
{
    std::vector<PresetInfo> results;
    
    for (const auto& preset : m_presets)
    {
        if (preset.matchesSearch(searchTerm))
            results.push_back(preset);
    }
    
    return results;
}

std::vector<PresetManager::PresetInfo> PresetManager::getPresetsByTag(const juce::String& tag) const
{
    std::vector<PresetInfo> results;
    
    for (const auto& preset : m_presets)
    {
        if (preset.tags.contains(tag))
            results.push_back(preset);
    }
    
    return results;
}

juce::StringArray PresetManager::getAllTags() const
{
    juce::StringArray allTags;
    
    for (const auto& preset : m_presets)
    {
        for (const auto& tag : preset.tags)
        {
            if (!allTags.contains(tag))
                allTags.add(tag);
        }
    }
    
    allTags.sort(true);
    return allTags;
}

juce::XmlElement* PresetManager::loadPreset(const juce::File& file)
{
    if (!file.exists())
        return nullptr;
    
    return juce::XmlDocument::parse(file).release();
}

bool PresetManager::savePreset(const juce::File& file, 
                               const juce::XmlElement& presetData,
                               const juce::String& description,
                               const juce::StringArray& tags)
{
    // Create a copy of the preset data and add metadata
    std::unique_ptr<juce::XmlElement> presetCopy(new juce::XmlElement(presetData));
    
    // Add metadata properties to the root element
    if (description.isNotEmpty())
        presetCopy->setAttribute("description", description);
    
    if (!tags.isEmpty())
        presetCopy->setAttribute("tags", tags.joinIntoString(","));
    
    // Write to file
    if (presetCopy->writeTo(file))
    {
        // Update cache
        PresetInfo info = extractMetadata(file);
        
        // Remove old entry if exists
        m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(),
            [&file](const PresetInfo& p) { return p.file == file; }), m_presets.end());
        
        // Add new entry
        m_presets.push_back(info);
        
        return true;
    }
    
    return false;
}

PresetManager::PresetInfo PresetManager::extractMetadata(const juce::File& file)
{
    PresetInfo info;
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    info.lastModified = file.getLastModificationTime();
    
    // Parse the XML to extract metadata
    std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(file);
    
    if (xml != nullptr)
    {
        // Extract description
        info.description = xml->getStringAttribute("description", "");
        
        // Extract tags
        juce::String tagsStr = xml->getStringAttribute("tags", "");
        if (tagsStr.isNotEmpty())
        {
            info.tags.addTokens(tagsStr, ",", "");
            info.tags.trim();
        }
    }
    
    return info;
}

