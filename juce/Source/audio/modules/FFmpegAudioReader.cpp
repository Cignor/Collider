#include "FFmpegAudioReader.h"

#if defined(_WIN32) || defined(_WIN64)
    #pragma warning(push)
    #pragma warning(disable: 4244 4996)
#endif

// Include FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#if defined(_WIN32) || defined(_WIN64)
    #pragma warning(pop)
#endif

FFmpegAudioReader::FFmpegAudioReader(const juce::String& filePath)
    : juce::AudioFormatReader(nullptr, filePath),
      filePath(filePath)
{
    // Initialize FFmpeg (if not already done)
    static bool ffmpegInitialized = false;
    if (!ffmpegInitialized)
    {
        avformat_network_init();
        ffmpegInitialized = true;
    }
    
    // Initialize member variables with safe defaults
    numChannels = 2;
    sampleRate = 44100.0;
    bitsPerSample = 32;
    lengthInSamples = 0;
    usesFloatingPointData = true;
    
    // Try to initialize and get actual audio properties
    if (initialize())
    {
        // Properties are set in initialize()
        isInitialized = true;
    }
    else
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to initialize for: " + filePath);
        cleanup();
    }
}

FFmpegAudioReader::~FFmpegAudioReader()
{
    cleanup();
}

bool FFmpegAudioReader::initialize()
{
    if (!juce::File(filePath).existsAsFile())
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] File does not exist: " + filePath);
        return false;
    }
    
    // Open input file
    formatContext = avformat_alloc_context();
    if (!formatContext)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to allocate format context");
        return false;
    }
    
    // Convert JUCE String to std::string for FFmpeg
    std::string path = filePath.toStdString();
    if (avformat_open_input(&formatContext, path.c_str(), nullptr, nullptr) < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to open input file: " + filePath);
        return false;
    }
    
    // Find stream info
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to find stream info");
        return false;
    }
    
    // Find the best audio stream
    streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] No audio stream found in file: " + filePath);
        return false;
    }
    
    audioStream = formatContext->streams[streamIndex];
    if (!audioStream)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Invalid audio stream");
        return false;
    }
    
    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Unsupported audio codec");
        return false;
    }
    
    // Allocate codec context
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to allocate codec context");
        return false;
    }
    
    // Copy codec parameters to context
    if (avcodec_parameters_to_context(codecContext, audioStream->codecpar) < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to copy codec parameters");
        return false;
    }
    
    // Open codec
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to open codec");
        return false;
    }
    
    // Get actual audio properties
    numChannels = codecContext->ch_layout.nb_channels;
    sampleRate = (double)codecContext->sample_rate;
    bitsPerSample = 32; // We convert to float
    usesFloatingPointData = true;
    
    // Calculate total duration in samples
    if (audioStream->duration != AV_NOPTS_VALUE && audioStream->time_base.num > 0)
    {
        double durationInSeconds = (double)audioStream->duration * av_q2d(audioStream->time_base);
        lengthInSamples = (juce::int64)(durationInSeconds * sampleRate);
    }
    else if (formatContext->duration != AV_NOPTS_VALUE)
    {
        double durationInSeconds = (double)formatContext->duration / AV_TIME_BASE;
        lengthInSamples = (juce::int64)(durationInSeconds * sampleRate);
    }
    else
    {
        // If we can't determine duration, set a large value
        // The reader will handle EOF gracefully
        lengthInSamples = std::numeric_limits<juce::int64>::max() / 2;
    }
    
    // Initialize resampler context (convert to 32-bit float, interleaved)
    // We preserve the original channel layout and convert to interleaved float for JUCE
    uint64_t outputChannelLayout = codecContext->ch_layout.u.mask;
    if (outputChannelLayout == 0)
    {
        // Fallback to stereo if channel layout is unknown
        outputChannelLayout = AV_CH_LAYOUT_STEREO;
    }
    
    // Allocate resampler context (modern FFmpeg API)
    resamplerContext = swr_alloc();
    if (!resamplerContext)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to allocate resampler context");
        return false;
    }
    
    // Set output parameters
    av_opt_set_int(resamplerContext, "out_channel_layout", (int64_t)outputChannelLayout, 0);
    av_opt_set_int(resamplerContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    av_opt_set_int(resamplerContext, "out_sample_rate", (int)sampleRate, 0);
    
    // Set input parameters
    av_opt_set_int(resamplerContext, "in_channel_layout", (int64_t)codecContext->ch_layout.u.mask, 0);
    av_opt_set_int(resamplerContext, "in_sample_fmt", codecContext->sample_fmt, 0);
    av_opt_set_int(resamplerContext, "in_sample_rate", codecContext->sample_rate, 0);
    
    // Initialize resampler
    if (swr_init(resamplerContext) < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to initialize resampler");
        swr_free(&resamplerContext);
        return false;
    }
    
    // Allocate frame and packet
    decodedFrame = av_frame_alloc();
    packet = av_packet_alloc();
    
    if (!decodedFrame || !packet)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Failed to allocate frame/packet");
        return false;
    }
    
    // Allocate buffers
    decodeBuffer.resize(192000 * 4); // Temporary decode buffer
    resampleBuffer.resize(192000 * 2); // Resample buffer (stereo)
    
    // Set initial position
    currentSamplePosition = 0;
    bufferedSamples = 0;
    bufferWritePos = 0;
    
    juce::Logger::writeToLog("[FFmpegAudioReader] Initialized successfully. "
                             "Channels: " + juce::String(numChannels) +
                             ", Sample Rate: " + juce::String(sampleRate) +
                             ", Duration: " + juce::String(lengthInSamples) + " samples");
    
    return true;
}

