// juce/Source/audio/modules/HumanDetectorModule.cpp

#include "HumanDetectorModule.h"

// Define UI parameters
juce::AudioProcessorValueTreeState::ParameterLayout HumanDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Detection Mode", juce::StringArray{"Faces (Haar)", "Bodies (HOG)"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("scaleFactor", "Scale Factor", 1.05f, 2.0f, 1.1f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("minNeighbors", "Min Neighbors", 1, 10, 3));
    return { params.begin(), params.end() };
}

HumanDetectorModule::HumanDetectorModule()
    : OpenCVModuleProcessor<DetectionResult>("Human Detector Thread"),
      apvts(*this, nullptr, "HumanParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    scaleFactorParam = apvts.getRawParameterValue("scaleFactor");
    minNeighborsParam = apvts.getRawParameterValue("minNeighbors");

    // Load the pre-trained Haar Cascade model for face detection.
    // NOTE: This file path is relative and may need adjustment.
    // You should copy the haarcascade file to your build's output directory.
    juce::File cascadeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                .getSiblingFile("haarcascade_frontalface_default.xml");
    if (cascadeFile.existsAsFile()) {
        faceCascade.load(cascadeFile.getFullPathName().toStdString());
    } else {
        // Handle error - model not found
        juce::Logger::writeToLog("ERROR: haarcascade_frontalface_default.xml not found!");
    }
    
    // Set up the HOG descriptor to use the default people detector.
    hog.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
}

// This runs on the low-priority video thread
DetectionResult HumanDetectorModule::processFrame(const cv::Mat& inputFrame)
{
    DetectionResult result;
    cv::Mat gray;
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(320, 240));

    std::vector<cv::Rect> detections;
    int mode = (int)modeParam->load();

    if (mode == 0) // Face Detection
    {
        faceCascade.detectMultiScale(gray, detections, scaleFactorParam->load(), (int)minNeighborsParam->load());
    }
    else // Body Detection
    {
        hog.detectMultiScale(gray, detections);
    }

    if (!detections.empty())
    {
        // Find the largest detection by area
        auto largest = std::max_element(detections.begin(), detections.end(), 
            [](const cv::Rect& a, const cv::Rect& b) {
                return a.area() < b.area();
            });

        result.numDetections = 1;
        result.x[0] = juce::jmap((float)largest->x, 0.0f, 320.0f, 0.0f, 1.0f);
        result.y[0] = juce::jmap((float)largest->y, 0.0f, 240.0f, 0.0f, 1.0f);
        result.width[0] = juce::jmap((float)largest->width, 0.0f, 320.0f, 0.0f, 1.0f);
        result.height[0] = juce::jmap((float)largest->height, 0.0f, 240.0f, 0.0f, 1.0f);
    }

    return result;
}

// This runs on the real-time audio thread
void HumanDetectorModule::consumeResult(const DetectionResult& result, juce::AudioBuffer<float>& outputBuffer)
{
    if (outputBuffer.getNumChannels() < 5) return;

    if (result.numDetections > 0)
    {
        outputBuffer.setSample(0, 0, result.x[0]);        // CV Out 1: Bounding Box X
        outputBuffer.setSample(1, 0, result.y[0]);        // CV Out 2: Bounding Box Y
        outputBuffer.setSample(2, 0, result.width[0]);    // CV Out 3: Bounding Box Width
        outputBuffer.setSample(3, 0, result.height[0]);   // CV Out 4: Bounding Box Height
        gateSamplesRemaining = 2; // Keep gate high for 2 blocks to ensure it's not missed
    }

    if (gateSamplesRemaining > 0) {
        outputBuffer.setSample(4, 0, 1.0f); // Gate Out 1: Presence
        gateSamplesRemaining--;
    } else {
        outputBuffer.setSample(4, 0, 0.0f);
    }
    
    // Fill the rest of the buffer
    for (int channel = 0; channel < 5; ++channel) {
        outputBuffer.copyFrom(channel, 1, outputBuffer, channel, 0, outputBuffer.getNumSamples() - 1);
    }
}

