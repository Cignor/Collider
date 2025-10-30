#include "MovementDetectorModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MovementDetectorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray{"Optical Flow", "Background Subtraction"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Sensitivity", 0.01f, 1.0f, 0.1f));
    return { params.begin(), params.end() };
}

MovementDetectorModule::MovementDetectorModule()
    : ModuleProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("Output", juce::AudioChannelSet::discreteChannels(4), true)),
      juce::Thread("Movement Detector Analysis Thread"),
      apvts(*this, nullptr, "MovementParams", createParameterLayout())
{
    modeParam = apvts.getRawParameterValue("mode");
    sensitivityParam = apvts.getRawParameterValue("sensitivity");
    pBackSub = cv::createBackgroundSubtractorMOG2();
    
    fifoBuffer.resize(16);
    fifo.setTotalSize(16);
}

MovementDetectorModule::~MovementDetectorModule()
{
    stopThread(5000);
}

void MovementDetectorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void MovementDetectorModule::releaseResources()
{
    signalThreadShouldExit();
}

void MovementDetectorModule::run()
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
            MovementResult result = analyzeFrame(frame);
            
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

MovementResult MovementDetectorModule::analyzeFrame(const cv::Mat& inputFrame)
{
    MovementResult result;
    cv::Mat gray, displayFrame;
    
    inputFrame.copyTo(displayFrame);
    cv::cvtColor(inputFrame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(320, 240));
    cv::resize(displayFrame, displayFrame, cv::Size(320, 240));

    int mode = (int)modeParam->load();

    if (mode == 0) // Optical Flow
    {
        if (prevPoints.size() < 50)
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
            for (size_t i = 0; i < prevPoints.size(); ++i)
            {
                if (status[i])
                {
                    cv::line(displayFrame, prevPoints[i], nextPoints[i], cv::Scalar(0, 255, 0), 1);
                    cv::circle(displayFrame, nextPoints[i], 2, cv::Scalar(255, 0, 0), -1);
                    
                    sumX += nextPoints[i].x - prevPoints[i].x;
                    sumY += nextPoints[i].y - prevPoints[i].y;
                    trackedCount++;
                }
            }

            if (trackedCount > 0)
            {
                result.avgMotionX = juce::jlimit(-1.0f, 1.0f, sumX / (trackedCount * 10.0f));
                result.avgMotionY = juce::jlimit(-1.0f, 1.0f, sumY / (trackedCount * 10.0f));
                result.motionAmount = juce::jlimit(0.0f, 1.0f, std::sqrt(result.avgMotionX * result.avgMotionX + result.avgMotionY * result.avgMotionY));
                
                if (result.motionAmount > sensitivityParam->load())
                {
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
        
        cv::Moments m = cv::moments(fgMask, true);
        result.motionAmount = juce::jlimit(0.0f, 1.0f, (float)(m.m00 / (320 * 240)));
        
        if (result.motionAmount > 0.001)
        {
             result.avgMotionX = juce::jmap((float)(m.m10 / m.m00), 0.0f, 320.0f, -1.0f, 1.0f);
             result.avgMotionY = juce::jmap((float)(m.m01 / m.m00), 0.0f, 240.0f, -1.0f, 1.0f);
             
             cv::Point2f centroid(m.m10 / m.m00, m.m01 / m.m00);
             cv::circle(displayFrame, centroid, 5, cv::Scalar(0, 255, 0), -1);
             cv::circle(displayFrame, centroid, 8, cv::Scalar(255, 255, 255), 2);
        }
        
        cv::Mat fgMaskColor;
        cv::cvtColor(fgMask, fgMaskColor, cv::COLOR_GRAY2BGR);
        cv::addWeighted(displayFrame, 0.7, fgMaskColor, 0.3, 0, displayFrame);
        
        if (result.motionAmount > sensitivityParam->load())
        {
            result.motionTrigger = true;
        }
    }

    updateGuiFrame(displayFrame);
    return result;
}

void MovementDetectorModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image MovementDetectorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void MovementDetectorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    if (outputBuffer.getNumChannels() < 4) return;
    
    outputBuffer.setSample(0, 0, lastResultForAudio.avgMotionX);
    outputBuffer.setSample(1, 0, lastResultForAudio.avgMotionY);
    outputBuffer.setSample(2, 0, lastResultForAudio.motionAmount);

    // Handle trigger
    if (lastResultForAudio.motionTrigger)
    {
        triggerSamplesRemaining = (int)(getSampleRate() * 0.01);
        lastResultForAudio.motionTrigger = false; // Clear to avoid repeating
    }

    if (triggerSamplesRemaining > 0)
    {
        outputBuffer.setSample(3, 0, 1.0f);
        triggerSamplesRemaining--;
    }
    else
    {
        outputBuffer.setSample(3, 0, 0.0f);
    }
    
    // Fill rest of buffer
    for (int channel = 0; channel < 4; ++channel)
    {
        outputBuffer.copyFrom(channel, 1, outputBuffer, channel, 0, outputBuffer.getNumSamples() - 1);
    }
}

#if defined(PRESET_CREATOR_UI)
void MovementDetectorModule::drawParametersInNode(float itemWidth,
                                                  const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                  const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // Mode selection
    int mode = (int)modeParam->load();
    const char* modes[] = { "Optical Flow", "Background Subtraction" };
    if (ImGui::Combo("Mode", &mode, modes, 2))
    {
        *dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")) = mode;
        onModificationEnded();
    }
    
    // Sensitivity slider
    float sensitivity = sensitivityParam->load();
    if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.01f, 1.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("sensitivity")) = sensitivity;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        onModificationEnded();
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

void MovementDetectorModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Motion X", 0);
    helpers.drawAudioOutputPin("Motion Y", 1);
    helpers.drawAudioOutputPin("Amount", 2);
    helpers.drawAudioOutputPin("Trigger", 3);
}
#endif
