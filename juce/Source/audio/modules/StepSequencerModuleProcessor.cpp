#include "StepSequencerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include <iostream>
#include <array>

using APVTS = juce::AudioProcessorValueTreeState;

static juce::NormalisableRange<float> makeRateRange()
{
    // FIX: Change the interval from 0.0f to a small, non-zero value like 0.01f.
    juce::NormalisableRange<float> r (0.1f, 20.0f, 0.01f, 0.5f); // semi-log response
    return r;
}

APVTS::ParameterLayout StepSequencerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterInt> ("numSteps", "Number of Steps", 1, StepSequencerModuleProcessor::MAX_STEPS, 8));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("rate", "Rate", makeRateRange(), 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateLength", "Gate Length", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    // Threshold to generate gate when step value >= threshold
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateThreshold", "Gate Threshold", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    
    // Add modulation parameters for rate, gate length and number of steps (absolute 0..1)
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("rate_mod", "Rate Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateLength_mod", "Gate Length Mod", 0.0f, 1.0f, 0.5f));
    // Neutral default at 0.5 means "no override" (we'll treat values ~0.5 as disconnected)
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("numSteps_mod", "Num Steps Mod", 0.0f, 1.0f, 0.5f));
    // Optional maximum steps bound (1..MAX_STEPS), default MAX_STEPS
    params.push_back (std::make_unique<juce::AudioParameterInt> ("numSteps_max", "Num Steps Max", 1, StepSequencerModuleProcessor::MAX_STEPS, StepSequencerModuleProcessor::MAX_STEPS));
    
    // Transport sync parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("sync", "Sync to Transport", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("rate_division", "Division", 
        juce::StringArray{ "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" }, 3)); // Default: 1/4 note
    
    for (int i = 0; i < StepSequencerModuleProcessor::MAX_STEPS; ++i)
    {
        const juce::String pid = "step" + juce::String (i + 1);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (pid, pid, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

        // ADD THIS: A new parameter for this step's modulation input
        const juce::String modPid = "step" + juce::String(i + 1) + "_mod";
        // Default 0.5 => no offset (unipolar 0..1 centered to bipolar -0.5..+0.5)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(modPid, modPid, 0.0f, 1.0f, 0.5f));

        // NEW: per-step Trigger checkbox and its modulation (absolute 0..1)
        const juce::String trigPid = "step" + juce::String(i + 1) + "_trig";
        params.push_back(std::make_unique<juce::AudioParameterBool>(trigPid, trigPid, false));
        const juce::String trigModPid = "step" + juce::String(i + 1) + "_trig_mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(trigModPid, trigModPid, 0.0f, 1.0f, 0.5f));

        // NEW: per-step Gate Level parameters
        const juce::String gatePid = "step" + juce::String(i + 1) + "_gate";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(gatePid, gatePid, 0.0f, 1.0f, 0.8f));
        const juce::String gateModPid = "step" + juce::String(i + 1) + "_gate_mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(gateModPid, gateModPid, 0.0f, 1.0f, 0.5f));
    }
    return { params.begin(), params.end() };
}

StepSequencerModuleProcessor::StepSequencerModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        // ARCHITECTURAL FIX: Single large bus for all inputs:
                        // 2 (Audio) + 4 (Global Mods: rate, gate, steps, stepsMax) + 16 (Step Mods) + 16 (Trig Mods) + 16 (Gate Mods) = 54 channels
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(2 + 4 + (MAX_STEPS * 3)), true)
                        // expand to 5 outputs (Pitch, Gate, Velocity, Mod, Trigger)
                        .withOutput ("CV Outputs", juce::AudioChannelSet::discreteChannels(6), true))
    , apvts (*this, nullptr, "SeqParams", createParameterLayout())
{
    // ADD THIS VERIFICATION LOGIC
    std::cout << "--- StepSequencerModuleProcessor Initializing ---" << std::endl;
    
    numStepsParam   = apvts.getRawParameterValue ("numSteps");
    rateParam       = apvts.getRawParameterValue ("rate");
    gateLengthParam = apvts.getRawParameterValue ("gateLength");
    gateThresholdParam = apvts.getRawParameterValue ("gateThreshold");
    rateModParam    = apvts.getRawParameterValue ("rate_mod");
    gateLengthModParam = apvts.getRawParameterValue ("gateLength_mod");
    numStepsModParam = apvts.getRawParameterValue ("numSteps_mod");
    stepsModMaxParam = apvts.getRawParameterValue ("numSteps_max");
    
    if (numStepsParam == nullptr) std::cout << "ERROR: 'numSteps' parameter is NULL!" << std::endl;
    if (rateParam == nullptr) std::cout << "ERROR: 'rate' parameter is NULL!" << std::endl;
    if (gateLengthParam == nullptr) std::cout << "ERROR: 'gateLength' parameter is NULL!" << std::endl;

    pitchParams.resize (MAX_STEPS);
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        pitchParams[i] = apvts.getRawParameterValue ("step" + juce::String (i + 1));
        if (pitchParams[i] == nullptr)
        {
            std::cout << "ERROR: 'step" << (i + 1) << "' parameter is NULL!" << std::endl;
        }
    }

    // Initialize stepModParams
    stepModParams.resize(MAX_STEPS);
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        const juce::String modPid = "step" + juce::String(i + 1) + "_mod";
        stepModParams[i] = apvts.getRawParameterValue(modPid);
        if (stepModParams[i] == nullptr)
        {
            std::cout << "ERROR: 'step" << (i + 1) << "_mod' parameter is NULL!" << std::endl;
        }
    }
    std::cout << "--- Initialization Check Complete ---" << std::endl;
    
    // Initialize output value tracking for tooltips (6 outputs: Pitch, Gate, Gate Nuanced, Velocity, Mod, Trigger)
    for (int i = 0; i < 6; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));

    // Cache pointers for per-step trigger params
    stepTrigParams.resize(MAX_STEPS);
    stepTrigModParams.resize(MAX_STEPS);
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        const juce::String trigPid = "step" + juce::String(i + 1) + "_trig";
        stepTrigParams[i] = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(trigPid));
        const juce::String trigModPid = "step" + juce::String(i + 1) + "_trig_mod";
        stepTrigModParams[i] = apvts.getRawParameterValue(trigModPid);
    }

    // Initialize gate parameter pointers
    stepGateParams.resize(MAX_STEPS);
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        const juce::String gatePid = "step" + juce::String(i + 1) + "_gate";
        stepGateParams[i] = apvts.getRawParameterValue(gatePid);
    }
}

void StepSequencerModuleProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    phase = 0.0;
}

void StepSequencerModuleProcessor::setTimingInfo(const TransportState& state)
{
    // Check if the transport has just started playing
    if (state.isPlaying && !wasPlaying)
    {
        // Reset to the beginning when play is pressed
        currentStep.store(0);
        phase = 0.0;
    }
    wasPlaying = state.isPlaying;
    
    m_currentTransport = state;
}

juce::ValueTree StepSequencerModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("SequencerState");
    vt.setProperty("sync", apvts.getRawParameterValue("sync")->load(), nullptr);
    vt.setProperty("rate_division", apvts.getRawParameterValue("rate_division")->load(), nullptr);
    return vt;
}

void StepSequencerModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("SequencerState"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("sync")))
            *p = (bool)vt.getProperty("sync", false);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("rate_division")))
            *p = (int)vt.getProperty("rate_division", 3);
    }
}

void StepSequencerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    if (rateParam == nullptr || numStepsParam == nullptr || gateLengthParam == nullptr)
    {
        buffer.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    
    // ARCHITECTURAL FIX: Read CV from single input bus
    const auto& inputBus = getBusBuffer(buffer, true, 0); // All inputs are now on bus 0
    
    // Get pointers to global modulation CV inputs, if they are connected
    const bool isRateMod = isParamInputConnected("rate_mod");
    const bool isGateLenMod = isParamInputConnected("gateLength_mod");
    const bool isStepsMod = isParamInputConnected("numSteps_mod");
    
    // Absolute channel map on single input bus:
    // 0=L,1=R, 2=rate, 3=gateLen, 4=steps, 5=stepsMax,
    // 6..21: step1..step16 value mods, 22..37: step1..step16 trig mods, 38..53: step1..step16 gate mods
    const float* rateCV = isRateMod && inputBus.getNumChannels() > 2 ? inputBus.getReadPointer(2) : nullptr;
    const float* gateLenCV = isGateLenMod && inputBus.getNumChannels() > 3 ? inputBus.getReadPointer(3) : nullptr;
    const float* stepsCV = isStepsMod && inputBus.getNumChannels() > 4 ? inputBus.getReadPointer(4) : nullptr;
    
    // Get write pointers for all 6 output channels
    auto* pitchOut       = buffer.getWritePointer(0);
    auto* gateOut        = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    auto* gateNuancedOut = buffer.getNumChannels() > 2 ? buffer.getWritePointer(2) : nullptr;
    auto* velocityOut    = buffer.getNumChannels() > 3 ? buffer.getWritePointer(3) : nullptr;
    auto* modOut         = buffer.getNumChannels() > 4 ? buffer.getWritePointer(4) : nullptr;
    auto* trigOut        = buffer.getNumChannels() > 5 ? buffer.getWritePointer(5) : nullptr;
    
    // The old, commented-out input logic can now be completely removed.
    
    // Get base parameter values ONCE
    const float baseRate = rateParam->load();
    const float baseGate = gateLengthParam != nullptr ? gateLengthParam->load() : 0.5f;
    const int baseSteps = numStepsParam != nullptr ? (int) numStepsParam->load() : 8;
    const int boundMax = stepsModMaxParam != nullptr ? juce::jlimit (1, MAX_STEPS, (int) stepsModMaxParam->load()) : MAX_STEPS;
    const float gateThreshold = gateThresholdParam != nullptr ? juce::jlimit(0.0f, 1.0f, gateThresholdParam->load()) : 0.5f;
    

    // --- UI Telemetry Bootstrap ---
    // Publish per-step live values for ALL steps this block (use first-sample snapshot)
    {
        const int totalCh = inputBus.getNumChannels();
        for (int si = 0; si < MAX_STEPS; ++si)
        {
            // Absolute channel for per-step value mod: 6..21
            const int ch = 6 + si;
            const bool hasCh = totalCh > ch;
            const float base = (pitchParams.size() > (size_t) si && pitchParams[si] != nullptr) ? pitchParams[si]->load() : 0.0f;
            float live = base;
            if (hasCh)
            {
                const float cv0 = inputBus.getReadPointer(ch)[0];
                live = juce::jlimit(0.0f, 1.0f, base + (cv0 - 0.5f));
            }
            setLiveParamValue("step_live_" + juce::String(si + 1), live);

            // Per-step gate live values: channels 38..53
            const int gateCh = 38 + si;
            const bool hasGateCh = totalCh > gateCh;
            const float baseGate = (stepGateParams.size() > (size_t) si && stepGateParams[si] != nullptr) ? stepGateParams[si]->load() : 0.8f;
            float liveGate = baseGate;
            if (hasGateCh)
            {
                const float cv0 = inputBus.getReadPointer(gateCh)[0];
                liveGate = juce::jlimit(0.0f, 1.0f, baseGate + (cv0 - 0.5f));
            }
            setLiveParamValue("gate_live_" + juce::String(si + 1), liveGate);

            // Per-step trigger live values: channels 22..37
            const int trigCh = 22 + si;
            const bool hasTrigCh = totalCh > trigCh;
            const bool baseTrig = (stepTrigParams.size() > (size_t) si && stepTrigParams[si] != nullptr) ? (bool)(*stepTrigParams[si]) : false;
            bool liveTrig = baseTrig;
            if (hasTrigCh)
            {
                const float cv0 = inputBus.getReadPointer(trigCh)[0];
                liveTrig = cv0 > 0.5f;
            }
            setLiveParamValue("trig_live_" + juce::String(si + 1), liveTrig ? 1.0f : 0.0f);
        }
    }

    bool stepAdvanced = false;
    float lastRateLive = baseRate;
    float lastGateLive = baseGate;
    float lastGateThresholdLive = gateThreshold;
    int   lastStepsLive = baseSteps;
    for (int i = 0; i < numSamples; ++i)
    {
        // PER-SAMPLE FIX: Calculate global modulation parameters FOR THIS SAMPLE
        int activeSteps = baseSteps;
        if (isStepsMod && stepsCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, stepsCV[i]);
            // CV adds/subtracts steps around base (±8 steps)
            const int offset = (int)std::round((cv - 0.5f) * 16.0f);
            activeSteps = baseSteps + offset;
            activeSteps = juce::jlimit(1, boundMax, activeSteps);
        }
        // FIX: clamp playhead immediately when steps shrink
        if (currentStep.load() >= activeSteps)
            currentStep.store(0);
        
        float rate = baseRate;
        if (isRateMod && rateCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            // CV modulates ±2 octaves (0.25x to 4x)
            const float octaveOffset = (cv - 0.5f) * 4.0f;
            rate = baseRate * std::pow(2.0f, octaveOffset);
            rate = juce::jlimit(0.1f, 20.0f, rate);
        }
        lastRateLive = rate;
        
        float gateLen = baseGate;
        if (isGateLenMod && gateLenCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, gateLenCV[i]);
            // CV adds offset to base gate length (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            gateLen = baseGate + offset;
            gateLen = juce::jlimit(0.0f, 1.0f, gateLen);
        }
        lastGateLive = gateLen;
        
        // Use gateLenCV for gate threshold modulation (reusing the existing "Gate Mod" input)
        float gateThreshold = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
        if (isGateLenMod && gateLenCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, gateLenCV[i]);
            gateThreshold = juce::jlimit(0.0f, 1.0f, cv);
        }
        lastGateThresholdLive = gateThreshold;
        
        // --- Transport Sync Logic ---
        const bool syncEnabled = apvts.getRawParameterValue("sync")->load() > 0.5f;

        if (syncEnabled && m_currentTransport.isPlaying)
        {
            // SYNC MODE: Use the global beat position
            int divisionIndex = (int)apvts.getRawParameterValue("rate_division")->load();
            // Use global division if a Tempo Clock has override enabled
            // IMPORTANT: Read from parent's LIVE transport state, not cached copy (which is stale)
            if (getParent())
            {
                int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
                if (globalDiv >= 0)
                    divisionIndex = globalDiv;
            }
            static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
            const double beatDivision = divisions[juce::jlimit(0, 8, divisionIndex)];
            
            // Calculate which step we should be on based on song position
            const int totalSteps = juce::jlimit(1, MAX_STEPS, activeSteps);
            const int stepForBeat = static_cast<int>(std::fmod(m_currentTransport.songPositionBeats * beatDivision, totalSteps));

            if (stepForBeat != currentStep.load())
            {
                currentStep.store(stepForBeat);
                stepAdvanced = true;
            }
        }
        else
        {
            // FREE-RUNNING MODE: Use the internal phase clock
            const double phaseInc = (sampleRate > 0.0 ? (double) rate / sampleRate : 0.0);
            phase += phaseInc;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                const int next = (currentStep.load() + 1) % juce::jlimit (1, MAX_STEPS, activeSteps);
                currentStep.store(next);
                stepAdvanced = true;
            }
        }
        lastStepsLive = activeSteps;

        const int currentStepIndex = currentStep.load();
        const float sliderValue = pitchParams[currentStepIndex] != nullptr ? pitchParams[currentStepIndex]->load() : 0.0f;
        
        // PER-SAMPLE FIX: Read modulation from CV input bus for THIS SAMPLE
        float rawModValue = 0.5f; // neutral
        {
            const juce::String stepModId = "step" + juce::String(currentStepIndex + 1) + "_mod";
            const bool stepModConnected = isParamInputConnected(stepModId);
            if (stepModConnected)
            {
                // Absolute: step1_mod at 6, step2_mod at 7, ...
                const int modChannel = 6 + currentStepIndex;
                if (inputBus.getNumChannels() > modChannel)
                    rawModValue = inputBus.getReadPointer(modChannel)[i];
            }
        }
        // Center modulation around 0.0 (convert unipolar 0-1 to bipolar -0.5 to +0.5)
        const float modValue = rawModValue - 0.5f;

        const float pitchValue = juce::jlimit (0.0f, 1.0f, sliderValue + modValue);
        
        // --- REWRITTEN GATE LOGIC ---
        // 1. Get the gate level for the current step from its own slider.
        float stepGateLevel = (stepGateParams[currentStepIndex] != nullptr) ? stepGateParams[currentStepIndex]->load() : 0.8f;

        // 2. Apply modulation to the step's gate level if connected.
        const juce::String gateModId = "step" + juce::String(currentStepIndex + 1) + "_gate_mod";
        if (isParamInputConnected(gateModId))
        {
            const int gateModChannel = 38 + currentStepIndex;
            if (inputBus.getNumChannels() > gateModChannel)
            {
                const float cv = inputBus.getReadPointer(gateModChannel)[i];
                stepGateLevel = juce::jlimit(0.0f, 1.0f, stepGateLevel + (cv - 0.5f));
            }
        }

        // --- NEW DUAL GATE LOGIC WITH FADE-IN ---
        // 1. Perform the comparison once.
        const bool isGateOn = (stepGateLevel >= gateThreshold);
        
        // 2. Handle gate fade-in transition
        if (isGateOn && !previousGateOn) {
            // Gate just turned on - start fade-in
            gateFadeProgress = 0.0f;
        } else if (isGateOn && previousGateOn) {
            // Gate is on - continue fade-in
            const float fadeIncrement = sampleRate > 0.0f ? (1000.0f / GATE_FADE_TIME_MS) / sampleRate : 0.0f;
            gateFadeProgress = juce::jmin(1.0f, gateFadeProgress + fadeIncrement);
        } else {
            // Gate is off - reset fade progress
            gateFadeProgress = 0.0f;
        }
        previousGateOn = isGateOn;
        
        // 3. Generate the binary "Gate" output with fade-in.
        const float gateBinaryValue = isGateOn ? gateFadeProgress : 0.0f;
        
        // 4. Generate the analog "Gate Nuanced" output with fade-in.
        const float gateNuancedValue = isGateOn ? (stepGateLevel * gateFadeProgress) : 0.0f;
        // --- END OF NEW LOGIC ---
        
        // Live gate level is already stored in the UI telemetry bootstrap

        // Determine Trigger state for this step (checkbox + mod, but only count mod when connected)
        bool trigBase = false;
        if (stepTrigParams.size() > (size_t) currentStepIndex && stepTrigParams[currentStepIndex] != nullptr)
            trigBase = (bool) (*stepTrigParams[currentStepIndex]);
        bool trigActive = trigBase;
        {
            const juce::String trigModId = "step" + juce::String(currentStepIndex + 1) + "_trig_mod";
            const bool trigModConnected = isParamInputConnected(trigModId);
            if (trigModConnected)
            {
                // Absolute: step1_trig_mod at 22, step2 at 23, ...
                const int trigModChannel = 22 + currentStepIndex;
                if (inputBus.getNumChannels() > trigModChannel)
                {
                    const float trigModNorm = inputBus.getReadPointer(trigModChannel)[i];
                    if (trigModNorm > 0.5f) trigActive = true;
                }
            }
        }

        // If we advanced to this step, only emit a pulse if this step is enabled (checkbox or connected mod>0.5)
        if (stepAdvanced)
        {
            pendingTriggerSamples = trigActive ? (int) std::round (0.001 * sampleRate) : 0;
            stepAdvanced = false;
        }

        pitchOut[i] = pitchValue;
        // Live step value is already stored in the UI telemetry bootstrap
        if (gateOut != nullptr)         gateOut[i] = gateBinaryValue;
        if (gateNuancedOut != nullptr)  gateNuancedOut[i] = gateNuancedValue;
        if (velocityOut != nullptr) velocityOut[i] = 0.85f;
        if (modOut != nullptr)      modOut[i] = 0.0f;
        // Timed gate remains level-based
        // Trigger Out: 1ms pulse after each step advance
        if (trigOut != nullptr)
        {
            float pulse = 0.0f;
            if (pendingTriggerSamples > 0)
            {
                pulse = 1.0f;
                --pendingTriggerSamples;
            }
            trigOut[i] = pulse;
        }
    }
    // Publish block-level live telemetry for UI reflection
    setLiveParamValue("rate_live", lastRateLive);
    setLiveParamValue("gateLength_live", lastGateLive);
    setLiveParamValue("gateThreshold_live", lastGateThresholdLive);
    setLiveParamValue("steps_live", (float) lastStepsLive);
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 6)
    {
        if (lastOutputValues[0] && pitchOut) lastOutputValues[0]->store(pitchOut[numSamples - 1]);
        if (lastOutputValues[1] && gateOut) lastOutputValues[1]->store(gateOut[numSamples - 1]);
        if (lastOutputValues[2] && gateNuancedOut) lastOutputValues[2]->store(gateNuancedOut[numSamples - 1]);
        if (lastOutputValues[3] && velocityOut) lastOutputValues[3]->store(velocityOut[numSamples - 1]);
        if (lastOutputValues[4] && modOut) lastOutputValues[4]->store(modOut[numSamples - 1]);
        if (lastOutputValues[5] && trigOut) lastOutputValues[5]->store(trigOut[numSamples - 1]);
    }

}

