#include "GainProcessor.h"

GainProcessor::GainProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MASTER",
                  { std::make_unique<juce::AudioParameterFloat>("gain", "Gain",
                      juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 1.0f), 0.7f) })
{
    gainParam = parameters.getRawParameterValue ("gain");
}

void GainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    const auto channels = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, channels };
    gain.prepare (spec);
    gain.setGainLinear (gainParam ? gainParam->load() : 0.7f);
}

void GainProcessor::releaseResources()
{
}

void GainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    gain.setGainLinear (gainParam ? gainParam->load() : 0.7f);
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    gain.process (ctx);
}

void GainProcessor::setLinearGain (float newGain)
{
    if (gainParam != nullptr)
        *gainParam = juce::jlimit (0.0f, 1.0f, newGain);
}


