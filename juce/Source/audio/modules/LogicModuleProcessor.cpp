#include "LogicModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout LogicModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Operation selector: 0=AND, 1=OR, 2=XOR, 3=NOT A
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "operation", "Operation", juce::StringArray({"AND", "OR", "XOR", "NOT A"}), 0));

    // Gate threshold for interpreting gates
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    return {params.begin(), params.end()};
}

LogicModuleProcessor::LogicModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("In A", juce::AudioChannelSet::mono(), true)
                          .withInput("In B", juce::AudioChannelSet::mono(), true)
                          .withOutput("AND", juce::AudioChannelSet::mono(), true)
                          .withOutput("OR", juce::AudioChannelSet::mono(), true)
                          .withOutput("XOR", juce::AudioChannelSet::mono(), true)
                          .withOutput("NOT A", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "LogicParams", createParameterLayout())
{
    operationParam = apvts.getRawParameterValue("operation");
    gateThresholdParam = apvts.getRawParameterValue("gateThreshold");
    
    // Initialize lastOutputValues for cable inspector (4 outputs)
    for (int i = 0; i < 4; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void LogicModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void LogicModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inA = getBusBuffer(buffer, true, 0);  // Input A
    auto inB = getBusBuffer(buffer, true, 1);  // Input B
    auto outAND = getBusBuffer(buffer, false, 0);  // AND output
    auto outOR = getBusBuffer(buffer, false, 1);   // OR output
    auto outXOR = getBusBuffer(buffer, false, 2);  // XOR output
    auto outNOTA = getBusBuffer(buffer, false, 3); // NOT A output

    const int numSamples = buffer.getNumSamples();
    const float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    const float* aData = inA.getReadPointer(0);
    const float* bData = inB.getReadPointer(0);

    float* andData = outAND.getWritePointer(0);
    float* orData = outOR.getWritePointer(0);
    float* xorData = outXOR.getWritePointer(0);
    float* notAData = outNOTA.getWritePointer(0);

    // Process each sample
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = aData[i] > gateThresh ? 1.0f : 0.0f;  // Gate threshold
        const float b = bData[i] > gateThresh ? 1.0f : 0.0f;  // Gate threshold

        // Perform all logical operations
        andData[i] = (a > 0.5f && b > 0.5f) ? 1.0f : 0.0f;   // A AND B
        orData[i] = (a > 0.5f || b > 0.5f) ? 1.0f : 0.0f;    // A OR B
        xorData[i] = ((a > 0.5f) != (b > 0.5f)) ? 1.0f : 0.0f; // A XOR B
        notAData[i] = (a > 0.5f) ? 0.0f : 1.0f;              // NOT A
    }
}

void LogicModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void LogicModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

bool LogicModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "operation") { outBusIndex = 2; outChannelIndexInBus = 0; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void LogicModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    
    int operation = static_cast<int>(operationParam->load());
    const char* operationNames[] = {"AND", "OR", "XOR", "NOT A"};
    
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::Combo("Operation", &operation, operationNames, 4))
    {
        *operationParam = static_cast<float>(operation);
        onModificationEnded();
    }
    float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    if (ImGui::SliderFloat("Gate Thresh", &gateThresh, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gateThreshold"))) *p = gateThresh;
        onModificationEnded();
    }
    ImGui::PopItemWidth();
}

void LogicModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In A", 0);
    helpers.drawAudioInputPin("In B", 1);
    helpers.drawAudioOutputPin("AND", 0);
    helpers.drawAudioOutputPin("OR", 1);
    helpers.drawAudioOutputPin("XOR", 2);
    helpers.drawAudioOutputPin("NOT A", 3);
}
#endif