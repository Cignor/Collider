#include "VideoFileLoaderModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/theme/ThemeManager.h"
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
    
    // Add the engine selection parameter, defaulting to Naive for performance
    params.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", juce::StringArray { "RubberBand", "Naive" }, 1));
    
    return { params.begin(), params.end() };
}

VideoFileLoaderModule::VideoFileLoaderModule()
    : ModuleProcessor(BusesProperties()
                     .withOutput("CV Out", juce::AudioChannelSet::mono(), true)
                     .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Video File Loader Thread"),
      apvts(*this, nullptr, "VideoFileLoaderParams", createParameterLayout())
{
    loopParam = apvts.getRawParameterValue("loop");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    speedParam = apvts.getRawParameterValue("speed");
    inNormParam = apvts.getRawParameterValue("in");
    outNormParam = apvts.getRawParameterValue("out");
    syncParam = apvts.getRawParameterValue("sync");
    engineParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("engine"));
    syncToTransport.store(syncParam && (*syncParam > 0.5f));
}

VideoFileLoaderModule::~VideoFileLoaderModule()
{
    stopThread(5000);
    VideoFrameManager::getInstance().removeSource(getLogicalId());
}

void VideoFileLoaderModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock(audioLock);
    audioSampleRate = sampleRate;
    timePitch.prepare(sampleRate, 2, samplesPerBlock); // Prepare for stereo
    
    // Initialize FIFO to ~5 seconds of stereo audio
    fifoSize = (int)(sampleRate * 5.0);
    audioFifo.setSize(2, fifoSize, false, true, true); // 2 channels
    abstractFifo.setTotalSize(fifoSize);
    
    startThread(juce::Thread::Priority::normal);
    // If we already had a file open previously, request re-open on thread start
    if (currentVideoFile.existsAsFile())
        videoFileToLoad = currentVideoFile;
}

void VideoFileLoaderModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
    
    const juce::ScopedLock lock(audioLock);
    timePitch.reset();
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
    // Resolve our logical ID once at the start
    juce::uint32 myLogicalId = storedLogicalId;
    if (myLogicalId == 0 && parentSynth != nullptr)
    {
        for (const auto& info : parentSynth->getModulesInfo())
        {
            if (parentSynth->getModuleForLogical(info.first) == this)
            {
                myLogicalId = info.first;
                storedLogicalId = myLogicalId; // Cache it
                break;
            }
        }
    }
    
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
                
                // Load audio from the video file
                loadAudioFromVideo();
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
        
        // --- UNIFIED SEEK LOGIC ---
        // This is now the single point of control for seeking, triggered by UI.
        float normSeekPos = pendingSeekNormalized.exchange(-1.0f);
        if (normSeekPos >= 0.0f)
        {
            const double durMs = totalDurationMs.load();
            if (durMs > 0.0)
            {
                double seekToMs = juce::jlimit(0.0, durMs, (double)normSeekPos * durMs);
                
                // Seek video
                const juce::ScopedLock capLock(captureLock);
                if (videoCapture.isOpened())
                    videoCapture.set(cv::CAP_PROP_POS_MSEC, seekToMs);
                
                // Seek audio by updating the read position - clear FIFO and reset
                const juce::ScopedLock audioLk(audioLock);
                if (audioReader) {
                    audioReadPosition = normSeekPos * audioReader->lengthInSamples;
                    currentAudioSamplePosition.store((juce::int64)audioReadPosition); // SET THE MASTER CLOCK
                    audioReader->resetPosition(); // Force seek on next read
                    abstractFifo.reset(); // Clear FIFO on seek
                    timePitch.reset(); // Reset time stretcher state after seek
                }
            }
            // Fallback to ratio seek if duration is not known
            else
            {
                const juce::ScopedLock capLock(captureLock);
                if (videoCapture.isOpened())
                    videoCapture.set(cv::CAP_PROP_POS_AVI_RATIO, (double)normSeekPos);
                
                // Seek audio by ratio - clear FIFO and reset
                const juce::ScopedLock audioLk(audioLock);
                if (audioReader) {
                    audioReadPosition = normSeekPos * audioReader->lengthInSamples;
                    currentAudioSamplePosition.store((juce::int64)audioReadPosition); // SET THE MASTER CLOCK
                    audioReader->resetPosition(); // Force seek on next read
                    abstractFifo.reset(); // Clear FIFO on seek
                    timePitch.reset();
                }
            }
            needPreviewFrame.store(true);
        }

        // Legacy frame-based seek support (for compatibility)
        int seekTo = pendingSeekFrame.exchange(-1);
        if (seekTo >= 0)
        {
            const juce::ScopedLock capLock(captureLock);
            if (videoCapture.isOpened())
                videoCapture.set(cv::CAP_PROP_POS_FRAMES, (double) seekTo);
            needPreviewFrame.store(true);
            
            // Also seek audio - clear FIFO and reset position
            const juce::ScopedLock audioLk(audioLock);
            if (audioReader) {
                int tfLocal = totalFrames.load();
                if (tfLocal > 1) {
                    float normPos = (float)seekTo / (float)(tfLocal - 1);
                    audioReadPosition = normPos * audioReader->lengthInSamples;
                    currentAudioSamplePosition.store((juce::int64)audioReadPosition); // SET THE MASTER CLOCK
                    audioReader->resetPosition(); // Force seek on next read
                    abstractFifo.reset(); // Clear FIFO on seek
                    timePitch.reset();
                }
            }
        }

        // On play edge: seek to IN point using unified seek
        bool nowPlaying = playing.load();
        bool wasPlaying = lastPlaying.exchange(nowPlaying);
        if (nowPlaying && !wasPlaying)
        {
            float inN = inNormParam ? inNormParam->load() : 0.0f;
            pendingSeekNormalized.store(inN); // Use unified seek
            needPreviewFrame.store(true);
            
            // Also seek audio - clear FIFO and reset position
            const juce::ScopedLock audioLk(audioLock);
            if (audioReader) {
                audioReadPosition = inN * audioReader->lengthInSamples;
                currentAudioSamplePosition.store((juce::int64)audioReadPosition); // SET THE MASTER CLOCK
                audioReader->resetPosition(); // Force seek on next read
                abstractFifo.reset(); // Clear FIFO on seek
                timePitch.reset();
            }
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
                        if (myLogicalId != 0)
                            VideoFrameManager::getInstance().setFrame(myLogicalId, preview);
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

        // Get current UI settings for this iteration
        const bool isLoopingEnabled = loopParam && loopParam->load() > 0.5f;
        const float startNormalized = inNormParam ? inNormParam->load() : 0.0f;
        const float endNormalized = outNormParam ? outNormParam->load() : 1.0f;
        
        // ==============================================================================
        //  SECTION 1: AUDIO DECODER (The "Producer")
        //  This thread's only job is to decode audio and keep the FIFO buffer full,
        //  respecting the start/end points and looping its read position.
        // ==============================================================================
        if (playing.load() && audioLoaded.load() && audioReader && abstractFifo.getFreeSpace() > 8192)
        {
            const juce::ScopedLock audioLk(audioLock);
            if (audioReader) 
            {
                const juce::int64 startSample = (juce::int64)(startNormalized * audioReader->lengthInSamples);
                juce::int64 endSample = (juce::int64)(endNormalized * audioReader->lengthInSamples);
                if (endSample <= startSample) endSample = audioReader->lengthInSamples;
                
                // If the decoder's read-head is past the end point, loop it back to the start.
                if (audioReadPosition >= endSample) {
                    if (isLoopingEnabled) {
                        audioReadPosition = (double)startSample;
                    }
                }
                
                // If the read-head is within the valid range, decode a chunk of audio.
                if (audioReadPosition < endSample) {
                    const int samplesToRead = 4096;
                    const int numSamplesAvailable = (int)(endSample - audioReadPosition);
                    const int numSamplesToReadNow = juce::jmin(samplesToRead, numSamplesAvailable);
                    
                    if (numSamplesToReadNow > 0) {
                        juce::AudioBuffer<float> tempReadBuffer(2, numSamplesToReadNow);
                        float* channelPointers[] = { tempReadBuffer.getWritePointer(0), tempReadBuffer.getWritePointer(1) };
                        if (audioReader->readSamples(reinterpret_cast<int* const*>(channelPointers), 2, 0, (juce::int64)audioReadPosition, numSamplesToReadNow)) {
                            audioReadPosition += numSamplesToReadNow;
                            
                            // Write decoded audio into the FIFO buffer for the audio thread
                            int s1, z1, s2, z2;
                            abstractFifo.prepareToWrite(numSamplesToReadNow, s1, z1, s2, z2);
                            if (z1 > 0) audioFifo.copyFrom(0, s1, tempReadBuffer, 0, 0, z1);
                            if (z1 > 0) audioFifo.copyFrom(1, s1, tempReadBuffer, 1, 0, z1);
                            if (z2 > 0) audioFifo.copyFrom(0, s2, tempReadBuffer, 0, z1, z2);
                            if (z2 > 0) audioFifo.copyFrom(1, s2, tempReadBuffer, 1, z1, z2);
                            abstractFifo.finishedWrite(z1 + z2);
                        }
                    }
                }
            }
        }
        
        // ==============================================================================
        //  SECTION 2: VIDEO DISPLAY (The "Slave")
        //  This thread syncs the video to the master audio clock and handles the actual loop event.
        // ==============================================================================
        if (playing.load() && sourceIsOpen && totalFrames.load() > 1 && videoFps > 0)
        {
            // Get the current time from the master audio clock
            const juce::int64 audioMasterPosition = currentAudioSamplePosition.load();
            const double sourceRate = sourceAudioSampleRate.load();
            const double currentTimeInSeconds = (double)audioMasterPosition / sourceRate;
            
            // Calculate the target video frame that corresponds to the audio time
            int targetVideoFrame = (int)(currentTimeInSeconds * videoFps);
            
            const int totalVideoFrames = totalFrames.load();
            const int startFrame = (int)(startNormalized * totalVideoFrames);
            const int endFrame = (int)(endNormalized * totalVideoFrames);
            
            // **THE CRITICAL FIX**: Check if the MASTER AUDIO CLOCK has passed the end point.
            if (targetVideoFrame >= endFrame)
            {
                if (isLoopingEnabled)
                {
                    // The loop is triggered HERE. Atomically reset all playback state for both audio and video.
                    const juce::ScopedLock audioLk(audioLock);
                    const juce::ScopedLock capLock(captureLock);
                    
                    const juce::int64 startSample = (juce::int64)(startNormalized * audioReader->lengthInSamples);
                    
                    // Reset master clock to the start position
                    currentAudioSamplePosition.store(startSample); 
                    // Reset audio decoder's read position
                    audioReadPosition = (double)startSample; 
                    // Reset video to the start frame
                    if (videoCapture.isOpened()) videoCapture.set(cv::CAP_PROP_POS_FRAMES, startFrame); 
                    // Flush all buffers
                    abstractFifo.reset(); 
                    timePitch.reset(); 
                    
                    // The new target is now the start frame
                    targetVideoFrame = startFrame; 
                }
                else 
                {
                    // Not looping, so stop playback
                    playing.store(false);
                }
            }
            
            // With the correct target frame determined, seek the video capture directly to that frame.
            if (playing.load() && videoCapture.isOpened())
            {
                const juce::ScopedLock capLock(captureLock);
                int currentVideoFrame = (int)videoCapture.get(cv::CAP_PROP_POS_FRAMES);
                
                // Only update the video if the target frame is different from the current one.
                if (currentVideoFrame != targetVideoFrame) {
                    videoCapture.set(cv::CAP_PROP_POS_FRAMES, targetVideoFrame);
                }
                
                // Read and display the single, correct frame for this point in time.
                cv::Mat frame;
                if (videoCapture.read(frame)) {
                    if (myLogicalId != 0)
                        VideoFrameManager::getInstance().setFrame(myLogicalId, frame);
                    updateGuiFrame(frame);
                    lastPosFrame.store((int)videoCapture.get(cv::CAP_PROP_POS_FRAMES));
                    if (lastFourcc.load() == 0)
                        lastFourcc.store((int)videoCapture.get(cv::CAP_PROP_FOURCC));
                }
            }
        }
        
        // A short sleep to prevent this thread from consuming 100% CPU
        wait(5);
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

void VideoFileLoaderModule::loadAudioFromVideo()
{
    const juce::ScopedLock lock(audioLock);
    
    audioReader.reset();
    timePitch.reset(); // Reset the processing engine
    audioReadPosition = 0.0;
    currentAudioSamplePosition.store(0); // Reset master clock
    abstractFifo.reset(); // Clear FIFO
    audioLoaded.store(false);
    
    if (!currentVideoFile.existsAsFile()) return;
    
    try
    {
        audioReader = std::make_unique<FFmpegAudioReader>(currentVideoFile.getFullPathName());
        
        if (audioReader != nullptr && audioReader->lengthInSamples > 0)
        {
            audioLoaded.store(true);
            sourceAudioSampleRate.store(audioReader->sampleRate); // Store the source sample rate
            juce::Logger::writeToLog("[VideoFileLoader] Audio loaded via FFmpeg.");
        }
        else
        {
            juce::Logger::writeToLog("[VideoFileLoader] Could not extract audio stream via FFmpeg.");
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[VideoFileLoader] Exception loading audio: " + juce::String(e.what()));
    }
}

void VideoFileLoaderModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    auto audioOutBus = getBusBuffer(buffer, false, 1);
    
    cvOutBus.clear();
    audioOutBus.clear();
    
    // --- BEGIN FIX ---
    // Find our own ID if it's not set
    juce::uint32 myLogicalId = storedLogicalId;
    if (myLogicalId == 0 && parentSynth != nullptr)
    {
        for (const auto& info : parentSynth->getModulesInfo())
        {
            if (parentSynth->getModuleForLogical(info.first) == this)
            {
                myLogicalId = info.first;
                storedLogicalId = myLogicalId; // Cache it
                break;
            }
        }
    }
    // --- END FIX ---
    
    // Output Source ID on CV bus
    if (cvOutBus.getNumChannels() > 0)
    {
        float sourceId = (float)myLogicalId; // Use the correct ID
        cvOutBus.setSample(0, 0, sourceId);
        for (int ch = 0; ch < cvOutBus.getNumChannels(); ++ch)
            cvOutBus.copyFrom(ch, 1, cvOutBus, ch, 0, cvOutBus.getNumSamples() - 1);
    }
    
    // Return early if not playing
    if (!audioLoaded.load() || !playing.load())
    {
        return;
    }
    
    // --- NEW FIFO-BASED AUDIO PROCESSING ---
    const juce::ScopedLock lock(audioLock);
    const int numSamples = audioOutBus.getNumSamples();
    
    // 1. Configure the time-stretcher
    const float speed = juce::jlimit(0.25f, 4.0f, speedParam ? speedParam->load() : 1.0f);
    const int engineIdx = engineParam ? engineParam->getIndex() : 1;
    auto requestedMode = (engineIdx == 0) ? TimePitchProcessor::Mode::RubberBand : TimePitchProcessor::Mode::Fifo;
    
    timePitch.setMode(requestedMode);
    
    // THIS IS THE FIX: Calculate the correct ratio based on which engine is active.
    double ratioForEngine = (double)speed;
    if (requestedMode == TimePitchProcessor::Mode::RubberBand)
    {
        // RubberBand expects a time-stretch ratio, which is the inverse of playback speed.
        // A speed of 2.0x requires a stretch ratio of 0.5 to play twice as fast.
        ratioForEngine = 1.0 / juce::jmax(0.01, (double)speed);
    }
    
    // The Naive engine expects a direct playback speed, so `ratioForEngine` is unchanged.
    timePitch.setTimeStretchRatio(ratioForEngine);
    
    // 2. Determine how many source frames we need to pull from the FIFO
    const int framesToReadFromFifo = (int)std::ceil((double)numSamples * speed);
    
    if (abstractFifo.getNumReady() >= framesToReadFromFifo)
    {
        // Use a temporary buffer for interleaving
        juce::AudioBuffer<float> interleavedBuffer(1, framesToReadFromFifo * 2);
        float* interleavedData = interleavedBuffer.getWritePointer(0);
        
        // 3. Read from FIFO and interleave the data
        int start1, size1, start2, size2;
        abstractFifo.prepareToRead(framesToReadFromFifo, start1, size1, start2, size2);
        
        const float* fifoDataL = audioFifo.getReadPointer(0);
        const float* fifoDataR = audioFifo.getReadPointer(1);
        
        for (int i = 0; i < size1; ++i) {
            interleavedData[2 * i] = fifoDataL[start1 + i];
            interleavedData[2 * i + 1] = fifoDataR[start1 + i];
        }
        if (size2 > 0) {
            for (int i = 0; i < size2; ++i) {
                interleavedData[2 * (size1 + i)] = fifoDataL[start2 + i];
                interleavedData[2 * (size1 + i) + 1] = fifoDataR[start2 + i];
            }
        }
        abstractFifo.finishedRead(size1 + size2);
        const int readCount = size1 + size2; // How many samples we actually read from FIFO
        
        // 4. Feed the time-stretcher
        timePitch.putInterleaved(interleavedData, framesToReadFromFifo);
        
        // 5. Retrieve processed audio
        juce::AudioBuffer<float> tempOutput(1, numSamples * 2);
        int produced = timePitch.receiveInterleaved(tempOutput.getWritePointer(0), numSamples);
        
        // 6. De-interleave into the output bus
        if (produced > 0)
        {
            const float* processedData = tempOutput.getReadPointer(0);
            for (int ch = 0; ch < audioOutBus.getNumChannels(); ++ch)
            {
                float* dest = audioOutBus.getWritePointer(ch);
                for (int i = 0; i < produced; ++i)
                {
                    dest[i] = (ch == 0) ? processedData[i * 2] : processedData[i * 2 + 1];
                }
            }
            
            // THIS IS THE FIX: Advance the master clock by the number of samples consumed
            currentAudioSamplePosition.store(currentAudioSamplePosition.load() + readCount);
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
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    ImGui::PushItemWidth(itemWidth);
    
    if (ImGui::Button("Load Video File...", ImVec2(itemWidth, 0)))
    {
        chooseVideoFile();
    }
    
    if (currentVideoFile.existsAsFile())
    {
        const juce::String fileName = currentVideoFile.getFileName();
        ThemeText(fileName.toRawUTF8(), theme.text.success);
    }
    else
    {
        ThemeText("No file loaded", theme.text.disabled);
    }
    
    bool loop = loopParam->load() > 0.5f;
    if (ImGui::Checkbox("Loop", &loop))
    {
        *dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("loop")) = loop;
        onModificationEnded();
    }
    
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
    
    const juce::String sourceIdText = juce::String::formatted("Source ID: %d", (int)getLogicalId());
    ThemeText(sourceIdText.toRawUTF8(), theme.text.section_header);
    {
        int fcc = lastFourcc.load();
        juce::String codec = fourccToString(fcc);
        juce::String friendly = fourccFriendlyName(codec);
        juce::String ext = currentVideoFile.getFileExtension();
        if (ext.startsWithChar('.')) ext = ext.substring(1);
        if (ext.isEmpty())
            ext = "unknown";

        juce::String codecLine = "Codec: " + codec + " (" + friendly + ")   Container: " + ext;
        ThemeText(codecLine.toRawUTF8(), theme.text.active);
        if (totalFrames.load() <= 1)
            ThemeText("Length unknown yet (ratio seeks)", theme.text.warning);
    }

    // ADD ENGINE SELECTION COMBO BOX
    int engineIdx = engineParam ? engineParam->getIndex() : 1;
    const char* items[] = { "RubberBand (High Quality)", "Naive (Low CPU)" };
    if (ImGui::Combo("Engine", &engineIdx, items, 2))
    {
        if (engineParam) *engineParam = engineIdx;
        onModificationEnded();
    }

    // --- Playback speed (slider only) ---
    float spd = speedParam ? speedParam->load() : 1.0f;
        if (ImGui::SliderFloat("Speed", &spd, 0.25f, 4.0f, "%.2fx"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("speed"))) *p = spd;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

    // --- Trim / Timeline --- (always visible, never greyed)
    {
        const int tf = juce::jmax(1, totalFrames.load());
        float inN = inNormParam ? inNormParam->load() : 0.0f;
        float outN = outNormParam ? outNormParam->load() : 1.0f;
        
        if (ImGui::SliderFloat("Start", &inN, 0.0f, 1.0f, "%.3f"))
        {
            inN = juce::jlimit(0.0f, outN - 0.01f, inN);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("in"))) *p = inN;
            pendingSeekNormalized.store(inN); // Use unified seek
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

        if (ImGui::SliderFloat("End", &outN, 0.0f, 1.0f, "%.3f"))
        {
            outN = juce::jlimit(inN + 0.01f, 1.0f, outN);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("out"))) *p = outN;
            onModificationEnded();
        }

        float pos = (tf > 1) ? ((float)lastPosFrame.load() / (float)tf) : 0.0f;
        // Clamp position UI between in/out
        float minPos = juce::jlimit(0.0f, 1.0f, inN);
        float maxPos = juce::jlimit(minPos, 1.0f, outN);
        pos = juce::jlimit(minPos, maxPos, pos);

        if (ImGui::SliderFloat("Position", &pos, 0.0f, 1.0f, "%.3f"))
        {
            pendingSeekNormalized.store(pos); // Use unified seek
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    }
    
    ImGui::PopItemWidth();
}

void VideoFileLoaderModule::drawIoPins(const NodePinHelpers& helpers)
{
    // Although getDynamicOutputPins takes precedence, this ensures correctness if it's ever used as a fallback.
    helpers.drawAudioOutputPin("Source ID", 0);
    helpers.drawAudioOutputPin("Audio L", 1);
    helpers.drawAudioOutputPin("Audio R", 2);
}
#endif

std::vector<DynamicPinInfo> VideoFileLoaderModule::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // CV Output: Bus 0, Channel 0 (mono - contains logical ID for video routing)
    pins.push_back({ "Source ID", 0, PinDataType::Video });
    
    // Audio Outputs: Bus 1, Channels 0-1 (stereo)
    // Note: Channel indices are absolute, so bus 1 channel 0 = absolute channel 1
    // (bus 0 has 1 channel, so bus 1 starts at absolute index 1)
    int bus1StartChannel = 1; // After bus 0's 1 channel
    pins.push_back({ "Audio L", bus1StartChannel + 0, PinDataType::Audio });
    pins.push_back({ "Audio R", bus1StartChannel + 1, PinDataType::Audio });
    
    return pins;
}

