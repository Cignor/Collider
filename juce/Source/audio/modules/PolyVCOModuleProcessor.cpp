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

    // Prevent denormals in Debug which can cause audible pops
    juce::ScopedNoDenormals noDenormals;

    // --- ADD GATE SMOOTHING FACTOR ---
    constexpr float GATE_SMOOTHING_FACTOR = 0.001f;
    constexpr float GATE_ATTACK_SECONDS = 0.001f;   // 1ms attack
    constexpr float GATE_RELEASE_SECONDS = 0.002f;  // 2ms release
    const float gateAttackCoeff = (float)((1.0 - std::exp(-1.0 / juce::jmax(1.0, getSampleRate() * GATE_ATTACK_SECONDS))));
    const float gateReleaseCoeff = (float)((1.0 - std::exp(-1.0 / juce::jmax(1.0, getSampleRate() * GATE_RELEASE_SECONDS))));
    constexpr float HYST_ON  = 0.55f; // hysteresis thresholds to avoid chatter
    constexpr float HYST_OFF = 0.45f;

    // PER-SAMPLE FIX: Check connections and also buffer activity once per block
    const bool isNumVoicesModulated = isParamInputConnected("numVoices");
    std::array<bool, MAX_VOICES> freqIsModulated, waveIsModulated, gateIsModulated;
    
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        const juce::String idx = juce::String(i + 1);
        freqIsModulated[i] = isParamInputConnected("freq_" + idx);
        waveIsModulated[i] = isParamInputConnected("wave_" + idx);
        gateIsModulated[i] = isParamInputConnected("gate_" + idx);
    }

    // Cache last per-voice values for once-per-block telemetry
    std::array<float, MAX_VOICES> lastFreq {};
    std::array<int,   MAX_VOICES> lastWave {};
    std::array<float, MAX_VOICES> lastGate {};

    // PER-SAMPLE processing to respond to changing modulation
    int finalActiveVoices = numVoicesParam != nullptr ? numVoicesParam->get() : 1;
    for (int sample = 0; sample < outBus.getNumSamples(); ++sample)
    {
        // Calculate active voices for this sample
        int activeVoices = numVoicesParam != nullptr ? numVoicesParam->get() : 1;
        if (isNumVoicesModulated)
        {
            // Interpret modulation as a raw number (not normalized CV)
            const float modValue = modInBus.getReadPointer(0)[sample];
            activeVoices = juce::roundToInt(modValue);
            activeVoices = juce::jlimit(1, MAX_VOICES, activeVoices);
        }
        finalActiveVoices = activeVoices; // Track the final value for cable inspector

        for (int voice = 0; voice < activeVoices; ++voice)
        {
            // Get base parameter values for this voice
            float freq = voiceFreqParams[voice] != nullptr ? voiceFreqParams[voice]->get() : 440.0f;
            int   waveChoice = voiceWaveParams[voice] != nullptr ? voiceWaveParams[voice]->getIndex() : 0;
            float gateLevel  = voiceGateParams[voice] != nullptr ? voiceGateParams[voice]->get() : 1.0f;

            // PER-SAMPLE FIX: Read modulation CV for THIS SAMPLE
            if (freqIsModulated[voice])
            {
                int chan = (voice + 1);
                if (chan < modInBus.getNumChannels())
                {
                    const float raw = modInBus.getReadPointer(chan)[sample];
                    const float cv01 = (raw >= 0.0f && raw <= 1.0f)
                                      ? juce::jlimit(0.0f, 1.0f, raw)
                                      : juce::jlimit(0.0f, 1.0f, (raw + 1.0f) * 0.5f);
                    // Absolute mapping: 20 Hz .. 20000 Hz on a logarithmic scale
                    constexpr float fMin = 20.0f;
                    constexpr float fMax = 20000.0f;
                    const float spanOct = std::log2(fMax / fMin); // ~9.97 octaves
                    freq = fMin * std::pow(2.0f, cv01 * spanOct);
                }
            }
            if (waveIsModulated[voice])
            {
                int chan = MAX_VOICES + (voice + 1);
                if (chan < modInBus.getNumChannels())
                    waveChoice = static_cast<int>(juce::jlimit(0.0f, 1.0f, (juce::jlimit(-1.0f, 1.0f, modInBus.getReadPointer(chan)[sample]) + 1.0f) * 0.5f) * 2.99f);
            }
            if (gateIsModulated[voice])
            {
                int chan = (2 * MAX_VOICES) + (voice + 1);
                if (chan < modInBus.getNumChannels())
                    gateLevel = juce::jlimit(0.0f, 1.0f, modInBus.getReadPointer(chan)[sample]);
            }

            // Hysteresis on gate detection to avoid 0/1 chatter from sequencers in Debug
            if (gateOnState[voice]) {
                if (gateLevel <= HYST_OFF) gateOnState[voice] = 0;
            } else {
                if (gateLevel >= HYST_ON) gateOnState[voice] = 1;
            }
            const float gateTarget = gateOnState[voice] ? 1.0f : 0.0f;

            // Smooth raw mod gate toward 0..1 (slow tiny smoothing of control)
            smoothedGateLevels[voice] += (gateLevel - smoothedGateLevels[voice]) * GATE_SMOOTHING_FACTOR;
            // Clickless envelope applied to audio multiplier (fast attack/release)
            const float coeff = (gateTarget > gateEnvelope[voice]) ? gateAttackCoeff : gateReleaseCoeff;
            gateEnvelope[voice] += coeff * (gateTarget - gateEnvelope[voice]);

            // RACE CONDITION FIX: Use pre-initialized oscillators instead of calling initialise() on audio thread
            juce::dsp::Oscillator<float>* currentOscillator = nullptr;
            if (waveChoice == 0)      currentOscillator = &sineOscillators[voice];
            else if (waveChoice == 1) currentOscillator = &sawOscillators[voice];
            else                      currentOscillator = &squareOscillators[voice];

            if (currentWaveforms[voice] != waveChoice)
            {
                currentWaveforms[voice] = waveChoice;
            }

            // Do not reset phase per-sample; only update frequency
            currentOscillator->setFrequency(freq, false);

            // PER-SAMPLE FIX: Process a single sample instead of entire block
            const float sampleValue = currentOscillator->processSample(0.0f);
            // Use the new smoothed gate value for multiplication.
            // Multiply oscillator by clickless gate envelope (not the raw/smoothed control)
            outBus.setSample(voice, sample, sampleValue * gateEnvelope[voice]);

            // Remember last values for this voice to publish once per block
            lastFreq[voice] = freq;
            lastWave[voice] = waveChoice;
            lastGate[voice] = gateLevel;
        }
        
        // Clear unused voices for this sample
        for (int voice = activeVoices; voice < MAX_VOICES; ++voice)
        {
            outBus.setSample(voice, sample, 0.0f);
        }
    }
    // Publish per-voice live telemetry once per block (cheap, avoids debug pops)
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

    // Voice count telemetry
    setLiveParamValue("numVoices_live", (float)finalActiveVoices);
}