#if defined(PRESET_CREATOR_UI)
void StepSequencerModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    int activeSteps = numStepsParam != nullptr ? (int) numStepsParam->load() : 8;
    const int boundMaxUi = stepsModMaxParam != nullptr ? juce::jlimit (1, MAX_STEPS, (int) stepsModMaxParam->load()) : MAX_STEPS;
    const bool stepsAreModulated = isParamModulated("numSteps_mod");
    if (stepsAreModulated) {
        // Reflect live steps from audio thread telemetry
        const int liveSteps = (int) std::round(getLiveParamValueFor("numSteps_mod", "steps_live", (float) activeSteps));
        activeSteps = juce::jlimit (1, boundMaxUi, liveSteps);
    }

    // Step count controls - now using a slider instead of +/- buttons
    int currentSteps = numStepsParam != nullptr ? (int)numStepsParam->load() : 8;

    // If modulated, the displayed value comes from the modulation input.
    // Otherwise, it comes from the parameter itself.
    int displayedSteps = currentSteps;
    if (stepsAreModulated) {
        const int liveSteps = (int) std::round(getLiveParamValueFor("numSteps_mod", "steps_live", (float) currentSteps));
        displayedSteps = juce::jlimit (1, boundMaxUi, liveSteps);
    }

    // Ensure the displayed value never exceeds the max bound
    displayedSteps = juce::jmin(displayedSteps, boundMaxUi);

    // Disable the slider if steps are being modulated
    if (stepsAreModulated) ImGui::BeginDisabled();

    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderInt("Steps", &displayedSteps, 1, boundMaxUi))
    {
        // Only update the parameter if the slider is not disabled
        if (!stepsAreModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numSteps"))) {
                *p = displayedSteps;
            }
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    
    // Add scroll wheel support for the Steps slider
    if (!stepsAreModulated)
    {
        adjustParamOnWheel(apvts.getParameter("numSteps"), "numSteps", (float)displayedSteps);
    }
    
    ImGui::PopItemWidth();

    if (stepsAreModulated) {
        ImGui::EndDisabled();
        ImGui::SameLine(); 
        ImGui::TextUnformatted("(mod)");
    }

    // Use the displayed steps value for the slider strip
    const int shown = juce::jlimit (1, MAX_STEPS, displayedSteps);
    const float sliderW = itemWidth / (float) juce::jmax (8, shown) * 0.8f;

    ImGui::PushItemWidth (sliderW);
    for (int i = 0; i < shown; ++i)
    {
        if (i > 0) ImGui::SameLine();

        // Build display and interaction state
        float baseValue = (pitchParams[i] != nullptr ? pitchParams[i]->load() : 0.5f);
        const juce::String modPid = "step" + juce::String(i + 1) + "_mod";
        const bool modConnected = isParamModulated(modPid);

        // Reflect live per-step value for the currently active step when modulated
        float liveValue = getLiveParamValueFor("step" + juce::String(i + 1) + "_mod",
                                              "step_live_" + juce::String(i + 1),
                                              baseValue);
        float sliderValue = modConnected ? liveValue : baseValue; // widget bound to display value

        const bool isActive = (i == currentStep.load());
        if (isActive)
        {
            ImGui::PushStyleColor (ImGuiCol_FrameBg, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            ImGui::PushStyleColor (ImGuiCol_SliderGrab, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        }

        const std::string label = "##s" + std::to_string(i);
        if (modConnected) ImGui::BeginDisabled();
        if (ImGui::VSliderFloat (label.c_str(), ImVec2 (sliderW, 60.0f), &sliderValue, 0.0f, 1.0f, ""))
        {
            if (!modConnected) {
                // Only update if not modulated
                float newBaseValue = juce::jlimit (0.0f, 1.0f, sliderValue);
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter ("step" + juce::String (i + 1)))) 
                    *p = newBaseValue;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }

        // Wheel fine-tune: identical semantics to drag
        if (!modConnected)
        {
            if (ImGui::IsItemHovered())
            {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    const float delta = (wheel > 0 ? 0.05f : -0.05f);
                    float newBaseValue = juce::jlimit (0.0f, 1.0f, baseValue + delta);
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter ("step" + juce::String (i + 1)))) 
                        *p = newBaseValue;
                }
            }
        }

        if (modConnected) { ImGui::EndDisabled(); }

        if (isActive) ImGui::PopStyleColor(2);
    }
    ImGui::PopItemWidth();


    // Per-step Gate Sliders
    ImGui::PushItemWidth(sliderW);
    
    // Capture the screen position before drawing the gate sliders
    ImVec2 gate_sliders_p0 = ImGui::GetCursorScreenPos();
    
    for (int i = 0; i < shown; ++i)
    {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(2000 + i); // Use a new ID base to avoid collisions

        float baseGateValue = (stepGateParams[i] != nullptr ? stepGateParams[i]->load() : 0.8f);
        const juce::String modPid = "step" + juce::String(i + 1) + "_gate_mod";
        const bool modConnected = isParamModulated(modPid);
        
        // Reflect live modulated value for gate level
        float sliderValue = baseGateValue;
        if (modConnected) {
            // Use live gate value from audio thread
            sliderValue = getLiveParamValueFor("step" + juce::String(i + 1) + "_gate_mod",
                                              "gate_live_" + juce::String(i + 1),
                                              baseGateValue);
        }
        const bool isActive = (i == currentStep.load());

        if (isActive) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        if (modConnected) ImGui::BeginDisabled();
        
        if (ImGui::VSliderFloat("##g", ImVec2(sliderW, 60.0f), &sliderValue, 0.0f, 1.0f, ""))
        {
            if (!modConnected && stepGateParams[i] != nullptr) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("step" + juce::String(i + 1) + "_gate"))) {
                    *p = sliderValue;
                }
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        // Wheel fine-tune: identical semantics to drag
        if (!modConnected)
        {
            if (ImGui::IsItemHovered())
            {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    const float delta = (wheel > 0 ? 0.05f : -0.05f);
                    float newBaseValue = juce::jlimit(0.0f, 1.0f, sliderValue + delta);
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("step" + juce::String(i + 1) + "_gate"))) {
                        *p = newBaseValue;
                    }
                }
            }
        }
        
        if (modConnected) ImGui::EndDisabled();
        if (isActive) ImGui::PopStyleColor();
        
        ImGui::PopID();
    }
    ImGui::PopItemWidth();

    // Draw the yellow threshold line immediately after the gate sliders
    // Use the same threshold value that will be used by the Gate Threshold slider
    const bool gtIsModulatedForLine = isParamModulated("gateLength_mod");
    const float threshold_value = gtIsModulatedForLine ? getLiveParamValueFor("gateLength_mod", "gateThreshold_live", (gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f))
                                                          : (gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f);
    const float slider_height = 60.0f; // This must match the VSliderFloat height
    const float row_width = (sliderW * shown) + (ImGui::GetStyle().ItemSpacing.x * (shown - 1));

    // Calculate the Y coordinate for the line. 
    // A threshold of 1.0 is at the top (y=0), 0.0 is at the bottom (y=height).
    const float line_y = gate_sliders_p0.y + (1.0f - threshold_value) * slider_height;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(
        ImVec2(gate_sliders_p0.x, line_y),
        ImVec2(gate_sliders_p0.x + row_width, line_y),
        IM_COL32(255, 255, 0, 200), // A bright, slightly transparent yellow
        2.0f
    );

    // Current step indicator
    ImGui::Text("Current Step: %d", currentStep.load() + 1);

    // --- SYNC CONTROLS ---
    bool sync = apvts.getRawParameterValue("sync")->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("sync"))) *p = sync;
        onModificationEnded();
    }

    ImGui::PushItemWidth(itemWidth);
    if (sync)
    {
        // Check if global division is active (Tempo Clock override)
        // IMPORTANT: Read from parent's LIVE transport state, not cached copy
        int globalDiv = getParent() ? getParent()->getTransportState().globalDivisionIndex.load() : -1;
        bool isGlobalDivisionActive = globalDiv >= 0;
        int division = isGlobalDivisionActive ? globalDiv : (int)apvts.getRawParameterValue("rate_division")->load();
        
        // Grey out if controlled by Tempo Clock
        if (isGlobalDivisionActive) ImGui::BeginDisabled();
        
        if (ImGui::Combo("Division", &division, "1/32\0""1/16\0""1/8\0""1/4\0""1/2\0""1\0""2\0""4\0""8\0\0"))
        {
            if (!isGlobalDivisionActive)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("rate_division"))) *p = division;
                onModificationEnded();
            }
        }
        
        if (isGlobalDivisionActive)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Tempo Clock Division Override Active");
                ImGui::TextUnformatted("A Tempo Clock node with 'Division Override' enabled is controlling the global division.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
    else
    {
        // Rate slider (only shown in free-running mode)
        const bool isRateModulated = isParamModulated("rate_mod");
        float rateDisplay = isRateModulated ? getLiveParamValueFor("rate_mod", "rate_live", rateParam->load()) : rateParam->load();
        
        if (isRateModulated) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Rate (Hz)", &rateDisplay, 0.1f, 20.0f, "%.2f")) {
            if (!isRateModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("rate"))) *p = rateDisplay;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (!isRateModulated) adjustParamOnWheel(apvts.getParameter("rate"), "rate", rateDisplay);
        if (isRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }
    ImGui::PopItemWidth();
    // --- END SYNC CONTROLS ---

    ImGui::PushItemWidth(itemWidth);

    const bool gtIsModulated = isParamModulated("gateLength_mod");
    float gtEff = gtIsModulated ? getLiveParamValueFor("gateLength_mod", "gateThreshold_live", (gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f))
                                 : (gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f);

    if (gtIsModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat ("Gate Threshold", &gtEff, 0.0f, 1.0f))
    {
        if (! gtIsModulated)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gateThreshold"))) *p = gtEff;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (! gtIsModulated)
        adjustParamOnWheel (apvts.getParameter ("gateThreshold"), "gateThreshold", gtEff);
    if (gtIsModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::PopItemWidth();

    // --- Per-step Trigger checkboxes row ---
    // Place checkboxes exactly under each slider, matching widths and exact columns
    {
        const float cbWidth = sliderW; // same width as sliders
        for (int i = 0; i < shown; ++i)
        {
            // Compute the same X layout as sliders
            if (i > 0) ImGui::SameLine();

            bool baseTrig = (stepTrigParams.size() > (size_t) i && stepTrigParams[i] != nullptr) ? (bool) (*stepTrigParams[i]) : false;
            // Only grey out when the TRIGGER mod is connected (not the value mod)
            const juce::String trigModId = "step" + juce::String(i + 1) + "_trig_mod";
            const bool trigIsModulated = isParamModulated(trigModId);

            // Use live value for display when modulated
            bool displayTrig = baseTrig;
            if (trigIsModulated) {
                displayTrig = getLiveParamValueFor("step" + juce::String(i + 1) + "_trig_mod",
                                                  "trig_live_" + juce::String(i + 1),
                                                  baseTrig ? 1.0f : 0.0f) > 0.5f;
            }

            if (trigIsModulated) ImGui::BeginDisabled();
            ImGui::PushID(1000 + i);
            ImGui::SetNextItemWidth(cbWidth);
            ImGui::PushItemWidth(cbWidth);
            bool changed = ImGui::Checkbox("##trig", &displayTrig);
            ImGui::PopItemWidth();
            if (changed && !trigIsModulated && stepTrigParams.size() > (size_t) i && stepTrigParams[i] != nullptr)
            {
                // Only update parameter if not modulated
                *stepTrigParams[i] = displayTrig;
            }
            // Fill remaining width so columns align exactly to sliderW
            {
                float used = ImGui::GetItemRectSize().x;
                if (used < cbWidth) { ImGui::SameLine(0.0f, 0.0f); ImGui::Dummy(ImVec2(cbWidth - used, 0.0f)); }
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
            ImGui::PopID();
            if (trigIsModulated) { ImGui::EndDisabled(); }
        }
        // Mod banner if any are modulated
        bool anyTrigMod = false;
        for (int i = 0; i < shown; ++i)
        {
            const juce::String trigModId = "step" + juce::String(i + 1) + "_trig_mod";
            if (isParamInputConnected(trigModId)) { anyTrigMod = true; break; }
        }
        if (anyTrigMod) { ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }
}

void StepSequencerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // ARCHITECTURAL FIX: All inputs are now on a single bus, so we can use direct channel indices
    
    // Main stereo audio input pins (Channels 0-1)
    helpers.drawAudioInputPin("Mod In L", 0);
    helpers.drawAudioInputPin("Mod In R", 1);
    
    // Global modulation inputs (Channels 2-4)
    helpers.drawAudioInputPin("Rate Mod", 2);
    helpers.drawAudioInputPin("Gate Mod", 3);
    helpers.drawAudioInputPin("Steps Mod", 4);
    
    // Dynamic per-step modulation inputs
    const int boundMaxPins = stepsModMaxParam != nullptr ? juce::jlimit (1, MAX_STEPS, (int) stepsModMaxParam->load()) : MAX_STEPS;
    int activeSteps = numStepsParam != nullptr ? (int) numStepsParam->load() : 8;

    // FIX: reflect live, modulated steps value (from audio thread telemetry)
    if (isParamInputConnected("numSteps_mod"))
    {
        const int liveSteps = (int) std::round (getLiveParamValueFor("numSteps_mod", "steps_live", (float) activeSteps));
        activeSteps = juce::jlimit (1, boundMaxPins, liveSteps);
    }
    else
    {
        activeSteps = juce::jlimit (1, boundMaxPins, activeSteps);
    }
    
    // Interleaved per-step pins: Step n Mod, Step n Trig Mod, Step n Gate Mod (absolute channels match pin DB)
    for (int i = 0; i < activeSteps; ++i)
    {
        const int stepIdx = i + 1;
        const int valChan  = 6 + (stepIdx - 1);           // 6..21
        const int trigChan = 22 + (stepIdx - 1);          // 22..37
        const int gateChan = 38 + (stepIdx - 1);          // 38..53
        helpers.drawAudioInputPin(("Step " + juce::String(stepIdx) + " Mod").toRawUTF8(), valChan);
        helpers.drawAudioInputPin(("Step " + juce::String(stepIdx) + " Trig Mod").toRawUTF8(), trigChan);
        helpers.drawAudioInputPin(("Step " + juce::String(stepIdx) + " Gate Mod").toRawUTF8(), gateChan);
    }

    // Output pins
    helpers.drawAudioOutputPin("Pitch", 0);
    helpers.drawAudioOutputPin("Gate", 1);
    helpers.drawAudioOutputPin("Gate Nuanced", 2);
    helpers.drawAudioOutputPin("Velocity", 3);
    helpers.drawAudioOutputPin("Mod", 4);
    helpers.drawAudioOutputPin("Trigger", 5);

    // Note: helpers API handles pin disappearance when the number of steps shrinks; no manual clear required here.
}
#endif

// Parameter bus contract implementation
bool StepSequencerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    // ARCHITECTURAL FIX: All modulation is now on the single input bus at index 0
    outBusIndex = 0;

    // Global Audio/Mod Inputs (Absolute Channels)
    // 0-1: Mod In L/R, 2: Rate, 3: GateLen, 4: Steps, 5: Steps Max
    if (paramId == "rate_mod")       { outChannelIndexInBus = 2; return true; }
    if (paramId == "gateLength_mod") { outChannelIndexInBus = 3; return true; }
    if (paramId == "numSteps_mod")   { outChannelIndexInBus = 4; return true; }

    // Per-Step Trigger Modulation (Absolute Channels 22..37) — check TRIGGER first to avoid matching generic "_mod" suffix
    if (paramId.startsWith("step") && paramId.endsWith("_trig_mod"))
    {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false)
                           .upToFirstOccurrenceOf("_trig_mod", false, false)
                           .getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS)
        {
            outChannelIndexInBus = 22 + (stepNum - 1); // e.g., step1_trig_mod is on channel 22
            return true;
        }
    }

    // Per-Step Value Modulation (Absolute Channels 6..21)
    if (paramId.startsWith("step") && paramId.endsWith("_mod") && !paramId.endsWith("_trig_mod") && !paramId.endsWith("_gate_mod"))
    {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false)
                           .upToFirstOccurrenceOf("_mod", false, false)
                           .getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS)
        {
            outChannelIndexInBus = 6 + (stepNum - 1); // e.g., step1_mod is on channel 6
            return true;
        }
    }

    // Per-Step Gate Level Modulation (Absolute Channels 38..53)
    if (paramId.startsWith("step") && paramId.endsWith("_gate_mod"))
    {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false)
                           .upToFirstOccurrenceOf("_gate_mod", false, false)
                           .getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS)
        {
            outChannelIndexInBus = 38 + (stepNum - 1); // e.g., step1_gate_mod is on channel 38
            return true;
        }
    }
    
    return false;
}

