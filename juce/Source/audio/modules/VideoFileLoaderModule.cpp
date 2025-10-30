#include "VideoFileLoaderModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout VideoFileLoaderModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterBool>("loop", "Loop", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    return { params.begin(), params.end() };
}

VideoFileLoaderModule::VideoFileLoaderModule()
    : ModuleProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      juce::Thread("Video File Loader Thread"),
      apvts(*this, nullptr, "VideoFileLoaderParams", createParameterLayout())
{
    loopParam = apvts.getRawParameterValue("loop");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
}

VideoFileLoaderModule::~VideoFileLoaderModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFileLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
}

void VideoFileLoaderModule::releaseResources()
{
    signalThreadShouldExit();
}

void VideoFileLoaderModule::chooseVideoFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select a video file...", 
                                                       juce::File{}, 
                                                       "*.mp4;*.mov;*.avi;*.mkv;*.wmv");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            videoFileToLoad = file;
        }
    });
}

void VideoFileLoaderModule::run()
{
    bool sourceIsOpen = false;
    double videoFps = 30.0; // Default FPS
    double frameDurationMs = 33.0; // Default to ~30fps
    
    while (!threadShouldExit())
    {
        // Check if user requested a new file
        if (videoFileToLoad.existsAsFile() && videoFileToLoad != currentVideoFile)
        {
            if (videoCapture.isOpened())
            {
                videoCapture.release();
            }
            
            if (videoCapture.open(videoFileToLoad.getFullPathName().toStdString()))
            {
                currentVideoFile = videoFileToLoad;
                sourceIsOpen = true;
                
                // Get the video's native FPS
                videoFps = videoCapture.get(cv::CAP_PROP_FPS);
                if (videoFps > 0.0 && videoFps < 1000.0) // Sanity check
                {
                    frameDurationMs = 1000.0 / videoFps;
                    juce::Logger::writeToLog("[VideoFileLoader] Opened: " + currentVideoFile.getFileName() + 
                                            " (FPS: " + juce::String(videoFps, 2) + ")");
                }
                else
                {
                    frameDurationMs = 33.0; // Fallback to 30fps
                    juce::Logger::writeToLog("[VideoFileLoader] Opened: " + currentVideoFile.getFileName() + 
                                            " (FPS unknown, using 30fps)");
                }
            }
            else
            {
                juce::Logger::writeToLog("[VideoFileLoader] Failed to open: " + videoFileToLoad.getFullPathName());
                videoFileToLoad = juce::File{};
            }
        }
        
        if (!sourceIsOpen)
        {
            wait(500);
            continue;
        }
        
        // Time the frame processing to maintain correct playback speed
        auto loopStartTime = juce::Time::getMillisecondCounterHiRes();
        
        cv::Mat frame;
        if (videoCapture.read(frame))
        {
            // Publish frame to central manager
            VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
            
            // Update local preview
            updateGuiFrame(frame);
        }
        else
        {
            // End of video file
            if (loopParam->load() > 0.5f && currentVideoFile.existsAsFile())
            {
                // Loop: reopen the file
                videoCapture.release();
                if (videoCapture.open(currentVideoFile.getFullPathName().toStdString()))
                {
                    sourceIsOpen = true;
                    juce::Logger::writeToLog("[VideoFileLoader] Looping: " + currentVideoFile.getFileName());
                }
                else
                {
                    sourceIsOpen = false;
                }
            }
            else
            {
                // Stop
                sourceIsOpen = false;
                videoCapture.release();
            }
        }
        
        // Calculate how long to wait to maintain the original FPS
        auto processingTime = juce::Time::getMillisecondCounterHiRes() - loopStartTime;
        int waitTime = (int)(frameDurationMs - processingTime);
        if (waitTime > 0)
        {
            wait(waitTime);
        }
        // If processing took longer than frame duration, don't wait (play as fast as possible)
    }
    
    videoCapture.release();
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFileLoaderModule::updateGuiFrame(const cv::Mat& frame)
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

juce::Image VideoFileLoaderModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void VideoFileLoaderModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    
    // Output this module's logical ID
    if (buffer.getNumChannels() > 0)
    {
        float sourceId = (float)getLogicalId();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample(0, sample, sourceId);
        }
    }
}

juce::ValueTree VideoFileLoaderModule::getExtraStateTree() const
{
    juce::ValueTree state("VideoFileLoaderState");
    // Save the absolute path of the currently loaded video file
    if (currentVideoFile.existsAsFile())
    {
        state.setProperty("videoFilePath", currentVideoFile.getFullPathName(), nullptr);
    }
    return state;
}

void VideoFileLoaderModule::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.hasType("VideoFileLoaderState")) return;
    
    juce::String filePath = state.getProperty("videoFilePath", "").toString();
    if (filePath.isNotEmpty())
    {
        juce::File restoredFile(filePath);
        if (restoredFile.existsAsFile())
        {
            videoFileToLoad = restoredFile;
            juce::Logger::writeToLog("[VideoFileLoader] Restored video file from preset: " + filePath);
        }
        else
        {
            juce::Logger::writeToLog("[VideoFileLoader] Warning: Saved video file not found: " + filePath);
        }
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 VideoFileLoaderModule::getCustomNodeSize() const
{
    // Return different width based on zoom level (0=240,1=480,2=960)
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void VideoFileLoaderModule::drawParametersInNode(float itemWidth,
                                                 const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                 const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    if (ImGui::Button("Load Video File...", ImVec2(itemWidth, 0)))
    {
        chooseVideoFile();
    }
    
    if (currentVideoFile.existsAsFile())
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", currentVideoFile.getFileName().toRawUTF8());
    }
    else
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No file loaded");
    }
    
    bool loop = loopParam->load() > 0.5f;
    if (ImGui::Checkbox("Loop", &loop))
    {
        *dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("loop")) = loop;
        onModificationEnded();
    }
    
    ImGui::Separator();
    
    // Zoom buttons (+/-) across 3 levels
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    float buttonWidth = (itemWidth / 2.0f) - 4.0f;
    const bool atMin = (level <= 0);
    const bool atMax = (level >= 2);

    if (atMin) ImGui::BeginDisabled();
    if (ImGui::Button("-", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmax(0, level - 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMin) ImGui::EndDisabled();

    ImGui::SameLine();

    if (atMax) ImGui::BeginDisabled();
    if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmin(2, level + 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMax) ImGui::EndDisabled();
    
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID: %d", (int)getLogicalId());
    
    ImGui::PopItemWidth();
}

void VideoFileLoaderModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Source ID", 0);
}
#endif

