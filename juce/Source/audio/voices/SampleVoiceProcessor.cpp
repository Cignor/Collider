#include "SampleVoiceProcessor.h"
#include "../../utils/RtLogger.h"

SampleVoiceProcessor::SampleVoiceProcessor(std::shared_ptr<SampleBank::Sample> sampleToPlay)
    : sourceSample(std::move(sampleToPlay))
{
}

void SampleVoiceProcessor::prepareToPlay(double rate, int samplesPerBlock)
{
    // Prepare base FX chain, then set sample-rate specific state
    VoiceProcessor::prepareToPlay (rate, samplesPerBlock);
    juce::Logger::writeToLog("[SampleVoice] prepareToPlay sr=" + juce::String(rate) + ", block=" + juce::String(samplesPerBlock));
    outputSampleRate = rate;
    // readPosition is set by reset() or setPlaybackRange() + reset(), not here

    // Always run stretcher in stereo; duplicate mono content upstream
    timePitch.prepare (rate, 2, samplesPerBlock);
    interleavedCapacityFrames = samplesPerBlock;
    interleavedInput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
    interleavedOutput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
    // Reset smoothing state
    smoothedTimeRatio = 1.0f;
    smoothedPitchSemis = 0.0f;
    smoothingBlocksRemainingTime = 0;
    smoothingBlocksRemainingPitch = 0;

    // Defaults per current preferred settings
    setSmoothingEnabled (true);
    setSmoothingTimeMs (100.0f, 100.0f);
    setSmoothingAlpha (0.4f, 0.4f);
    setSmoothingMaxBlocks (1, 1);
    setSmoothingSnapThresholds (0.5f, 3.0f);
    setSmoothingResetPolicy (true, true);
}

