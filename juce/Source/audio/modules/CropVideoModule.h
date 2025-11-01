#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
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
    juce::Image getLatestFrame();

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
    
    // CV Input Values (read from processBlock, used in run())
    std::atomic<juce::uint32> currentSourceId { 0 };
    std::atomic<float> cvCropX { -1.0f }; // -1 means not connected
    std::atomic<float> cvCropY { -1.0f };
    std::atomic<float> cvCropW { -1.0f };
    std::atomic<float> cvCropH { -1.0f };
    
    // UI Preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};

