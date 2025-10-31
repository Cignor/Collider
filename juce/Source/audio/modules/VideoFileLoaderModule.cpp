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
    params.push_back(std::make_unique<juce::AudioParameterBool>("sync", "Sync to Transport", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    // Playback controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed", "Speed", juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "in", "Start", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "out", "End", 0.0f, 1.0f, 1.0f));
    
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
    speedParam = apvts.getRawParameterValue("speed");
    inNormParam = apvts.getRawParameterValue("in");
    outNormParam = apvts.getRawParameterValue("out");
    syncParam = apvts.getRawParameterValue("sync");
    syncToTransport.store(syncParam && (*syncParam > 0.5f));
}

VideoFileLoaderModule::~VideoFileLoaderModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFileLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startThread(juce::Thread::Priority::normal);
    // If we already had a file open previously, request re-open on thread start
    if (currentVideoFile.existsAsFile())
        videoFileToLoad = currentVideoFile;
}

void VideoFileLoaderModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
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
    // One-time OpenCV build summary: detect if FFMPEG is integrated
    {
        static std::atomic<bool> buildInfoLogged { false };
        if (!buildInfoLogged.exchange(true))
        {
            juce::String info(cv::getBuildInformation().c_str());
            bool ffmpegYes = false;
            juce::String ffmpegLine;
            {
                juce::StringArray lines;
                lines.addLines(info);
                for (const auto& ln : lines)
                {
                    if (ln.containsIgnoreCase("FFMPEG:"))
                    {
                        ffmpegLine = ln.trim();
                        if (ln.containsIgnoreCase("YES")) ffmpegYes = true;
                        break;
                    }
                }
            }
            juce::Logger::writeToLog("[OpenCV Build] FFMPEG integrated: " + juce::String(ffmpegYes ? "YES" : "NO") +
                                     (ffmpegLine.isNotEmpty() ? juce::String(" | ") + ffmpegLine : juce::String()));
        }
    }

    bool sourceIsOpen = false;
    double videoFps = 30.0; // Default FPS
    double frameDurationMs = 33.0; // Default to ~30fps
    
    while (!threadShouldExit())
    {
        // Check if user requested a new file OR we need to (re)open the same file after restart
        if (videoFileToLoad.existsAsFile() && (!videoCapture.isOpened() || videoFileToLoad != currentVideoFile))
        {
            if (videoCapture.isOpened())
            {
                videoCapture.release();
            }
            
            bool opened = videoCapture.open(videoFileToLoad.getFullPathName().toStdString(), cv::CAP_FFMPEG);
            if (!opened)
            {
                juce::Logger::writeToLog("[VideoFileLoader] FFmpeg backend open failed, retrying default backend: " + videoFileToLoad.getFullPathName());
                opened = videoCapture.open(videoFileToLoad.getFullPathName().toStdString());
            }
            if (opened)
            {
                currentVideoFile = videoFileToLoad;
                videoFileToLoad = juce::File{}; // Clear request immediately after processing
                sourceIsOpen = true;
                needPreviewFrame.store(true);
                // Log backend name (helps diagnose FFmpeg vs MSMF at runtime)
               #if (CV_VERSION_MAJOR >= 4)
                juce::String backendName = videoCapture.getBackendName().c_str();
                juce::Logger::writeToLog("[VideoFileLoader] Backend: " + backendName);
               #endif
                // Reset state for new media, but keep user-defined in/out ranges
                totalFrames.store(0); // force re-evaluation
                lastPosFrame.store(0);
                pendingSeekFrame.store(0);
                
                // Get the video's native FPS and codec
                videoFps = videoCapture.get(cv::CAP_PROP_FPS);
                lastFourcc.store((int) videoCapture.get(cv::CAP_PROP_FOURCC));
                {
                    // Backend and raw FOURCC diagnostics
                    #if CV_VERSION_MAJOR >= 4
                    const std::string backendName = videoCapture.getBackendName();
                    juce::Logger::writeToLog("[VideoFileLoader] Backend: " + juce::String(backendName.c_str()));
                    #endif
                    const int fourccRaw = lastFourcc.load();
                    juce::Logger::writeToLog("[VideoFileLoader] Metadata: FPS=" + juce::String(videoFps,2) +
                                             ", Raw FOURCC=" + juce::String(fourccRaw) +
                                             " ('" + fourccToString(fourccRaw) + "')");
                }
                {
                    int tf = (int) videoCapture.get(cv::CAP_PROP_FRAME_COUNT);
                    if (tf <= 1) tf = 0; // treat unknown/invalid as 0 so UI uses normalized seeks
                    totalFrames.store(tf);
                    juce::Logger::writeToLog("[VideoFileLoader] Opened '" + currentVideoFile.getFileName() + "' frames=" + juce::String(tf) +
                                             ", fps=" + juce::String(videoFps,2) + ", fourcc='" + fourccToString(lastFourcc.load()) + "'");
                    if (tf > 1 && videoFps > 0.0)
                        totalDurationMs.store((double)tf * (1000.0 / videoFps));
                    else
                        totalDurationMs.store(0.0);
                }
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
        
        // Apply any pending seeks (UI-driven) before processing
        int tfLocal = totalFrames.load();
        {
            // 1) If we have a normalized pending seek, prefer time-based seek when duration is known
            float pendingNorm = pendingSeekNorm.exchange(-1.0f);
            if (pendingNorm >= 0.0f)
            {
                const double durMs = totalDurationMs.load();
                if (durMs > 0.0)
                {
                    double seekToMs = juce::jlimit(0.0, durMs, (double)pendingNorm * durMs);
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        videoCapture.set(cv::CAP_PROP_POS_MSEC, seekToMs);
                    needPreviewFrame.store(true);
                    juce::Logger::writeToLog("[VideoFileLoader][Seek] Applied normalized seek via MSEC=" + juce::String(seekToMs));
                }
                else if (tfLocal > 1)
                {
                    int toFrame = (int) std::round(juce::jlimit(0.0f, 1.0f, pendingNorm) * (tfLocal - 1));
                    pendingSeekFrame.store(toFrame);
                }
                else
                {
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        videoCapture.set(cv::CAP_PROP_POS_AVI_RATIO, juce::jlimit(0.0, 1.0, (double) pendingNorm));
                    needPreviewFrame.store(true);
                    juce::Logger::writeToLog("[VideoFileLoader][Seek] WARNING ratio seek");
                }
            }

            // 2) If we have a normalized pending start and frames are now known, convert and seek
            float pendingStart = pendingStartNorm.exchange(-1.0f);
            if (pendingStart >= 0.0f)
            {
                const double durMs = totalDurationMs.load();
                if (durMs > 0.0)
                {
                    double seekToMs = juce::jlimit(0.0, durMs, (double)pendingStart * durMs);
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        videoCapture.set(cv::CAP_PROP_POS_MSEC, seekToMs);
                    needPreviewFrame.store(true);
                    juce::Logger::writeToLog("[VideoFileLoader][Seek] Applied normalized start via MSEC=" + juce::String(seekToMs));
                }
                else if (tfLocal > 1)
                {
                    int inF = (int) std::round(juce::jlimit(0.0f, 1.0f, pendingStart) * (tfLocal - 1));
                    pendingSeekFrame.store(inF);
                }
                else
                {
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        videoCapture.set(cv::CAP_PROP_POS_AVI_RATIO, juce::jlimit(0.0, 1.0, (double) pendingStart));
                    needPreviewFrame.store(true);
                    juce::Logger::writeToLog("[VideoFileLoader][Seek] Applied normalized start ratio=" + juce::String(pendingStart, 3));
                }
            }
        }

        // On play edge: seek to IN point
        bool nowPlaying = playing.load();
        bool wasPlaying = lastPlaying.exchange(nowPlaying);
        if (nowPlaying && !wasPlaying)
        {
            float inN = inNormParam ? inNormParam->load() : 0.0f;
            const double durMs = totalDurationMs.load();
            if (durMs > 0.0)
            {
                double seekToMs = juce::jlimit(0.0, durMs, (double)inN * durMs);
                const juce::ScopedLock capLock(captureLock);
                if (videoCapture.isOpened())
                    videoCapture.set(cv::CAP_PROP_POS_MSEC, seekToMs);
                juce::Logger::writeToLog("[VideoFileLoader][PlayEdge] Seeking to in(ms)=" + juce::String(seekToMs));
            }
            else if (tfLocal > 1)
            {
                int inF = juce::jlimit(0, juce::jmax(1, tfLocal) - 1, (int) std::round(inN * (tfLocal - 1)));
                pendingSeekFrame.store(inF);
                juce::Logger::writeToLog("[VideoFileLoader][PlayEdge] Seeking to inF=" + juce::String(inF));
            }
            else
            {
                pendingSeekNorm.store(juce::jlimit(0.0f, 1.0f, inN));
                juce::Logger::writeToLog("[VideoFileLoader][PlayEdge] Seeking to inNorm=" + juce::String(inN, 3));
            }
            needPreviewFrame.store(true);
        }

        int seekTo = pendingSeekFrame.exchange(-1);
        if (seekTo >= 0)
        {
            const juce::ScopedLock capLock(captureLock);
            if (videoCapture.isOpened())
                videoCapture.set(cv::CAP_PROP_POS_FRAMES, (double) seekTo);
            needPreviewFrame.store(true);
            juce::Logger::writeToLog("[VideoFileLoader][Seek] Applied pending seek to frame=" + juce::String(seekTo));
        }

        // Respect play/pause
        if (!playing.load())
        {
            // If paused, show a single preview frame after (re)open
            if (needPreviewFrame.exchange(false))
            {
                cv::Mat preview;
                if (videoCapture.isOpened())
                {
                    videoCapture.read(preview);
                }
                {
                    if (!preview.empty())
                    {
                        VideoFrameManager::getInstance().setFrame(getLogicalId(), preview);
                        updateGuiFrame(preview);
                        juce::Logger::writeToLog("[VideoFileLoader][Preview] Published paused preview frame");
                        lastPosFrame.store((int) videoCapture.get(cv::CAP_PROP_POS_FRAMES));
                        if (lastFourcc.load() == 0)
                            lastFourcc.store((int) videoCapture.get(cv::CAP_PROP_FOURCC));
                        // Also try to refresh total frames after the first paused read
                        if (totalFrames.load() <= 1)
                        {
                            int tf = (int) videoCapture.get(cv::CAP_PROP_FRAME_COUNT);
                            if (tf > 1)
                            {
                                totalFrames.store(tf);
                                juce::Logger::writeToLog("[VideoFileLoader] Frame count acquired after paused read: " + juce::String(tf));
                            }
                        }
                    }
                }
            }
            wait(40);
            continue;
        }

        // Time the frame processing to maintain correct playback speed
        auto loopStartTime = juce::Time::getMillisecondCounterHiRes();
        
        cv::Mat frame;
        if (videoCapture.isOpened())
            videoCapture.read(frame);
        if (!frame.empty())
        {
            // Publish frame to central manager
            VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
            
            // Update local preview
            updateGuiFrame(frame);
            lastPosFrame.store((int) videoCapture.get(cv::CAP_PROP_POS_FRAMES));
            if (lastFourcc.load() == 0)
                lastFourcc.store((int) videoCapture.get(cv::CAP_PROP_FOURCC));
            // If frame count was unknown at open time, refresh it after first (or any) read
            if (totalFrames.load() <= 1)
            {
                int tf = (int) videoCapture.get(cv::CAP_PROP_FRAME_COUNT);
                if (tf > 1)
                {
                    totalFrames.store(tf);
                    if (videoFps > 0.0)
                        totalDurationMs.store((double)tf * (1000.0 / videoFps));
                    juce::Logger::writeToLog("[VideoFileLoader] Frame count acquired after playing read: " + juce::String(tf));
                }
            }

            // Trim window enforcement (only when frame count available)
            if (totalFrames.load() > 1)
            {
                int pos = 0;
                {
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        pos = (int) videoCapture.get(cv::CAP_PROP_POS_FRAMES);
                }
                float inN = inNormParam ? inNormParam->load() : 0.0f;
                float outN = outNormParam ? outNormParam->load() : 1.0f;
                inN = juce::jlimit(0.0f, 1.0f, inN);
                outN = juce::jlimit(0.0f, 1.0f, outN);
                if (outN <= inN) outN = juce::jmin(1.0f, inN + 0.01f);
                const int tfLocal = totalFrames.load();
                int inF = juce::jlimit(0, juce::jmax(0, tfLocal - 1), (int)std::round(inN * (tfLocal - 1)));
                int outF = juce::jlimit(inF + 1, juce::jmax(1, tfLocal), (int)std::round(outN * (tfLocal - 1)));
                if (pos < inF)
                {
                    juce::Logger::writeToLog("[VideoFileLoader][Trim] pos=" + juce::String(pos) + " < inF=" + juce::String(inF) + " -> seeking to inF");
                    const juce::ScopedLock capLock(captureLock);
                    if (videoCapture.isOpened())
                        videoCapture.set(cv::CAP_PROP_POS_FRAMES, (double)inF);
                }
                else if (pos >= outF)
                {
                    if (loopParam && loopParam->load() > 0.5f)
                    {
                        juce::Logger::writeToLog("[VideoFileLoader][Trim] pos>=outF looping to inF=" + juce::String(inF));
                        const juce::ScopedLock capLock(captureLock);
                        if (videoCapture.isOpened())
                            videoCapture.set(cv::CAP_PROP_POS_FRAMES, (double)inF);
                    }
                    else
                    {
                        juce::Logger::writeToLog("[VideoFileLoader][Trim] pos>=outF stopping playback");
                        playing.store(false);
                    }
                }
            }
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
        float spd = speedParam ? speedParam->load() : 1.0f;
        spd = juce::jlimit(0.1f, 4.0f, spd);
        int waitTime = (int)((frameDurationMs / spd) - processingTime);
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
    
    // Transport sync and play/stop controls
    bool sync = syncParam ? (*syncParam > 0.5f) : true;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        syncToTransport.store(sync);
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("sync"))) *p = sync;
        if (sync)
        {
            playing.store(lastTransportPlaying.load());
            // Ensure thread will (re)open current file if needed after transport resumes
            if (currentVideoFile.existsAsFile())
                videoFileToLoad = currentVideoFile;
        }
    }

    ImGui::SameLine();
    bool localPlaying = playing.load();
    if (sync) ImGui::BeginDisabled();
    const char* btn = localPlaying ? "Stop" : "Play";
    if (sync)
    {
        // Mirror transport state in label when synced
        btn = lastTransportPlaying.load() ? "Stop" : "Play";
    }
    if (ImGui::Button(btn))
    {
        playing.store(!localPlaying);
    }
    if (sync) ImGui::EndDisabled();

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
    {
        int fcc = lastFourcc.load();
        juce::String codec = fourccToString(fcc);
        juce::String friendly = fourccFriendlyName(codec);
        juce::String ext = currentVideoFile.getFileExtension();
        if (ext.startsWithChar('.')) ext = ext.substring(1);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Codec: %s (%s)   Container: %s",
                           codec.toRawUTF8(), friendly.toRawUTF8(), (ext.isEmpty() ? "unknown" : ext.toRawUTF8()));
        if (totalFrames.load() <= 1)
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Length unknown yet (ratio seeks)");
    }

    // --- Playback speed (slider only) ---
    float spd = speedParam ? speedParam->load() : 1.0f;
    ImGui::Separator();
    ImGui::SliderFloat("Speed", &spd, 0.25f, 4.0f, "%.2fx");
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("speed"))) *p = spd;

    // --- Trim / Timeline --- (always visible, never greyed)
    {
        const int tf = juce::jmax(1, totalFrames.load());
        float inN = inNormParam ? inNormParam->load() : 0.0f;
        float outN = outNormParam ? outNormParam->load() : 1.0f;
        ImGui::Separator();
        if (ImGui::SliderFloat("Start", &inN, 0.0f, 1.0f))
        {
            inN = juce::jlimit(0.0f, outN - 0.01f, inN);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("in"))) *p = inN;
            if (tf > 1)
            {
                int inF = (int)std::round(inN * (tf - 1));
                pendingSeekFrame.store(inF);
                needPreviewFrame.store(true);
                juce::Logger::writeToLog("[VideoFileLoader][UI] Start set: inN=" + juce::String(inN, 3) + " inF=" + juce::String(inF));
            }
            else
            {
                pendingStartNorm.store(inN);
            }
        }
        if (ImGui::SliderFloat("End", &outN, 0.0f, 1.0f))
        {
            outN = juce::jlimit(inN + 0.01f, 1.0f, outN);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("out"))) *p = outN;
            if (tf > 1)
            {
                int outF = (int)std::round(outN * (tf - 1));
                juce::Logger::writeToLog("[VideoFileLoader][UI] End set: outN=" + juce::String(outN, 3) + " outF=" + juce::String(outF));
            }
        }

        int currentFrame = lastPosFrame.load();
        float pos = (tf > 1) ? ((float) currentFrame / (float) tf) : 0.0f;
        // Clamp position UI between in/out
        float minPos = juce::jlimit(0.0f, 1.0f, inN);
        float maxPos = juce::jlimit(minPos, 1.0f, outN);
        pos = juce::jlimit(minPos, maxPos, pos);
        if (ImGui::SliderFloat("Position", &pos, 0.0f, 1.0f))
        {
            if (tf > 1)
            {
                int targetF = (int)std::round(pos * (float)(tf - 1));
                pendingSeekFrame.store(targetF);
                needPreviewFrame.store(true);
                juce::Logger::writeToLog("[VideoFileLoader][UI] Position set: posN=" + juce::String(pos, 3) + " frame=" + juce::String(targetF));
            }
            else
            {
                pendingSeekNorm.store(pos);
            }
        }
    }
    
    ImGui::PopItemWidth();
}

void VideoFileLoaderModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Source ID", 0);
}
#endif

