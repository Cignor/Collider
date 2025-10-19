#include "DeCrackleModuleProcessor.h"

DeCrackleModuleProcessor::DeCrackleModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DeCrackleParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue("threshold");
    smoothingTimeMsParam = apvts.getRawParameterValue("smoothing_time");
    amountParam = apvts.getRawParameterValue("amount");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout DeCrackleModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Threshold: 0.01 to 1.0
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold",
        juce::NormalisableRange<float>(0.01f, 1.0f),
        0.1f));
    
    // Smoothing time: 0.1ms to 20.0ms (logarithmic)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "smoothing_time", "Smoothing Time",
        juce::NormalisableRange<float>(0.1f, 20.0f, 0.0f, 0.3f),
        5.0f));
    
    // Amount (dry/wet): 0.0 to 1.0
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "amount", "Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        1.0f));
    
    return { params.begin(), params.end() };
}

void DeCrackleModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    
    // Reset state
    for (int ch = 0; ch < 2; ++ch)
    {
        lastInputSample[ch] = 0.0f;
        lastOutputSample[ch] = 0.0f;
        smoothingSamplesRemaining[ch] = 0;
    }
}

void DeCrackleModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    
    const int nSamps = buffer.getNumSamples();
    const int numChannels = juce::jmin(out.getNumChannels(), 2);
    
    // Get parameter values
    const float threshold = thresholdParam != nullptr ? thresholdParam->load() : 0.1f;
    const float smoothingMs = smoothingTimeMsParam != nullptr ? smoothingTimeMsParam->load() : 5.0f;
    const float wet = amountParam != nullptr ? amountParam->load() : 1.0f;
    const float dry = 1.0f - wet;
    
    // Calculate smoothing coefficient
    // Use a fixed, fast coefficient for the smoothing
    const float smoothingCoeff = 0.1f;
    
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* input = in.getReadPointer(juce::jmin(ch, in.getNumChannels() - 1));
        float* output = out.getWritePointer(ch);
        
        for (int i = 0; i < nSamps; ++i)
        {
            float inputSample = input[i];
            
            // 1. Detect Crackle (discontinuity)
            float delta = std::abs(inputSample - lastInputSample[ch]);
            if (delta > threshold)
            {
                // A crackle is detected. Activate smoothing for a short period.
                smoothingSamplesRemaining[ch] = static_cast<int>(smoothingMs * 0.001f * currentSampleRate);
            }
            
            // 2. Apply Smoothing if Active
            float processedSample;
            if (smoothingSamplesRemaining[ch] > 0)
            {
                // Apply fast slew to smooth the transition
                lastOutputSample[ch] += (inputSample - lastOutputSample[ch]) * smoothingCoeff;
                processedSample = lastOutputSample[ch];
                smoothingSamplesRemaining[ch]--;
            }
            else
            {
                // No smoothing needed, output is the same as input
                processedSample = inputSample;
                lastOutputSample[ch] = inputSample;
            }
            
            // 3. Apply Dry/Wet Mix
            output[i] = (inputSample * dry) + (processedSample * wet);
            
            // 4. Store last input sample for the next iteration's delta calculation
            lastInputSample[ch] = inputSample;
        }
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getSample(0, nSamps - 1));
        if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(out.getSample(1, nSamps - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
bool DeCrackleModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    juce::ignoreUnused(paramId, outBusIndex, outChannelIndexInBus);
    // No modulation inputs for this module
    return false;
}
#endif

