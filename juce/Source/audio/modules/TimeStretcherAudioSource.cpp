#include "TimeStretcherAudioSource.h"

TimeStretcherAudioSource::TimeStretcherAudioSource(juce::PositionableAudioSource* input, bool deleteInputWhenDeleted)
    : inputSource(input), deleteInput(deleteInputWhenDeleted)
{
    timePitch.setMode(TimePitchProcessor::Mode::RubberBand);
}

TimeStretcherAudioSource::~TimeStretcherAudioSource()
{
    releaseResources();
    if (deleteInput && inputSource != nullptr)
        delete inputSource;
}

void TimeStretcherAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;

    if (inputSource != nullptr)
        inputSource->prepareToPlay(samplesPerBlockExpected, sampleRate);

    // Get number of channels from the reader source (typically stereo for video audio)
    int numChannels = 2; // Default to stereo
    if (inputSource != nullptr && inputSource->getTotalLength() > 0)
    {
        // Try to determine channel count - for AudioFormatReaderSource, we'll use 2 as default
        // since video audio is typically stereo
        numChannels = 2;
    }
    timePitch.prepare(sampleRate, numChannels, samplesPerBlockExpected);
    timePitch.reset();

    inputBuffer.setSize(numChannels, samplesPerBlockExpected * 2);
    stretchedBuffer.setSize(numChannels, samplesPerBlockExpected * 2);
    interleavedInput.setSize(1, samplesPerBlockExpected * numChannels * 2);
    interleavedOutput.setSize(1, samplesPerBlockExpected * numChannels * 2);

    isPrepared = true;
    isPrimed = false;
}

void TimeStretcherAudioSource::releaseResources()
{
    isPrepared = false;
    isPrimed = false;
    if (inputSource != nullptr)
        inputSource->releaseResources();
    timePitch.reset();
}

void TimeStretcherAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (!isPrepared || inputSource == nullptr || bufferToFill.buffer == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();
    
    // Prime the stretcher if needed (RubberBand needs initial frames)
    if (!isPrimed && timePitch.availableFrames() < numSamples)
    {
        inputBuffer.setSize(numChannels, numSamples * 4, false, false, true);
        juce::AudioSourceChannelInfo primeInfo(inputBuffer);
        inputSource->getNextAudioBlock(primeInfo);
        
        // Convert to interleaved and feed to stretcher
        interleavedInput.setSize(1, primeInfo.numSamples * numChannels, false, false, true);
        float* interleaved = interleavedInput.getWritePointer(0);
        for (int i = 0; i < primeInfo.numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                interleaved[i * numChannels + ch] = inputBuffer.getSample(ch, i);
            }
        }
        timePitch.putInterleaved(interleaved, primeInfo.numSamples);
        isPrimed = true;
    }

    // Read from input source
    inputBuffer.setSize(numChannels, numSamples, false, false, true);
    juce::AudioSourceChannelInfo inputInfo(inputBuffer);
    inputSource->getNextAudioBlock(inputInfo);

    if (inputInfo.numSamples <= 0)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    // Convert to interleaved
    interleavedInput.setSize(1, inputInfo.numSamples * numChannels, false, false, true);
    float* interleaved = interleavedInput.getWritePointer(0);
    for (int i = 0; i < inputInfo.numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            interleaved[i * numChannels + ch] = inputBuffer.getSample(ch, i);
        }
    }

    // Feed to time stretcher
    timePitch.putInterleaved(interleaved, inputInfo.numSamples);

    // Retrieve stretched audio
    interleavedOutput.setSize(1, numSamples * numChannels, false, false, true);
    float* stretchedInterleaved = interleavedOutput.getWritePointer(0);
    int framesReceived = timePitch.receiveInterleaved(stretchedInterleaved, numSamples);

    // Convert back to planar and fill output
    if (framesReceived > 0)
    {
        const int actualFrames = juce::jmin(framesReceived, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* output = bufferToFill.buffer->getWritePointer(ch, bufferToFill.startSample);
            for (int i = 0; i < actualFrames; ++i)
            {
                output[i] = stretchedInterleaved[i * numChannels + ch];
            }
            // Zero any remaining samples
            if (actualFrames < numSamples)
            {
                juce::FloatVectorOperations::clear(output + actualFrames, numSamples - actualFrames);
            }
        }
    }
    else
    {
        bufferToFill.clearActiveBufferRegion();
    }
}

void TimeStretcherAudioSource::setNextReadPosition(juce::int64 newPosition)
{
    if (inputSource != nullptr)
    {
        inputSource->setNextReadPosition(newPosition);
        timePitch.reset(); // Reset stretcher on seek
        isPrimed = false; // Need to re-prime after seek
    }
}

juce::int64 TimeStretcherAudioSource::getNextReadPosition() const
{
    return inputSource != nullptr ? inputSource->getNextReadPosition() : 0;
}

juce::int64 TimeStretcherAudioSource::getTotalLength() const
{
    return inputSource != nullptr ? inputSource->getTotalLength() : 0;
}

bool TimeStretcherAudioSource::isLooping() const
{
    return isLooping_;
}

void TimeStretcherAudioSource::setLooping(bool shouldLoop)
{
    isLooping_ = shouldLoop;
    if (inputSource != nullptr)
        inputSource->setLooping(shouldLoop);
}

void TimeStretcherAudioSource::setSpeed(double newSpeed)
{
    currentSpeed = juce::jlimit(0.25, 4.0, newSpeed);
    timePitch.setTimeStretchRatio(currentSpeed);
}

