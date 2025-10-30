// juce/Source/audio/modules/MovementDetectorModule.cpp

#include "MovementDetectorModule.h"

// Define UI parameters
juce::AudioProcessorValueTreeState::ParameterLayout MovementDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray{"Optical Flow", "Background Subtraction"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Sensitivity", 0.01f, 1.0f, 0.1f));
    return { params.begin(), params.end() };
}

MovementDetectorModule::MovementDetectorModule()
    : OpenCVModuleProcessor<MovementResult>("Movement Detector Thread"),
      apvts(*this, nullptr, "MovementParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    sensitivityParam = apvts.getRawParameterValue("sensitivity");
    pBackSub = cv::createBackgroundSubtractorMOG2();
}

// This runs on the low-priority video thread
MovementResult MovementDetectorModule::processFrame(const cv::Mat& inputFrame)
{
    MovementResult result;
    cv::Mat gray;
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(320, 240)); // Analyze at lower resolution for performance

    int mode = (int)modeParam->load();

    if (mode == 0) // Optical Flow
    {
        if (prevPoints.size() < 50) // If we lose too many points, find new ones
        {
            cv::goodFeaturesToTrack(gray, prevPoints, 100, 0.3, 7);
        }

        if (!prevGrayFrame.empty() && !prevPoints.empty())
        {
            std::vector<cv::Point2f> nextPoints;
            std::vector<uchar> status;
            cv::calcOpticalFlowPyrLK(prevGrayFrame, gray, prevPoints, nextPoints, status, cv::noArray());

            float sumX = 0.0f, sumY = 0.0f;
            int trackedCount = 0;
            for (size_t i = 0; i < prevPoints.size(); ++i) {
                if (status[i]) {
                    sumX += nextPoints[i].x - prevPoints[i].x;
                    sumY += nextPoints[i].y - prevPoints[i].y;
                    trackedCount++;
                }
            }

            if (trackedCount > 0) {
                result.avgMotionX = juce::jlimit(-1.0f, 1.0f, sumX / (trackedCount * 10.0f)); // Normalize
                result.avgMotionY = juce::jlimit(-1.0f, 1.0f, sumY / (trackedCount * 10.0f));
                result.motionAmount = juce::jlimit(0.0f, 1.0f, std::sqrt(result.avgMotionX * result.avgMotionX + result.avgMotionY * result.avgMotionY));
                
                if (result.motionAmount > sensitivityParam->load()) {
                    result.motionTrigger = true;
                }
            }
            prevPoints = nextPoints;
        }
        gray.copyTo(prevGrayFrame);
    }
    else // Background Subtraction
    {
        cv::Mat fgMask;
        pBackSub->apply(gray, fgMask);
        
        // Use image moments to find the center of motion
        cv::Moments m = cv::moments(fgMask, true);
        result.motionAmount = juce::jlimit(0.0f, 1.0f, (float)(m.m00 / (320 * 240))); // Normalized area
        
        if (result.motionAmount > 0.001) {
             result.avgMotionX = juce::jmap((float)(m.m10 / m.m00), 0.0f, 320.0f, -1.0f, 1.0f); // Centroid X
             result.avgMotionY = juce::jmap((float)(m.m01 / m.m00), 0.0f, 240.0f, -1.0f, 1.0f); // Centroid Y
        }
        
        if (result.motionAmount > sensitivityParam->load()) {
            result.motionTrigger = true;
        }
    }

    return result;
}

// This runs on the real-time audio thread
void MovementDetectorModule::consumeResult(const MovementResult& result, juce::AudioBuffer<float>& outputBuffer)
{
    // Ensure the output buffer has enough channels
    if (outputBuffer.getNumChannels() < 4) return;
    
    // Map results to output channels
    outputBuffer.setSample(0, 0, result.avgMotionX);   // CV Out 1: Motion X
    outputBuffer.setSample(1, 0, result.avgMotionY);   // CV Out 2: Motion Y
    outputBuffer.setSample(2, 0, result.motionAmount); // CV Out 3: Motion Amount

    // Handle the one-shot trigger
    if (result.motionTrigger) {
        triggerSamplesRemaining = (int)(getSampleRate() * 0.01); // 10ms trigger pulse
    }

    if (triggerSamplesRemaining > 0) {
        outputBuffer.setSample(3, 0, 1.0f); // Gate Out 1: Motion Trigger
        triggerSamplesRemaining--;
    } else {
        outputBuffer.setSample(3, 0, 0.0f);
    }
    
    // Fill the rest of the buffer with the same values
    for (int channel = 0; channel < 4; ++channel) {
        outputBuffer.copyFrom(channel, 1, outputBuffer, channel, 0, outputBuffer.getNumSamples() - 1);
    }
}

