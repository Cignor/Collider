// juce/Source/audio/modules/HumanDetectorModule.h

#pragma once

#include "OpenCVModuleProcessor.h" // Include the base class

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

class HumanDetectorModule : public OpenCVModuleProcessor<DetectionResult>
{
public:
    HumanDetectorModule();
    ~HumanDetectorModule() override = default;

    const juce::String getName() const override { return "human_detector"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

protected:
    // Implement the pure virtual methods from the base class
    DetectionResult processFrame(const cv::Mat& inputFrame) override;
    void consumeResult(const DetectionResult& result, juce::AudioBuffer<float>& outputBuffer) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* scaleFactorParam = nullptr;
    std::atomic<float>* minNeighborsParam = nullptr;

    // OpenCV objects for detection
    cv::CascadeClassifier faceCascade;
    cv::HOGDescriptor hog;
    
    // State for trigger generation
    int gateSamplesRemaining = 0;
};

