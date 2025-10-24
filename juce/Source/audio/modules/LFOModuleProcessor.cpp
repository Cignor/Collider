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
    syncParam = apvts.getRawParameterValue(paramIdSync);
    rateDivisionParam = apvts.getRawParameterValue(paramIdRateDivision);
    
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
    p.push_back(std::make_unique<juce::AudioParameterBool>(paramIdSync, "Sync", false));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdRateDivision, "Division", 
        juce::StringArray{ "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" }, 3)); // Default: 1/4 note
    return { p.begin(), p.end() };
}

void LFOModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 2 };
    osc.prepare(spec);
}

void LFOModuleProcessor::setTimingInfo(const TransportState& state)
{
    m_currentTransport = state;
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
    const bool syncEnabled = syncParam->load() > 0.5f;
    int rateDivisionIndex = static_cast<int>(rateDivisionParam->load());
    // If a global division is broadcast by a master clock, adopt it when sync is enabled
    if (syncEnabled && m_currentTransport.globalDivisionIndex >= 0)
        rateDivisionIndex = m_currentTransport.globalDivisionIndex;

    // Rate division map: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8
    static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
    const double beatDivision = divisions[juce::jlimit(0, 8, rateDivisionIndex)];

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
        
        float lfoSample = 0.0f;
        
        if (syncEnabled && m_currentTransport.isPlaying)
        {
            // Transport-synced mode: calculate phase directly from song position
            double phase = std::fmod(m_currentTransport.songPositionBeats * beatDivision, 1.0);
            double phaseRadians = phase * juce::MathConstants<double>::twoPi;
            
            // Generate waveform based on phase
            if (w == 0) // Sine
                lfoSample = std::sin(phaseRadians);
            else if (w == 1) // Triangle
                lfoSample = 2.0f / juce::MathConstants<float>::pi * std::asin(std::sin(phaseRadians));
            else // Saw
                lfoSample = (phaseRadians / juce::MathConstants<float>::pi);
        }
        else
        {
            // Free-running mode: use internal oscillator
            osc.setFrequency(finalRate);
            lfoSample = osc.processSample(0.0f);
        }
        
        const float finalSample = (bipolar ? lfoSample : (lfoSample * 0.5f + 0.5f)) * depth;

        out.setSample(0, i, finalSample);
    }
    
    // Update inspector values
    updateOutputTelemetry(out);

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

// Helper function for tooltip with help marker
static void HelpMarkerLFO(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void LFOModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    
    bool isRateModulated = isParamInputConnected(paramIdRateMod);
    bool isDepthModulated = isParamInputConnected(paramIdDepthMod);
    bool isWaveModulated = isParamInputConnected(paramIdWaveMod);
    
    float rate = isRateModulated ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
    float depth = isDepthModulated ? getLiveParamValueFor(paramIdDepthMod, "depth_live", depthParam->load()) : depthParam->load();
    int wave = isWaveModulated ? (int)getLiveParamValueFor(paramIdWaveMod, "wave_live", (float)static_cast<int>(waveParam->load())) : static_cast<int>(waveParam->load());
    bool bipolar = bipolarParam->load() > 0.5f;
    
    ImGui::PushItemWidth(itemWidth);

    // === LFO PARAMETERS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "LFO Parameters");
    ImGui::Spacing();

    // Rate slider with tooltip
    if (isRateModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic)) if (!isRateModulated) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isRateModulated) onModificationEnded();
    if (isRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarkerLFO("LFO rate in Hz\nLogarithmic scale from 0.05 Hz to 20 Hz");
    
    // Depth slider with tooltip
    if (isDepthModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Depth", &depth, 0.0f, 1.0f)) if (!isDepthModulated) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDepth)) = depth;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isDepthModulated) onModificationEnded();
    if (isDepthModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarkerLFO("LFO depth/amplitude (0-1)\nControls output signal strength");

    // Wave combo with tooltip
    if (isWaveModulated) ImGui::BeginDisabled();
    if (ImGui::Combo("Wave", &wave, "Sine\0Tri\0Saw\0\0")) if (!isWaveModulated) *dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWave)) = wave;
    if (ImGui::IsItemDeactivatedAfterEdit() && !isWaveModulated) onModificationEnded();
    if (isWaveModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarkerLFO("Waveform shape:\nSine = smooth\nTri = linear\nSaw = ramp");

    // Bipolar checkbox
    if (ImGui::Checkbox("Bipolar", &bipolar)) *dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramIdBipolar)) = bipolar;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    ImGui::SameLine();
    HelpMarkerLFO("Bipolar: -1 to +1\nUnipolar: 0 to +1");

    ImGui::Spacing();
    ImGui::Spacing();

    // === TRANSPORT SYNC SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Transport Sync");
    ImGui::Spacing();

    // Sync checkbox
    bool sync = syncParam->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync)) *dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramIdSync)) = sync;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    ImGui::SameLine();
    HelpMarkerLFO("Sync LFO rate to host transport tempo");
    
    if (sync)
    {
        // Division combo
        int division = static_cast<int>(rateDivisionParam->load());
        const char* items[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" };
        if (ImGui::Combo("Division", &division, items, (int)(sizeof(items)/sizeof(items[0]))))
            *dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdRateDivision)) = division;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::SameLine();
        HelpMarkerLFO("Note division for tempo sync\n1/16 = sixteenth notes, 1 = whole notes, etc.");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === LIVE OUTPUT SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live Output");
    ImGui::Spacing();

    // Visual LFO output indicator
    float currentValue = lastOutputValues.size() > 0 ? lastOutputValues[0]->load() : 0.0f;
    ImGui::Text("Output: %.3f", currentValue);

    // Progress bar showing current LFO position
    float normalizedValue = bipolar ? (currentValue + 1.0f) / 2.0f : currentValue;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.55f, 0.7f, 0.8f).Value);
    ImGui::ProgressBar(normalizedValue, ImVec2(itemWidth, 0), "");
    ImGui::PopStyleColor();

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