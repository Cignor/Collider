#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#if defined(PRESET_CREATOR_UI)
#include <juce_gui_basics/juce_gui_basics.h>
#endif

class CropVideoModule : public ModuleProcessor, private juce::Thread
{
public:
    CropVideoModule();
    ~CropVideoModule() override;

    const juce::String getName() const override { return "crop_video"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    juce::Image getLatestFrame(); // This will return the CROPPED output frame for chaining previews

    // Override to declare modulatable parameters
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    ImVec2 getCustomNodeSize() const override;
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    void updateInputGuiFrame(const cv::Mat& frame); // NEW: For the uncropped preview
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameters
    std::atomic<float>* zoomLevelParam = nullptr;
    std::atomic<float>* paddingParam = nullptr;
    juce::AudioParameterChoice* aspectRatioModeParam = nullptr;
    std::atomic<float>* cropXParam = nullptr;
    std::atomic<float>* cropYParam = nullptr;
    std::atomic<float>* cropWParam = nullptr;
    std::atomic<float>* cropHParam = nullptr;
    
    // CV Input Value
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // Tracking State
    std::atomic<bool> trackingActive { false };
    cv::Ptr<cv::Tracker> tracker;
    juce::CriticalSection trackerLock;
    cv::Mat lastFrameForTracker;
    
    // UI Previews
    juce::Image latestInputFrameForGui;  // NEW: Holds the uncropped input for drawing
    juce::Image latestOutputFrameForGui; // RENAMED: Holds the cropped output
    juce::CriticalSection imageLock;
};

