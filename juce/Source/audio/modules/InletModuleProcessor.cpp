#include "InletModuleProcessor.h"

InletModuleProcessor::InletModuleProcessor()
    : ModuleProcessor(BusesProperties()
                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "InletParams", createParameterLayout()),
      customLabel("Inlet")
{
    // Initialize output value tracking
    lastOutputValues.clear();
    for (int i = 0; i < 2; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout InletModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterInt>(
        paramIdChannelCount,
        "Channel Count",
        1, 16, 2
    ));
    
    return layout;
}

void InletModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void InletModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // If we have an incoming buffer from the parent MetaModule, copy it
    if (incomingBuffer != nullptr && incomingBuffer->getNumSamples() > 0)
    {
        const int numChannelsToCopy = juce::jmin(buffer.getNumChannels(), incomingBuffer->getNumChannels());
        const int numSamplesToCopy = juce::jmin(buffer.getNumSamples(), incomingBuffer->getNumSamples());
        
        for (int ch = 0; ch < numChannelsToCopy; ++ch)
        {
            buffer.copyFrom(ch, 0, *incomingBuffer, ch, 0, numSamplesToCopy);
        }
        
        // Update output telemetry
        if (lastOutputValues.size() >= 2)
        {
            lastOutputValues[0]->store(buffer.getMagnitude(0, 0, buffer.getNumSamples()));
            if (buffer.getNumChannels() > 1)
                lastOutputValues[1]->store(buffer.getMagnitude(1, 0, buffer.getNumSamples()));
        }
    }
    else
    {
        // No incoming buffer - output silence
        buffer.clear();
    }
}

juce::ValueTree InletModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("InletState");
    vt.setProperty("customLabel", customLabel, nullptr);
    vt.setProperty("pinIndex", pinIndex, nullptr);
    vt.setProperty("version", 1, nullptr);
    vt.setProperty("externalLogicalId", (int)externalLogicalId, nullptr);
    vt.setProperty("externalChannel", externalChannel, nullptr);
    return vt;
}

void InletModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("InletState"))
    {
        customLabel = vt.getProperty("customLabel", "Inlet").toString();
        pinIndex = (int)vt.getProperty("pinIndex", pinIndex);
        externalLogicalId = (juce::uint32)(int)vt.getProperty("externalLogicalId", (int)externalLogicalId);
        externalChannel = (int)vt.getProperty("externalChannel", externalChannel);
    }
}

#if defined(PRESET_CREATOR_UI)
void InletModuleProcessor::drawParametersInNode(float itemWidth,
                                                const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded);
    
    auto& ap = getAPVTS();
    
    ImGui::PushItemWidth(itemWidth);
    
    // Label editor
    char labelBuf[64];
    strncpy(labelBuf, customLabel.toRawUTF8(), sizeof(labelBuf) - 1);
    labelBuf[sizeof(labelBuf) - 1] = '\0';
    
    if (ImGui::InputText("Label", labelBuf, sizeof(labelBuf)))
    {
        customLabel = juce::String(labelBuf);
    }
    
    // Channel count
    int channelCount = 2;
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
        channelCount = p->get();
    
    if (ImGui::SliderInt("Channels", &channelCount, 1, 16))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
            *p = channelCount;
    }
    
    ImGui::PopItemWidth();
}
#endif

