// juce/Source/audio/modules/MovementDetectorModuleProcessor.h

#pragma once

#include "OpenCVModuleProcessor.h"

/**
    A simple data structure to pass motion analysis results from the video thread
    to the audio thread. This struct is small and copyable, making it ideal for
    lock-free transfer via the FIFO.
*/
struct MovementResult
{
    float motionAmount = 0.0f;     // 0.0 to 1.0: overall motion intensity
    float horizontalFlow = 0.0f;   // -1.0 to 1.0: average horizontal movement
    float verticalFlow = 0.0f;     // -1.0 to 1.0: average vertical movement
};

/**
    A concrete OpenCV module that detects movement in video and outputs CV signals
    based on the detected motion.
    
    Output Pins:
    - Pin 0: Motion Amount (0.0 to 1.0)
    - Pin 1: Horizontal Flow (-1.0 to 1.0, left to right)
    - Pin 2: Vertical Flow (-1.0 to 1.0, up to down)
*/
class MovementDetectorModuleProcessor : public OpenCVModuleProcessor<MovementResult>
{
public:
    MovementDetectorModuleProcessor()
        : OpenCVModuleProcessor<MovementResult>("MovementDetector")
    {
        // Configure the module's output channels (3 CV outputs)
        BusesProperties buses;
        buses.addBus(true, "Input", juce::AudioChannelSet::stereo(), false); // Optional audio input
        buses.addBus(false, "Output", juce::AudioChannelSet::discreteChannels(3), true);
        
        setBusesLayout(buses);
    }

    const juce::String getName() const override { return "Movement Detector"; }

protected:
    //==============================================================================
    //== OPENCV ANALYSIS (runs on background thread) ===============================
    //==============================================================================

    MovementResult processFrame(const cv::Mat& inputFrame) override
    {
        MovementResult result;

        // Convert to grayscale for optical flow calculation
        cv::Mat gray;
        cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);

        // First frame: just store it and return zero motion
        if (previousFrame.empty())
        {
            previousFrame = gray.clone();
            return result;
        }

        // Calculate dense optical flow using Farneback algorithm
        cv::Mat flow;
        cv::calcOpticalFlowFarneback(
            previousFrame, gray, flow,
            0.5,    // pyr_scale: image scale (<1) to build pyramids
            3,      // levels: number of pyramid layers
            15,     // winsize: averaging window size
            3,      // iterations: number of iterations at each pyramid level
            5,      // poly_n: size of pixel neighborhood
            1.2,    // poly_sigma: standard deviation of Gaussian for derivative
            0       // flags
        );

        // Analyze the flow field to compute our result values
        std::vector<cv::Mat> flowChannels(2);
        cv::split(flow, flowChannels);

        cv::Mat flowX = flowChannels[0];
        cv::Mat flowY = flowChannels[1];

        // Calculate motion magnitude
        cv::Mat magnitude, angle;
        cv::cartToPolar(flowX, flowY, magnitude, angle);

        // Compute average motion metrics
        cv::Scalar meanMag = cv::mean(magnitude);
        cv::Scalar meanX = cv::mean(flowX);
        cv::Scalar meanY = cv::mean(flowY);

        // Normalize and clamp results
        result.motionAmount = std::min(1.0f, static_cast<float>(meanMag[0]) / 10.0f);
        result.horizontalFlow = std::clamp(static_cast<float>(meanX[0]) / 5.0f, -1.0f, 1.0f);
        result.verticalFlow = std::clamp(static_cast<float>(meanY[0]) / 5.0f, -1.0f, 1.0f);

        // Store current frame for next iteration
        previousFrame = gray.clone();

        return result;
    }

    //==============================================================================
    //== CV SIGNAL GENERATION (runs on real-time audio thread) =====================
    //==============================================================================

    void consumeResult(const MovementResult& result, juce::AudioBuffer<float>& outputBuffer) override
    {
        // Safety check: ensure we have the expected output channels
        if (outputBuffer.getNumChannels() < 3)
            return;

        const int numSamples = outputBuffer.getNumSamples();

        // Write constant CV signals for this block
        // Channel 0: Motion Amount
        std::fill(outputBuffer.getWritePointer(0), 
                  outputBuffer.getWritePointer(0) + numSamples, 
                  result.motionAmount);

        // Channel 1: Horizontal Flow
        std::fill(outputBuffer.getWritePointer(1), 
                  outputBuffer.getWritePointer(1) + numSamples, 
                  result.horizontalFlow);

        // Channel 2: Vertical Flow
        std::fill(outputBuffer.getWritePointer(2), 
                  outputBuffer.getWritePointer(2) + numSamples, 
                  result.verticalFlow);
    }

private:
    cv::Mat previousFrame; // Store previous frame for optical flow calculation
};


