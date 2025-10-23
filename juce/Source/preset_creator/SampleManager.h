#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <memory>

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
    };

    struct DirectoryNode
    {
        juce::String name;
        juce::File directory;
        std::vector<SampleInfo> samples;
        std::vector<std::unique_ptr<DirectoryNode>> subdirectories;
    };

    SampleManager() : rootNode(std::make_unique<DirectoryNode>())
    {
        formatManager.registerBasicFormats();
    }

    DirectoryNode* getRootNode() const { return rootNode.get(); }

    void scanDirectory(const juce::File& directory)
    {
        rootNode->name = directory.getFileName();
        rootNode->directory = directory;
        rootNode->samples.clear();
        rootNode->subdirectories.clear();
        scanRecursively(rootNode.get());
    }

    void clearCache()
    {
        rootNode = std::make_unique<DirectoryNode>();
    }

private:
    void scanRecursively(DirectoryNode* node)
    {
        if (!node->directory.isDirectory()) return;

        for (const auto& entry : juce::RangedDirectoryIterator(node->directory, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto& file = entry.getFile();
            if (file.isDirectory())
            {
                auto subdir = std::make_unique<DirectoryNode>();
                subdir->name = file.getFileName();
                subdir->directory = file;
                scanRecursively(subdir.get());
                node->subdirectories.push_back(std::move(subdir));
            }
            else if (file.hasFileExtension(".wav") || file.hasFileExtension(".aif") || file.hasFileExtension(".flac") || file.hasFileExtension(".mp3") || file.hasFileExtension(".ogg"))
            {
                SampleInfo info;
                info.name = file.getFileNameWithoutExtension();
                info.file = file;
                if (auto* reader = formatManager.createReaderFor(file))
                {
                    info.durationSeconds = reader->lengthInSamples / reader->sampleRate;
                    info.sampleRate = (int)reader->sampleRate;
                    info.numChannels = (int)reader->numChannels;
                    delete reader;
                }
                node->samples.push_back(info);
            }
        }
    }
    
    juce::AudioFormatManager formatManager;
    std::unique_ptr<DirectoryNode> rootNode;
};
