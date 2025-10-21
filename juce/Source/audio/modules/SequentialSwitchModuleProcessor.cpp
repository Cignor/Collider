#include "SequentialSwitchModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "imgui.h"
#endif

SequentialSwitchModuleProcessor::SequentialSwitchModuleProcessor()
    : ModuleProcessor(
        BusesProperties()
            .withInput  ("Inputs",   juce::AudioChannelSet::discreteChannels(5), true)
            .withOutput ("Outputs",  juce::AudioChannelSet::discreteChannels(4), true)
      )
    , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Cache parameter pointers
    threshold1Param = apvts.getRawParameterValue(paramIdThreshold1);
    threshold2Param = apvts.getRawParameterValue(paramIdThreshold2);
    threshold3Param = apvts.getRawParameterValue(paramIdThreshold3);
    threshold4Param = apvts.getRawParameterValue(paramIdThreshold4);
    
    // Initialize lastOutputValues for cable inspector (4 outputs)
    for (int i = 0; i < 4; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout SequentialSwitchModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{paramIdThreshold1, 1},
        "Threshold 1",
        0.0f,  // min
        1.0f,  // max
        0.5f   // default
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{paramIdThreshold2, 1},
        "Threshold 2",
        0.0f,  // min
        1.0f,  // max
        0.5f   // default
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{paramIdThreshold3, 1},
        "Threshold 3",
        0.0f,  // min
        1.0f,  // max
        0.5f   // default
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{paramIdThreshold4, 1},
        "Threshold 4",
        0.0f,  // min
        1.0f,  // max
        0.5f   // default
    ));

    return layout;
}

void SequentialSwitchModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void SequentialSwitchModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    // Get input bus
    auto inBus = getBusBuffer(buffer, true, 0);

    // Get output bus (4 channels)
    auto outBus = getBusBuffer(buffer, false, 0);

    // Safety check
    if (outBus.getNumChannels() == 0)
        return;

    const int numSamples = buffer.getNumSamples();

    // Check which thresholds are modulated
    const bool isThresh1Mod = isParamInputConnected(paramIdThreshold1Mod);
    const bool isThresh2Mod = isParamInputConnected(paramIdThreshold2Mod);
    const bool isThresh3Mod = isParamInputConnected(paramIdThreshold3Mod);
    const bool isThresh4Mod = isParamInputConnected(paramIdThreshold4Mod);

    // Get input pointers
    const float* gateIn    = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* thresh1CV = isThresh1Mod && inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* thresh2CV = isThresh2Mod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* thresh3CV = isThresh3Mod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* thresh4CV = isThresh4Mod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    // Get base threshold values
    const float baseThreshold1 = threshold1Param->load();
    const float baseThreshold2 = threshold2Param->load();
    const float baseThreshold3 = threshold3Param->load();
    const float baseThreshold4 = threshold4Param->load();

    // Process each sample
    for (int i = 0; i < numSamples; ++i)
    {
        // Read input signal (default to 0.0 if not connected)
        const float inputSignal = gateIn ? gateIn[i] : 0.0f;

        // Calculate modulated thresholds
        float threshold1 = baseThreshold1;
        if (isThresh1Mod && thresh1CV)
            threshold1 = juce::jlimit(0.0f, 1.0f, thresh1CV[i]);

        float threshold2 = baseThreshold2;
        if (isThresh2Mod && thresh2CV)
            threshold2 = juce::jlimit(0.0f, 1.0f, thresh2CV[i]);

        float threshold3 = baseThreshold3;
        if (isThresh3Mod && thresh3CV)
            threshold3 = juce::jlimit(0.0f, 1.0f, thresh3CV[i]);

        float threshold4 = baseThreshold4;
        if (isThresh4Mod && thresh4CV)
            threshold4 = juce::jlimit(0.0f, 1.0f, thresh4CV[i]);

        // Output to each channel based on threshold comparison
        // If input >= threshold, pass the signal, otherwise output 0
        outBus.setSample(0, i, inputSignal >= threshold1 ? inputSignal : 0.0f);
        outBus.setSample(1, i, inputSignal >= threshold2 ? inputSignal : 0.0f);
        outBus.setSample(2, i, inputSignal >= threshold3 ? inputSignal : 0.0f);
        outBus.setSample(3, i, inputSignal >= threshold4 ? inputSignal : 0.0f);

        // Update live values periodically for UI
        if ((i & 0x3F) == 0)
        {
            setLiveParamValue("threshold1_live", threshold1);
            setLiveParamValue("threshold2_live", threshold2);
            setLiveParamValue("threshold3_live", threshold3);
            setLiveParamValue("threshold4_live", threshold4);
        }
    }
}

