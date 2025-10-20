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
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(7), true) // 0-1: Audio In, 2-6: Threshold/Ratio/Attack/Release/Makeup Mods
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
    auto outBus = getBusBuffer(buffer, false, 0);

    // Copy input to output for in-place processing
    const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        outBus.copyFrom(ch, 0, inBus, ch, 0, buffer.getNumSamples());
    }

    // --- Update DSP Parameters from unified input bus (once per block) ---
    float finalThreshold = thresholdParam->load();
    if (isParamInputConnected(paramIdThresholdMod) && inBus.getNumChannels() > 2)
        finalThreshold = juce::jmap(inBus.getSample(2, 0), 0.0f, 1.0f, -60.0f, 0.0f);
        
    float finalRatio = ratioParam->load();
    if (isParamInputConnected(paramIdRatioMod) && inBus.getNumChannels() > 3)
        finalRatio = juce::jmap(inBus.getSample(3, 0), 0.0f, 1.0f, 1.0f, 20.0f);

    float finalAttack = attackParam->load();
    if (isParamInputConnected(paramIdAttackMod) && inBus.getNumChannels() > 4)
        finalAttack = juce::jmap(inBus.getSample(4, 0), 0.0f, 1.0f, 0.1f, 200.0f);

    float finalRelease = releaseParam->load();
    if (isParamInputConnected(paramIdReleaseMod) && inBus.getNumChannels() > 5)
        finalRelease = juce::jmap(inBus.getSample(5, 0), 0.0f, 1.0f, 5.0f, 1000.0f);
        
    float finalMakeup = makeupParam->load();
    if (isParamInputConnected(paramIdMakeupMod) && inBus.getNumChannels() > 6)
        finalMakeup = juce::jmap(inBus.getSample(6, 0), 0.0f, 1.0f, -12.0f, 12.0f);

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
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == paramIdThresholdMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdRatioMod)     { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdAttackMod)    { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdReleaseMod)   { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdMakeupMod)    { outChannelIndexInBus = 6; return true; }
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

    // Lambda that correctly handles modulation
    auto drawSlider = [&](const char* label, const juce::String& paramId, const juce::String& modId, float min, float max, const char* format) {
        bool isMod = isParamModulated(modId);
        float value = isMod ? getLiveParamValueFor(modId, paramId + juce::String("_live"), ap.getRawParameterValue(paramId)->load())
                            : ap.getRawParameterValue(paramId)->load();
        
        if (isMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    };

    drawSlider("Threshold", paramIdThreshold, paramIdThresholdMod, -60.0f, 0.0f, "%.1f dB");
    drawSlider("Ratio", paramIdRatio, paramIdRatioMod, 1.0f, 20.0f, "%.1f : 1");
    drawSlider("Attack", paramIdAttack, paramIdAttackMod, 0.1f, 200.0f, "%.1f ms");
    drawSlider("Release", paramIdRelease, paramIdReleaseMod, 5.0f, 1000.0f, "%.0f ms");
    drawSlider("Makeup", paramIdMakeup, paramIdMakeupMod, -12.0f, 12.0f, "%.1f dB");

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

