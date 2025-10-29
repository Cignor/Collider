#include "VCAModuleProcessor.h"

VCAModuleProcessor::VCAModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // 0-1: Audio In, 2: Gain Mod
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VCAParams", createParameterLayout())
{
    gainParam = apvts.getRawParameterValue("gain");
    relativeGainModParam = apvts.getRawParameterValue("relativeGainMod");
    
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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeGainMod", "Relative Gain Mod", true));

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
    float gainModCV = 0.5f; // Default to neutral (0.5 for relative mode)
    const bool isGainMod = isParamInputConnected("gain");
    
    if (isGainMod)
    {
        const auto& inBus = getBusBuffer(buffer, true, 0);
        if (inBus.getNumChannels() > 2)
            gainModCV = inBus.getReadPointer(2)[0]; // Read first sample from channel 2
    }
    
    const float baseGainDb = gainParam != nullptr ? gainParam->load() : 0.0f;
    const bool relativeMode = relativeGainModParam && relativeGainModParam->load() > 0.5f;
    
    // Process sample by sample to apply modulation
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float finalGainDb = baseGainDb;
            
            if (isGainMod)
            {
                const float cv = juce::jlimit(0.0f, 1.0f, gainModCV);
                
                if (relativeMode)
                {
                    // RELATIVE: CV modulates around base gain (±30dB range)
                    const float dbOffset = (cv - 0.5f) * 60.0f; // ±30dB
                    finalGainDb = baseGainDb + dbOffset;
                }
                else
                {
                    // ABSOLUTE: CV directly maps to full gain range (-60dB to +6dB)
                    finalGainDb = juce::jmap(cv, -60.0f, 6.0f);
                }
            }
            
            finalGainDb = juce::jlimit(-60.0f, 6.0f, finalGainDb);
            const float finalGain = juce::Decibels::decibelsToGain(finalGainDb);
            channelData[i] *= finalGain;
        }
    }
    
    // Store live modulated values for UI display
    float displayGainDb = baseGainDb;
    if (isGainMod)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, gainModCV);
        if (relativeMode)
        {
            const float dbOffset = (cv - 0.5f) * 60.0f;
            displayGainDb = baseGainDb + dbOffset;
        }
        else
        {
            displayGainDb = juce::jmap(cv, -60.0f, 6.0f);
        }
    }
    setLiveParamValue("gain_live", juce::jlimit(-60.0f, 6.0f, displayGainDb));

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