void FFmpegAudioReader::cleanup()
{
    if (resamplerContext)
    {
        swr_free(&resamplerContext);
        resamplerContext = nullptr;
    }
    
    if (decodedFrame)
    {
        av_frame_free(&decodedFrame);
    }
    
    if (packet)
    {
        av_packet_free(&packet);
    }
    
    if (codecContext)
    {
        avcodec_free_context(&codecContext);
    }
    
    if (formatContext)
    {
        avformat_close_input(&formatContext);
    }
}

bool FFmpegAudioReader::seekToSample(juce::int64 samplePosition)
{
    if (!isInitialized || !formatContext || streamIndex < 0)
        return false;
    
    // Convert sample position to time
    double timeInSeconds = static_cast<double>(samplePosition) / sampleRate;
    
    // Convert to stream time base
    int64_t timestamp = static_cast<int64_t>(timeInSeconds / av_q2d(audioStream->time_base));
    
    // Seek in the format context
    int ret = av_seek_frame(formatContext, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        juce::Logger::writeToLog("[FFmpegAudioReader] Seek failed");
        return false;
    }
    
    // Flush codec buffers
    avcodec_flush_buffers(codecContext);
    
    // Clear our internal buffers
    bufferedSamples = 0;
    bufferWritePos = 0;
    currentSamplePosition = samplePosition;
    
    return true;
}

