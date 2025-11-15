#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/video.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
    #include <opencv2/cudaimgproc.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#if defined(PRESET_CREATOR_UI)
#include <juce_gui_basics/juce_gui_basics.h>
#endif

struct ContourResult
{
    float area = 0.0f;
    float complexity = 0.0f;
    float aspectRatio = 0.0f;
    bool zoneHits[4] = {false, false, false, false};  // Zone hit detection results
};

class ContourDetectorModule : public ModuleProcessor, private juce::Thread
{
public:
    ContourDetectorModule();
    ~ContourDetectorModule() override;

    const juce::String getName() const override { return "contour_detector"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    juce::Image getLatestFrame();

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
    
    std::atomic<float>* sourceIdParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    juce::AudioParameterBool* noiseReductionParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr;
    juce::AudioParameterBool* useGpuParam = nullptr;
    
    // Zone rectangles structure: each color zone can have multiple rectangles
    struct ZoneRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };
    
    // Helper functions to serialize/deserialize zone rectangles
    static juce::String serializeZoneRects(const std::vector<ZoneRect>& rects);
    static std::vector<ZoneRect> deserializeZoneRects(const juce::String& data);
    void loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const;
    void saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects);
    
    cv::Ptr<cv::BackgroundSubtractor> backSub;
    
    std::atomic<juce::uint32> currentSourceId { 0 };
    ContourResult lastResultForAudio;
    juce::AbstractFifo fifo { 16 };
    std::vector<ContourResult> fifoBuffer;
    
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};


