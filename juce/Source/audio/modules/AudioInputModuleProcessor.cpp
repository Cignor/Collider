#include "AudioInputModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout AudioInputModuleProcessor::
    createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(
        std::make_unique<juce::AudioParameterInt>(
            paramIdNumChannels, "Channels", 1, MAX_CHANNELS, 2));

    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdGateThreshold, "Gate Threshold", 0.0f, 1.0f, 0.1f));
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdTriggerThreshold, "Trigger Threshold", 0.0f, 1.0f, 0.5f));

    // Create a parameter for each potential channel mapping
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        params.push_back(
            std::make_unique<juce::AudioParameterInt>(
                "channelMap" + juce::String(i),
                "Channel " + juce::String(i + 1) + " Source",
                0,
                255,
                i));
    }
    return {params.begin(), params.end()};
}

AudioInputModuleProcessor::AudioInputModuleProcessor()
    : ModuleProcessor(
          BusesProperties()
              .withInput("In", juce::AudioChannelSet::discreteChannels(MAX_CHANNELS), true)
              .withOutput(
                  "Out",
                  juce::AudioChannelSet::discreteChannels(MAX_CHANNELS + 3),
                  true)), // +3 for Gate, Trig, EOP
      apvts(*this, nullptr, "AudioInputParams", createParameterLayout())
{
    numChannelsParam =
        dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdNumChannels));

    gateThresholdParam =
        dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdGateThreshold));
    triggerThresholdParam =
        dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdTriggerThreshold));

    channelMappingParams.resize(MAX_CHANNELS);
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        channelMappingParams[i] = dynamic_cast<juce::AudioParameterInt*>(
            apvts.getParameter("channelMap" + juce::String(i)));
    }

    lastOutputValues.resize(MAX_CHANNELS + 3); // +3 for new outputs
    channelLevels.resize(MAX_CHANNELS);
    for (int i = 0; i < MAX_CHANNELS + 3; ++i)
    {
        if (i < (int)channelLevels.size())
            channelLevels[i] = std::make_unique<std::atomic<float>>(0.0f);
        lastOutputValues[i] = std::make_unique<std::atomic<float>>(0.0f);
    }

    peakState.resize(MAX_CHANNELS, PeakState::SILENT);
    lastTriggerState.resize(MAX_CHANNELS, false);
    silenceCounter.resize(MAX_CHANNELS, 0);
    eopPulseRemaining.resize(MAX_CHANNELS, 0);
    trigPulseRemaining.resize(MAX_CHANNELS, 0);
}

void AudioInputModuleProcessor::prepareToPlay(double, int)
{
    // Reset all state variables
    std::fill(peakState.begin(), peakState.end(), PeakState::SILENT);
    std::fill(lastTriggerState.begin(), lastTriggerState.end(), false);
    std::fill(silenceCounter.begin(), silenceCounter.end(), 0);
    std::fill(eopPulseRemaining.begin(), eopPulseRemaining.end(), 0);
    std::fill(trigPulseRemaining.begin(), trigPulseRemaining.end(), 0);
}

void AudioInputModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int    activeChannels = numChannelsParam ? numChannelsParam->get() : 2;
    const int    numSamples = buffer.getNumSamples();
    const double sampleRate = getSampleRate();

    const float gateThresh = gateThresholdParam ? gateThresholdParam->get() : 0.1f;
    const float trigThresh = triggerThresholdParam ? triggerThresholdParam->get() : 0.5f;

    const int gateOutChannel = MAX_CHANNELS + 0;
    const int trigOutChannel = MAX_CHANNELS + 1;
    const int eopOutChannel = MAX_CHANNELS + 2;

    auto* gateOut =
        outBus.getNumChannels() > gateOutChannel ? outBus.getWritePointer(gateOutChannel) : nullptr;
    auto* trigOut =
        outBus.getNumChannels() > trigOutChannel ? outBus.getWritePointer(trigOutChannel) : nullptr;
    auto* eopOut =
        outBus.getNumChannels() > eopOutChannel ? outBus.getWritePointer(eopOutChannel) : nullptr;

    // Perform CV analysis on the FIRST MAPPED CHANNEL only
    // This allows the user to select which input drives the Gate/Trigger logic
    int cvSourceIndex = -1;
    if (activeChannels > 0 && !channelMappingParams.empty() && channelMappingParams[0])
    {
        cvSourceIndex = channelMappingParams[0]->get();
    }

    if (cvSourceIndex >= 0 && cvSourceIndex < inBus.getNumChannels())
    {
        const float* inData = inBus.getReadPointer(cvSourceIndex);

        for (int s = 0; s < numSamples; ++s)
        {
            const float sampleAbs = std::abs(inData[s]);

            // GATE LOGIC
            if (gateOut)
                gateOut[s] = (sampleAbs > gateThresh) ? 1.0f : 0.0f;

            // TRIGGER LOGIC
            bool isAboveTrig = sampleAbs > trigThresh;
            if (isAboveTrig && !lastTriggerState[0])
            {
                trigPulseRemaining[0] = (int)(0.001 * sampleRate); // 1ms pulse
            }
            lastTriggerState[0] = isAboveTrig;

            if (trigOut)
            {
                trigOut[s] = (trigPulseRemaining[0] > 0) ? 1.0f : 0.0f;
                if (trigPulseRemaining[0] > 0)
                    --trigPulseRemaining[0];
            }

            // EOP LOGIC
            if (peakState[0] == PeakState::PEAK)
            {
                if (sampleAbs < gateThresh)
                {
                    silenceCounter[0]++;
                    if (silenceCounter[0] >= MIN_SILENCE_SAMPLES)
                    {
                        peakState[0] = PeakState::SILENT;
                        eopPulseRemaining[0] = (int)(0.001 * sampleRate); // Fire 1ms pulse
                    }
                }
                else
                {
                    silenceCounter[0] = 0;
                }
            }
            else
            { // PeakState::SILENT
                if (sampleAbs > gateThresh)
                {
                    peakState[0] = PeakState::PEAK;
                    silenceCounter[0] = 0;
                }
            }

            if (eopOut)
            {
                eopOut[s] = (eopPulseRemaining[0] > 0) ? 1.0f : 0.0f;
                if (eopPulseRemaining[0] > 0)
                    --eopPulseRemaining[0];
            }
        }
    }
    else
    {
        // If the source for CV is invalid, clear CV outputs
        if (gateOut)
            juce::FloatVectorOperations::clear(gateOut, numSamples);
        if (trigOut)
            juce::FloatVectorOperations::clear(trigOut, numSamples);
        if (eopOut)
            juce::FloatVectorOperations::clear(eopOut, numSamples);
    }

    // Now, loop through the active channels for pass-through and metering
    for (int i = 0; i < activeChannels; ++i)
    {
        // Determine source channel from parameters
        int sourceChannelIndex = i; // Default fallback
        if (i < (int)channelMappingParams.size() && channelMappingParams[i])
            sourceChannelIndex = channelMappingParams[i]->get();

        if (i < outBus.getNumChannels())
        {
            if (sourceChannelIndex >= 0 && sourceChannelIndex < inBus.getNumChannels())
            {
                // Valid source: copy audio and update meter
                outBus.copyFrom(i, 0, inBus, sourceChannelIndex, 0, numSamples);

                float peakForMeter = inBus.getMagnitude(sourceChannelIndex, 0, numSamples);
                if (i < (int)channelLevels.size() && channelLevels[i])
                {
                    channelLevels[i]->store(peakForMeter);
                }
            }
            else
            {
                // Invalid source: clear output and meter
                outBus.clear(i, 0, numSamples);
                if (i < (int)channelLevels.size() && channelLevels[i])
                {
                    channelLevels[i]->store(0.0f);
                }
            }

            // Update inspector value for this output channel
            if (i < (int)lastOutputValues.size() && lastOutputValues[i] && numSamples > 0)
                lastOutputValues[i]->store(outBus.getSample(i, numSamples - 1));
        }
    }

    // Clear any unused audio output channels (but NOT the CV channels)
    for (int i = activeChannels; i < MAX_CHANNELS; ++i)
    {
        if (i < outBus.getNumChannels())
            outBus.clear(i, 0, numSamples);
        if (i < (int)channelLevels.size() && channelLevels[i])
            channelLevels[i]->store(0.0f);
    }

    // Update the inspector values for the new CV outs
    if (numSamples > 0)
    {
        if (gateOut && lastOutputValues[gateOutChannel])
            lastOutputValues[gateOutChannel]->store(gateOut[numSamples - 1]);
        if (trigOut && lastOutputValues[trigOutChannel])
            lastOutputValues[trigOutChannel]->store(trigOut[numSamples - 1]);
        if (eopOut && lastOutputValues[eopOutChannel])
            lastOutputValues[eopOutChannel]->store(eopOut[numSamples - 1]);
    }
}

juce::ValueTree AudioInputModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("AudioInputState");
    vt.setProperty("deviceName", selectedDeviceName, nullptr);
    vt.addChild(apvts.state.createCopy(), -1, nullptr);
    return vt;
}

void AudioInputModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("AudioInputState"))
    {
        selectedDeviceName = vt.getProperty("deviceName", "").toString();
        auto params = vt.getChildWithName(apvts.state.getType());
        if (params.isValid())
        {
            apvts.replaceState(params);
        }
    }
}

juce::String AudioInputModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel < MAX_CHANNELS)
        return "HW In " + juce::String(channel + 1);
    return {};
}

juce::String AudioInputModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == MAX_CHANNELS + 0)
        return "Gate";
    if (channel == MAX_CHANNELS + 1)
        return "Trigger";
    if (channel == MAX_CHANNELS + 2)
        return "EOP";

    if (channel < MAX_CHANNELS)
        return "Out " + juce::String(channel + 1);
    return {};
}

#if defined(PRESET_CREATOR_UI)
// The actual UI drawing will be handled by a special case in ImGuiNodeEditorComponent,
// so this can remain empty.
void AudioInputModuleProcessor::drawParametersInNode(
    float,
    const std::function<bool(const juce::String&)>&,
    const std::function<void()>&)
{
    // UI is custom-drawn in ImGuiNodeEditorComponent.cpp
}

void AudioInputModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Dynamically draw output pins based on the number of active channels
    int numChannels = numChannelsParam ? numChannelsParam->get() : 2;
    for (int i = 0; i < numChannels; ++i)
    {
        helpers.drawAudioOutputPin(("Out " + juce::String(i + 1)).toRawUTF8(), i);
    }

    // Draw CV outputs
    helpers.drawAudioOutputPin("Gate", MAX_CHANNELS + 0);
    helpers.drawAudioOutputPin("Trigger", MAX_CHANNELS + 1);
    helpers.drawAudioOutputPin("EOP", MAX_CHANNELS + 2);
}
#endif
