#include "LimiterModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout LimiterModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdThreshold, "Threshold", -20.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRelease, "Release", 1.0f, 200.0f, 10.0f));
    
    return { params.begin(), params.end() };
}

LimiterModuleProcessor::LimiterModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          .withInput("Modulation In", juce::AudioChannelSet::discreteChannels(2), true) // Threshold, Release
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "LimiterParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    releaseParam = apvts.getRawParameterValue(paramIdRelease);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void LimiterModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 2;

    limiter.prepare(spec);
    limiter.reset();
}

void LimiterModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto modBus = getBusBuffer(buffer, true, 1);
    auto outBus = getBusBuffer(buffer, false, 0);

    // Copy input to output for in-place processing
    const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        outBus.copyFrom(ch, 0, inBus, ch, 0, buffer.getNumSamples());
    }

    // --- Update DSP Parameters (once per block) ---
    float finalThreshold = thresholdParam->load();
    if (isParamInputConnected(paramIdThresholdMod) && modBus.getNumChannels() > 0)
        finalThreshold = juce::jmap(modBus.getSample(0, 0), 0.0f, 1.0f, -20.0f, 0.0f);
        
    float finalRelease = releaseParam->load();
    if (isParamInputConnected(paramIdReleaseMod) && modBus.getNumChannels() > 1)
        finalRelease = juce::jmap(modBus.getSample(1, 0), 0.0f, 1.0f, 1.0f, 200.0f);

    limiter.setThreshold(finalThreshold);
    limiter.setRelease(finalRelease);
    
    // --- Process the Audio ---
    juce::dsp::AudioBlock<float> block(outBus);
    juce::dsp::ProcessContextReplacing<float> context(block);
    limiter.process(context);

    // --- Update UI Telemetry & Tooltips ---
    setLiveParamValue("threshold_live", finalThreshold);
    setLiveParamValue("release_live", finalRelease);
    
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool LimiterModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 1; // All modulation on Bus 1
    if (paramId == paramIdThresholdMod) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdReleaseMod)   { outChannelIndexInBus = 1; return true; }
    return false;
}

juce::String LimiterModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    if (channel == 2) return "Thresh Mod";
    if (channel == 3) return "Release Mod";
    return {};
}

juce::String LimiterModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void LimiterModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    auto drawSlider = [&](const char* label, const juce::String& paramId, const juce::String& modId, float min, float max, const char* format) {
        bool isMod = isParamModulated(modId);
        float value = isMod ? getLiveParamValueFor(modId, paramId + "_live", ap.getRawParameterValue(paramId)->load())
                            : ap.getRawParameterValue(paramId)->load();
        
        if (isMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    };

    drawSlider("Threshold", paramIdThreshold, paramIdThresholdMod, -20.0f, 0.0f, "%.1f dB");
    drawSlider("Release", paramIdRelease, paramIdReleaseMod, 1.0f, 200.0f, "%.0f ms");

    ImGui::PopItemWidth();
}

void LimiterModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Thresh Mod", 2);
    helpers.drawAudioInputPin("Release Mod", 3);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

