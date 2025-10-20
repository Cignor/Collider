#include "VCAModuleProcessor.h"

VCAModuleProcessor::VCAModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // 0-1: Audio In, 2: Gain Mod
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VCAParams", createParameterLayout())
{
    gainParam = apvts.getRawParameterValue("gain");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout VCAModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void VCAModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 2 };
    gain.prepare(spec);
}

void VCAModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Read CV from unified input bus (if connected)
    float gainModCV = 1.0f; // Default to no modulation
    
    if (isParamInputConnected("gain"))
    {
        const auto& inBus = getBusBuffer(buffer, true, 0);
        if (inBus.getNumChannels() > 2)
            gainModCV = inBus.getReadPointer(2)[0]; // Read first sample from channel 2
    }
    
    // Process sample by sample to apply modulation
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // Get base gain from the dB parameter
            const float gainFromParam = juce::Decibels::decibelsToGain(gainParam != nullptr ? gainParam->load() : 0.0f);
            
            // Use CV modulation (0-1 range)
            const float finalGain = gainFromParam * gainModCV;
            channelData[i] *= finalGain;
        }
    }
    
    // Store live modulated values for UI display
    const float gainFromParam = juce::Decibels::decibelsToGain(gainParam != nullptr ? gainParam->load() : 0.0f);
    const float finalGainDb = isParamInputConnected("gain") ? 
        juce::Decibels::gainToDecibels(gainFromParam * gainModCV) : 
        (gainParam != nullptr ? gainParam->load() : 0.0f);
    setLiveParamValue("gain_live", finalGainDb);

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(buffer.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(buffer.getSample(1, buffer.getNumSamples() - 1));
    }
}

// Parameter bus contract implementation
bool VCAModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "gain") { outChannelIndexInBus = 2; return true; }
    return false;
}


