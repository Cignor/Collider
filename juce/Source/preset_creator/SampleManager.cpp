#include "SampleManager.h"

SampleManager::SampleManager()
{
    // Register common audio formats
    m_formatManager.registerBasicFormats();
}

void SampleManager::scanDirectory(const juce::File& directory, bool recursive)
{
    if (!directory.exists() || !directory.isDirectory())
        return;
    
    // Supported audio file extensions
    juce::String wildcardPattern = "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg";
    
    // Find all audio files
    juce::Array<juce::File> audioFiles;
    directory.findChildFiles(audioFiles, 
                             juce::File::findFiles, 
                             recursive, 
                             wildcardPattern);
    
    for (const auto& file : audioFiles)
    {
        SampleInfo info = extractMetadata(file);
        if (info.sampleRate > 0) // Valid audio file
            m_samples.push_back(info);
    }
    
    juce::Logger::writeToLog("[SampleManager] Scanned " + juce::String(m_samples.size()) + 
                            " samples from: " + directory.getFullPathName());
}

std::vector<SampleManager::SampleInfo> SampleManager::searchSamples(const juce::String& searchTerm) const
{
    std::vector<SampleInfo> results;
    
    for (const auto& sample : m_samples)
    {
        if (sample.matchesSearch(searchTerm))
            results.push_back(sample);
    }
    
    return results;
}

SampleManager::SampleInfo SampleManager::extractMetadata(const juce::File& file)
{
    SampleInfo info;
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    info.lastModified = file.getLastModificationTime();
    
    // Try to read audio file metadata
    std::unique_ptr<juce::AudioFormatReader> reader(m_formatManager.createReaderFor(file));
    
    if (reader != nullptr)
    {
        info.sampleRate = static_cast<int>(reader->sampleRate);
        info.numChannels = static_cast<int>(reader->numChannels);
        info.lengthInSamples = reader->lengthInSamples;
        
        if (info.sampleRate > 0)
            info.durationSeconds = info.lengthInSamples / static_cast<double>(info.sampleRate);
    }
    
    return info;
}

