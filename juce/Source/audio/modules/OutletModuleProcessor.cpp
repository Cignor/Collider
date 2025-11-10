#include "OutletModuleProcessor.h"

OutletModuleProcessor::OutletModuleProcessor()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "OutletParams", createParameterLayout()),
      customLabel("Outlet")
{
}

juce::AudioProcessorValueTreeState::ParameterLayout OutletModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterInt>(
        paramIdChannelCount,
        "Channel Count",
        1, 16, 2
    ));
    
    return layout;
}

void OutletModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate);
    cachedBuffer.setSize(2, samplesPerBlock);
}

void OutletModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Cache the incoming buffer so the parent MetaModule can read it
    cachedBuffer.makeCopyOf(buffer);
}

juce::ValueTree OutletModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("OutletState");
    vt.setProperty("customLabel", customLabel, nullptr);
    vt.setProperty("pinIndex", pinIndex, nullptr);
    return vt;
}

void OutletModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("OutletState"))
    {
        customLabel = vt.getProperty("customLabel", "Outlet").toString();
        pinIndex = (int)vt.getProperty("pinIndex", pinIndex);
    }
}

#if defined(PRESET_CREATOR_UI)
void OutletModuleProcessor::drawParametersInNode(float itemWidth,
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

