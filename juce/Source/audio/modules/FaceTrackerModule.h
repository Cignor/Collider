#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

constexpr int FACE_NUM_KEYPOINTS = 70;

struct FaceResult
{
    float keypoints[FACE_NUM_KEYPOINTS][2] = {{0}};
    int detectedPoints = 0;
};

class FaceTrackerModule : public ModuleProcessor, private juce::Thread
{
public:
    FaceTrackerModule();
    ~FaceTrackerModule() override;

    const juce::String getName() const override { return "face_tracker"; }
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
    void parseFaceOutput(const cv::Mat& netOutput, const cv::Rect& faceBox, FaceResult& result);
    void loadModel();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* sourceIdParam = nullptr;
    std::atomic<float>* confidenceThresholdParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr;

    cv::CascadeClassifier faceCascade;
    cv::dnn::Net net;
    bool modelLoaded = false;

    std::atomic<juce::uint32> currentSourceId { 0 };

    FaceResult lastResultForAudio;
    juce::AbstractFifo fifo { 16 };
    std::vector<FaceResult> fifoBuffer;

    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;
};


