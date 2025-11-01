#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
    #include <opencv2/cudaimgproc.hpp>
    #include <opencv2/cudawarping.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

constexpr int HAND_NUM_KEYPOINTS = 21;

struct HandResult
{
    float keypoints[HAND_NUM_KEYPOINTS][2] = {{0}}; // [index][x/y]
    int detectedPoints = 0;
};

class HandTrackerModule : public ModuleProcessor, private juce::Thread
{
public:
    HandTrackerModule();
    ~HandTrackerModule() override;

    const juce::String getName() const override { return "hand_tracker"; }
    juce::Image getLatestFrame();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

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
    void parseHandOutput(const cv::Mat& netOutput, int frameWidth, int frameHeight, HandResult& result);
    void loadModel();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* sourceIdParam = nullptr;
    std::atomic<float>* confidenceThresholdParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr; // 0/1/2 small/normal/large
    juce::AudioParameterBool* useGpuParam = nullptr;

    cv::dnn::Net net;
    bool modelLoaded = false;

    std::atomic<juce::uint32> currentSourceId { 0 };

    HandResult lastResultForAudio;
    juce::AbstractFifo fifo { 16 };
    std::vector<HandResult> fifoBuffer;

    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};


