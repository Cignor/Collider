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
#include <juce_graphics/juce_graphics.h>
#if defined(PRESET_CREATOR_UI)
#include <juce_gui_basics/juce_gui_basics.h>
#endif

// A real-time safe struct to hold the bounding box of the detected object.
struct ObjectDetectionResult
{
    float x = 0.0f;     // normalized center X (0..1)
    float y = 0.0f;     // normalized center Y (0..1)
    float width = 0.0f; // normalized width (0..1)
    float height = 0.0f;// normalized height (0..1)
    bool detected = false;
};

class ObjectDetectorModule : public ModuleProcessor, private juce::Thread
{
public:
    ObjectDetectorModule();
    ~ObjectDetectorModule() override;

    const juce::String getName() const override { return "object_detector"; }
    
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
    void loadModel();
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameters
    std::atomic<float>* sourceIdParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr; // 0=Small,1=Normal,2=Large
    juce::AudioParameterChoice* targetClassParam = nullptr;
    std::atomic<float>* confidenceThresholdParam = nullptr;
    juce::AudioParameterBool* useGpuParam = nullptr;
    
    // DNN
    cv::dnn::Net net;
    bool modelLoaded = false;
    std::vector<std::string> classNames;
    
    // UI-selected target class (independent of AudioParameterChoice list)
    std::atomic<int> selectedClassId { 0 };

    // Source ID (set by audio thread)
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // FIFO for communication
    ObjectDetectionResult lastResultForAudio;
    juce::AbstractFifo fifo { 16 };
    std::vector<ObjectDetectionResult> fifoBuffer;
    
    // UI preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};


