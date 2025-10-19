#include "AttenuverterModuleProcessor.h"

AttenuverterModuleProcessor::AttenuverterModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withInput ("Amount Mod", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AttenuverterParams", createParameterLayout())
{
    amountParam = apvts.getRawParameterValue ("amount");
    rectifyParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("rectify"));
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout AttenuverterModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("amount", "Amount", juce::NormalisableRange<float> (-10.0f, 10.0f), 1.0f));
    // Add a non-automatable, hidden parameter for rectification mode
    params.push_back (std::make_unique<juce::AudioParameterBool> ("rectify", "Rectify", false));
    return { params.begin(), params.end() };
}

void AttenuverterModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void AttenuverterModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    auto in  = getBusBuffer (buffer, true, 0);
    auto modIn = getBusBuffer (buffer, true, 1); // Amount Mod bus
    auto out = getBusBuffer (buffer, false, 0);
    
    const float baseGain = amountParam != nullptr ? amountParam->load() : 1.0f;
    const bool shouldRectify = rectifyParam != nullptr ? rectifyParam->get() : false;
    const int nSamps = buffer.getNumSamples();
    
    // Check if amount is modulated
    const bool isAmountModulated = isParamInputConnected("amount");
    const float* modSignal = isAmountModulated ? modIn.getReadPointer(0) : nullptr;
    
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        const float* s = in.getReadPointer (juce::jmin(ch, in.getNumChannels()-1));
        float* d = out.getWritePointer (ch);
        for (int i = 0; i < nSamps; ++i)
        {
            float sample = s[i];
            if (shouldRectify)
                sample = std::abs(sample);
            
            // Apply modulation if connected
            float currentGain = baseGain;
            if (isAmountModulated && modSignal)
                currentGain = juce::jmap(modSignal[i], 0.0f, 1.0f, -10.0f, 10.0f);
            
            // Update telemetry for live UI feedback (throttled to every 64 samples)
            if ((i & 0x3F) == 0) {
                setLiveParamValue("amount_live", currentGain);
            }
                
            d[i] = sample * currentGain;
        }
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getSample(0, nSamps - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(out.getSample(1, nSamps - 1));
    }
}

bool AttenuverterModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "amount")
    {
        outBusIndex = 1; // "Amount Mod" is Bus 1
        outChannelIndexInBus = 0;
        return true;
    }
    return false;
}
