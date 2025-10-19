#include "GateModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout GateModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdThreshold, "Threshold", -80.0f, 0.0f, -40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdAttack, "Attack", 0.1f, 100.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRelease, "Release", 5.0f, 1000.0f, 50.0f));
    
    return { params.begin(), params.end() };
}

GateModuleProcessor::GateModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          // For now, no modulation inputs. Can be added later if desired.
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GateParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    attackParam = apvts.getRawParameterValue(paramIdAttack);
    releaseParam = apvts.getRawParameterValue(paramIdRelease);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void GateModuleProcessor::prepareToPlay(double newSampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = newSampleRate;
    envelope = 0.0f;
}

void GateModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // Copy input to output
    const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        outBus.copyFrom(ch, 0, inBus, ch, 0, numSamples);
    }

    // Get parameters
    const float thresholdLinear = juce::Decibels::decibelsToGain(thresholdParam->load());
    // Convert attack/release times from ms to a per-sample coefficient
    const float attackCoeff = 1.0f - std::exp(-1.0f / (attackParam->load() * 0.001f * (float)currentSampleRate));
    const float releaseCoeff = 1.0f - std::exp(-1.0f / (releaseParam->load() * 0.001f * (float)currentSampleRate));

    auto* leftData = outBus.getWritePointer(0);
    auto* rightData = numChannels > 1 ? outBus.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Get the magnitude of the input signal (mono or stereo)
        float magnitude = std::abs(leftData[i]);
        if (rightData)
            magnitude = std::max(magnitude, std::abs(rightData[i]));

        // Determine if the gate should be open or closed
        float target = (magnitude >= thresholdLinear) ? 1.0f : 0.0f;

        // Move the envelope towards the target using the appropriate attack or release time
        if (target > envelope)
            envelope += (target - envelope) * attackCoeff;
        else
            envelope += (target - envelope) * releaseCoeff;
        
        // Apply the envelope as a gain to the signal
        leftData[i] *= envelope;
        if (rightData)
            rightData[i] *= envelope;
    }

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(leftData[numSamples - 1]);
        if (lastOutputValues[1] && rightData) lastOutputValues[1]->store(rightData[numSamples - 1]);
    }
}

bool GateModuleProcessor::getParamRouting(const juce::String& /*paramId*/, int& /*outBusIndex*/, int& /*outChannelIndexInBus*/) const
{
    // No modulation inputs in this version
    return false;
}

juce::String GateModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    return {};
}

juce::String GateModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void GateModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
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

    drawSlider("Threshold", paramIdThreshold, -80.0f, 0.0f, "%.1f dB");
    drawSlider("Attack", paramIdAttack, 0.1f, 100.0f, "%.1f ms");
    drawSlider("Release", paramIdRelease, 5.0f, 1000.0f, "%.0f ms");

    ImGui::PopItemWidth();
}

void GateModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

