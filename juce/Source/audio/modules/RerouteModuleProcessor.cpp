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

    // True passthrough: copy input to output, channel-matched, avoid aliasing
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    const int n = buffer.getNumSamples();
    
    // Mono passthrough; avoid aliasing
    if (in.getNumChannels() > 0 && out.getNumChannels() > 0)
    {
        auto* srcCh = in.getReadPointer(0);
        auto* dst = out.getWritePointer(0);
        if (dst != srcCh)
            juce::FloatVectorOperations::copy(dst, srcCh, n);
    }

    // Update inspector with block peak of output
    if (!lastOutputValues.empty())
    {
        const float* p = out.getNumChannels() > 0 ? out.getReadPointer(0) : nullptr;
        float m = 0.0f;
        if (p)
        {
            for (int i = 0; i < n; ++i)
                m = juce::jmax(m, std::abs(p[i]));
        }
        lastOutputValues[0]->store(m);
    }
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
    helpers.drawParallelPins("In", 0, "Out", 0);
}
#endif