bool FFmpegAudioReader::fillBuffer(int targetSamples)
{
    if (!isInitialized || !formatContext || !codecContext || !decodedFrame || !packet)
        return false;
    
    // Keep decoding until we have enough samples
    while (bufferedSamples < targetSamples)
    {
        // Read packet
        int ret = av_read_frame(formatContext, packet);
        if (ret < 0)
        {
            // EOF or error
            if (ret == AVERROR_EOF)
            {
                // End of file - return what we have
                break;
            }
            return false;
        }
        
        // Check if this packet belongs to our audio stream
        if (packet->stream_index != streamIndex)
        {
            av_packet_unref(packet);
            continue;
        }
        
        // Send packet to decoder
        ret = avcodec_send_packet(codecContext, packet);
        if (ret < 0)
        {
            av_packet_unref(packet);
            continue;
        }
        
        // Receive decoded frames
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(codecContext, decodedFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            if (ret < 0)
            {
                av_packet_unref(packet);
                return false;
            }
            
            // Resample the frame
            int outSampleCount = swr_get_out_samples(resamplerContext, decodedFrame->nb_samples);
            if (outSampleCount <= 0)
            {
                outSampleCount = decodedFrame->nb_samples;
            }
            
            // Ensure buffer is large enough
            const int numSourceChannels = static_cast<int>(numChannels);
            int neededSize = (bufferedSamples + outSampleCount) * numSourceChannels;
            if (neededSize > static_cast<int>(resampleBuffer.size()))
            {
                resampleBuffer.resize(static_cast<size_t>(neededSize * 2)); // Allocate extra space
            }
            
            // Resample into buffer
            float* outBuffer = resampleBuffer.data() + (bufferedSamples * numSourceChannels);
            const uint8_t** inData = (const uint8_t**)decodedFrame->data;
            
            int samplesConverted = swr_convert(
                resamplerContext,
                (uint8_t**)&outBuffer,
                outSampleCount,
                inData,
                decodedFrame->nb_samples
            );
            
            if (samplesConverted > 0)
            {
                bufferedSamples += samplesConverted;
            }
        }
        
        av_packet_unref(packet);
    }
    
    return true;
}

bool FFmpegAudioReader::readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples)
{
    if (!isInitialized)
        return false;
    
    // Seek if necessary
    if (currentSamplePosition != startSampleInFile)
    {
        if (!seekToSample(startSampleInFile))
        {
            return false;
        }
    }
    
    // Fill buffer with enough samples
    if (!fillBuffer(numSamples))
    {
        // If we can't fill, try to return what we have
        if (bufferedSamples == 0)
            return false;
    }
    
    // Copy samples from our buffer to destination
    int samplesToCopy = juce::jmin(numSamples, bufferedSamples);
    if (samplesToCopy <= 0)
        return false;
    
    // Convert from interleaved float buffer to separate channel buffers
    // JUCE expects int* but we're working with floats, so we need to handle the conversion
    const int numSourceChannels = static_cast<int>(numChannels);
    const int channelsToProcess = juce::jmin(numDestChannels, numSourceChannels);
    
    for (int ch = 0; ch < channelsToProcess; ++ch)
    {
        if (destSamples[ch] != nullptr)
        {
            int* dest = destSamples[ch] + startOffsetInDestBuffer;
            float* src = resampleBuffer.data() + (bufferWritePos * numSourceChannels) + ch;
            
            for (int i = 0; i < samplesToCopy; ++i)
            {
                // Convert float to int32 (JUCE uses 32-bit int samples)
                float sample = *src;
                // Clamp to [-1.0, 1.0] range
                sample = juce::jlimit(-1.0f, 1.0f, sample);
                // Convert to int32: multiply by 2^31
                dest[i] = static_cast<int>(sample * 2147483647.0f);
                src += numSourceChannels;
            }
        }
    }
    
    // Fill remaining channels with zeros if requested more channels than available
    for (int ch = numSourceChannels; ch < numDestChannels; ++ch)
    {
        if (destSamples[ch] != nullptr)
        {
            int* dest = destSamples[ch] + startOffsetInDestBuffer;
            juce::zeromem(dest, samplesToCopy * sizeof(int));
        }
    }
    
    // Update buffer position
    bufferWritePos += samplesToCopy;
    bufferedSamples -= samplesToCopy;
    currentSamplePosition += samplesToCopy;
    
    // Shift remaining samples in buffer to the beginning
    if (bufferedSamples > 0 && bufferWritePos > 0)
    {
        const int numSourceChannels = static_cast<int>(numChannels);
        float* src = resampleBuffer.data() + (bufferWritePos * numSourceChannels);
        float* dst = resampleBuffer.data();
        memmove(dst, src, static_cast<size_t>(bufferedSamples * numSourceChannels * sizeof(float)));
        bufferWritePos = 0;
    }
    
    return true;
}

