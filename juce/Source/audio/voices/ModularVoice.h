#pragma once

#include "../graph/VoiceProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include "../dsp/TimePitchProcessor.h"

// Adapter that lets ModularSynthProcessor be used as a VoiceProcessor in the harness
class ModularVoice : public VoiceProcessor
{
public:
    ModularVoice()
    {
        modularSynth = std::make_unique<ModularSynthProcessor>();
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        VoiceProcessor::prepareToPlay (sampleRate, samplesPerBlock);
        if (modularSynth)
            modularSynth->prepareToPlay (sampleRate, samplesPerBlock);
            
        // Prepare time/pitch post-processing (copied from SynthVoiceProcessor)
        timePitch.prepare (sampleRate, 2, samplesPerBlock);
        interleavedCapacityFrames = samplesPerBlock;
        interleavedInput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
        interleavedOutput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
    }

    void renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        // 1) Render modular synth into isolated temporary buffer to avoid re-entrancy on the graph buffer
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (tempBuffer.getNumChannels() != juce::jmax (1, numCh) || tempBuffer.getNumSamples() != numSamples)
            tempBuffer.setSize (juce::jmax (1, numCh), numSamples, false, false, true);
        tempBuffer.clear();
        if (modularSynth)
            modularSynth->processBlock (tempBuffer, midi);

        // 2) Copy from temp into the main buffer (wrapping channel index if needed)
        for (int ch = 0; ch < numCh; ++ch)
        {
            const int srcCh = ch % juce::jmax (1, tempBuffer.getNumChannels());
            buffer.copyFrom (ch, 0, tempBuffer, srcCh, 0, numSamples);
        }

        if (numSamples <= 0)
            return;

        // 3) Read time/pitch parameters
        const float apTime  = apvts.getRawParameterValue ("timeStretchRatio") ? apvts.getRawParameterValue ("timeStretchRatio")->load() : 1.0f;
        const float apPitch = apvts.getRawParameterValue ("pitchSemitones")  ? apvts.getRawParameterValue ("pitchSemitones")->load()  : 0.0f;

        // 4) Apply time/pitch post-processing to the main buffer when non-neutral
        if (std::abs(apTime - 1.0f) > 0.001f || std::abs(apPitch) > 0.001f)
        {
            timePitch.setTimeStretchRatio (apTime);
            timePitch.setPitchSemitones (apPitch);

            // Ensure interleaved buffers are large enough
            if (interleavedCapacityFrames < numSamples)
            {
                interleavedCapacityFrames = numSamples;
                interleavedInput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
                interleavedOutput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
            }

            // Convert planar buffer to interleaved for processing
            auto* L = buffer.getReadPointer (0);
            auto* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;
            for (int i = 0; i < numSamples; ++i)
            {
                interleavedInput[i * 2 + 0] = L[i];
                interleavedInput[i * 2 + 1] = R[i];
            }

            // Process via time/pitch and write back to planar buffer
            timePitch.putInterleaved (interleavedInput.getData(), numSamples);
            const int framesOut = timePitch.receiveInterleaved (interleavedOutput.getData(), numSamples);
            buffer.clear();
            const int n = juce::jmin (framesOut, numSamples);
            auto* wL = buffer.getWritePointer (0);
            auto* wR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : wL;
            for (int i = 0; i < n; ++i)
            {
                wL[i] = interleavedOutput[i * 2 + 0];
                wR[i] = interleavedOutput[i * 2 + 1];
            }
            if (n < numSamples)
            {
                if (buffer.getNumChannels() > 1)
                {
                    buffer.clear (0, n, numSamples - n);
                    buffer.clear (1, n, numSamples - n);
                }
                else
                {
                    buffer.clear (n, numSamples - n);
                }
            }
        }
        // If the 'if' condition is false, the buffer with the raw synth audio is passed through untouched.
    }

    ModularSynthProcessor* getModularSynth() { return modularSynth.get(); }

private:
    std::unique_ptr<ModularSynthProcessor> modularSynth;
    juce::AudioBuffer<float> tempBuffer;
    
    // Time/Pitch post-processing members (copied from SynthVoiceProcessor)
    TimePitchProcessor timePitch;
    juce::HeapBlock<float> interleavedInput, interleavedOutput;
    int interleavedCapacityFrames { 0 };
    // Stuttering members for time-stretch effect
    int stutterPos { 0 };
    double baseStutterMs { 80.0 }; // segment base length
    float stutterDuty { 0.35f };   // on-fraction of segment
};


