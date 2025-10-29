#include "HarmonicShaperModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout HarmonicShaperModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Global Parameters ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMasterFreq, "Master Frequency",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 440.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMasterDrive, "Master Drive", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdOutputGain, "Output Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.5f));

    // --- Per-Oscillator Parameters ---
    for (int i = 0; i < NUM_OSCILLATORS; ++i)
    {
        auto idx = juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>("ratio_" + idx, "Ratio " + idx,
            juce::NormalisableRange<float>(0.125f, 16.0f, 0.001f, 0.25f), (float)(i + 1)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("detune_" + idx, "Detune " + idx, -100.0f, 100.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("waveform_" + idx, "Waveform " + idx,
            juce::StringArray{ "Sine", "Saw", "Square", "Triangle" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("drive_" + idx, "Drive " + idx, 0.0f, 1.0f, 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("level_" + idx, "Level " + idx, 0.0f, 1.0f, i == 0 ? 1.0f : 0.0f));
    }
    
    // Relative modulation parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeFreqMod", "Relative Freq Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeDriveMod", "Relative Drive Mod", true));

    return { params.begin(), params.end() };
}

HarmonicShaperModuleProcessor::HarmonicShaperModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
        .withInput("Modulation", juce::AudioChannelSet::discreteChannels(2), true) // Freq, Drive
        .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HarmonicShaperParams", createParameterLayout())
{
    // Cache global parameter pointers
    masterFreqParam = apvts.getRawParameterValue(paramIdMasterFreq);
    masterDriveParam = apvts.getRawParameterValue(paramIdMasterDrive);
    outputGainParam = apvts.getRawParameterValue(paramIdOutputGain);

    // Initialize oscillators and cache per-oscillator parameter pointers
    for (int i = 0; i < NUM_OSCILLATORS; ++i)
    {
        oscillators[i].initialise([](float x) { return std::sin(x); }, 128);
        currentWaveforms[i] = -1; // Force initial waveform setup

        auto idx = juce::String(i + 1);
        ratioParams[i] = apvts.getRawParameterValue("ratio_" + idx);
        detuneParams[i] = apvts.getRawParameterValue("detune_" + idx);
        waveformParams[i] = apvts.getRawParameterValue("waveform_" + idx);
        driveParams[i] = apvts.getRawParameterValue("drive_" + idx);
        levelParams[i] = apvts.getRawParameterValue("level_" + idx);
    }
    
    relativeFreqModParam = apvts.getRawParameterValue("relativeFreqMod");
    relativeDriveModParam = apvts.getRawParameterValue("relativeDriveMod");
}

void HarmonicShaperModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };
    for (auto& osc : oscillators)
    {
        osc.prepare(spec);
    }
    smoothedMasterFreq.reset(sampleRate, 0.02);
    smoothedMasterDrive.reset(sampleRate, 0.02);
}

void HarmonicShaperModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto audioInBus = getBusBuffer(buffer, true, 0);
    auto modInBus = getBusBuffer(buffer, true, 1);
    auto outBus = getBusBuffer(buffer, false, 0);

    const bool isFreqMod = isParamInputConnected(paramIdMasterFreqMod);
    const bool isDriveMod = isParamInputConnected(paramIdMasterDriveMod);

    const float* freqCV = isFreqMod && modInBus.getNumChannels() > 0 ? modInBus.getReadPointer(0) : nullptr;
    const float* driveCV = isDriveMod && modInBus.getNumChannels() > 1 ? modInBus.getReadPointer(1) : nullptr;

    const float baseFrequency = masterFreqParam->load();
    const float baseMasterDrive = masterDriveParam->load();
    const float outputGain = outputGainParam->load();
    const bool relativeFreqMode = relativeFreqModParam && relativeFreqModParam->load() > 0.5f;
    const bool relativeDriveMode = relativeDriveModParam && relativeDriveModParam->load() > 0.5f;
    
    auto* outL = outBus.getWritePointer(0);
    auto* outR = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : outL;
    
    const float* inL = audioInBus.getReadPointer(0);
    const float* inR = audioInBus.getNumChannels() > 1 ? audioInBus.getReadPointer(1) : inL;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // === 1. Calculate Global Modulated Parameters (per-sample) ===
        float currentMasterFreq = baseFrequency;
        if (freqCV) {
            const float cv = juce::jlimit(0.0f, 1.0f, freqCV[i]);
            if (relativeFreqMode) {
                // RELATIVE: ±4 octaves around base frequency
                const float octaveOffset = (cv - 0.5f) * 8.0f;
                currentMasterFreq = baseFrequency * std::pow(2.0f, octaveOffset);
            } else {
                // ABSOLUTE: CV directly sets frequency (20-20000 Hz)
                const float spanOct = std::log2(20000.0f / 20.0f);
                currentMasterFreq = 20.0f * std::pow(2.0f, cv * spanOct);
            }
            currentMasterFreq = juce::jlimit(20.0f, 20000.0f, currentMasterFreq);
        }
        smoothedMasterFreq.setTargetValue(currentMasterFreq);

        float currentMasterDrive = baseMasterDrive;
        if (driveCV) {
            const float cv = juce::jlimit(0.0f, 1.0f, driveCV[i]);
            if (relativeDriveMode) {
                // RELATIVE: ±0.5 offset
                const float offset = (cv - 0.5f) * 1.0f;
                currentMasterDrive = baseMasterDrive + offset;
            } else {
                // ABSOLUTE: CV directly sets drive
                currentMasterDrive = cv;
            }
            currentMasterDrive = juce::jlimit(0.0f, 1.0f, currentMasterDrive);
        }
        smoothedMasterDrive.setTargetValue(currentMasterDrive);
        
        // === 2. Generate and Sum the 8 Oscillators ===
        float carrierSample = 0.0f;
        const float smoothedFreq = smoothedMasterFreq.getNextValue();
        const float smoothedDrive = smoothedMasterDrive.getNextValue();

        for (int osc = 0; osc < NUM_OSCILLATORS; ++osc)
        {
            const float level = levelParams[osc]->load();
            if (level <= 0.001f) continue; // Skip silent oscillators

            const int waveform = (int)waveformParams[osc]->load();
            if (currentWaveforms[osc] != waveform)
            {
                if (waveform == 0)      oscillators[osc].initialise([](float x) { return std::sin(x); });
                else if (waveform == 1) oscillators[osc].initialise([](float x) { return x / juce::MathConstants<float>::pi; });
                else if (waveform == 2) oscillators[osc].initialise([](float x) { return x < 0.0f ? -1.0f : 1.0f; });
                else                    oscillators[osc].initialise([](float x) { return 2.0f / juce::MathConstants<float>::pi * std::asin(std::sin(x)); });
                currentWaveforms[osc] = waveform;
            }

            const float frequency = smoothedFreq * ratioParams[osc]->load() + detuneParams[osc]->load();
            oscillators[osc].setFrequency(juce::jlimit(1.0f, (float)getSampleRate() * 0.5f, frequency), true);

            const float oscSample = oscillators[osc].processSample(0.0f);
            const float drive = driveParams[osc]->load() * smoothedDrive;
            const float shapedSample = std::tanh(oscSample * (1.0f + drive * 9.0f));
            carrierSample += shapedSample * level;
        }

        // Soft-clip the summed carrier signal to prevent extreme levels while preserving harmonic richness
        carrierSample = std::tanh(carrierSample);
        
        // === 3. Modulate Input with Carrier and Apply Gain ===
        outL[i] = inL[i] * carrierSample * outputGain;
        outR[i] = inR[i] * carrierSample * outputGain;

        // === 4. Update UI Telemetry (Throttled) ===
        if ((i & 63) == 0) {
            setLiveParamValue("masterFrequency_live", smoothedFreq);
            setLiveParamValue("masterDrive_live", smoothedDrive);
        }
    }
}

