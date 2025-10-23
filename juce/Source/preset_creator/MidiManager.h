#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>

// Manages scanning and caching of MIDI files (.mid, .midi)
class MidiManager
{
public:
    struct MidiInfo
    {
        juce::String name;
        juce::File file;
    };

    struct DirectoryNode
    {
        juce::String name;
        juce::File directory;
        std::vector<MidiInfo> midiFiles;
        std::vector<std::unique_ptr<DirectoryNode>> subdirectories;
    };

    MidiManager() : rootNode(std::make_unique<DirectoryNode>()) {}

    DirectoryNode* getRootNode() const { return rootNode.get(); }

    void scanDirectory(const juce::File& directory)
    {
        rootNode->name = directory.getFileName();
        rootNode->directory = directory;
        rootNode->midiFiles.clear();
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
            else if (file.hasFileExtension(".mid") || file.hasFileExtension(".midi"))
            {
                MidiInfo info;
                info.name = file.getFileNameWithoutExtension();
                info.file = file;
                node->midiFiles.push_back(info);
            }
        }
    }

    std::unique_ptr<DirectoryNode> rootNode;
};

