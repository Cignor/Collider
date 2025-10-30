// juce/Source/audio/modules/MovementDetectorModule.h

#pragma once

#include "OpenCVModuleProcessor.h" // Include our new base class

// 1. Define the real-time safe struct for this module's results.
struct MovementResult
{
    float avgMotionX = 0.0f;     // Average horizontal motion (-1 to 1)
    float avgMotionY = 0.0f;     // Average vertical motion (-1 to 1)
    float motionAmount = 0.0f;   // Magnitude of motion or area of detected movement (0 to 1)
    bool motionTrigger = false;  // A one-shot trigger on significant motion
};

class MovementDetectorModule : public OpenCVModuleProcessor<MovementResult>
{
public:
    MovementDetectorModule();
    ~MovementDetectorModule() override = default;

    const juce::String getName() const override { return "movement_detector"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

protected:
    // 2. Implement the pure virtual methods from the base class.
    MovementResult processFrame(const cv::Mat& inputFrame) override;
    void consumeResult(const MovementResult& result, juce::AudioBuffer<float>& outputBuffer) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;

    // State for Optical Flow mode
    cv::Mat prevGrayFrame;
    std::vector<cv::Point2f> prevPoints;

    // State for Background Subtraction mode
    cv::Ptr<cv::BackgroundSubtractorMOG2> pBackSub;
    
    // State for trigger generation
    int triggerSamplesRemaining = 0;
};

