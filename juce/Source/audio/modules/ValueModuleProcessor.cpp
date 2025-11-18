#include "ValueModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

ValueModuleProcessor::ValueModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withOutput("Out", juce::AudioChannelSet::discreteChannels(5), true)), // 5 outputs now (added CV Out)
      apvts(*this, nullptr, "ValueParams", createParameterLayout())
{
    valueParam = apvts.getRawParameterValue("value");
    cvMinParam = apvts.getRawParameterValue("cvMin");
    cvMaxParam = apvts.getRawParameterValue("cvMax");

    // Initialize value tooltips for all five outputs
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For the new CV Out
}

juce::AudioProcessorValueTreeState::ParameterLayout ValueModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("value", "Value", -20000.0f, 20000.0f, 1.0f));
    
    // Add new CV range parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cvMin", "CV Min", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cvMax", "CV Max", 0.0f, 1.0f, 1.0f));
    
    return { params.begin(), params.end() };
}

void ValueModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    buffer.clear();

    const float rawValue = valueParam->load();
    auto* param = static_cast<juce::AudioParameterFloat*>(apvts.getParameter("value"));
    const float normalizedValue = param->getNormalisableRange().convertTo0to1(rawValue);
    
    // Calculate CV output value
    const float cvMin = cvMinParam->load();
    const float cvMax = cvMaxParam->load();
    const float cvOutputValue = juce::jmap(normalizedValue, cvMin, cvMax);

    auto* outRaw = buffer.getWritePointer(0);
    auto* outNorm = buffer.getWritePointer(1);
    auto* outInv = buffer.getWritePointer(2);
    auto* outInt = buffer.getWritePointer(3);
    auto* outCV = buffer.getWritePointer(4); // Get pointer for new output

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        outRaw[i] = rawValue;
        outNorm[i] = normalizedValue;
        outInv[i] = -rawValue;
        outInt[i] = std::round(rawValue);
        outCV[i] = cvOutputValue; // Write to the new output
    }

    // Update tooltips
    if (lastOutputValues.size() >= 5)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outRaw[buffer.getNumSamples() - 1]);
        if (lastOutputValues[1]) lastOutputValues[1]->store(outNorm[buffer.getNumSamples() - 1]);
        if (lastOutputValues[2]) lastOutputValues[2]->store(outInv[buffer.getNumSamples() - 1]);
        if (lastOutputValues[3]) lastOutputValues[3]->store(outInt[buffer.getNumSamples() - 1]);
        if (lastOutputValues[4]) lastOutputValues[4]->store(outCV[buffer.getNumSamples() - 1]); // Update new tooltip
    }

#if defined(PRESET_CREATOR_UI)
    // Store values for visualization
    vizData.rawValue.store(rawValue);
    vizData.normalizedValue.store(normalizedValue);
    vizData.invertedValue.store(-rawValue);
    vizData.integerValue.store(std::round(rawValue));
    vizData.cvValue.store(cvOutputValue);
    vizData.currentValue.store(rawValue);
    vizData.currentCvMin.store(cvMin);
    vizData.currentCvMax.store(cvMax);
#endif
}
