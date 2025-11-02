#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../dsp/TimePitchProcessor.h"

/**
 * Wrapper around a PositionableAudioSource that applies time-stretching using RubberBand.
 * This allows audio playback speed to be synchronized with video playback speed.
 */
class TimeStretcherAudioSource : public juce::PositionableAudioSource
{
public:
    TimeStretcherAudioSource(juce::PositionableAudioSource* input, bool deleteInputWhenDeleted);
    ~TimeStretcherAudioSource() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    void setNextReadPosition(juce::int64 newPosition) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    bool isLooping() const override;
    void setLooping(bool shouldLoop) override;

    // Custom method to control playback speed (0.25x to 4.0x)
    void setSpeed(double newSpeed);

private:
    juce::PositionableAudioSource* inputSource;
    bool deleteInput;

    TimePitchProcessor timePitch;
    double currentSpeed { 1.0 };
    bool isLooping_ { false };

    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> stretchedBuffer;
    juce::AudioBuffer<float> interleavedInput;
    juce::AudioBuffer<float> interleavedOutput;

    bool isPrepared { false };
    bool isPrimed { false };
    double currentSampleRate { 44100.0 };
    int currentBlockSize { 512 };
};

