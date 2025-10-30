// juce/Source/audio/modules/OpenCVModuleProcessor.h

#pragma once

// CRITICAL: Always include external libraries before JUCE headers.
#include <opencv2/opencv.hpp>
#include <JuceHeader.h>

#include "ModuleProcessor.h"

/**
    An abstract base class for JUCE modules that perform OpenCV video analysis.

    This class encapsulates the necessary multi-threaded architecture to ensure
    real-time audio safety. It runs all OpenCV operations on a low-priority
    background thread and uses a lock-free FIFO to communicate results to the
    audio thread's processBlock().

    @tparam ResultStruct A simple, POD (Plain Old Data) struct used to transfer
                         analysis results from the video thread to the audio thread.
*/
template <typename ResultStruct>
class OpenCVModuleProcessor : public ModuleProcessor, private juce::Thread
{
public:
    OpenCVModuleProcessor(const juce::String& threadName)
        : ModuleProcessor(BusesProperties()), juce::Thread(threadName)
    {
        // The FIFO buffer size determines how many results can be queued.
        // A small size is fine as the audio thread only needs the latest result.
        fifoBuffer.resize(16);
        fifo.setTotalSize(16);
    }

    ~OpenCVModuleProcessor() override
    {
        // Safely signal the thread to exit and wait for it to finish.
        stopThread(5000);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        // Start the video processing thread when audio starts.
        startThread(juce::Thread::Priority::normal);
    }

    void releaseResources() override
    {
        // Signal the thread to stop when audio stops.
        signalThreadShouldExit();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        // This is the real-time audio thread.

        // 1. Check for a new result from the video thread without blocking.
        if (fifo.getNumReady() > 0)
        {
            auto readScope = fifo.read(1);
            if (readScope.blockSize1 > 0)
            {
                lastResultForAudio = fifoBuffer[readScope.startIndex1];
            }
        }

        // 2. Delegate to the subclass to turn the result into audio/CV.
        consumeResult(lastResultForAudio, buffer);
    }

    // Public method for the UI thread to get the latest video frame for display.
    juce::Image getLatestFrame()
    {
        const juce::ScopedLock lock(imageLock);
        return latestFrameForGui;
    }

protected:
    //==============================================================================
    //== PURE VIRTUAL METHODS FOR SUBCLASSES TO IMPLEMENT ==========================
    //==============================================================================

    /**
        Performs the specific OpenCV algorithm on the input frame.
        This method is called repeatedly on the low-priority video thread.
        @param inputFrame The captured video frame.
        @return A ResultStruct containing the analysis data.
    */
    virtual ResultStruct processFrame(const cv::Mat& inputFrame) = 0;

    /**
        Uses the latest analysis result to generate audio or CV signals.
        This method is called on every block by the real-time audio thread.
        @param result The latest available analysis result.
        @param outputBuffer The audio buffer to fill with CV/audio signals.
    */
    virtual void consumeResult(const ResultStruct& result, juce::AudioBuffer<float>& outputBuffer) = 0;

private:
    //==============================================================================
    //== THREADING AND DATA MARSHALLING ============================================
    //==============================================================================

    void run() override
    {
        // This is the main loop for the background video thread.
        
        // Open the default camera. Error handling should be added here.
        videoCapture.open(0);
        if (!videoCapture.isOpened())
        {
            // Handle error: camera not found.
            return;
        }

        while (!threadShouldExit())
        {
            cv::Mat frame;
            if (videoCapture.read(frame))
            {
                // 1. Perform the subclass-specific CV analysis.
                ResultStruct result = processFrame(frame);

                // 2. Push the analysis result to the lock-free FIFO for the audio thread.
                if (fifo.getFreeSpace() >= 1)
                {
                    auto writeScope = fifo.write(1);
                    if(writeScope.blockSize1 > 0)
                        fifoBuffer[writeScope.startIndex1] = result;
                }

                // 3. Convert and share the frame with the GUI thread.
                updateGuiFrame(frame);
            }
            
            // Control the frame rate to conserve CPU.
            wait(66); // ~15 FPS
        }
    }
    
    void updateGuiFrame(const cv::Mat& frame)
    {
        // Convert from OpenCV's BGR to JUCE's ARGB format.
        cv::Mat bgraFrame;
        cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);

        const juce::ScopedLock lock(imageLock); // Lock to safely access the shared image.
        if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgraFrame.cols || latestFrameForGui.getHeight() != bgraFrame.rows)
        {
            latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
        }

        juce::Image::BitmapData destData(latestFrameForGui, juce::Image::BitmapData::writeOnly);
        memcpy(destData.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
    }

    // --- Thread-safe data transfer members ---
    juce::AbstractFifo fifo { 16 };
    std::vector<ResultStruct> fifoBuffer;
    ResultStruct lastResultForAudio;

    // --- Video source ---
    cv::VideoCapture videoCapture;

    // --- GUI communication members ---
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock; // Protects latestFrameForGui
};

