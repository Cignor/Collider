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
 * Custom audio format reader that uses FFmpeg to decode audio from video files.
 * This provides robust support for a wide range of audio codecs (AAC, AC3, etc.)
 * found in video containers (.mp4, .mkv, .mov).
 */
class FFmpegAudioReader final : public juce::AudioFormatReader
{
public:
    explicit FFmpegAudioReader(const juce::String& filePath);
    ~FFmpegAudioReader() override;

    bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override;
    
    // Reset internal position tracker - call this before seeking to ensure a seek occurs
    void resetPosition() { currentPositionInSamples = -1; }

private:
    void cleanup();

    // FFmpeg-related members
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVStream* audioStream = nullptr;
    SwrContext* resamplerContext = nullptr;
    AVFrame* decodedFrame = nullptr;
    AVPacket* packet = nullptr;
    
    int streamIndex = -1;
    juce::String filePath;
    bool isInitialized = false;
    
    // Track current position for sequential reading (avoid unnecessary seeks)
    juce::int64 currentPositionInSamples = 0;

    // A temporary buffer for holding resampled audio data before copying to JUCE's buffers
    juce::AudioBuffer<float> tempResampledBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegAudioReader)
};
