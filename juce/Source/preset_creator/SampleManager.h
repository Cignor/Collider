#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

/**
 * SampleManager - Manages audio sample file scanning, caching, and metadata
 * * This class provides sample browsing functionality for the node editor.
 * It scans directories for audio files and extracts metadata (duration, sample rate, etc.)
 */
class SampleManager
{
public:
    struct SampleInfo
    {
        juce::String name;
        juce::File file;
        double durationSeconds;
        int sampleRate;
        int numChannels;
        juce::int64 fileSizeBytes;
        juce::Time lastModified;
    };
    
    SampleManager()
    {
        formatManager.registerBasicFormats();
    }
    
    ~SampleManager() = default;
    
    /**
     * Scan a directory for audio sample files
     * @param directory The directory to scan
     * @param recursive Whether to scan subdirectories
     */
    void scanDirectory(const juce::File& directory, bool recursive = true)
    {
        if (!directory.exists() || !directory.isDirectory())
            return;
        
        // Supported audio formats
        juce::String wildcards = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
        
        auto files = directory.findChildFiles(
            juce::File::findFiles,
            recursive,
            wildcards
        );
        
        for (const auto& file : files)
        {
            if (file.existsAsFile())
            {
                SampleInfo info;
                info.name = file.getFileNameWithoutExtension();
                info.file = file;
                info.fileSizeBytes = file.getSize();
                info.lastModified = file.getLastModificationTime();
                
                // Try to read audio file metadata
                std::unique_ptr<juce::AudioFormatReader> reader(
                    formatManager.createReaderFor(file)
                );
                
                if (reader)
                {
                    info.durationSeconds = reader->lengthInSamples / reader->sampleRate;
                    info.sampleRate = (int)reader->sampleRate;
                    info.numChannels = (int)reader->numChannels;
                }
                else
                {
                    // Couldn't read file - use defaults
                    info.durationSeconds = 0.0;
                    info.sampleRate = 0;
                    info.numChannels = 0;
                }
                
                samples.add(info);
            }
        }
    }
    
    /**
     * Search for samples matching a query
     * @param query The search term (searches name only)
     * @return Vector of matching samples
     */
    std::vector<SampleInfo> searchSamples(const juce::String& query) const
    {
        std::vector<SampleInfo> results;
        
        juce::String lowerQuery = query.toLowerCase();
        
        for (const auto& sample : samples)
        {
            if (query.isEmpty() ||
                sample.name.toLowerCase().contains(lowerQuery))
            {
                results.push_back(sample);
            }
        }
        
        return results;
    }
    
    /**
     * Clear the sample cache
     */
    void clearCache()
    {
        samples.clear();
    }
    
    /**
     * Get the number of cached samples
     */
    int getNumSamples() const { return samples.size(); }
    
private:
    juce::AudioFormatManager formatManager;
    juce::Array<SampleInfo> samples;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleManager)
};