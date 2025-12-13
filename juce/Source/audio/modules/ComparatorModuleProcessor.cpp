#include "ComparatorModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ComparatorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold", 0.0f, 1.0f, 0.5f));
    return { p.begin(), p.end() };
}

ComparatorModuleProcessor::ComparatorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("In", juce::AudioChannelSet::mono(), true)
                        .withOutput("Out", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "ComparatorParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue("threshold");
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void ComparatorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto* in = buffer.getReadPointer(0);
    auto* out = buffer.getWritePointer(0);
    const float threshold = thresholdParam->load();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        out[i] = (in[i] >= threshold) ? 1.0f : 0.0f;

    if (!lastOutputValues.empty())
        lastOutputValues[0]->store(out[buffer.getNumSamples() - 1]);
}

#if defined(PRESET_CREATOR_UI)
void ComparatorModuleProcessor::drawParametersInNode(float itemWidth,
                                                    const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                    const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    
    // Check if threshold is modulated
    bool isThresholdModulated = isParamModulated("threshold");
    
    // Get current value (live if modulated, otherwise parameter value)
    float threshold = isThresholdModulated ? 
        (thresholdParam ? thresholdParam->load() : 0.5f) : // If modulated, use live value (for now just use param)
        (thresholdParam ? thresholdParam->load() : 0.5f);
    
    ImGui::PushItemWidth(itemWidth);
    
    // Disable slider if modulated
    if (isThresholdModulated) ImGui::BeginDisabled();
    
    if (ImGui::SliderFloat("Threshold", &threshold, 0.0f, 1.0f))
    {
        if (!isThresholdModulated)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("threshold")))
                *p = threshold;
        }
    }
    
    // Add scroll wheel support when not modulated
    if (!isThresholdModulated) adjustParamOnWheel(ap.getParameter("threshold"), "threshold", threshold);
    
    if (ImGui::IsItemDeactivatedAfterEdit() && !isThresholdModulated)
        onModificationEnded();
    
    if (isThresholdModulated) ImGui::EndDisabled();
    
    ImGui::PopItemWidth();
}

void ComparatorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In", 0, "Out", 0);
}
#endif


