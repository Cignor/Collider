#include "PolyVCOModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PolyVCOModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // The single parameter for controlling the voice count.
    p.push_back(std::make_unique<juce::AudioParameterInt>("numVoices", "Num Voices", 1, MAX_VOICES, 8));
    
    // Global relative frequency modulation mode
    p.push_back(std::make_unique<juce::AudioParameterBool>("relativeFreqMod", "Relative Freq Mod", true));
    
    // Global portamento time
    p.push_back(std::make_unique<juce::AudioParameterFloat>("portamento", "Portamento",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.5f), 0.0f));

    // Create all 32 potential per-voice parameters up-front.
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        const auto idx = juce::String(i + 1);
        p.push_back(std::make_unique<juce::AudioParameterFloat>("freq_" + idx, "Frequency " + idx,
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 440.0f));
        p.push_back(std::make_unique<juce::AudioParameterChoice>("wave_" + idx, "Waveform " + idx,
            juce::StringArray{ "Sine", "Saw", "Square" }, 0));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("gate_" + idx, "Gate " + idx, 0.0f, 1.0f, 1.0f));
    }
    return { p.begin(), p.end() };
}


PolyVCOModuleProcessor::PolyVCOModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Mod In", juce::AudioChannelSet::discreteChannels(1 + (MAX_VOICES * 3)), true) // 1 for NumVoices + 3 for each voice (Freq, Wave, Gate)
          .withOutput("Out", juce::AudioChannelSet::discreteChannels(MAX_VOICES), true)),
      apvts(*this, nullptr, "PolyVCOParams", createParameterLayout())
{
    numVoicesParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numVoices"));
    relativeFreqModParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("relativeFreqMod"));
    portamentoParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("portamento"));
    
    // RACE CONDITION FIX: Pre-initialize all oscillators for each waveform type
    // This avoids dangerous memory allocations on the audio thread
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        sineOscillators[i].initialise([](float x) { return std::sin(x); }, 128);
        sawOscillators[i].initialise([](float x) { return x / juce::MathConstants<float>::pi; }, 128);
        squareOscillators[i].initialise([](float x) { return x < 0.0f ? -1.0f : 1.0f; }, 128);
        currentWaveforms[i] = -1; // Initialize to invalid state
    }

    voiceFreqParams.resize(MAX_VOICES);
    voiceWaveParams.resize(MAX_VOICES);
    voiceGateParams.resize(MAX_VOICES);
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        const auto idx = juce::String(i + 1);
        voiceFreqParams[i] = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("freq_" + idx));
        voiceWaveParams[i] = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("wave_" + idx));
        voiceGateParams[i] = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gate_" + idx));
    }
    
    // ADD THIS LINE: Initialize all waveforms to -1 (an invalid state) to force setup on first block
    currentWaveforms.fill(-1);
    
    // Initialize lastOutputValues for cable inspector
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    }
}

void PolyVCOModuleProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 1 }; // Mono spec
    
    // Prepare all pre-initialized oscillators
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        sineOscillators[i].prepare(spec);
        sawOscillators[i].prepare(spec);
        squareOscillators[i].prepare(spec);
        smoothedGateLevels[i] = 0.0f;
        gateEnvelope[i] = 0.0f;
        gateOnState[i] = 0;
        currentFrequencies[i] = 440.0f; // Initialize portamento frequencies
    }
}

void PolyVCOModuleProcessor::releaseResources()
{
    // Nothing to release for PolyVCO
}

void PolyVCOModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto modInBus = getBusBuffer(buffer, true, 0);
    auto outBus   = getBusBuffer(buffer, false, 0);

    juce::ScopedNoDenormals noDenormals;

    constexpr float GATE_ATTACK_SECONDS = 0.001f;
    constexpr float GATE_RELEASE_SECONDS = 0.002f;
    const float gateAttackCoeff = (float)((1.0 - std::exp(-1.0 / juce::jmax(1.0, getSampleRate() * GATE_ATTACK_SECONDS))));
    const float gateReleaseCoeff = (float)((1.0 - std::exp(-1.0 / juce::jmax(1.0, getSampleRate() * GATE_RELEASE_SECONDS))));
    
    // Calculate portamento coefficient
    const float portamentoTime = portamentoParam ? portamentoParam->get() : 0.0f;
    float portamentoCoeff = 1.0f; // Instant (no smoothing)
    if (portamentoTime > 0.001f)
    {
        const double timeInSamples = portamentoTime * sampleRate;
        portamentoCoeff = static_cast<float>(1.0 - std::exp(-1.0 / timeInSamples));
    }

    const bool isNumVoicesModulated = isParamInputConnected("numVoices");
    std::array<bool, MAX_VOICES> freqIsModulated, waveIsModulated, gateIsModulated;
    
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        const juce::String idx = juce::String(i + 1);
        freqIsModulated[i] = isParamInputConnected("freq_" + idx);
        waveIsModulated[i] = isParamInputConnected("wave_" + idx);
        gateIsModulated[i] = isParamInputConnected("gate_" + idx);
    }

    std::array<float, MAX_VOICES> lastFreq {};
    std::array<int,   MAX_VOICES> lastWave {};
    std::array<float, MAX_VOICES> lastGate {};

    int finalActiveVoices = numVoicesParam != nullptr ? numVoicesParam->get() : 1;
    for (int sample = 0; sample < outBus.getNumSamples(); ++sample)
    {
        int activeVoices = numVoicesParam != nullptr ? numVoicesParam->get() : 1;
        if (isNumVoicesModulated)
        {
            const float modValue = modInBus.getReadPointer(0)[sample];
            activeVoices = juce::roundToInt(modValue);
            activeVoices = juce::jlimit(1, MAX_VOICES, activeVoices);
        }
        finalActiveVoices = activeVoices;

        for (int voice = 0; voice < activeVoices; ++voice)
        {
            float freq = voiceFreqParams[voice] != nullptr ? voiceFreqParams[voice]->get() : 440.0f;
            int   waveChoice = voiceWaveParams[voice] != nullptr ? voiceWaveParams[voice]->getIndex() : 0;
            
            float freqModCV = 0.0f; // Default to 0 if not connected/modulated
            if (freqIsModulated[voice])
            {
                int chan = 1 + voice;
                if (chan < modInBus.getNumChannels())
                {
                    freqModCV = modInBus.getReadPointer(chan)[sample];
                    const float cv01 = (freqModCV >= 0.0f && freqModCV <= 1.0f) ? freqModCV : (freqModCV + 1.0f) * 0.5f;
                    
                    const bool relativeMode = relativeFreqModParam != nullptr && relativeFreqModParam->get();
                    if (relativeMode)
                    {
                        // RELATIVE: CV modulates around base frequency (±4 octaves)
                        const float octaveOffset = (juce::jlimit(0.0f, 1.0f, cv01) - 0.5f) * 8.0f;
                        freq = freq * std::pow(2.0f, octaveOffset);
                    }
                    else
                    {
                        // ABSOLUTE: CV directly maps to full frequency range
                        constexpr float fMin = 20.0f, fMax = 20000.0f;
                        freq = fMin * std::pow(2.0f, juce::jlimit(0.0f, 1.0f, cv01) * std::log2(fMax / fMin));
                    }
                }
            }
            if (waveIsModulated[voice])
            {
                int chan = 1 + MAX_VOICES + voice;
                if (chan < modInBus.getNumChannels())
                    waveChoice = static_cast<int>(juce::jlimit(0.0f, 1.0f, (modInBus.getReadPointer(chan)[sample] + 1.0f) * 0.5f) * 2.99f);
            }
            
            // Apply portamento/glide smoothing to frequency
            if (portamentoTime > 0.001f)
            {
                currentFrequencies[voice] += (freq - currentFrequencies[voice]) * portamentoCoeff;
            }
            else
            {
                currentFrequencies[voice] = freq; // Instant, no glide
            }
            
            // --- THIS IS THE DEFINITIVE FIX ---
            float finalGateMultiplier = 0.0f;
            if (gateIsModulated[voice])
            {
                // MODE 1: Gate is modulated by an external signal (e.g., an ADSR).
                // Use the incoming CV directly as the gate's level.
                int chan = 1 + (2 * MAX_VOICES) + voice;
                if (chan < modInBus.getNumChannels())
                {
                    float gateCV = modInBus.getReadPointer(chan)[sample];
                    // Apply light smoothing to the direct CV to prevent clicks from stepped signals.
                    gateEnvelope[voice] += (gateCV - gateEnvelope[voice]) * 0.005f; 
                    finalGateMultiplier = gateEnvelope[voice];
                }
            }
            else
            {
                // MODE 2: Gate is NOT modulated. The oscillator is free-running.
                // The internal "Gate" slider now acts as a simple level/volume control.
                float targetLevel = voiceGateParams[voice] ? voiceGateParams[voice]->get() : 1.0f;

                // Smooth the parameter value to prevent zipper noise when the user moves the slider.
                const float smoothingCoeff = 0.001f;
                gateEnvelope[voice] += (targetLevel - gateEnvelope[voice]) * smoothingCoeff;
                finalGateMultiplier = gateEnvelope[voice];
            }
            // --- END OF FIX ---

            juce::dsp::Oscillator<float>* currentOscillator = nullptr;
            if (waveChoice == 0)      currentOscillator = &sineOscillators[voice];
            else if (waveChoice == 1) currentOscillator = &sawOscillators[voice];
            else                      currentOscillator = &squareOscillators[voice];

            if (currentWaveforms[voice] != waveChoice)
            {
                currentWaveforms[voice] = waveChoice;
            }

            currentOscillator->setFrequency(currentFrequencies[voice], false);
            const float sampleValue = currentOscillator->processSample(0.0f);
            
            outBus.setSample(voice, sample, sampleValue * finalGateMultiplier);

            lastFreq[voice] = freq;
            lastWave[voice] = waveChoice;
            lastGate[voice] = finalGateMultiplier; // Store the actual applied gain
        }
        
        for (int voice = activeVoices; voice < MAX_VOICES; ++voice)
        {
            outBus.setSample(voice, sample, 0.0f);
        }
    }

    for (int voice = 0; voice < finalActiveVoices; ++voice)
    {
        const auto idxStr = juce::String(voice + 1);
        setLiveParamValue("freq_" + idxStr + "_live", lastFreq[voice]);
        setLiveParamValue("wave_" + idxStr + "_live", (float) lastWave[voice]);
        setLiveParamValue("gate_" + idxStr + "_live", lastGate[voice]);
    }
    
    // Update lastOutputValues for cable inspector
    if (lastOutputValues.size() >= (size_t)finalActiveVoices)
    {
        for (int voice = 0; voice < finalActiveVoices; ++voice)
        {
            if (lastOutputValues[voice])
            {
                lastOutputValues[voice]->store(outBus.getSample(voice, outBus.getNumSamples() - 1));
            }
        }
    }

    setLiveParamValue("numVoices_live", (float)finalActiveVoices);
}

