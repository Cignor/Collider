// juce/Source/audio/modules/HumanDetectorModule.h

#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
    #include <opencv2/cudaimgproc.hpp>
    #include <opencv2/cudaobjdetect.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

// We can reuse the same POD struct as it fits our needs perfectly.
struct DetectionResult
{
    static constexpr int maxDetections = 1; // This module only outputs the largest detection
    int numDetections = 0;
    float x[maxDetections] = {0};
    float y[maxDetections] = {0};
    float width[maxDetections] = {0};
    float height[maxDetections] = {0};
};

/**
 * Processing node that detects humans from a video source via the VideoFrameManager.
 * Requires a "Source ID" input connection from a Webcam or Video File Loader.
 */
class HumanDetectorModule : public ModuleProcessor, private juce::Thread
{
public:
    HumanDetectorModule();
    ~HumanDetectorModule() override;

    const juce::String getName() const override { return "human_detector"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    // For UI: get latest annotated frame
    juce::Image getLatestFrame();

    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    // Allow custom width (Small=240, Normal=480, Large=960)
    ImVec2 getCustomNodeSize() const override {
        int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
        level = juce::jlimit(0, 2, level);
        const float widths[3] { 240.0f, 480.0f, 960.0f };
        return ImVec2(widths[level], 0.0f);
    }
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    DetectionResult analyzeFrame(const cv::Mat& inputFrame);
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* scaleFactorParam = nullptr;
    std::atomic<float>* minNeighborsParam = nullptr;
    // 0=Small,1=Normal,2=Large
    std::atomic<float>* zoomLevelParam = nullptr;
    juce::AudioParameterBool* useGpuParam = nullptr;

    // OpenCV objects for detection (CPU)
    cv::CascadeClassifier faceCascade;
    bool faceCascadeLoaded = false; // Track if cascade was successfully loaded
    cv::HOGDescriptor hog;
    
    // OpenCV objects for detection (GPU)
    #if WITH_CUDA_SUPPORT
        cv::Ptr<cv::cuda::CascadeClassifier> faceCascadeGpu;
        cv::Ptr<cv::cuda::HOG> hogGpu;
    #endif
    
    // State for trigger generation
    int gateSamplesRemaining = 0;
    
    // Thread-safe data transfer from video thread to audio thread
    juce::AbstractFifo fifo { 16 };
    std::vector<DetectionResult> fifoBuffer;
    DetectionResult lastResultForAudio;
    
    // Current source ID (read from input pin)
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // GUI preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};