void SampleVoiceProcessor::renderBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    // buffer.clear(); // REMOVED: This was wiping out the audio before processing

    if (sourceSample == nullptr || sourceSample->stereo.getNumSamples() < 2 || outputSampleRate <= 0.0)
        return;

    const int numDestSamples = buffer.getNumSamples();
    auto& sourceBuffer = sourceSample->stereo;
    const int sourceLength = sourceBuffer.getNumSamples();

    // Apply UI smoothing toggle atomically at audio rate
    smoothingEnabled = requestedSmoothingEnabled.load(std::memory_order_relaxed);

    // Parameters to stretcher
    const float apTime = apvts.getRawParameterValue("timeStretchRatio") ? apvts.getRawParameterValue("timeStretchRatio")->load() : 1.0f;
    const float apPitch = apvts.getRawParameterValue("pitchSemitones") ? apvts.getRawParameterValue("pitchSemitones")->load() : 0.0f;
    // Treat effectiveTime as a unified SPEED multiplier (2.0 = faster, 0.5 = slower)
    const float effectiveTime = juce::jlimit(0.25f, 4.0f, apTime * zoneTimeStretchRatio);
    const float effectivePitchSemis = basePitchSemitones + apPitch;

    // If smoothing is disabled, apply parameters immediately
    if (!smoothingEnabled)
    { smoothedTimeRatio = effectiveTime; smoothedPitchSemis = effectivePitchSemis; if (resetOnChangeWhenNoSmoothing) timePitch.reset(); }
    else
    { smoothedTimeRatio = effectiveTime; smoothedPitchSemis = effectivePitchSemis; }

    timePitch.setTimeStretchRatio(smoothedTimeRatio);
    timePitch.setPitchSemitones(smoothedPitchSemis);

    // Branch engines cleanly: Naive vs RubberBand
    if (engine.load(std::memory_order_relaxed) == Engine::Naive)
    {
        auto* destL = buffer.getWritePointer(0);
        auto* destR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : destL;
        auto* srcL = sourceBuffer.getReadPointer(0);
        auto* srcR = sourceBuffer.getNumChannels() > 1 ? sourceBuffer.getReadPointer(1) : srcL;
        const double pitchScale = std::pow(2.0, (double) effectivePitchSemis / 12.0);
        // Naive engine: interpret effectiveTime as SPEED (not duration ratio).
        // Higher speed => larger step per frame (faster playback).
        const double step = pitchScale * (double) juce::jmax(0.0001f, effectiveTime);
        const double effectiveEndSample = (endSamplePos < 0.0 || endSamplePos >= sourceLength) ? (double)sourceLength - 1 : endSamplePos;
        for (int i = 0; i < numDestSamples; ++i)
        {
            int base = (int) readPosition;
            if (readPosition >= effectiveEndSample)
            {
                if (isLooping) 
                {
                    readPosition = startSamplePos + (readPosition - effectiveEndSample);
                    base = (int)readPosition;
                }
                else 
                { 
                    isPlaying = false;
                    buffer.clear(i, numDestSamples - i); 
                    break; 
                }
            }
            const int next = juce::jmin(sourceLength - 1, base + 1);
            const float frac = (float) (readPosition - (double) base);
            const float l = srcL[base] + frac * (srcL[next] - srcL[base]);
            const float r = srcR[base] + frac * (srcR[next] - srcR[base]);
            destL[i] = l; if (destR) destR[i] = r; readPosition += step;
        }
        return;
    }

    // Ensure interleaved buffers large enough
    if (numDestSamples > interleavedCapacityFrames)
    {
        interleavedCapacityFrames = numDestSamples;
        interleavedInput.allocate((size_t)(interleavedCapacityFrames * 2), true);
        interleavedOutput.allocate((size_t)(interleavedCapacityFrames * 2), true);
    }

    float* inLR = interleavedInput.getData();
    auto* srcL = sourceBuffer.getReadPointer(0);
    auto* srcR = sourceBuffer.getNumChannels() > 1 ? sourceBuffer.getReadPointer(1) : srcL;

    // RubberBand path: feed contiguous raw frames equal to output block size
    const double effectiveEndSample = (endSamplePos < 0.0 || endSamplePos >= sourceLength) ? (double)sourceLength - 1 : endSamplePos;
    int framesFed = 0;
    for (int i = 0; i < numDestSamples; ++i)
    {
        int pos = (int) readPosition;
        if (readPosition >= effectiveEndSample)
        {
            if (isLooping)
            {
                readPosition = startSamplePos + (readPosition - effectiveEndSample);
                pos = (int)readPosition;
            }
            else 
            {
                isPlaying = false;
                break;
            }
        }
        inLR[2 * i + 0] = srcL[pos];
        inLR[2 * i + 1] = srcR[pos];
        readPosition += 1.0;
        framesFed++;
    }

    if (framesFed > 0)
        timePitch.putInterleaved(inLR, framesFed);
    float* outLR = interleavedOutput.getData();
    int produced = timePitch.receiveInterleaved(outLR, numDestSamples);
    if (produced > 0)
    {
        auto* destL = buffer.getWritePointer(0);
        auto* destR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : destL;
        for (int i = 0; i < produced; ++i)
        { destL[i] = outLR[2 * i + 0]; if (destR) destR[i] = outLR[2 * i + 1]; }
        if (produced < numDestSamples)
        { buffer.clear(0, produced, numDestSamples - produced); if (destR) buffer.clear(1, produced, numDestSamples - produced); }
    }
}

void SampleVoiceProcessor::setCurrentPosition(double newSamplePosition)
{
    if (sourceSample == nullptr) return;
    
    // 1. Clamp to valid range of the actual sample data
    // We don't clamp to rangeStart/rangeEnd here because a scrub might
    // intentionally go outside the loop points temporarily.
    double maxSample = (double)sourceSample->stereo.getNumSamples();
    readPosition = juce::jlimit(0.0, maxSample, newSamplePosition);
    
    // 2. Reset Time Stretcher
    // If we jump the read head, the time stretcher's internal buffers are now invalid.
    // We must flush them to prevent "ghost" audio from the previous location.
    timePitch.reset();
}


