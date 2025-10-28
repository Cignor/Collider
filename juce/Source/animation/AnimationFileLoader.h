#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include "RawAnimationData.h"
#include "FbxLoader.h"
#include "GltfLoader.h"

/**
 * AnimationFileLoader - Background thread loader for animation files
 * 
 * This class loads FBX, GLB, and GLTF animation files on a background thread
 * to prevent freezing the main UI. It inherits from juce::Thread to run on
 * its own thread and juce::ChangeBroadcaster to notify listeners when loading
 * completes.
 * 
 * Usage:
 *   1. Create an instance and add a listener to it
 *   2. Call startLoadingFile(file) to begin loading
 *   3. When loading completes, your listener's changeListenerCallback() is called
 *   4. Call getLoadedData() to retrieve the loaded animation data
 */
class AnimationFileLoader : public juce::Thread,
                            public juce::ChangeBroadcaster
{
public:
    // The constructor initializes the thread with a descriptive name.
    AnimationFileLoader() : juce::Thread("Animation File Loader Thread") {}

    // The destructor ensures the thread is stopped safely.
    ~AnimationFileLoader() override
    {
        stopThread(5000); // Wait up to 5 seconds for the thread to stop gracefully
    }

    /**
     * Starts loading an animation file on the background thread.
     * This is called from the main thread.
     * 
     * @param fileToLoad The animation file to load (.fbx, .glb, or .gltf)
     */
    void startLoadingFile(const juce::File& fileToLoad);

    /**
     * The main method that runs on the background thread.
     * This is called automatically by JUCE when startThread() is invoked.
     */
    void run() override;

    /**
     * Thread-safe check to see if the loader is currently busy.
     * 
     * @return true if a file is currently being loaded
     */
    bool isLoading() const { return m_isLoading.load(); }

    /**
     * Gets the loaded animation data.
     * This transfers ownership of the unique_ptr to the caller.
     * Should be called after receiving the change notification.
     * 
     * @return The loaded RawAnimationData, or nullptr if loading failed
     */
    std::unique_ptr<RawAnimationData> getLoadedData();

    /**
     * Gets the path of the file that was loaded (or attempted to load).
     * 
     * @return The file path as a string
     */
    juce::String getLoadedFilePath() const;

private:
    juce::File m_fileToLoad;
    std::unique_ptr<RawAnimationData> m_loadedData;

    // Atomics are great for simple thread-safe flags.
    std::atomic<bool> m_isLoading { false };

    // A critical section protects access to non-atomic data
    // shared between threads (like our unique_ptr).
    juce::CriticalSection m_criticalSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationFileLoader)
};

