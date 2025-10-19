#include "SynthVoiceProcessor.h"

SynthVoiceProcessor::SynthVoiceProcessor() = default;

void SynthVoiceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	// Ensure base class prepares FX chain and internal state
	VoiceProcessor::prepareToPlay (sampleRate, samplesPerBlock);
	lastSampleRate = sampleRate;
	phase = 0.0;
	// Prepare audio-level time/pitch like samples
	timePitch.prepare (sampleRate, 2, samplesPerBlock);
	interleavedCapacityFrames = samplesPerBlock;
	interleavedInput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
	interleavedOutput.allocate ((size_t) (interleavedCapacityFrames * 2), true);
}

void SynthVoiceProcessor::renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ignoreUnused (midi);
    // VoiceProcessor clears the out bus before calling renderBlock

	const int numSamples = buffer.getNumSamples();
	if (numSamples <= 0)
		return;

	const float baseFreq = apvts.getRawParameterValue ("frequency") ? apvts.getRawParameterValue ("frequency")->load() : 440.0f;
	const float apPitch = apvts.getRawParameterValue ("pitchSemitones") ? apvts.getRawParameterValue ("pitchSemitones")->load() : 0.0f;
	const float apPitchRatio = apvts.getRawParameterValue ("pitchRatio") ? apvts.getRawParameterValue ("pitchRatio")->load() : 1.0f;
	const double oscPitchMul = (double) apPitchRatio * std::pow (2.0, (double) apPitch / 12.0);
	const double freq = juce::jlimit (20.0, 20000.0, (double) baseFreq * oscPitchMul);
	const double sr = lastSampleRate > 0.0 ? lastSampleRate : 48000.0;
	const double delta = juce::MathConstants<double>::twoPi * (double) freq / sr;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    for (int i = 0; i < numSamples; ++i)
    {
        phase += delta;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
        const float s = std::sin ((float) phase);
        L[i] += s;
        R[i] += s;
    }
}


