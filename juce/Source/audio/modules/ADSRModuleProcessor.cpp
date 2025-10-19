#include "ADSRModuleProcessor.h"

ADSRModuleProcessor::ADSRModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Inputs", juce::AudioChannelSet::discreteChannels(6), true) // ch0 Gate, ch1 Trigger, ch2-5 mods
                        .withOutput("Output", juce::AudioChannelSet::quadraphonic(), true)),
      apvts (*this, nullptr, "ADSRParams", createParameterLayout())
{
    attackParam  = apvts.getRawParameterValue (paramIdAttack);
    decayParam   = apvts.getRawParameterValue (paramIdDecay);
    sustainParam = apvts.getRawParameterValue (paramIdSustain);
    releaseParam = apvts.getRawParameterValue (paramIdRelease);
    attackModParam  = apvts.getRawParameterValue (paramIdAttackMod);
    decayModParam   = apvts.getRawParameterValue (paramIdDecayMod);
    sustainModParam = apvts.getRawParameterValue (paramIdSustainMod);
    releaseModParam = apvts.getRawParameterValue (paramIdReleaseMod);
    
    // CORRECTED INITIALIZATION:
    // Create unique_ptrs to heap-allocated atomics for each output channel.
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Channel 0
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Channel 1
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Channel 2
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Channel 3
}

juce::AudioProcessorValueTreeState::ParameterLayout ADSRModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdAttack,  "Attack",  juce::NormalisableRange<float> (0.001f, 5.0f, 0.01f, 0.4f), 0.01f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdDecay,   "Decay",   juce::NormalisableRange<float> (0.001f, 5.0f, 0.01f, 0.4f), 0.1f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdSustain, "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdRelease, "Release", juce::NormalisableRange<float> (0.001f, 5.0f, 0.01f, 0.4f), 0.2f));
    
    // Add modulation parameters
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdAttackMod,  "Attack Mod",  0.0f, 1.0f, 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdDecayMod,   "Decay Mod",   0.0f, 1.0f, 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdSustainMod, "Sustain Mod", 0.0f, 1.0f, 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdReleaseMod, "Release Mod", 0.0f, 1.0f, 0.0f));
    return { p.begin(), p.end() };
}

void ADSRModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    eorPending = eocPending = 0; envLevel = 0.0f; lastGate = false; lastTrigger = false; stage = Stage::Idle;
}

void ADSRModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    // Read CV from input buses (if connected)
    float attackModCV = 0.5f;   // Default to neutral (0.5)
    float decayModCV = 0.5f;    // Default to neutral (0.5)
    float sustainModCV = 0.5f;  // Default to neutral (0.5)
    float releaseModCV = 0.5f;  // Default to neutral (0.5)
    
    // Get single bus for all modulation inputs
    auto inBus = getBusBuffer(buffer, true, 0);
    
    // Check if attack mod is connected and read CV (channel 2)
    if (isParamInputConnected(paramIdAttackMod) && inBus.getNumChannels() > 2)
        attackModCV = inBus.getReadPointer(2)[0]; // Read first sample
    
    // Check if decay mod is connected and read CV (channel 3)
    if (isParamInputConnected(paramIdDecayMod) && inBus.getNumChannels() > 3)
        decayModCV = inBus.getReadPointer(3)[0]; // Read first sample
    
    // Check if sustain mod is connected and read CV (channel 4)
    if (isParamInputConnected(paramIdSustainMod) && inBus.getNumChannels() > 4)
        sustainModCV = inBus.getReadPointer(4)[0]; // Read first sample
    
    // Check if release mod is connected and read CV (channel 5)
    if (isParamInputConnected(paramIdReleaseMod) && inBus.getNumChannels() > 5)
        releaseModCV = inBus.getReadPointer(5)[0]; // Read first sample

    const float aBase = attackParam  ? attackParam->load()  : 0.01f;
    const float dBase = decayParam   ? decayParam->load()   : 0.10f;
    const float sBase = sustainParam ? sustainParam->load() : 0.70f;
    const float rBase = releaseParam ? releaseParam->load() : 0.20f;

    // Relative modulation: 0.5 = neutral. Times scaled roughly 0.25x..4x; sustain +/-0.5.
    auto timeScale = [](float norm){ return juce::jlimit(0.25f, 4.0f, std::pow(2.0f, (norm - 0.5f) * 2.0f)); }; // 0.25..4.0
    auto sustDelta = [](float norm){ return juce::jlimit(-0.5f, 0.5f, norm - 0.5f); };

    // Apply CV modulation or fallback to parameter values
    const float aEff = juce::jlimit(0.001f, 5.0f, aBase * (isParamInputConnected(paramIdAttackMod) ? timeScale(attackModCV) : 1.0f));
    const float dEff = juce::jlimit(0.001f, 5.0f, dBase * (isParamInputConnected(paramIdDecayMod) ? timeScale(decayModCV) : 1.0f));
    const float sEff = juce::jlimit(0.0f,   1.0f, sBase + (isParamInputConnected(paramIdSustainMod) ? sustDelta(sustainModCV) : 0.0f));
    const float rEff = juce::jlimit(0.001f, 5.0f, rBase * (isParamInputConnected(paramIdReleaseMod) ? timeScale(releaseModCV) : 1.0f));

    // Ensure UI reflects effective modulation in tooltips (optional debug)

    const float atkSec = aEff;
    const float decSec = dEff;
    const float susLvl = sEff;
    const float relSec = rEff;

    // Gate input from bus 0 channel 0; Trigger from bus 0 channel 1
    const float* gateIn = inBus.getNumChannels() > 0 ? inBus.getReadPointer (0) : nullptr;
    const float* trigIn = inBus.getNumChannels() > 1 ? inBus.getReadPointer (1) : nullptr;

    // Clear only our output bus before writing envelope
    auto out = getBusBuffer (buffer, false, 0);
    float* envOut = out.getWritePointer (0);
    float* invOut = out.getNumChannels() > 1 ? out.getWritePointer (1) : envOut;
    float* eorGate = out.getNumChannels() > 2 ? out.getWritePointer (2) : nullptr;
    float* eocGate = out.getNumChannels() > 3 ? out.getWritePointer (3) : nullptr;
    const int n = buffer.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        const bool trigHigh = trigIn != nullptr ? (trigIn[i] > 0.5f) : false;
        const bool gateHigh = gateIn != nullptr ? (gateIn[i] > 0.5f) : false;
        const bool trigRise = (trigHigh && ! lastTrigger);
        lastTrigger = trigHigh;

        if (trigRise) { stage = Stage::Attack; }
        else {
            if (gateHigh && ! lastGate) stage = Stage::Attack;
            else if (! gateHigh && lastGate && stage != Stage::Idle) stage = Stage::Release;
        }
        lastGate = gateHigh;
        const bool wasActive = (stage != Stage::Idle);

        // advance envelope one sample
        const float dt = 1.0f / (float) sr;
        switch (stage)
        {
            case Stage::Idle: envLevel = 0.0f; break;
            case Stage::Attack: {
                const float rate = (atkSec <= 0.0005f) ? 1.0f : dt / atkSec;
                envLevel += rate;
                if (envLevel >= 1.0f) { envLevel = 1.0f; stage = Stage::Decay; }
            } break;
            case Stage::Decay: {
                const float target = susLvl;
                const float rate = (decSec <= 0.0005f) ? 1.0f : dt / decSec;
                envLevel += (target - envLevel) * rate;
                if (std::abs(envLevel - target) < 0.0005f) { envLevel = target; stage = Stage::Sustain; }
            } break;
            case Stage::Sustain: {
                envLevel = susLvl;
                if (!gateHigh) stage = Stage::Release;
            } break;
            case Stage::Release: {
                const float rate = (relSec <= 0.0005f) ? 1.0f : dt / relSec;
                envLevel += (0.0f - envLevel) * rate;
                if (envLevel <= 0.0005f) { envLevel = 0.0f; stage = Stage::Idle; }
            } break;
        }
        const bool isActive = (stage != Stage::Idle);
        
        // EOR/EOC pulses
        if (wasActive && !isActive) { eorPending = (int) std::round (0.001f * (float)sr); }
        // EOC when we arrive at sustain level (end of attack/decay) OR when we return to Idle
        static bool atSustainPrev = false;
        const bool atSustainNow = (stage == Stage::Sustain);
        if (atSustainNow && !atSustainPrev) { eocPending = (int) std::round (0.001f * (float)sr); }
        if (!isActive && stage == Stage::Idle && wasActive) { eocPending = (int) std::round (0.001f * (float)sr); }
        atSustainPrev = atSustainNow;
        const float eorValue = (eorPending > 0 ? 1.0f : 0.0f);
        const float eocValue = (eocPending > 0 ? 1.0f : 0.0f);
        if (eorPending > 0) --eorPending;
        if (eocPending > 0) --eocPending;
        
        envOut[i] = envLevel;
        invOut[i] = 1.0f - envLevel;
        if (eorGate != nullptr) eorGate[i] = eorValue;
        if (eocGate != nullptr) eocGate[i] = eocValue;
    }

    // Inspector values: use block peak magnitude to capture fast changes
    if (lastOutputValues.size() >= 4)
    {
        auto peakAbs = [&](int ch){ if (ch >= out.getNumChannels()) return 0.0f; const float* p = out.getReadPointer(ch); float m=0.0f; for (int i=0;i<n;++i) m = juce::jmax(m, std::abs(p[i])); return m; };
        if (lastOutputValues[0]) lastOutputValues[0]->store(peakAbs(0));
        if (lastOutputValues[1]) lastOutputValues[1]->store(peakAbs(1));
        if (lastOutputValues[2]) lastOutputValues[2]->store(peakAbs(2));
        if (lastOutputValues[3]) lastOutputValues[3]->store(peakAbs(3));
    }

    // Store live modulated values for UI display
    setLiveParamValue("attack_live", aEff);
    setLiveParamValue("decay_live", dEff);
    setLiveParamValue("sustain_live", sEff);
    setLiveParamValue("release_live", rEff);
}

// Parameter bus contract implementation
bool ADSRModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All inputs are on bus 0
    if (paramId == paramIdAttackMod)  { outChannelIndexInBus = 2; return true; }  // Attack Mod
    if (paramId == paramIdDecayMod)   { outChannelIndexInBus = 3; return true; }  // Decay Mod
    if (paramId == paramIdSustainMod) { outChannelIndexInBus = 4; return true; }  // Sustain Mod
    if (paramId == paramIdReleaseMod) { outChannelIndexInBus = 5; return true; }  // Release Mod
    return false;
}


