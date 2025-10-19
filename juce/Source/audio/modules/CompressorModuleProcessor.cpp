#include "CompressorModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout CompressorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdThreshold, "Threshold", -60.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRatio, "Ratio", 1.0f, 20.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdAttack, "Attack", 0.1f, 200.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRelease, "Release", 5.0f, 1000.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMakeup, "Makeup Gain", -12.0f, 12.0f, 0.0f));
    
    return { params.begin(), params.end() };
}

CompressorModuleProcessor::CompressorModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          // Add 5 mono mod inputs, one for each parameter
          .withInput("Modulation In", juce::AudioChannelSet::discreteChannels(5), true)
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CompressorParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    ratioParam = apvts.getRawParameterValue(paramIdRatio);
    attackParam = apvts.getRawParameterValue(paramIdAttack);
    releaseParam = apvts.getRawParameterValue(paramIdRelease);
    makeupParam = apvts.getRawParameterValue(paramIdMakeup);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void CompressorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 2;

    compressor.prepare(spec);
    compressor.reset();
}

void CompressorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    // (A per-sample loop would be needed for audio-rate modulation, but this is efficient and common)
    float finalThreshold = thresholdParam->load();
    if (isParamInputConnected(paramIdThreshold) && modBus.getNumChannels() > 0)
        finalThreshold = juce::jmap(modBus.getSample(0, 0), 0.0f, 1.0f, -60.0f, 0.0f);
        
    float finalRatio = ratioParam->load();
    if (isParamInputConnected(paramIdRatio) && modBus.getNumChannels() > 1)
        finalRatio = juce::jmap(modBus.getSample(1, 0), 0.0f, 1.0f, 1.0f, 20.0f);

    float finalAttack = attackParam->load();
    if (isParamInputConnected(paramIdAttack) && modBus.getNumChannels() > 2)
        finalAttack = juce::jmap(modBus.getSample(2, 0), 0.0f, 1.0f, 0.1f, 200.0f);

    float finalRelease = releaseParam->load();
    if (isParamInputConnected(paramIdRelease) && modBus.getNumChannels() > 3)
        finalRelease = juce::jmap(modBus.getSample(3, 0), 0.0f, 1.0f, 5.0f, 1000.0f);
        
    float finalMakeup = makeupParam->load();
    if (isParamInputConnected(paramIdMakeup) && modBus.getNumChannels() > 4)
        finalMakeup = juce::jmap(modBus.getSample(4, 0), 0.0f, 1.0f, -12.0f, 12.0f);

    compressor.setThreshold(finalThreshold);
    compressor.setRatio(finalRatio);
    compressor.setAttack(finalAttack);
    compressor.setRelease(finalRelease);
    
    // --- Process the Audio ---
    juce::dsp::AudioBlock<float> block(outBus);
    juce::dsp::ProcessContextReplacing<float> context(block);
    compressor.process(context);

    // Apply makeup gain
    outBus.applyGain(juce::Decibels::decibelsToGain(finalMakeup));

    // --- Update UI Telemetry & Tooltips ---
    setLiveParamValue("threshold_live", finalThreshold);
    setLiveParamValue("ratio_live", finalRatio);
    setLiveParamValue("attack_live", finalAttack);
    setLiveParamValue("release_live", finalRelease);
    setLiveParamValue("makeup_live", finalMakeup);
    
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool CompressorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 1; // All modulation is on Bus 1
    if (paramId == paramIdThreshold) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdRatio)     { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdAttack)    { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdRelease)   { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdMakeup)    { outChannelIndexInBus = 4; return true; }
    return false;
}

juce::String CompressorModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    // Modulation bus starts at channel 2
    if (channel == 2) return "Thresh Mod";
    if (channel == 3) return "Ratio Mod";
    if (channel == 4) return "Attack Mod";
    if (channel == 5) return "Release Mod";
    if (channel == 6) return "Makeup Mod";
    return {};
}

juce::String CompressorModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void CompressorModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
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

    drawSlider("Threshold", paramIdThreshold, -60.0f, 0.0f, "%.1f dB");
    drawSlider("Ratio", paramIdRatio, 1.0f, 20.0f, "%.1f : 1");
    drawSlider("Attack", paramIdAttack, 0.1f, 200.0f, "%.1f ms");
    drawSlider("Release", paramIdRelease, 5.0f, 1000.0f, "%.0f ms");
    drawSlider("Makeup", paramIdMakeup, -12.0f, 12.0f, "%.1f dB");

    ImGui::PopItemWidth();
}

void CompressorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Thresh Mod", 2);
    helpers.drawAudioInputPin("Ratio Mod", 3);
    helpers.drawAudioInputPin("Attack Mod", 4);
    helpers.drawAudioInputPin("Release Mod", 5);
    helpers.drawAudioInputPin("Makeup Mod", 6);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