std::vector<DynamicPinInfo> PolyVCOModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const int activeVoices = getEffectiveNumVoices(); // Use the new helper
    
    // Always include the NumVoices modulation input
    pins.push_back({ "NumVoices Mod", 0, PinDataType::Raw });
    
    // Add per-voice modulation inputs for active voices only
    for (int i = 0; i < activeVoices; ++i)
    {
        const juce::String idx = juce::String(i + 1);
        
        // Frequency modulation (channels 1-32)
        pins.push_back({ "Freq " + idx + " Mod", 1 + i, PinDataType::CV });
        
        // Waveform modulation (channels 33-64)
        pins.push_back({ "Wave " + idx + " Mod", 1 + MAX_VOICES + i, PinDataType::CV });
        
        // Gate modulation (channels 65-96)
        pins.push_back({ "Gate " + idx + " Mod", 1 + (2 * MAX_VOICES) + i, PinDataType::Gate });
    }
    return pins;
}

std::vector<DynamicPinInfo> PolyVCOModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const int activeVoices = getEffectiveNumVoices(); // Use the new helper
    pins.reserve((size_t)activeVoices);
    for (int i = 0; i < activeVoices; ++i)
    {
        // Label outputs as "Freq 1", "Freq 2", etc. to match their function
        pins.push_back({ "Freq " + juce::String(i + 1), i, PinDataType::Audio });
    }
    return pins;
}

