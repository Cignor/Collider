#pragma once
#include "../graph/VoiceProcessor.h"
#include <atomic>
#include "../assets/SampleBank.h"
#include "../dsp/TimePitchProcessor.h"

class SampleVoiceProcessor : public VoiceProcessor
{
public:
    SampleVoiceProcessor(std::shared_ptr<SampleBank::Sample> sampleToPlay);
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void renderBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { readPosition = startSamplePos; timePitch.reset(); isPlaying = true; }
    void resetPosition() { readPosition = startSamplePos; timePitch.reset(); } // Reset position without starting playback
    void setLooping (bool shouldLoop) { isLooping = shouldLoop; }
    void setBasePitchSemitones (float semitones) { basePitchSemitones = semitones; }
    void setZoneTimeStretchRatio (float ratio) { zoneTimeStretchRatio = juce::jlimit (0.25f, 4.0f, ratio); }
    void setSourceName (const juce::String& name) { sourceName = name; }
    juce::String getSourceName () const { return sourceName; }

    // Smoothing controls
    void setSmoothingEnabled (bool enabled) { requestedSmoothingEnabled.store(enabled, std::memory_order_relaxed); }
    void setSmoothingTimeMs (float timeMs, float pitchMs) { smoothingTimeMsTime = timeMs; smoothingTimeMsPitch = pitchMs; }
    void setSmoothingAlpha (float alphaTime, float alphaPitch) { smoothingAlphaTime = alphaTime; smoothingAlphaPitch = alphaPitch; }
    void setSmoothingMaxBlocks (int maxBlocksTime, int maxBlocksPitch) { smoothingMaxBlocksTime = juce::jmax(1, maxBlocksTime); smoothingMaxBlocksPitch = juce::jmax(1, maxBlocksPitch); }
    void setSmoothingSnapThresholds (float timeRatioDelta, float pitchSemisDelta) { smoothingSnapThresholdTime = timeRatioDelta; smoothingSnapThresholdPitch = pitchSemisDelta; }
    void setSmoothingResetPolicy (bool resetOnLargeChange, bool resetWhenNoSmoothing) { resetOnSnap = resetOnLargeChange; resetOnChangeWhenNoSmoothing = resetWhenNoSmoothing; }

    // Engine selection
    enum class Engine { RubberBand = 0, Naive = 1 };
    void setEngine (Engine e)
    {
        Engine current = engine.load(std::memory_order_relaxed);
        if (current == e) return; // avoid resetting every block
        engine.store(e, std::memory_order_relaxed);
        timePitch.setMode(e==Engine::RubberBand? TimePitchProcessor::Mode::RubberBand : TimePitchProcessor::Mode::Fifo);
        timePitch.reset();
    }
    void setRubberBandOptions (bool windowShort, bool phaseIndependent) { timePitch.setOptions(windowShort, phaseIndependent); }
    
    void setPlaybackRange(double startSample, double endSample)
    {
        startSamplePos = startSample;
        endSamplePos = endSample;
    }

public:
    bool isLooping { true };
    bool isPlaying { false }; // MOVED TO PUBLIC

private:
    std::shared_ptr<SampleBank::Sample> sourceSample;
    juce::String sourceName;
    double readPosition { 0.0 };
    double outputSampleRate { 48000.0 };
    float basePitchSemitones { 0.0f }; // grid-based pitch at spawn
    float zoneTimeStretchRatio { 1.0f }; // per-voice, dynamic (zones)
    float independentPitchRatio { 1.0f }; // pitch factor that does not affect tempo
    TimePitchProcessor timePitch; // placeholder; swap to SoundTouch impl later
    juce::HeapBlock<float> interleavedInput;
    double startSamplePos { 0.0 };
    double endSamplePos { -1.0 }; // -1 indicates playback to the end of the sample
    juce::HeapBlock<float> interleavedOutput;
    int interleavedCapacityFrames { 0 };
    float lastEffectiveTime { 1.0f };
    float lastEffectivePitchSemis { 0.0f };
    // Smooth parameter transitions (independent for time & pitch)
    float smoothedTimeRatio { 1.0f };
    float smoothedPitchSemis { 0.0f };
    int   smoothingBlocksRemainingTime { 0 };
    int   smoothingBlocksRemainingPitch { 0 };
    float timeStepPerBlock { 0.0f };
    float pitchStepPerBlock { 0.0f };
    float smoothingTimeMsTime { 100.0f };  // defaults per user preference
    float smoothingTimeMsPitch { 100.0f };
    bool  smoothingEnabled { true };
    float smoothingAlphaTime { 0.4f };
    float smoothingAlphaPitch { 0.4f };
    int   smoothingMaxBlocksTime { 1 };
    int   smoothingMaxBlocksPitch { 1 };
    float smoothingSnapThresholdTime { 0.5f };    // time ratio delta to snap
    float smoothingSnapThresholdPitch { 3.0f };   // semitone delta to snap
    bool  resetOnSnap { true };
    bool  resetOnChangeWhenNoSmoothing { true };
    std::atomic<bool> requestedSmoothingEnabled { true };

    std::atomic<Engine> engine { Engine::RubberBand };
    // Fast handover state
    bool  inBypassHandover { false };
    int   handoverFramesRemaining { 0 };
    int   handoverCrossfadeFrames { 240 };
};