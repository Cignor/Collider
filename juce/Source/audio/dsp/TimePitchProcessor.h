// Facade exposing a unified API with two independent engines
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <rubberband/RubberBandStretcher.h>

class TimePitchProcessor
{
public:
    enum class Mode { RubberBand, Fifo };

    void setMode(Mode m) { mode = m; }

    void prepare (double sampleRate, int numChannels, int blockSize)
    {
        rb.prepare (sampleRate, numChannels, blockSize, optWindowShort, optPhaseIndependent);
        fifo.prepare (sampleRate, numChannels);
    }

    void reset()
    {
        rb.reset();
        fifo.reset();
    }

    void setTimeStretchRatio (double ratio)
    {
        rb.setTimeStretchRatio (ratio);
        fifo.setTimeStretchRatio (ratio);
    }

    void setPitchSemitones (double semis)
    {
        rb.setPitchSemitones (semis);
        fifo.setPitchSemitones (semis);
    }

    int putInterleaved (const float* inputLR, int frames)
    {
        return (mode == Mode::RubberBand) ? rb.putInterleaved (inputLR, frames)
                                          : fifo.putInterleaved (inputLR, frames);
    }

    int receiveInterleaved (float* outLR, int framesRequested)
    {
        return (mode == Mode::RubberBand) ? rb.receiveInterleaved (outLR, framesRequested)
                                          : fifo.receiveInterleaved (outLR, framesRequested);
    }

    int availableFrames() const
    {
        return (mode == Mode::RubberBand) ? rb.availableFrames() : fifo.availableFrames();
    }

    void setOptions (bool windowShort, bool phaseIndependent)
    {
        optWindowShort = windowShort; optPhaseIndependent = phaseIndependent;
    }

private:
    // RubberBand engine
    struct RubberBandEngine {
        void prepare (double sampleRate, int numChannels, int blockSize, bool windowShort, bool phaseInd)
        {
            sr = sampleRate; channels = juce::jmax (1, numChannels);
            using RB = RubberBand::RubberBandStretcher;
            int opts = RB::OptionProcessRealTime | RB::OptionPitchHighQuality | RB::OptionTransientsSmooth | RB::OptionPhaseIndependent |
                       (windowShort ? RB::OptionWindowShort : RB::OptionWindowStandard) |
                       (phaseInd ? RB::OptionPhaseIndependent : 0);
            stretcher = std::make_unique<RB>((size_t) sr, (size_t) channels, (RB::Options) opts);
            stretcher->setPitchScale(1.0); stretcher->setTimeRatio(1.0);
            if (blockSize > 0) stretcher->setMaxProcessSize ((size_t) blockSize);
            planarInput.setSize (channels, juce::jmax (1, blockSize));
            planarOutput.setSize (channels, juce::jmax (1, blockSize * 2));
        }
        void reset() { if (stretcher) stretcher->reset(); }
        void setTimeStretchRatio (double ratio) { if (stretcher) stretcher->setTimeRatio (juce::jlimit(0.25,4.0,ratio)); }
        void setPitchSemitones (double semis) { if (stretcher) stretcher->setPitchScale (std::pow(2.0, juce::jlimit(-24.0,24.0,semis)/12.0)); }
        int putInterleaved (const float* inputLR, int frames)
        {
            if (!stretcher || frames<=0 || channels<=0) return 0;
            if (planarInput.getNumSamples() < frames) planarInput.setSize (channels, frames, false, true, true);
            for (int ch = 0; ch < channels; ++ch)
            {
                float* dest = planarInput.getWritePointer (ch);
                for (int i = 0; i < frames; ++i) dest[i] = inputLR[i * channels + ch];
            }
            stretcher->process (planarInput.getArrayOfReadPointers(), (size_t) frames, false);
            return frames;
        }
        int receiveInterleaved (float* outLR, int framesRequested)
        {
            if (!stretcher || framesRequested<=0 || channels<=0) return 0;
            const size_t avail = stretcher->available();
            const int toGet = (int) juce::jmin<size_t>((size_t) framesRequested, avail);
            if (toGet<=0) return 0;
            if (planarOutput.getNumSamples() < toGet) planarOutput.setSize (channels, toGet, false, true, true);
            stretcher->retrieve (planarOutput.getArrayOfWritePointers(), (size_t) toGet);
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* src = planarOutput.getReadPointer (ch);
                for (int i = 0; i < toGet; ++i) outLR[i * channels + ch] = src[i];
            }
            return toGet;
        }
        int availableFrames() const { return stretcher ? (int) stretcher->available() : 0; }

        double sr { 48000.0 }; int channels { 2 };
        std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
        juce::AudioBuffer<float> planarInput, planarOutput;
    } rb;

    // FIFO naive engine
    struct FifoEngine {
        void prepare (double sampleRate, int numChannels) { sr = sampleRate; channels = juce::jmax(1,numChannels); reset(); }
        void reset() { fifo.clearQuick(); readFramePos = 0.0; }
        void setTimeStretchRatio (double ratio) { timeRatio = (float) juce::jlimit(0.25,4.0,ratio); }
        void setPitchSemitones (double semis) { pitchSemi = (float) juce::jlimit(-24.0,24.0,semis); }
        int putInterleaved (const float* inputLR, int frames)
        {
            const int samples = frames * channels; const int start = fifo.size();
            fifo.resize (start + samples);
            std::memcpy (fifo.getRawDataPointer() + start, inputLR, (size_t) samples * sizeof (float));
            return frames;
        }
        int receiveInterleaved (float* outLR, int framesRequested)
        {
            if (channels<=0) channels=2;
            const double pitchFactor = std::pow (2.0, (double) pitchSemi / 12.0);
            const double stepFrames  = (double) juce::jmax (0.001f, timeRatio) * pitchFactor;
            const int availableFrames = (int) (fifo.size() / channels);
            int framesWritten = 0; float* out = outLR;
            while (framesWritten < framesRequested)
            {
                const int baseIdx = (int) std::floor (readFramePos);
                if (baseIdx + 1 >= availableFrames) break;
                const double frac = readFramePos - (double) baseIdx;
                const int idx0 = baseIdx * channels; const int idx1 = (baseIdx + 1) * channels;
                for (int ch = 0; ch < channels; ++ch)
                { const float s0 = fifo[idx0 + ch]; const float s1 = fifo[idx1 + ch]; out[ch] = (float)((1.0 - frac)*s0 + frac*s1); }
                out += channels; ++framesWritten; readFramePos += stepFrames;
            }
            const int framesConsumed = (int) std::floor (readFramePos);
            if (framesConsumed > 0)
            { const int samplesConsumed = framesConsumed * channels; const int remainingSamples = (int) fifo.size() - samplesConsumed;
              if (remainingSamples > 0) std::memmove (fifo.getRawDataPointer(), fifo.getRawDataPointer() + samplesConsumed, (size_t) remainingSamples * sizeof (float));
              fifo.resize (remainingSamples); readFramePos -= (double) framesConsumed; }
            return framesWritten;
        }
        int availableFrames() const { return (int) (fifo.size() / juce::jmax(1,channels)); }

        double sr { 48000.0 }; int channels { 2 }; float timeRatio { 1.0f }; float pitchSemi { 0.0f };
        juce::Array<float> fifo; double readFramePos { 0.0 };
    } fifo;

    Mode mode { Mode::RubberBand };
    bool optWindowShort { true }; bool optPhaseIndependent { true };
};

