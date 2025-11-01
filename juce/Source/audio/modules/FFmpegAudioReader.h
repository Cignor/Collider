#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Forward-declare FFmpeg structs to keep headers clean
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwrContext;

/**
 * Custom audio format reader that uses FFmpeg libraries to decode audio
 * from video files. Supports virtually all audio codecs that FFmpeg can decode.
 */
class FFmpegAudioReader : public juce::AudioFormatReader
{
public:
    /**
     * Creates an FFmpeg audio reader from a file path.
     * @param filePath Path to the video file
     */
    explicit FFmpegAudioReader(const juce::String& filePath);
    
    /**
     * Destructor - cleans up FFmpeg resources
     */
    ~FFmpegAudioReader() override;

    /**
     * Reads audio samples from the source file.
     * @param destSamples Array of pointers to destination sample buffers (one per channel)
     * @param numDestChannels Number of destination channels
     * @param startOffsetInDestBuffer Starting offset in destination buffer
     * @param startSampleInFile Starting sample position in the source file
     * @param numSamples Number of samples to read
     * @return true if successful, false otherwise
     */
    bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override;

private:
    /**
     * Initializes FFmpeg and opens the file
     * @return true if successful
     */
    bool initialize();
    
    /**
     * Cleans up FFmpeg resources
     */
    void cleanup();
    
    /**
     * Seeks to the specified sample position
     * @param samplePosition Target sample position
     * @return true if successful
     */
    bool seekToSample(juce::int64 samplePosition);
    
    /**
     * Reads and decodes packets until we have enough samples in the buffer
     * @param targetSamples Number of samples needed
     * @return true if successful
     */
    bool fillBuffer(int targetSamples);
    
    // FFmpeg-related members
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVStream* audioStream = nullptr;
    SwrContext* resamplerContext = nullptr;
    AVFrame* decodedFrame = nullptr;
    AVPacket* packet = nullptr;
    
    int streamIndex = -1;
    juce::String filePath;
    
    // Audio buffer for decoded samples (before resampling)
    std::vector<uint8_t> decodeBuffer;
    std::vector<float> resampleBuffer;
    juce::int64 currentSamplePosition = 0;
    
    // Buffer management
    std::vector<float> interleavedBuffer;
    int bufferedSamples = 0;
    int bufferWritePos = 0;
    
    bool isInitialized = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegAudioReader)
};

