#pragma once

#include <opencv2/core.hpp>
#include <juce_core/juce_core.h>
#include <map>

/**
 * Thread-safe singleton for sharing video frames between source and processing nodes.
 * Source nodes publish frames, processing nodes consume them.
 */
class VideoFrameManager
{
public:
    static VideoFrameManager& getInstance() 
    {
        static VideoFrameManager instance;
        return instance;
    }

    // Called by source node's background thread to publish a frame
    void setFrame(juce::uint32 sourceId, const cv::Mat& frame) 
    {
        const juce::ScopedLock lock(frameMapLock);
        if (!frame.empty())
        {
            frame.copyTo(frameMap[sourceId]);
        }
    }

    // Called by processing node's background thread to retrieve a frame
    cv::Mat getFrame(juce::uint32 sourceId) 
    {
        const juce::ScopedLock lock(frameMapLock);
        auto it = frameMap.find(sourceId);
        if (it != frameMap.end() && !it->second.empty())
        {
            return it->second.clone();
        }
        return cv::Mat();
    }

    // Called when a source node is deleted
    void removeSource(juce::uint32 sourceId) 
    {
        const juce::ScopedLock lock(frameMapLock);
        frameMap.erase(sourceId);
    }

    // For UI: get list of active sources
    juce::StringArray getAvailableSources()
    {
        const juce::ScopedLock lock(frameMapLock);
        juce::StringArray sources;
        for (const auto& pair : frameMap)
        {
            sources.add(juce::String((int)pair.first));
        }
        return sources;
    }

private:
    VideoFrameManager() = default;
    ~VideoFrameManager() = default;
    VideoFrameManager(const VideoFrameManager&) = delete;
    VideoFrameManager& operator=(const VideoFrameManager&) = delete;

    std::map<juce::uint32, cv::Mat> frameMap;
    juce::CriticalSection frameMapLock;
};

