#pragma once
#include "../graph/VoiceProcessor.h"
#include "../dsp/TimePitchProcessor.h"

class SynthVoiceProcessor : public VoiceProcessor
{
public:
    SynthVoiceProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void renderBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    double phase { 0.0 };
    double lastSampleRate { 48000.0 };
    TimePitchProcessor timePitch; // audio-level time/pitch like samples
    juce::HeapBlock<float> interleavedInput;
    juce::HeapBlock<float> interleavedOutput;
    int interleavedCapacityFrames { 0 };
    // Simple modulation so time-stretch is audible on synth
    double lfoPhase { 0.0 };
    double baseLfoRateHz { 3.0 }; // tremolo
    float tremoloDepth { 0.5f };   // 0..1
    // Stutter gate to strongly expose time scaling without changing pitch
    int stutterPos { 0 };
    double baseStutterMs { 80.0 }; // segment base length
    float stutterDuty { 0.35f };   // on-fraction of segment
};