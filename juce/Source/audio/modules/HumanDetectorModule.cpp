#include "HumanDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout HumanDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Detection Mode", juce::StringArray{"Faces (Haar)", "Bodies (HOG)"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("scaleFactor", "Scale Factor", 1.05f, 2.0f, 1.1f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("minNeighbors", "Min Neighbors", 1, 10, 3));
    return { params.begin(), params.end() };
}

HumanDetectorModule::HumanDetectorModule()
    : ModuleProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)),
      juce::Thread("Human Detector Analysis Thread"),
      apvts(*this, nullptr, "HumanParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    scaleFactorParam = apvts.getRawParameterValue("scaleFactor");
    minNeighborsParam = apvts.getRawParameterValue("minNeighbors");

    // Load Haar Cascade
    juce::File cascadeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                .getSiblingFile("haarcascade_frontalface_default.xml");
    if (cascadeFile.existsAsFile())
    {
        faceCascade.load(cascadeFile.getFullPathName().toStdString());
    }
    else
    {
        juce::Logger::writeToLog("ERROR: haarcascade_frontalface_default.xml not found!");
    }
    
    // Set up HOG detector
    hog.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
    
    fifoBuffer.resize(16);
    fifo.setTotalSize(16);
}

HumanDetectorModule::~HumanDetectorModule()
{
    stopThread(5000);
}

void HumanDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void HumanDetectorModule::releaseResources()
{
    signalThreadShouldExit();
}

void HumanDetectorModule::run()
{
    // Analysis loop runs on background thread
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        
        if (sourceId == 0)
        {
            // No source connected
            wait(100);
            continue;
        }
        
        // Get frame from VideoFrameManager
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        if (!frame.empty())
        {
            // Perform analysis
            DetectionResult result = analyzeFrame(frame);
            
            // Push result to FIFO for audio thread
            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                {
                    fifoBuffer[writeScope.startIndex1] = result;
                }
            }
        }
        
        wait(33); // ~30 FPS analysis rate
    }
}

DetectionResult HumanDetectorModule::analyzeFrame(const cv::Mat& inputFrame)
{
    DetectionResult result;
    cv::Mat gray, displayFrame;
    
    inputFrame.copyTo(displayFrame);
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(320, 240));
    cv::resize(displayFrame, displayFrame, cv::Size(320, 240));

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

    // Draw all detections (smaller ones in gray)
    for (const auto& detection : detections)
    {
        cv::rectangle(displayFrame, detection, cv::Scalar(128, 128, 128), 1);
    }

    if (!detections.empty())
    {
        // Find the largest detection
        auto largest = std::max_element(detections.begin(), detections.end(), 
            [](const cv::Rect& a, const cv::Rect& b) {
                return a.area() < b.area();
            });

        // Draw the largest detection
        cv::rectangle(displayFrame, *largest, cv::Scalar(0, 255, 0), 2);
        
        std::string label = (mode == 0) ? "Face" : "Person";
        cv::putText(displayFrame, label, cv::Point(largest->x, largest->y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);

        result.numDetections = 1;
        result.x[0] = juce::jmap((float)largest->x, 0.0f, 320.0f, 0.0f, 1.0f);
        result.y[0] = juce::jmap((float)largest->y, 0.0f, 240.0f, 0.0f, 1.0f);
        result.width[0] = juce::jmap((float)largest->width, 0.0f, 320.0f, 0.0f, 1.0f);
        result.height[0] = juce::jmap((float)largest->height, 0.0f, 240.0f, 0.0f, 1.0f);
    }

    updateGuiFrame(displayFrame);
    return result;
}

void HumanDetectorModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    
    const juce::ScopedLock lock(imageLock);
    
    if (latestFrameForGui.isNull() || 
        latestFrameForGui.getWidth() != bgraFrame.cols || 
        latestFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    
    juce::Image::BitmapData destData(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(destData.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image HumanDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void HumanDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Read Source ID from input pin
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
    {
        float sourceIdFloat = inputBuffer.getSample(0, 0);
        currentSourceId.store((juce::uint32)sourceIdFloat);
    }
    
    // Get latest result from analysis thread via FIFO
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
        {
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
        }
    }
    
    // Write results to output channels
    auto outputBuffer = getBusBuffer(buffer, false, 0);
    if (outputBuffer.getNumChannels() < 5) return;
    
    if (lastResultForAudio.numDetections > 0)
    {
        outputBuffer.setSample(0, 0, lastResultForAudio.x[0]);
        outputBuffer.setSample(1, 0, lastResultForAudio.y[0]);
        outputBuffer.setSample(2, 0, lastResultForAudio.width[0]);
        outputBuffer.setSample(3, 0, lastResultForAudio.height[0]);
        gateSamplesRemaining = 2; // Keep gate high
    }

    if (gateSamplesRemaining > 0)
    {
        outputBuffer.setSample(4, 0, 1.0f);
        gateSamplesRemaining--;
    }
    else
    {
        outputBuffer.setSample(4, 0, 0.0f);
    }
    
    // Fill rest of buffer
    for (int channel = 0; channel < 5; ++channel)
    {
        outputBuffer.copyFrom(channel, 1, outputBuffer, channel, 0, outputBuffer.getNumSamples() - 1);
    }
}

#if defined(PRESET_CREATOR_UI)
void HumanDetectorModule::drawParametersInNode(float itemWidth,
                                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                               const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // Mode selection
    int mode = (int)modeParam->load();
    const char* modes[] = { "Faces (Haar)", "Bodies (HOG)" };
    if (ImGui::Combo("Mode", &mode, modes, 2))
    {
        *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = mode;
        onModificationEnded();
    }
    
    // Conditional parameters for Haar mode
    if (mode == 0)
    {
        float scaleFactor = scaleFactorParam->load();
        if (ImGui::SliderFloat("Scale Factor", &scaleFactor, 1.05f, 2.0f, "%.2f"))
        {
            *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("scaleFactor")) = scaleFactor;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            onModificationEnded();
        }
        
        int minNeighbors = (int)minNeighborsParam->load();
        if (ImGui::SliderInt("Min Neighbors", &minNeighbors, 1, 10))
        {
            *dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("minNeighbors")) = minNeighbors;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            onModificationEnded();
        }
    }
    
    // Show current source ID
    juce::uint32 sourceId = currentSourceId.load();
    if (sourceId > 0)
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected to Source: %d", (int)sourceId);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No source connected");
    }
    
    ImGui::PopItemWidth();
}

void HumanDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("X", 0);
    helpers.drawAudioOutputPin("Y", 1);
    helpers.drawAudioOutputPin("Width", 2);
    helpers.drawAudioOutputPin("Height", 3);
    helpers.drawAudioOutputPin("Gate", 4);
}
#endif
