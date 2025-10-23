#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>

class PresetManager
{
public:
    struct PresetInfo
    {
        juce::String name;
        juce::File file;
        juce::String description;
        juce::StringArray tags;
    };

    struct DirectoryNode
    {
        juce::String name;
        juce::File directory;
        std::vector<PresetInfo> presets;
        std::vector<std::unique_ptr<DirectoryNode>> subdirectories;
    };

    PresetManager() : rootNode(std::make_unique<DirectoryNode>()) {}

    DirectoryNode* getRootNode() const { return rootNode.get(); }

    void scanDirectory(const juce::File& directory)
    {
        rootNode->name = directory.getFileName();
        rootNode->directory = directory;
        rootNode->presets.clear();
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
            else if (file.hasFileExtension(".xml"))
            {
                PresetInfo info;
                info.name = file.getFileNameWithoutExtension();
                info.file = file;
                if (auto xml = juce::parseXML(file))
                {
                    info.description = xml->getStringAttribute("description", "");
                    info.tags = juce::StringArray::fromTokens(xml->getStringAttribute("tags", ""), ",", "");
                }
                node->presets.push_back(info);
            }
        }
    }

    std::unique_ptr<DirectoryNode> rootNode;
};
