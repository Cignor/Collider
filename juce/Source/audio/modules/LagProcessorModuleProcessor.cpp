#include "LagProcessorModuleProcessor.h"

LagProcessorModuleProcessor::LagProcessorModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Signal In", juce::AudioChannelSet::mono(), true)
                        .withInput ("Rise Mod", juce::AudioChannelSet::mono(), true)
                        .withInput ("Fall Mod", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "LagProcessorParams", createParameterLayout())
{
    riseTimeParam = apvts.getRawParameterValue("rise_time");
    fallTimeParam = apvts.getRawParameterValue("fall_time");
    modeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode"));
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout LagProcessorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Rise time: 0.1ms to 4000ms (logarithmic)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rise_time", "Rise Time",
        juce::NormalisableRange<float>(0.1f, 4000.0f, 0.0f, 0.3f),
        10.0f));
    
    // Fall time: 0.1ms to 4000ms (logarithmic)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fall_time", "Fall Time",
        juce::NormalisableRange<float>(0.1f, 4000.0f, 0.0f, 0.3f),
        10.0f));
    
    // Mode: Slew Limiter or Envelope Follower
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode",
        juce::StringArray{"Slew Limiter", "Envelope Follower"},
        0));
    
    return { params.begin(), params.end() };
}

void LagProcessorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    currentOutput = 0.0f;
}

void LagProcessorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto signalIn = getBusBuffer(buffer, true, 0);
    auto riseModIn = getBusBuffer(buffer, true, 1);
    auto fallModIn = getBusBuffer(buffer, true, 2);
    auto out = getBusBuffer(buffer, false, 0);
    
    const int nSamps = buffer.getNumSamples();
    
    // Get base parameter values
    float baseRiseMs = riseTimeParam != nullptr ? riseTimeParam->load() : 10.0f;
    float baseFallMs = fallTimeParam != nullptr ? fallTimeParam->load() : 10.0f;
    const int mode = modeParam != nullptr ? modeParam->getIndex() : 0;
    
    // Check for modulation
    const bool isRiseModulated = isParamInputConnected("rise_time_mod");
    const bool isFallModulated = isParamInputConnected("fall_time_mod");
    
    const float* riseModSignal = isRiseModulated ? riseModIn.getReadPointer(0) : nullptr;
    const float* fallModSignal = isFallModulated ? fallModIn.getReadPointer(0) : nullptr;
    
    const float* input = signalIn.getReadPointer(0);
    float* output = out.getWritePointer(0);
    
    for (int i = 0; i < nSamps; ++i)
    {
        // Get modulated rise/fall times if connected
        float riseMs = baseRiseMs;
        float fallMs = baseFallMs;
        
        if (isRiseModulated && riseModSignal) {
            // Map CV (0..1) to time range (0.1..4000ms) logarithmically
            riseMs = 0.1f * std::pow(40000.0f, riseModSignal[i]);
            riseMs = juce::jlimit(0.1f, 4000.0f, riseMs);
        }
        
        if (isFallModulated && fallModSignal) {
            fallMs = 0.1f * std::pow(40000.0f, fallModSignal[i]);
            fallMs = juce::jlimit(0.1f, 4000.0f, fallMs);
        }
        
        // Update telemetry (throttled)
        if ((i & 0x3F) == 0) {
            setLiveParamValue("rise_time_live", riseMs);
            setLiveParamValue("fall_time_live", fallMs);
        }
        
        // Calculate smoothing coefficients
        // Formula: coeff = 1.0 - exp(-1.0 / (time_in_seconds * sampleRate))
        float riseCoeff = 1.0f - std::exp(-1.0f / (riseMs * 0.001f * static_cast<float>(currentSampleRate)));
        float fallCoeff = 1.0f - std::exp(-1.0f / (fallMs * 0.001f * static_cast<float>(currentSampleRate)));
        
        // Get target value based on mode
        float inputSample = input[i];
        float targetValue = inputSample;
        
        if (mode == 1) // Envelope Follower
        {
            // Rectify the signal to extract amplitude envelope
            targetValue = std::abs(inputSample);
        }
        
        // Apply smoothing
        if (targetValue > currentOutput) // Rising
        {
            currentOutput += (targetValue - currentOutput) * riseCoeff;
        }
        else // Falling
        {
            currentOutput += (targetValue - currentOutput) * fallCoeff;
        }
        
        output[i] = currentOutput;
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 1 && lastOutputValues[0]) {
        lastOutputValues[0]->store(currentOutput);
    }
}

#if defined(PRESET_CREATOR_UI)
bool LagProcessorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "rise_time_mod")
    {
        outBusIndex = 1; // "Rise Mod" is Bus 1
        outChannelIndexInBus = 0;
        return true;
    }
    else if (paramId == "fall_time_mod")
    {
        outBusIndex = 2; // "Fall Mod" is Bus 2
        outChannelIndexInBus = 0;
        return true;
    }
    return false;
}
#endif

