#include "DriveModuleProcessor.h"
#include <cmath> // For std::tanh

juce::AudioProcessorValueTreeState::ParameterLayout DriveModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDrive, "Drive", 0.0f, 2.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMix, "Mix", 0.0f, 1.0f, 1.0f));
    
    return { params.begin(), params.end() };
}

DriveModuleProcessor::DriveModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DriveParams", createParameterLayout())
{
    driveParam = apvts.getRawParameterValue(paramIdDrive);
    mixParam = apvts.getRawParameterValue(paramIdMix);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void DriveModuleProcessor::prepareToPlay(double /*sampleRate*/, int samplesPerBlock)
{
    tempBuffer.setSize(2, samplesPerBlock);
}

void DriveModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    const float driveAmount = driveParam->load();
    const float mixAmount = mixParam->load();

    // Copy input to output
    const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        outBus.copyFrom(ch, 0, inBus, ch, 0, buffer.getNumSamples());
    }

    // If drive is zero and mix is fully dry, we can skip processing entirely.
    if (driveAmount <= 0.001f && mixAmount <= 0.001f)
    {
        // Update output values for tooltips
        if (lastOutputValues.size() >= 2)
        {
            if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
            if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
        }
        return;
    }

    // --- Dry/Wet Mix Implementation (inspired by VoiceProcessor.cpp) ---
    // 1. Make a copy of the original (dry) signal.
    tempBuffer.makeCopyOf(outBus);

    // 2. Apply the distortion to the temporary buffer to create the wet signal.
    const float k = juce::jlimit(0.0f, 10.0f, driveAmount) * 5.0f;
    for (int ch = 0; ch < tempBuffer.getNumChannels(); ++ch)
    {
        auto* data = tempBuffer.getWritePointer(ch);
        for (int i = 0; i < tempBuffer.getNumSamples(); ++i)
        {
            data[i] = std::tanh(k * data[i]);
        }
    }

    // 3. Blend the dry and wet signals in the main output buffer.
    const float dryLevel = 1.0f - mixAmount;
    const float wetLevel = mixAmount;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        // First, scale the original (dry) signal down.
        outBus.applyGain(ch, 0, buffer.getNumSamples(), dryLevel);
        // Then, add the scaled wet signal from our temporary buffer.
        outBus.addFrom(ch, 0, tempBuffer, ch, 0, buffer.getNumSamples(), wetLevel);
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool DriveModuleProcessor::getParamRouting(const juce::String& /*paramId*/, int& /*outBusIndex*/, int& /*outChannelIndexInBus*/) const
{
    // No modulation inputs in this version
    return false;
}

juce::String DriveModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    return {};
}

juce::String DriveModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void DriveModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    auto drawSlider = [&](const char* label, const juce::String& paramId, float min, float max, const char* format) {
        float value = ap.getRawParameterValue(paramId)->load();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    };

    drawSlider("Drive", paramIdDrive, 0.0f, 2.0f, "%.2f");
    drawSlider("Mix", paramIdMix, 0.0f, 1.0f, "%.2f");

    ImGui::PopItemWidth();
}

void DriveModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