#if defined(PRESET_CREATOR_UI)

void HarmonicShaperModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    
    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };

    // === MASTER CONTROLS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Master Controls");
    ImGui::Spacing();

    const bool freqIsMod = isParamModulated(paramIdMasterFreqMod);
    float freq = freqIsMod ? getLiveParamValueFor(paramIdMasterFreqMod, "masterFrequency_live", masterFreqParam->load()) : masterFreqParam->load();

    const bool driveIsMod = isParamModulated(paramIdMasterDriveMod);
    float drive = driveIsMod ? getLiveParamValueFor(paramIdMasterDriveMod, "masterDrive_live", masterDriveParam->load()) : masterDriveParam->load();

    float gain = outputGainParam->load();

    ImGui::PushItemWidth(itemWidth);

    if (freqIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Master Freq", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic)) {
        if (!freqIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMasterFreq))) *p = freq;
    }
    if (!freqIsMod) adjustParamOnWheel(ap.getParameter(paramIdMasterFreq), "masterFreqHz", freq);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (freqIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Base frequency for all harmonics");

    if (driveIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Master Drive", &drive, 0.0f, 1.0f, "%.2f")) {
        if (!driveIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMasterDrive))) *p = drive;
    }
    if (!driveIsMod) adjustParamOnWheel(ap.getParameter(paramIdMasterDrive), "masterDrive", drive);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (driveIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Overall harmonic richness");

    if (ImGui::SliderFloat("Output Gain", &gain, 0.0f, 1.0f, "%.2f")) {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdOutputGain))) *p = gain;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Spacing();

    // === PER-OSCILLATOR CONTROLS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Oscillators");
    ImGui::Spacing();
    
    for (int i = 0; i < NUM_OSCILLATORS; ++i)
    {
        auto idx = juce::String(i + 1);
        ImGui::PushID(i);
        ImGui::Text("Oscillator %d", i + 1);
        
        // --- START OF THE ACTUAL FIX: Manual Layout for each control ---
        // Define an alignment position for the sliders
        const float label_width = 80.0f;
        const float control_width = itemWidth - label_width - ImGui::GetStyle().ItemSpacing.x;

        // Level
        float level = levelParams[i]->load();
        ImGui::Text("Level"); ImGui::SameLine(label_width); ImGui::PushItemWidth(control_width);
        if (ImGui::SliderFloat("##level", &level, 0.0f, 1.0f, "%.2f"))
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("level_" + idx))) *p = level;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::PopItemWidth();

        // Ratio
        float ratio = ratioParams[i]->load();
        ImGui::Text("Ratio"); ImGui::SameLine(label_width); ImGui::PushItemWidth(control_width);
        if (ImGui::SliderFloat("##ratio", &ratio, 0.125f, 16.0f, "%.3fx", ImGuiSliderFlags_Logarithmic))
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("ratio_" + idx))) *p = ratio;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::PopItemWidth();

        // Detune
        float detune = detuneParams[i]->load();
        ImGui::Text("Detune"); ImGui::SameLine(label_width); ImGui::PushItemWidth(control_width);
        if (ImGui::SliderFloat("##detune", &detune, -100.0f, 100.0f, "%.2f cents"))
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("detune_" + idx))) *p = detune;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::PopItemWidth();

        // Waveform
        int wave = (int)waveformParams[i]->load();
        ImGui::Text("Waveform"); ImGui::SameLine(label_width); ImGui::PushItemWidth(control_width);
        if (ImGui::Combo("##wave", &wave, "Sine\0Saw\0Square\0Triangle\0\0"))
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("waveform_" + idx))) *p = wave;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::PopItemWidth();
        
        // Drive
        float oscDrive = driveParams[i]->load();
        ImGui::Text("Drive"); ImGui::SameLine(label_width); ImGui::PushItemWidth(control_width);
        if (ImGui::SliderFloat("##drive", &oscDrive, 0.0f, 1.0f, "%.2f"))
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("drive_" + idx))) *p = oscDrive;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        ImGui::PopItemWidth();

        ImGui::PopID();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === RELATIVE MODULATION SECTION ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CV Input Modes");
    ImGui::Spacing();
    
    // Relative Freq Mod checkbox
    bool relativeFreqMod = relativeFreqModParam != nullptr && relativeFreqModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Freq Mod", &relativeFreqMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeFreqMod")))
            *p = relativeFreqMod;
        juce::Logger::writeToLog("[HarmonicShaper UI] Relative Freq Mod: " + juce::String(relativeFreqMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±4 octaves)\nOFF: CV directly sets freq (20-20000 Hz)");
    }
    
    // Relative Drive Mod checkbox
    bool relativeDriveMod = relativeDriveModParam != nullptr && relativeDriveModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Drive Mod", &relativeDriveMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDriveMod")))
            *p = relativeDriveMod;
        juce::Logger::writeToLog("[HarmonicShaper UI] Relative Drive Mod: " + juce::String(relativeDriveMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±0.5)\nOFF: CV directly sets drive (0-1)");
    }
}

void HarmonicShaperModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Freq Mod", 2);
    helpers.drawAudioInputPin("Drive Mod", 3);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}

juce::String HarmonicShaperModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel) {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Freq Mod";
        case 3: return "Drive Mod";
        default: return {};
    }
}

juce::String HarmonicShaperModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel) {
        case 0: return "Out L";
        case 1: return "Out R";
        default: return {};
    }
}
#endif

std::vector<DynamicPinInfo> HarmonicShaperModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (bus 0, channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (bus 1, channels 0-1, but appear as channels 2-3 in processBlock)
    pins.push_back({"Freq Mod", 2, PinDataType::CV});
    pins.push_back({"Drive Mod", 3, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> HarmonicShaperModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}

bool HarmonicShaperModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    // Modulation inputs are on bus 1
    outBusIndex = 1; 
    if (paramId == paramIdMasterFreqMod) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdMasterDriveMod) { outChannelIndexInBus = 1; return true; }
    return false;
}