std::vector<DynamicPinInfo> PolyVCOModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    if (numVoicesParam != nullptr)
    {
        const int activeVoices = numVoicesParam->get();
        
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
    }
    return pins;
}

std::vector<DynamicPinInfo> PolyVCOModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    if (numVoicesParam != nullptr)
    {
        const int activeVoices = numVoicesParam->get();
        pins.reserve((size_t)activeVoices); // Pre-allocate memory for efficiency
        for (int i = 0; i < activeVoices; ++i)
        {
            // Use the new struct and its constructor
            pins.push_back({ "Voice " + juce::String(i + 1), i, PinDataType::Audio });
        }
    }
    return pins;
}

int PolyVCOModuleProcessor::getEffectiveNumVoices() const
{
    return numVoicesParam ? numVoicesParam->get() : 1;
}

#if defined(PRESET_CREATOR_UI)
void PolyVCOModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                 const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    auto& ap = getAPVTS();

    // --- Master Voice Count Control with Live Feedback ---
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

    // --- Per-Voice Controls (rows follow live voice count when modded) ---
    const int activeVoices = juce::jlimit(1, MAX_VOICES,
        (int)(isCountModulated ? getLiveParamValueFor("numVoices", "numVoices_live", (float)getEffectiveNumVoices())
                               : (float)getEffectiveNumVoices()));
    for (int i = 0; i < activeVoices; ++i)
    {
        const auto idx = juce::String(i + 1);
        ImGui::PushID(i);

        // Frequency Slider with live feedback
        const bool isFreqModulated = isParamModulated("freq_" + idx);
        float freq = isFreqModulated ? getLiveParamValueFor("freq_" + idx, "freq_" + idx + "_live", (voiceFreqParams[i] != nullptr ? voiceFreqParams[i]->get() : 440.0f))
                                     : (voiceFreqParams[i] != nullptr ? voiceFreqParams[i]->get() : 440.0f);
        if (isFreqModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(("Freq " + idx).toRawUTF8(), &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic)) {
            if (!isFreqModulated) *voiceFreqParams[i] = freq;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !isFreqModulated) onModificationEnded();
        if (!isFreqModulated) adjustParamOnWheel(ap.getParameter("freq_" + idx), "freq_" + idx, freq);
        if (isFreqModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        // Waveform Combo with live feedback
        const bool isWaveModulated = isParamModulated("wave_" + idx);
        int wave = isWaveModulated ? (int)getLiveParamValueFor("wave_" + idx, "wave_" + idx + "_live", (float)(voiceWaveParams[i] != nullptr ? voiceWaveParams[i]->getIndex() : 0))
                                   : (voiceWaveParams[i] != nullptr ? voiceWaveParams[i]->getIndex() : 0);
        if (isWaveModulated) ImGui::BeginDisabled();
        if (ImGui::Combo(("Wave " + idx).toRawUTF8(), &wave, "Sine\0Saw\0Square\0\0")) {
            if (!isWaveModulated) *voiceWaveParams[i] = wave;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !isWaveModulated) onModificationEnded();
        if (isWaveModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        // Gate Slider with live feedback
        const bool isGateModulated = isParamModulated("gate_" + idx);
        float gate = isGateModulated ? getLiveParamValueFor("gate_" + idx, "gate_" + idx + "_live", (voiceGateParams[i] != nullptr ? voiceGateParams[i]->get() : 1.0f))
                                     : (voiceGateParams[i] != nullptr ? voiceGateParams[i]->get() : 1.0f);
        if (isGateModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(("Gate " + idx).toRawUTF8(), &gate, 0.0f, 1.0f, "%.2f")) {
            if (!isGateModulated) *voiceGateParams[i] = gate;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !isGateModulated) onModificationEnded();
        if (!isGateModulated) adjustParamOnWheel(ap.getParameter("gate_" + idx), "gate_" + idx, gate);
        if (isGateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

        ImGui::PopID();
    }
    ImGui::PopItemWidth();
}
#endif

#if defined(PRESET_CREATOR_UI)
void PolyVCOModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("NumVoices Mod", 0);
    
    if (numVoicesParam != nullptr)
    {
        const int activeVoices = numVoicesParam->get();
        for (int i = 0; i < activeVoices; ++i)
        {
            const juce::String idx = juce::String(i + 1);

            // --- THIS IS THE FIX ---
            // Convert juce::String to const char* before passing to the helper.
            helpers.drawAudioInputPin(("Freq " + idx).toRawUTF8(), 1 + i);
            helpers.drawAudioInputPin(("Wave " + idx).toRawUTF8(), 1 + MAX_VOICES + i);
            helpers.drawAudioInputPin(("Gate " + idx).toRawUTF8(), 1 + (2 * MAX_VOICES) + i);
            
            helpers.drawAudioOutputPin(("Voice " + idx).toRawUTF8(), i);
            // --- END OF FIX ---
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
