#include "RerouteModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout RerouteModuleProcessor::createParameterLayout()
{
    return {};
}

RerouteModuleProcessor::RerouteModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("In", juce::AudioChannelSet::mono(), true)
                          .withOutput("Out", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "RerouteParams", createParameterLayout())
{
    // Track output magnitude for the cable inspector
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void RerouteModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);

    output.clear();

    const int numChannels = juce::jmin(input.getNumChannels(), output.getNumChannels());
    const int numSamples = juce::jmin(input.getNumSamples(), output.getNumSamples());

    if (numChannels > 0 && numSamples > 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            output.copyFrom(ch, 0, input, ch, 0, numSamples);
    }

    updateOutputTelemetry(output);
}

void RerouteModuleProcessor::setPassthroughType(PinDataType newType)
{
    PinDataType sanitized = kDefaultType;
    switch (newType)
    {
        case PinDataType::Audio:
        case PinDataType::CV:
        case PinDataType::Gate:
        case PinDataType::Raw:
        case PinDataType::Video:
            sanitized = newType;
            break;
        default:
            break;
    }

    const auto previous = currentType.load(std::memory_order_relaxed);
    if (previous != sanitized)
        currentType.store(sanitized, std::memory_order_relaxed);
}

PinDataType RerouteModuleProcessor::getPassthroughType() const
{
    return currentType.load(std::memory_order_relaxed);
}

std::vector<DynamicPinInfo> RerouteModuleProcessor::getDynamicInputPins() const
{
    return { DynamicPinInfo("In", 0, getPassthroughType()) };
}

std::vector<DynamicPinInfo> RerouteModuleProcessor::getDynamicOutputPins() const
{
    return { DynamicPinInfo("Out", 0, getPassthroughType()) };
}

juce::ValueTree RerouteModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree state("RerouteState");
    state.setProperty("type", static_cast<int>(getPassthroughType()), nullptr);
    return state;
}

void RerouteModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.isValid())
        return;

    const auto restored = static_cast<PinDataType>(static_cast<int>(state.getProperty("type", static_cast<int>(kDefaultType))));
    setPassthroughType(restored);
}

#if defined(PRESET_CREATOR_UI)
void RerouteModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawIoPins(this);
}
#endif