int PolyVCOModuleProcessor::getEffectiveNumVoices() const
{
    if (isParamInputConnected("numVoices"))
    {
        // If modulated, get the live value from the audio thread.
        return (int)getLiveParamValueFor("numVoices", "numVoices_live", (float)(numVoicesParam ? numVoicesParam->get() : 1));
    }
    
    // Otherwise, return the slider's base value.
    return numVoicesParam ? numVoicesParam->get() : 1;
}

#if defined(PRESET_CREATOR_UI)
void PolyVCOModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                 const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    // Helper for tooltips
    auto HelpMarkerPoly = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // === MASTER CONTROLS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Master Controls");
    ImGui::Spacing();

    // Master Voice Count Control with Live Feedback
    const bool isCountModulated = isParamModulated("numVoices");
    int displayedVoices = isCountModulated ? (int)getLiveParamValueFor("numVoices", "numVoices_live", (float)(numVoicesParam != nullptr ? numVoicesParam->get() : 1))
                                           : (numVoicesParam != nullptr ? numVoicesParam->get() : 1);

    if (isCountModulated) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("##numvoices", &displayedVoices, 1, MAX_VOICES)) {
        if (!isCountModulated) {
            *numVoicesParam = displayedVoices;
            onModificationEnded();
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !isCountModulated) { onModificationEnded(); }
    if (!isCountModulated) {
        adjustParamOnWheel(ap.getParameter("numVoices"), "numVoices", (float)displayedVoices);
    }
    if (isCountModulated) { 
        ImGui::EndDisabled(); 
        ImGui::SameLine(); 
        ImGui::TextUnformatted("(mod)");
    }
    ImGui::SameLine();
    ImGui::Text("Voices");
    ImGui::SameLine();
    HelpMarkerPoly("Number of active voices (1-32)\nEach voice is an independent oscillator");

    ImGui::Spacing();
    ImGui::Spacing();

    // === FREQUENCY MODULATION MODE SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Frequency Modulation");
    ImGui::Spacing();
    
    bool relativeFreqMod = relativeFreqModParam ? relativeFreqModParam->get() : true;
    if (ImGui::Checkbox("Relative Frequency Mod", &relativeFreqMod))
    {
        if (relativeFreqModParam)
        {
            *relativeFreqModParam = relativeFreqMod;
            juce::Logger::writeToLog("[PolyVCO UI] Relative Frequency Mod changed to: " + juce::String(relativeFreqMod ? "TRUE" : "FALSE"));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    ImGui::SameLine();
    HelpMarkerPoly("Relative: CV modulates around slider frequency (±4 octaves)\nAbsolute: CV directly controls frequency (20Hz-20kHz, ignores sliders)\n\nApplies to all voice frequency inputs");

    ImGui::Spacing();
    ImGui::Spacing();

    // === PORTAMENTO SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Glide");
    ImGui::Spacing();
    
    float portamentoTime = portamentoParam ? portamentoParam->get() : 0.0f;
    ImGui::SetNextItemWidth(itemWidth * 0.6f);
    if (ImGui::SliderFloat("##portamento", &portamentoTime, 0.0f, 2.0f, "%.3f s", ImGuiSliderFlags_Logarithmic))
    {
        if (portamentoParam)
        {
            *portamentoParam = portamentoTime;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    
    ImGui::SameLine();
    ImGui::Text("Portamento");
    ImGui::SameLine();
    HelpMarkerPoly("Pitch glide time for all voices\n0s = instant (no glide)\n0.05s = fast slide\n0.2s = smooth glide\n0.5s+ = slow portamento");

    // Quick preset buttons
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    float btnWidth = (itemWidth - 12) / 4.0f;
    
    if (ImGui::Button("Off", ImVec2(btnWidth, 0)))
    {
        if (portamentoParam)
        {
            *portamentoParam = 0.0f;
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("No glide");
    
    ImGui::SameLine();
    if (ImGui::Button("Fast", ImVec2(btnWidth, 0)))
    {
        if (portamentoParam)
        {
            *portamentoParam = 0.05f;
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("50ms");
    
    ImGui::SameLine();
    if (ImGui::Button("Medium", ImVec2(btnWidth, 0)))
    {
        if (portamentoParam)
        {
            *portamentoParam = 0.2f;
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("200ms");
    
    ImGui::SameLine();
    if (ImGui::Button("Slow", ImVec2(btnWidth, 0)))
    {
        if (portamentoParam)
        {
            *portamentoParam = 0.5f;
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("500ms");
    
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Spacing();

    // === PER-VOICE CONTROLS SECTION (TABLE SYSTEM) ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Voice Parameters");
    ImGui::TextDisabled("Clean table view - inputs on left, outputs on right");
    ImGui::Spacing();

    const int activeVoices = juce::jlimit(1, MAX_VOICES,
        (int)(isCountModulated ? getLiveParamValueFor("numVoices", "numVoices_live", (float)getEffectiveNumVoices())
                               : (float)getEffectiveNumVoices()));
    
    // Table system like MIDI Buttons - clean, compact, and NO BLEEDING!
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit |
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_ScrollY;
    
    float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4;
    // Scale table height with number of voices, with scrolling for many voices
    float tableHeight = rowHeight * juce::jmin(activeVoices + 1.5f, 16.0f); // Show up to 16 rows, then scroll
    
    // CRITICAL: Constrain table width to itemWidth to prevent bleeding
    if (ImGui::BeginTable("##voices_table", 4, flags, ImVec2(itemWidth, tableHeight)))
    {
        ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Waveform", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Frequency", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Gate", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();
        
        for (int i = 0; i < activeVoices; ++i)
        {
            const auto idx = juce::String(i + 1);
            ImGui::PushID(i);
            
            ImGui::TableNextRow();
            
            // Column 0: Voice number with color
            ImGui::TableNextColumn();
            float hue = (float)i / (float)MAX_VOICES;
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.7f, 1.0f).Value);
            ImGui::Text("V%d", i + 1);
            ImGui::PopStyleColor();
            
            // Column 1: Waveform
            ImGui::TableNextColumn();
            const bool isWaveModulated = isParamModulated("wave_" + idx);
            int wave = isWaveModulated ? (int)getLiveParamValueFor("wave_" + idx, "wave_" + idx + "_live", (float)(voiceWaveParams[i] ? voiceWaveParams[i]->getIndex() : 0))
                                       : (voiceWaveParams[i] ? voiceWaveParams[i]->getIndex() : 0);
            
            if (isWaveModulated) ImGui::BeginDisabled();
            ImGui::PushItemWidth(-1);
            if (ImGui::Combo(("##wave" + idx).toRawUTF8(), &wave, "Sine\0Saw\0Square\0\0"))
            {
                if (!isWaveModulated && voiceWaveParams[i]) {
                    *voiceWaveParams[i] = wave;
                    onModificationEnded();
                }
            }
            ImGui::PopItemWidth();
            if (isWaveModulated) ImGui::EndDisabled();
            
            // Column 2: Frequency
            ImGui::TableNextColumn();
            const bool isFreqModulated = isParamModulated("freq_" + idx);
            float freq = isFreqModulated ? getLiveParamValueFor("freq_" + idx, "freq_" + idx + "_live", voiceFreqParams[i] ? voiceFreqParams[i]->get() : 440.0f)
                                         : (voiceFreqParams[i] ? voiceFreqParams[i]->get() : 440.0f);
            
            if (isFreqModulated) ImGui::BeginDisabled();
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat(("##freq" + idx).toRawUTF8(), &freq, 1.0f, 20.0f, 20000.0f, "%.0f"))
            {
                if (!isFreqModulated && voiceFreqParams[i]) *voiceFreqParams[i] = freq;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && !isFreqModulated) onModificationEnded();
            ImGui::PopItemWidth();
            if (isFreqModulated) ImGui::EndDisabled();
            
            // Column 3: Gate
            ImGui::TableNextColumn();
            const bool isGateModulated = isParamModulated("gate_" + idx);
            float gate = isGateModulated ? getLiveParamValueFor("gate_" + idx, "gate_" + idx + "_live", voiceGateParams[i] ? voiceGateParams[i]->get() : 1.0f)
                                         : (voiceGateParams[i] ? voiceGateParams[i]->get() : 1.0f);
            
            if (isGateModulated) ImGui::BeginDisabled();
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat(("##gate" + idx).toRawUTF8(), &gate, 0.01f, 0.0f, 1.0f, "%.2f"))
            {
                if (!isGateModulated && voiceGateParams[i]) *voiceGateParams[i] = gate;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && !isGateModulated) onModificationEnded();
            ImGui::PopItemWidth();
            if (isGateModulated) ImGui::EndDisabled();
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}
#endif

#if defined(PRESET_CREATOR_UI)
void PolyVCOModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // 1. Draw the single master input for the voice count. It has no output.
    helpers.drawParallelPins("NumVoices Mod", 0, nullptr, -1);
    ImGui::Spacing();

    const int activeVoices = getEffectiveNumVoices();
    for (int i = 0; i < activeVoices; ++i)
    {
        const juce::String idx = juce::String(i + 1);
        const int inFreq = 1 + i;
        const int inWave = 1 + MAX_VOICES + i;
        const int inGate = 1 + (2 * MAX_VOICES) + i;
        const int outChannel = i;

        // Use a group to keep all pins for a voice together visually.
        ImGui::BeginGroup();

        // Row 1: The main Freq input is paired with the voice's audio output.
        helpers.drawParallelPins(
            ("Freq " + idx + " Mod").toRawUTF8(),
            inFreq,
            ("Freq " + idx).toRawUTF8(), // The output is also labeled "Freq"
            outChannel
        );

        // Row 2: The Wave input stands alone on the left.
        helpers.drawParallelPins(
            ("Wave " + idx + " Mod").toRawUTF8(),
            inWave,
            nullptr,  // No output text on this row
            -1        // No output pin on this row
        );

        // Row 3: The Gate input also stands alone on the left.
        helpers.drawParallelPins(
            ("Gate " + idx + " Mod").toRawUTF8(),
            inGate,
            nullptr,
            -1
        );

        ImGui::EndGroup();

        // Add visual separation between voice groups.
        if (i < activeVoices - 1)
        {
            ImGui::Spacing();
            ImGui::Spacing();
        }
    }
}
#endif

juce::String PolyVCOModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "Num Voices Mod";
    
    // Frequency modulation buses (channels 1-32)
    if (channel >= 1 && channel <= MAX_VOICES)
    {
        return "Freq " + juce::String(channel) + " Mod";
    }
    
    // Waveform modulation buses (channels 33-64)
    if (channel >= MAX_VOICES + 1 && channel <= 2 * MAX_VOICES)
    {
        return "Wave " + juce::String(channel - MAX_VOICES) + " Mod";
    }
    
    // Gate modulation buses (channels 65-96)
    if (channel >= 2 * MAX_VOICES + 1 && channel <= 3 * MAX_VOICES)
    {
        return "Gate " + juce::String(channel - 2 * MAX_VOICES) + " Mod";
    }
    
    return juce::String("In ") + juce::String(channel + 1);
}

// Parameter bus contract implementation
#if defined(PRESET_CREATOR_UI)

#endif

bool PolyVCOModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // ALL modulation uses the single input bus at index 0.

    if (paramId == "numVoices") { outChannelIndexInBus = 0; return true; }

    if (paramId.startsWith("freq_"))
    {
        outChannelIndexInBus = paramId.getTrailingIntValue(); // 1 to 32
        return (outChannelIndexInBus > 0 && outChannelIndexInBus <= MAX_VOICES);
    }
    if (paramId.startsWith("wave_"))
    {
        outChannelIndexInBus = MAX_VOICES + paramId.getTrailingIntValue(); // 33 to 64
        return (outChannelIndexInBus > MAX_VOICES && outChannelIndexInBus <= 2 * MAX_VOICES);
    }
    if (paramId.startsWith("gate_"))
    {
        outChannelIndexInBus = (2 * MAX_VOICES) + paramId.getTrailingIntValue(); // 65 to 96
        return (outChannelIndexInBus > 2 * MAX_VOICES && outChannelIndexInBus <= 3 * MAX_VOICES);
    }

    return false;
}
