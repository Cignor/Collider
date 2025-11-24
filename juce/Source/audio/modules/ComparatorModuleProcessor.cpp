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
                                                    const std::function<bool(const juce::String& /*paramId*/)>&,
                                                    const std::function<void()>& onModificationEnded)
{
    float t = thresholdParam->load();
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderFloat("Threshold", &t, 0.0f, 1.0f))
        *thresholdParam = t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        onModificationEnded();
    ImGui::PopItemWidth();
}

void ComparatorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In", 0, "Out", 0);
}
#endif


