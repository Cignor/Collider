#include "FFmpegAudioReader.h"
#include <mutex>

#if defined(_WIN32) || defined(_WIN64)
    #pragma warning(push)
    #pragma warning(disable: 4244 4996)
#endif

// Include FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#if defined(_WIN32) || defined(_WIN64)
    #pragma warning(pop)
#endif

FFmpegAudioReader::FFmpegAudioReader(const juce::String& path)
    : juce::AudioFormatReader(nullptr, "FFmpeg"), filePath(path)
{
    // Initialize FFmpeg
    static std::once_flag ffmpegInitialized;
    std::call_once(ffmpegInitialized, []() { avformat_network_init(); });

    if (avformat_open_input(&formatContext, filePath.toUTF8(), nullptr, nullptr) != 0) return;
    if (avformat_find_stream_info(formatContext, nullptr) < 0) { cleanup(); return; }

    streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) { cleanup(); return; }

    audioStream = formatContext->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) { cleanup(); return; }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) { cleanup(); return; }
    
    if (avcodec_parameters_to_context(codecContext, audioStream->codecpar) < 0) { cleanup(); return; }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) { cleanup(); return; }

    // Set up JUCE properties
    this->sampleRate = (double)codecContext->sample_rate;
    this->numChannels = (unsigned int)codecContext->ch_layout.nb_channels;
    this->bitsPerSample = 32;
    this->usesFloatingPointData = true;

    // Calculate duration
    if (audioStream->duration != AV_NOPTS_VALUE) {
        double durationInSeconds = (double)audioStream->duration * av_q2d(audioStream->time_base);
        this->lengthInSamples = (juce::int64)(durationInSeconds * this->sampleRate);
    }

    // Set up the resampler to convert any input format to 32-bit float planar for JUCE
    resamplerContext = swr_alloc();
    if (!resamplerContext) { cleanup(); return; }
    
    av_opt_set_chlayout(resamplerContext, "in_chlayout", &codecContext->ch_layout, 0);
    av_opt_set_int(resamplerContext, "in_sample_rate", codecContext->sample_rate, 0);
    av_opt_set_sample_fmt(resamplerContext, "in_sample_fmt", codecContext->sample_fmt, 0);
    av_opt_set_chlayout(resamplerContext, "out_chlayout", &codecContext->ch_layout, 0);
    av_opt_set_int(resamplerContext, "out_sample_rate", (int)this->sampleRate, 0);
    av_opt_set_sample_fmt(resamplerContext, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);  // Planar float for JUCE
    
    if (swr_init(resamplerContext) < 0) { cleanup(); return; }

    decodedFrame = av_frame_alloc();
    packet = av_packet_alloc();
    isInitialized = (decodedFrame && packet);
}

FFmpegAudioReader::~FFmpegAudioReader() {
    cleanup();
}

void FFmpegAudioReader::cleanup() {
    if (resamplerContext) swr_free(&resamplerContext);
    if (decodedFrame) av_frame_free(&decodedFrame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
}

bool FFmpegAudioReader::readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples)
{
    if (!isInitialized || numSamples <= 0) return true;

    // **THE FIX**: Cast the destination buffer to float**, as this is what JUCE provides
    // when usesFloatingPointData is true.
    auto* floatDestSamples = reinterpret_cast<float* const*>(destSamples);

    // Seek to the requested position
    int64_t targetTimestamp = (int64_t)((double)startSampleInFile / sampleRate / av_q2d(audioStream->time_base));
    if (av_seek_frame(formatContext, streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(codecContext);

    int samplesWritten = 0;
    while (samplesWritten < numSamples)
    {
        if (av_read_frame(formatContext, packet) < 0) break; // End of file

        if (packet->stream_index == streamIndex)
        {
            if (avcodec_send_packet(codecContext, packet) == 0)
            {
                while (avcodec_receive_frame(codecContext, decodedFrame) == 0)
                {
                    int maxOutSamples = (int)av_rescale_rnd(decodedFrame->nb_samples, (int)this->sampleRate, codecContext->sample_rate, AV_ROUND_UP);
                    tempResampledBuffer.setSize((int)this->numChannels, maxOutSamples);
                    
                    // Prepare output pointers for planar format (one pointer per channel)
                    uint8_t* outData[64];  // FFmpeg supports up to 64 channels
                    for (int ch = 0; ch < (int)this->numChannels; ++ch) {
                        outData[ch] = (uint8_t*)tempResampledBuffer.getWritePointer(ch);
                    }
                    
                    int samplesConverted = swr_convert(resamplerContext, outData, maxOutSamples, (const uint8_t**)decodedFrame->extended_data, decodedFrame->nb_samples);
                    
                    if (samplesConverted > 0)
                    {
                        int samplesToCopy = std::min(samplesConverted, numSamples - samplesWritten);
                        for (int ch = 0; ch < std::min((int)this->numChannels, numDestChannels); ++ch) {
                            if (floatDestSamples[ch] != nullptr) {
                                juce::FloatVectorOperations::copy(floatDestSamples[ch] + startOffsetInDestBuffer + samplesWritten,
                                                                  tempResampledBuffer.getReadPointer(ch),
                                                                  samplesToCopy);
                            }
                        }
                        samplesWritten += samplesToCopy;
                    }
                }
            }
        }
        av_packet_unref(packet);
        if (samplesWritten >= numSamples) break;
    }

    // Clear any remaining part of the destination buffer
    for (int ch = 0; ch < numDestChannels; ++ch) {
        if (floatDestSamples[ch] != nullptr) {
            juce::FloatVectorOperations::clear(floatDestSamples[ch] + startOffsetInDestBuffer + samplesWritten, numSamples - samplesWritten);
        }
    }

    return true;
}