#if defined(PRESET_CREATOR_UI)
void SequentialSwitchModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String& paramId)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);

    // Threshold 1
    {
        const bool thresh1IsMod = isParamModulated(paramIdThreshold1Mod);
        float thresh1 = thresh1IsMod ? getLiveParamValueFor(paramIdThreshold1Mod, "threshold1_live", threshold1Param->load())
                                     : threshold1Param->load();

        if (thresh1IsMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Threshold 1", &thresh1, 0.0f, 1.0f, "%.3f"))
        {
            if (!thresh1IsMod)
                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdThreshold1)))
                    *param = thresh1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && onModificationEnded)
            onModificationEnded();
        if (thresh1IsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }

    // Threshold 2
    {
        const bool thresh2IsMod = isParamModulated(paramIdThreshold2Mod);
        float thresh2 = thresh2IsMod ? getLiveParamValueFor(paramIdThreshold2Mod, "threshold2_live", threshold2Param->load())
                                     : threshold2Param->load();

        if (thresh2IsMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Threshold 2", &thresh2, 0.0f, 1.0f, "%.3f"))
        {
            if (!thresh2IsMod)
                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdThreshold2)))
                    *param = thresh2;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && onModificationEnded)
            onModificationEnded();
        if (thresh2IsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }

    // Threshold 3
    {
        const bool thresh3IsMod = isParamModulated(paramIdThreshold3Mod);
        float thresh3 = thresh3IsMod ? getLiveParamValueFor(paramIdThreshold3Mod, "threshold3_live", threshold3Param->load())
                                     : threshold3Param->load();

        if (thresh3IsMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Threshold 3", &thresh3, 0.0f, 1.0f, "%.3f"))
        {
            if (!thresh3IsMod)
                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdThreshold3)))
                    *param = thresh3;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && onModificationEnded)
            onModificationEnded();
        if (thresh3IsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }

    // Threshold 4
    {
        const bool thresh4IsMod = isParamModulated(paramIdThreshold4Mod);
        float thresh4 = thresh4IsMod ? getLiveParamValueFor(paramIdThreshold4Mod, "threshold4_live", threshold4Param->load())
                                     : threshold4Param->load();

        if (thresh4IsMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Threshold 4", &thresh4, 0.0f, 1.0f, "%.3f"))
        {
            if (!thresh4IsMod)
                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdThreshold4)))
                    *param = thresh4;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && onModificationEnded)
            onModificationEnded();
        if (thresh4IsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }

    ImGui::PopItemWidth();
}

void SequentialSwitchModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Draw input pins (left side)
    helpers.drawAudioInputPin("Gate In", 0);
    helpers.drawAudioInputPin("Thresh 1 CV", 1);
    helpers.drawAudioInputPin("Thresh 2 CV", 2);
    helpers.drawAudioInputPin("Thresh 3 CV", 3);
    helpers.drawAudioInputPin("Thresh 4 CV", 4);

    // Draw output pins (right side)
    helpers.drawAudioOutputPin("Out 1", 0);
    helpers.drawAudioOutputPin("Out 2", 1);
    helpers.drawAudioOutputPin("Out 3", 2);
    helpers.drawAudioOutputPin("Out 4", 3);
}

juce::String SequentialSwitchModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Gate In";
        case 1: return "Thresh 1 CV";
        case 2: return "Thresh 2 CV";
        case 3: return "Thresh 3 CV";
        case 4: return "Thresh 4 CV";
        default: return "";
    }
}

juce::String SequentialSwitchModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Out 1";
        case 1: return "Out 2";
        case 2: return "Out 3";
        case 3: return "Out 4";
        default: return "";
    }
}
#endif

bool SequentialSwitchModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdThreshold1Mod) { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdThreshold2Mod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdThreshold3Mod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdThreshold4Mod) { outChannelIndexInBus = 4; return true; }
    return false;
}
