#include "AnimationFileLoader.h"

void AnimationFileLoader::startLoadingFile(const juce::File& fileToLoad)
{
    // If we're already loading, don't start a new job.
    if (isLoading())
    {
        juce::Logger::writeToLog("AnimationFileLoader: Already loading a file. Ignoring new request.");
        return;
    }

    // Validate that the file exists
    if (!fileToLoad.existsAsFile())
    {
        juce::Logger::writeToLog("AnimationFileLoader ERROR: File does not exist: " + fileToLoad.getFullPathName());
        return;
    }

    // Validate file extension
    juce::String ext = fileToLoad.getFileExtension().toLowerCase();
    if (ext != ".fbx" && ext != ".glb" && ext != ".gltf")
    {
        juce::Logger::writeToLog("AnimationFileLoader ERROR: Unsupported file type: " + ext);
        return;
    }

    // Safely copy the file object and start the thread.
    m_fileToLoad = fileToLoad;
    
    juce::Logger::writeToLog("AnimationFileLoader: Starting background load of: " + fileToLoad.getFullPathName());
    
    startThread(); // This will call run() on the background thread
}

void AnimationFileLoader::run()
{
    // This code executes on the background thread.
    juce::Logger::writeToLog("AnimationFileLoader: Background thread started.");

    // 1. Set the loading flag.
    m_isLoading.store(true);

    // 2. Prepare a temporary pointer for the loaded data.
    std::unique_ptr<RawAnimationData> rawData = nullptr;

    // 3. Check the file extension and use the correct loader.
    juce::String extension = m_fileToLoad.getFileExtension().toLowerCase();
    std::string filePath = m_fileToLoad.getFullPathName().toStdString();

    try
    {
        if (extension == ".fbx")
        {
            juce::Logger::writeToLog("AnimationFileLoader: Using FbxLoader for: " + m_fileToLoad.getFileName());
            rawData = FbxLoader::LoadFromFile(filePath);
        }
        else if (extension == ".glb" || extension == ".gltf")
        {
            juce::Logger::writeToLog("AnimationFileLoader: Using GltfLoader for: " + m_fileToLoad.getFileName());
            rawData = GltfLoader::LoadFromFile(filePath);
        }
        else
        {
            // This shouldn't happen due to validation in startLoadingFile
            juce::Logger::writeToLog("AnimationFileLoader ERROR: Unsupported file type: " + extension);
        }

        // Check if loading was successful
        if (rawData != nullptr)
        {
            juce::Logger::writeToLog("AnimationFileLoader: Successfully loaded raw animation data.");
            juce::Logger::writeToLog("  Nodes: " + juce::String(rawData->nodes.size()));
            juce::Logger::writeToLog("  Bones: " + juce::String(rawData->bones.size()));
            juce::Logger::writeToLog("  Clips: " + juce::String(rawData->clips.size()));
            
            // --- SECONDARY VALIDATION CHECK ---
            // The loaders should have already validated, but this is a defensive double-check
            std::string validationError;
            if (!RawAnimationData::validate(*rawData, validationError))
            {
                juce::Logger::writeToLog("AnimationFileLoader ERROR: Secondary validation failed!");
                juce::Logger::writeToLog("Validation message: " + juce::String(validationError));
                rawData = nullptr; // Discard invalid data
            }
            else
            {
                juce::Logger::writeToLog("AnimationFileLoader: Secondary validation passed.");
            }
            // --- END OF SECONDARY VALIDATION ---
        }
        else
        {
            juce::Logger::writeToLog("AnimationFileLoader ERROR: Loader returned nullptr.");
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("AnimationFileLoader EXCEPTION: " + juce::String(e.what()));
        rawData = nullptr;
    }
    catch (...)
    {
        juce::Logger::writeToLog("AnimationFileLoader ERROR: Unknown exception during loading.");
        rawData = nullptr;
    }

    // 4. Safely store the result using the critical section.
    {
        const juce::ScopedLock lock(m_criticalSection);
        m_loadedData = std::move(rawData);
    }

    // 5. Reset the loading flag.
    m_isLoading.store(false);

    juce::Logger::writeToLog("AnimationFileLoader: Background thread finished. Notifying listeners...");

    // 6. Notify listeners on the main thread that we are done.
    // This is a thread-safe call - the actual callback will happen on the message thread.
    sendChangeMessage();
}

std::unique_ptr<RawAnimationData> AnimationFileLoader::getLoadedData()
{
    // Use the critical section to safely access and transfer the data.
    const juce::ScopedLock lock(m_criticalSection);
    
    if (m_loadedData)
    {
        juce::Logger::writeToLog("AnimationFileLoader: Transferring loaded data to caller.");
    }
    else
    {
        juce::Logger::writeToLog("AnimationFileLoader: No data available (loading may have failed).");
    }
    
    return std::move(m_loadedData);
}

juce::String AnimationFileLoader::getLoadedFilePath() const
{
    return m_fileToLoad.getFullPathName();
}

