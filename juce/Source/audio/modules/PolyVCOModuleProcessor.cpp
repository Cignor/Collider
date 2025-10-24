#include "PolyVCOModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PolyVCOModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // The single parameter for controlling the voice count.
    p.push_back(std::make_unique<juce::AudioParameterInt>("numVoices", "Num Voices", 1, MAX_VOICES, 8));

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

void PolyVCOModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
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
                    constexpr float fMin = 20.0f, fMax = 20000.0f;
                    freq = fMin * std::pow(2.0f, juce::jlimit(0.0f, 1.0f, cv01) * std::log2(fMax / fMin));
                }
            }
            if (waveIsModulated[voice])
            {
                int chan = 1 + MAX_VOICES + voice;
                if (chan < modInBus.getNumChannels())
                    waveChoice = static_cast<int>(juce::jlimit(0.0f, 1.0f, (modInBus.getReadPointer(chan)[sample] + 1.0f) * 0.5f) * 2.99f);
            }
            
            // --- THIS IS THE DEFINITIVE FIX ---
            float finalGateMultiplier = 0.0f;
            if (gateIsModulated[voice])
            {
                // MODE 1: Gate is modulated by an external signal (e.g., ADSR).
                // Use the incoming CV directly as the envelope.
                int chan = 1 + (2 * MAX_VOICES) + voice;
                if (chan < modInBus.getNumChannels())
                {
                    float gateCV = modInBus.getReadPointer(chan)[sample];
                    // Apply light smoothing to the direct CV to prevent clicks.
                    gateEnvelope[voice] += (gateCV - gateEnvelope[voice]) * 0.005f; 
                    finalGateMultiplier = gateEnvelope[voice];
                }
            }
            else
            {
                // MODE 2: Gate is NOT modulated. Use the internal slider as a threshold
                // against the signal from the 'Freq Mod' input.
                float gateThreshold = voiceGateParams[voice] != nullptr ? voiceGateParams[voice]->get() : 0.5f;
                float gateTarget = (freqModCV > gateThreshold) ? 1.0f : 0.0f;
                
                // Use the fast attack/release envelope for a snappy response.
                const float coeff = (gateTarget > gateEnvelope[voice]) ? gateAttackCoeff : gateReleaseCoeff;
                gateEnvelope[voice] += coeff * (gateTarget - gateEnvelope[voice]);
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

            currentOscillator->setFrequency(freq, false);
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
        pins.push_back({ "Voice " + juce::String(i + 1), i, PinDataType::Audio });
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

    ImGui::PushItemWidth(itemWidth);
    
    // Master Voice Count Control with Live Feedback
    const bool isCountModulated = isParamModulated("numVoices");
    int displayedVoices = isCountModulated ? (int)getLiveParamValueFor("numVoices", "numVoices_live", (float)(numVoicesParam != nullptr ? numVoicesParam->get() : 1))
                                           : (numVoicesParam != nullptr ? numVoicesParam->get() : 1);

    if (isCountModulated) ImGui::BeginDisabled();
    if (ImGui::SliderInt("Num Voices", &displayedVoices, 1, MAX_VOICES)) {
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
    HelpMarkerPoly("Number of active voices (1-32)\nEach voice is an independent oscillator");
    
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Spacing();

    // === PER-VOICE CONTROLS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Voice Parameters");
    ImGui::Spacing();

    // Expand/Collapse All controls
    static bool expandAllState = false;
    static bool collapseAllState = false;
    
    if (ImGui::SmallButton("Expand All")) {
        expandAllState = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse All")) {
        collapseAllState = true;
    }
    
    ImGui::Spacing();

    // Per-voice controls with collapsible sections
    const int activeVoices = juce::jlimit(1, MAX_VOICES,
        (int)(isCountModulated ? getLiveParamValueFor("numVoices", "numVoices_live", (float)getEffectiveNumVoices())
                               : (float)getEffectiveNumVoices()));
    
    for (int i = 0; i < activeVoices; ++i)
    {
        const auto idx = juce::String(i + 1);
        ImGui::PushID(i);

        // Apply expand/collapse all
        if (expandAllState) {
            ImGui::SetNextItemOpen(true);
        }
        if (collapseAllState) {
            ImGui::SetNextItemOpen(false);
        }
        
        // First 4 voices open by default (on first use only)
        ImGui::SetNextItemOpen(i < 4, ImGuiCond_Once);

        // Color-code voice number using HSV
        float hue = (float)i / (float)MAX_VOICES;
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.7f, 1.0f).Value);
        
        juce::String voiceLabel = "Voice " + idx;
        bool expanded = ImGui::CollapsingHeader(voiceLabel.toRawUTF8(), 
                                                ImGuiTreeNodeFlags_SpanAvailWidth | 
                                                ImGuiTreeNodeFlags_FramePadding);
        
        ImGui::PopStyleColor();
        
        if (expanded)
        {
            // Use a 3-column table for compact layout
            const float columnWidth = itemWidth / 3.0f;
            
            if (ImGui::BeginTable("voiceTable", 3, 
                                  ImGuiTableFlags_SizingFixedFit | 
                                  ImGuiTableFlags_NoBordersInBody,
                                  ImVec2(itemWidth, 0)))
            {
                // Column 1: Waveform
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(columnWidth - 8.0f);
                
                const bool isWaveModulated = isParamModulated("wave_" + idx);
                int wave = isWaveModulated ? (int)getLiveParamValueFor("wave_" + idx, "wave_" + idx + "_live", (float)(voiceWaveParams[i] != nullptr ? voiceWaveParams[i]->getIndex() : 0))
                                           : (voiceWaveParams[i] != nullptr ? voiceWaveParams[i]->getIndex() : 0);
                if (isWaveModulated) ImGui::BeginDisabled();
                if (ImGui::Combo("##wave", &wave, "Sine\0Saw\0Square\0\0")) {
                    if (!isWaveModulated) *voiceWaveParams[i] = wave;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && !isWaveModulated) onModificationEnded();
                if (isWaveModulated) ImGui::EndDisabled();
                
                ImGui::TextUnformatted("Wave");
                if (isWaveModulated) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "(mod)");
                }
                
                ImGui::PopItemWidth();
                
                // Column 2: Frequency
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(columnWidth - 8.0f);
                
                const bool isFreqModulated = isParamModulated("freq_" + idx);
                float freq = isFreqModulated ? getLiveParamValueFor("freq_" + idx, "freq_" + idx + "_live", (voiceFreqParams[i] != nullptr ? voiceFreqParams[i]->get() : 440.0f))
                                             : (voiceFreqParams[i] != nullptr ? voiceFreqParams[i]->get() : 440.0f);
                if (isFreqModulated) ImGui::BeginDisabled();
                if (ImGui::SliderFloat("##freq", &freq, 20.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
                    if (!isFreqModulated) *voiceFreqParams[i] = freq;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && !isFreqModulated) onModificationEnded();
                if (!isFreqModulated) adjustParamOnWheel(ap.getParameter("freq_" + idx), "freq_" + idx, freq);
                if (isFreqModulated) ImGui::EndDisabled();
                
                ImGui::TextUnformatted("Hz");
                if (isFreqModulated) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "(mod)");
                }
                
                ImGui::PopItemWidth();
                
                // Column 3: Gate
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(columnWidth - 8.0f);
                
                const bool isGateModulated = isParamModulated("gate_" + idx);
                float gate = isGateModulated ? getLiveParamValueFor("gate_" + idx, "gate_" + idx + "_live", (voiceGateParams[i] != nullptr ? voiceGateParams[i]->get() : 1.0f))
                                             : (voiceGateParams[i] != nullptr ? voiceGateParams[i]->get() : 1.0f);
                if (isGateModulated) ImGui::BeginDisabled();
                if (ImGui::SliderFloat("##gate", &gate, 0.0f, 1.0f, "%.2f")) {
                    if (!isGateModulated) *voiceGateParams[i] = gate;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && !isGateModulated) onModificationEnded();
                if (!isGateModulated) adjustParamOnWheel(ap.getParameter("gate_" + idx), "gate_" + idx, gate);
                if (isGateModulated) ImGui::EndDisabled();
                
                ImGui::TextUnformatted("Gate");
                if (isGateModulated) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "(mod)");
                }
                
                ImGui::PopItemWidth();
                
                ImGui::EndTable();
            }
        }

        ImGui::PopID();
    }
    
    // Reset expand/collapse state after processing all voices
    expandAllState = false;
    collapseAllState = false;
}
#endif

#if defined(PRESET_CREATOR_UI)
void PolyVCOModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // NumVoices modulation input (no output pairing)
    helpers.drawParallelPins("NumVoices Mod", 0, nullptr, -1);
    
    const int activeVoices = getEffectiveNumVoices();
    for (int i = 0; i < activeVoices; ++i)
    {
        const juce::String idx = juce::String(i + 1);
        const int inFreq = 1 + i;
        const int inWave = 1 + MAX_VOICES + i;
        const int inGate = 1 + (2 * MAX_VOICES) + i;
        
        // Pair Freq input with Voice output on the same row
        helpers.drawParallelPins(("Freq " + idx + " Mod").toRawUTF8(), inFreq,
                                 ("Voice " + idx).toRawUTF8(), i);
        
        // Wave and Gate inputs without outputs
        helpers.drawParallelPins(("Wave " + idx + " Mod").toRawUTF8(), inWave, nullptr, -1);
        helpers.drawParallelPins(("Gate " + idx + " Mod").toRawUTF8(), inGate, nullptr, -1);
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