std::optional<RhythmInfo> StepSequencerModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;
    
    // Build display name with logical ID
    info.displayName = "Sequencer #" + juce::String(getLogicalId());
    info.sourceType = "sequencer";
    
    // Check if synced to transport
    const bool syncEnabled = apvts.getRawParameterValue("sync")->load() > 0.5f;
    info.isSynced = syncEnabled;
    
    // Check if active (transport playing in sync mode, or always active in free-running)
    if (syncEnabled)
    {
        info.isActive = m_currentTransport.isPlaying;
    }
    else
    {
        info.isActive = true; // Free-running is always active
    }
    
    // Calculate effective BPM
    if (syncEnabled && info.isActive)
    {
        // In sync mode: calculate effective BPM from transport + division
        int divisionIndex = (int)apvts.getRawParameterValue("rate_division")->load();
        
        // Check for global division override from Tempo Clock
        if (getParent())
        {
            int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
            if (globalDiv >= 0)
                divisionIndex = globalDiv;
        }
        
        // Division multipliers: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8
        static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
        const double beatDivision = divisions[juce::jlimit(0, 8, divisionIndex)];
        
        // Effective BPM = transport BPM * division * num_steps
        // (Each complete cycle through all steps = one "measure" in BPM terms)
        const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
        info.bpm = static_cast<float>(m_currentTransport.bpm * beatDivision * numSteps);
    }
    else if (!syncEnabled)
    {
        // Free-running mode: convert Hz rate to BPM
        // Rate is in steps per second, convert to beats per minute
        const float rate = rateParam ? rateParam->load() : 2.0f;
        const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
        
        // One full cycle through all steps = one "beat"
        // BPM = (cycles_per_second) * 60
        // cycles_per_second = rate_hz / num_steps
        info.bpm = (rate / static_cast<float>(numSteps)) * 60.0f;
    }
    else
    {
        // Synced but transport stopped
        info.bpm = 0.0f;
    }
    
    return info;
}


