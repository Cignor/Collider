#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

/**
 * Source node that loads a video file and publishes frames to VideoFrameManager.
 * Outputs its own logical ID as a CV signal for routing to processing nodes.
 */
class VideoFileLoaderModule : public ModuleProcessor, private juce::Thread
{
public:
    VideoFileLoaderModule();
    ~VideoFileLoaderModule() override;

    const juce::String getName() const override { return "video_file_loader"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void setTimingInfo(const TransportState& state) override { lastTransportPlaying.store(state.isPlaying); if (syncToTransport.load()) playing.store(state.isPlaying); }
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // State management for saving/loading video file path
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

    // For UI
    juce::Image getLatestFrame();
    void chooseVideoFile();

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    
    // Override to specify custom node width. Height is calculated dynamically based on video aspect ratio.
    // Width changes based on zoom level (Small=240px, Normal=480px, Large=960px).
    ImVec2 getCustomNodeSize() const override;
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    
    std::atomic<float>* loopParam = nullptr;
    // 0 = Small (240), 1 = Normal (480), 2 = Large (960)
    std::atomic<float>* zoomLevelParam = nullptr;
    // Playback controls
    std::atomic<float>* speedParam = nullptr;   // 0.25 .. 4.0 (1.0 default)
    std::atomic<float>* inNormParam = nullptr;  // 0..1
    std::atomic<float>* outNormParam = nullptr; // 0..1
    std::atomic<float>* syncParam = nullptr; // bool as float
    std::atomic<bool> playing { true };
    std::atomic<bool> syncToTransport { true };
    std::atomic<bool> lastTransportPlaying { false };
    std::atomic<bool> needPreviewFrame { false };
    std::atomic<bool> lastPlaying { false }; // for play-edge detection
    std::atomic<int> lastFourcc { 0 }; // cached FOURCC
    std::atomic<int> pendingSeekFrame { -1 };
    std::atomic<int> lastPosFrame { 0 };
    // Normalized pending requests (used when totalFrames not ready yet)
    std::atomic<float> pendingSeekNorm { -1.0f };
    std::atomic<float> pendingStartNorm { -1.0f };
    
    cv::VideoCapture videoCapture;
    juce::CriticalSection captureLock;

    static juce::String fourccToString(int fourcc)
    {
        if (fourcc == 0) return "unknown";
        char c[5];
        c[0] = (char)(fourcc & 0xFF);
        c[1] = (char)((fourcc >> 8) & 0xFF);
        c[2] = (char)((fourcc >> 16) & 0xFF);
        c[3] = (char)((fourcc >> 24) & 0xFF);
        c[4] = '\0';
        return juce::String(c);
    }

    static juce::String fourccFriendlyName(const juce::String& code)
    {
        const juce::String c = code.toLowerCase();
        if (c == "avc1" || c == "h264") return "H.264";
        if (c == "hvc1" || c == "hevc" || c == "hev1") return "H.265/HEVC";
        if (c == "mp4v" || c == "m4v") return "MPEG-4 Part 2";
        if (c == "mjpg" || c == "mjpa" || c == "jpeg") return "Motion JPEG";
        if (c == "xvid" ) return "MPEG-4 ASP (Xvid)";
        if (c == "vp09") return "VP9";
        if (c == "av01") return "AV1";
        if (c == "wmv3" || c == "wvc1") return "VC-1";
        if (c == "h263") return "H.263";
        return "unknown";
    }
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
    
    juce::File videoFileToLoad;
    juce::File currentVideoFile;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Cached metadata (atomic for cross-thread visibility)
    std::atomic<int> totalFrames { 0 };
};

