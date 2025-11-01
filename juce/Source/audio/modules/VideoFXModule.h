#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#if defined(WITH_CUDA_SUPPORT)
    #include <opencv2/core/cuda.hpp>
#endif

/**
 * A "Swiss Army knife" video processing node.
 * Takes a source ID as input, applies a chain of effects, and outputs a new
 * source ID for the processed video stream, allowing for effect chaining.
 */
class VideoFXModule : public ModuleProcessor, private juce::Thread
{
public:
    VideoFXModule();
    ~VideoFXModule() override;

    const juce::String getName() const override { return "video_fx"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    juce::Image getLatestFrame();
    
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

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
    juce::AudioParameterBool* useGpuParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr;
    
    // Color Adjustments
    std::atomic<float>* brightnessParam = nullptr; // -100 to 100
    std::atomic<float>* contrastParam = nullptr;   // 0.0 to 3.0
    std::atomic<float>* saturationParam = nullptr; // 0.0 to 3.0
    std::atomic<float>* hueShiftParam = nullptr;   // -180 to 180 degrees
    std::atomic<float>* gainRedParam = nullptr;
    std::atomic<float>* gainGreenParam = nullptr;
    std::atomic<float>* gainBlueParam = nullptr;
    juce::AudioParameterBool* sepiaParam = nullptr;
    std::atomic<float>* temperatureParam = nullptr; // -1.0 to 1.0

    // Filters & Effects
    std::atomic<float>* sharpenParam = nullptr;    // 0.0 to 2.0
    std::atomic<float>* blurParam = nullptr;       // 0 to 20
    juce::AudioParameterBool* grayscaleParam = nullptr;
    juce::AudioParameterBool* invertParam = nullptr;
    juce::AudioParameterBool* flipHorizontalParam = nullptr;
    juce::AudioParameterBool* flipVerticalParam = nullptr;
    
    // Threshold Effect
    juce::AudioParameterBool* thresholdEnableParam = nullptr;
    std::atomic<float>* thresholdLevelParam = nullptr; // 0 to 255
    
    // New Effects
    juce::AudioParameterInt* posterizeLevelsParam = nullptr; // 2 to 16
    std::atomic<float>* vignetteAmountParam = nullptr; // 0.0 to 1.0
    std::atomic<float>* vignetteSizeParam = nullptr; // 0.1 to 2.0
    juce::AudioParameterInt* pixelateBlockSizeParam = nullptr; // 2 to 64
    juce::AudioParameterBool* cannyEnableParam = nullptr;
    std::atomic<float>* cannyThresh1Param = nullptr; // 0 to 255
    std::atomic<float>* cannyThresh2Param = nullptr; // 0 to 255
    juce::AudioParameterChoice* kaleidoscopeModeParam = nullptr; // None, 4-Way, 8-Way

    // Source ID (read from input pin)
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // UI Preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};

