#include "NoiseVoiceProcessor.h"

void NoiseVoiceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	VoiceProcessor::prepareToPlay (sampleRate, samplesPerBlock);
	juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
	lfo.prepare (spec);
	lfo.setFrequency (0.3f);
	filter.reset();
	filter.prepare (spec);
	filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void NoiseVoiceProcessor::renderBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ignoreUnused(midi);
	buffer.clear();
	const int n = buffer.getNumSamples();
	if (n <= 0) return;

	auto* L = buffer.getWritePointer(0);
	auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
	for (int i = 0; i < n; ++i)
	{
		const float s = random.nextFloat() * 2.0f - 1.0f;
		L[i] = s;
		R[i] = s;
	}

	const float lfoSample = lfo.processSample (0.0f);
	const float cutoff = juce::jmap (lfoSample, -1.0f, 1.0f, 300.0f, 2000.0f);
	filter.setCutoffFrequency (cutoff);

	juce::dsp::AudioBlock<float> block (buffer);
	juce::dsp::ProcessContextReplacing<float> context (block);
	filter.process (context);
}
