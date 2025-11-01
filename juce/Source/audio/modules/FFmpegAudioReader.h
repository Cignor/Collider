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

class FFmpegAudioReader final : public juce::AudioFormatReader
{
public:
    explicit FFmpegAudioReader(const juce::String& filePath);
    ~FFmpegAudioReader() override;

    // This is the correct, simpler readSamples method used by AudioFormatReaderSource
    bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override;

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

    // Buffer for resampled audio, used temporarily inside readSamples
    juce::AudioBuffer<float> tempResampledBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegAudioReader)
};
