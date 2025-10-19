#include "LFOModuleProcessor.h"

LFOModuleProcessor::LFOModuleProcessor()
    // CORRECTED: Use a single input bus with 3 discrete channels
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // ch0:Rate, ch1:Depth, ch2:Wave
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "LFOParams", createParameterLayout())
{
    rateParam = apvts.getRawParameterValue(paramIdRate);
    depthParam = apvts.getRawParameterValue(paramIdDepth);
    bipolarParam = apvts.getRawParameterValue(paramIdBipolar);
    waveParam = apvts.getRawParameterValue(paramIdWave);
    
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    
    osc.initialise([](float x) { return std::sin(x); }, 128);
}

juce::AudioProcessorValueTreeState::ParameterLayout LFOModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate", juce::NormalisableRange<float>(0.05f, 20.0f, 0.01f, 0.3f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDepth, "Depth", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(paramIdBipolar, "Bipolar", true));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdWave, "Wave", juce::StringArray{ "Sine", "Tri", "Saw" }, 0));
    return { p.begin(), p.end() };
}

void LFOModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 2 };
    osc.prepare(spec);
}

void LFOModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto out = getBusBuffer(buffer, false, 0);

    // CORRECTED: All inputs are on a single bus at index 0
    auto inBus = getBusBuffer(buffer, true, 0);
    
    // CORRECTED: Use the _mod IDs to check for connections
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isDepthMod = isParamInputConnected(paramIdDepthMod);
    const bool isWaveMod = isParamInputConnected(paramIdWaveMod);

    // CORRECTED: Read CVs from the correct channels on the single input bus
    const float* rateCV = isRateMod && inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* depthCV = isDepthMod && inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* waveCV = isWaveMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;

    const float baseRate = rateParam->load();
    const float baseDepth = depthParam->load();
    const int baseWave = static_cast<int>(waveParam->load());
    const bool bipolar = bipolarParam->load() > 0.5f;

    float lastRate = baseRate, lastDepth = baseDepth;
    int lastWave = baseWave;

    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        float finalRate = baseRate;
        if (isRateMod && rateCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            finalRate = baseRate * std::pow(4.0f, cv - 0.5f); // Modulate by +/- 2 octaves
        }
        
        float depth = baseDepth;
        if (isDepthMod && depthCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, depthCV[i]);
            depth = juce::jlimit(0.0f, 1.0f, baseDepth + (cv - 0.5f)); // Additive modulation
        }
        
        int w = baseWave;
        if (isWaveMod && waveCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, waveCV[i]);
            w = static_cast<int>(cv * 2.99f); // Absolute control
        }

        lastRate = finalRate;
        lastDepth = depth;
        lastWave = w;
        
        if (currentWaveform != w) {
            if (w == 0)      osc.initialise ([](float x){ return std::sin(x); }, 128);
            else if (w == 1) osc.initialise ([](float x){ return 2.0f / juce::MathConstants<float>::pi * std::asin(std::sin(x)); }, 128);
            else             osc.initialise ([](float x){ return (x / juce::MathConstants<float>::pi); }, 128);
            currentWaveform = w;
        }
        
        osc.setFrequency(finalRate);
        const float lfoSample = osc.processSample(0.0f);
        const float finalSample = (bipolar ? lfoSample : (lfoSample * 0.5f + 0.5f)) * depth;

        out.setSample(0, i, finalSample);
    }
    
    // Update inspector values
    if (lastOutputValues.size() >= 1)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getSample(0, out.getNumSamples() - 1));
    }

    // Store live modulated values for UI display
    setLiveParamValue("rate_live", lastRate);
    setLiveParamValue("depth_live", lastDepth);
    setLiveParamValue("wave_live", (float)lastWave);
}

// CORRECTED: Clean, unambiguous routing for a single multi-channel input bus
bool LFOModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus.
    if (paramId == paramIdRateMod) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdDepthMod) { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdWaveMod) { outChannelIndexInBus = 2; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)

void LFOModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    
    // CORRECTED: Use the _mod IDs to check modulation status
    bool isRateModulated = isParamModulated(paramIdRateMod);
    bool isDepthModulated = isParamModulated(paramIdDepthMod);
    bool isWaveModulated = isParamModulated(paramIdWaveMod);
    
    // CORRECTED: Use _mod IDs as the first argument to getLiveParamValueFor
    float rate = isRateModulated ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
    float depth = isDepthModulated ? getLiveParamValueFor(paramIdDepthMod, "depth_live", depthParam->load()) : depthParam->load();
    int wave = isWaveModulated ? (int)getLiveParamValueFor(paramIdWaveMod, "wave_live", (float)static_cast<int>(waveParam->load())) : static_cast<int>(waveParam->load());
    bool bipolar = bipolarParam->load() > 0.5f;
    
    ImGui::PushItemWidth(itemWidth);

    if (isRateModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic)) if (!isRateModulated) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isRateModulated) onModificationEnded();
    if (isRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    if (isDepthModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Depth", &depth, 0.0f, 1.0f)) if (!isDepthModulated) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDepth)) = depth;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isDepthModulated) onModificationEnded();
    if (isDepthModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (isWaveModulated) ImGui::BeginDisabled();
    if (ImGui::Combo("Wave", &wave, "Sine\0Tri\0Saw\0\0")) if (!isWaveModulated) *dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWave)) = wave;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isWaveModulated) onModificationEnded();
    if (isWaveModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (ImGui::Checkbox("Bipolar", &bipolar)) *dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramIdBipolar)) = bipolar;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

    ImGui::PopItemWidth();
}

void LFOModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Rate Mod", 0);
    helpers.drawAudioInputPin("Depth Mod", 1);
    helpers.drawAudioInputPin("Wave Mod", 2);
    helpers.drawAudioOutputPin("Out", 0);
}

juce::String LFOModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Rate Mod";
        case 1: return "Depth Mod";
        case 2: return "Wave Mod";
        default: return {};
    }
}

juce::String LFOModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Out";
        default: return {};
    }
}
#endif