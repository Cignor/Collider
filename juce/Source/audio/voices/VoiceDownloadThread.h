#pragma once

#include "VoiceDownloadHelper.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <queue>

/**
 * VoiceDownloadThread - Background thread for downloading Piper TTS voices.
 * 
 * Similar to SynthesisThread pattern, this handles all network I/O off the audio thread.
 * Downloads voices from HuggingFace CDN and saves them to the models directory.
 */
class VoiceDownloadThread : public juce::Thread
{
public:
    VoiceDownloadThread();
    ~VoiceDownloadThread() override;
    
    void run() override;
    
    /**
     * Queue a single voice for download.
     */
    void downloadVoice(const juce::String& voiceName);
    
    /**
     * Queue multiple voices for download (sequential).
     */
    void downloadBatch(const std::vector<juce::String>& voices);
    
    /**
     * Cancel the current download operation.
     */
    void cancelCurrentDownload();
    
    /**
     * Get current download progress (0.0 to 1.0, or -1.0 for error).
     */
    float getProgress() const { return progress.load(); }
    
    /**
     * Get the name of the voice currently being downloaded.
     */
    juce::String getCurrentVoice() const;
    
    /**
     * Get current status message.
     */
    juce::String getStatusMessage() const;
    
    /**
     * Check if a download is currently in progress.
     */
    bool isDownloading() const { return isDownloading_.load(); }
    
private:
    // Download queue (thread-safe)
    juce::AbstractFifo downloadQueue;
    std::vector<juce::String> downloadQueueBuffer;
    juce::CriticalSection queueLock;
    
    // Progress and status (thread-safe)
    std::atomic<float> progress{0.0f};
    std::atomic<bool> shouldCancel{false};
    std::atomic<bool> isDownloading_{false};
    
    juce::String currentVoice;
    juce::String statusMessage;
    juce::CriticalSection statusLock;
    
    // Base URL for Piper TTS models (HuggingFace CDN)
    static constexpr const char* BASE_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0";
    
    /**
     * Download a single voice (both .onnx and .onnx.json files).
     */
    bool downloadSingleVoice(const juce::String& voiceName);
    
    /**
     * Build the download URL for a voice file.
     */
    juce::URL buildVoiceUrl(const juce::String& voiceName, bool isOnnx);
    
    /**
     * Update status message (thread-safe).
     */
    void setStatusMessage(const juce::String& message);
};

