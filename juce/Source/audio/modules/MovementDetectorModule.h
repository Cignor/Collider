// juce/Source/audio/modules/MovementDetectorModule.h

#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/video.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
    #include <opencv2/cudaimgproc.hpp>
    #include <opencv2/cudaoptflow.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

// 1. Define the real-time safe struct for this module's results.
struct MovementResult
{
    float avgMotionX = 0.0f;     // Average horizontal motion (-1 to 1)
    float avgMotionY = 0.0f;     // Average vertical motion (-1 to 1)
    float motionAmount = 0.0f;   // Magnitude of motion or area of detected movement (0 to 1)
    bool motionTrigger = false;  // A one-shot trigger on significant motion
};

/**
 * Processing node that analyzes video from a source node via the VideoFrameManager.
 * Requires a "Source ID" input connection from a Webcam or Video File Loader.
 */
class MovementDetectorModule : public ModuleProcessor, private juce::Thread
{
public:
    MovementDetectorModule();
    ~MovementDetectorModule() override;

    const juce::String getName() const override { return "movement_detector"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    // For UI: get latest annotated frame
    juce::Image getLatestFrame();

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
    MovementResult analyzeFrame(const cv::Mat& inputFrame, juce::uint32 logicalId);
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;
    // 0=Small,1=Normal,2=Large
    std::atomic<float>* zoomLevelParam = nullptr;
    juce::AudioParameterBool* useGpuParam = nullptr;

    // NEW: Algorithm-specific tuning parameters
    juce::AudioParameterInt* maxFeaturesParam = nullptr;
    juce::AudioParameterBool* noiseReductionParam = nullptr;

    // State for Optical Flow mode
    cv::Mat prevGrayFrame;
    std::vector<cv::Point2f> prevPoints;
    int lastMaxFeatures = 0; // Track last maxFeatures value to detect parameter changes

    // State for Background Subtraction mode
    cv::Ptr<cv::BackgroundSubtractorMOG2> pBackSub;
    
    // State for trigger generation
    int triggerSamplesRemaining = 0;
    
    // Thread-safe data transfer from video thread to audio thread
    juce::AbstractFifo fifo { 16 };
    std::vector<MovementResult> fifoBuffer;
    MovementResult lastResultForAudio;
    
    // Current source ID (read from input pin)
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // GUI preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
    
    // Cached last output frame for continuous video passthrough
    cv::Mat lastOutputFrame;
    juce::CriticalSection lastOutputFrameLock;
};

