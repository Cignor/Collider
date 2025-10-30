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
    // Width changes based on zoom state (480px normal, 960px zoomed).
    ImVec2 getCustomNodeSize() const override;
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    
    std::atomic<float>* loopParam = nullptr;
    juce::AudioParameterBool* isZoomedParam = nullptr;
    
    cv::VideoCapture videoCapture;
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
    
    juce::File videoFileToLoad;
    juce::File currentVideoFile;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

