#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

/**
 * Source node that captures from a webcam and publishes frames to VideoFrameManager.
 * Outputs its own logical ID as a CV signal for routing to processing nodes.
 */
class WebcamLoaderModule : public ModuleProcessor, private juce::Thread
{
public:
    WebcamLoaderModule();
    ~WebcamLoaderModule() override;

    const juce::String getName() const override { return "webcam_loader"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // For UI: get latest frame for preview
    juce::Image getLatestFrame();

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
    
    std::atomic<float>* cameraIndexParam = nullptr;
    // 0 = Small (240), 1 = Normal (480), 2 = Large (960)
    std::atomic<float>* zoomLevelParam = nullptr;
    
    cv::VideoCapture videoCapture;
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};

