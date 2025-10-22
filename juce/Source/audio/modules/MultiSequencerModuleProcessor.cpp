#include "MultiSequencerModuleProcessor.h"
#include <iostream>
#include <array>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

using APVTS = juce::AudioProcessorValueTreeState;

// This function is a direct copy from the original StepSequencerModuleProcessor.cpp
static juce::NormalisableRange<float> makeRateRange()
{
    juce::NormalisableRange<float> r (0.1f, 20.0f, 0.01f, 0.5f);
    return r;
}

// This function is a direct copy from the original StepSequencerModuleProcessor.cpp
APVTS::ParameterLayout MultiSequencerModuleProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterInt> ("numSteps", "Number of Steps", 1, MAX_STEPS, 8));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("rate", "Rate", makeRateRange(), 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateLength", "Gate Length", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateThreshold", "Gate Threshold", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("rate_mod", "Rate Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateLength_mod", "Gate Length Mod", 0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("numSteps_mod", "Num Steps Mod", 0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("numSteps_max", "Num Steps Max", 1, MAX_STEPS, MAX_STEPS));
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        const juce::String pid = "step" + juce::String (i + 1);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (pid, pid, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        const juce::String modPid = "step" + juce::String(i + 1) + "_mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(modPid, modPid, 0.0f, 1.0f, 0.5f));
        const juce::String trigPid = "step" + juce::String(i + 1) + "_trig";
        params.push_back(std::make_unique<juce::AudioParameterBool>(trigPid, trigPid, false));
        const juce::String trigModPid = "step" + juce::String(i + 1) + "_trig_mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(trigModPid, trigModPid, 0.0f, 1.0f, 0.5f));
        const juce::String gatePid = "step" + juce::String(i + 1) + "_gate";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(gatePid, gatePid, 0.0f, 1.0f, 0.8f));
        const juce::String gateModPid = "step" + juce::String(i + 1) + "_gate_mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(gateModPid, gateModPid, 0.0f, 1.0f, 0.5f));
	}
	return { params.begin(), params.end() };
}

MultiSequencerModuleProcessor::MultiSequencerModuleProcessor()
    // <<< CHANGE 1: Update the output bus to add per-step outputs
    : ModuleProcessor (BusesProperties()
                           .withInput("Inputs", juce::AudioChannelSet::discreteChannels(2 + 4 + (MAX_STEPS * 3)), true)
                           .withOutput ("Outputs", juce::AudioChannelSet::discreteChannels(6 + (MAX_STEPS * 3)), true)),
      apvts (*this, nullptr, "SeqParams", createParameterLayout())
{
    numStepsParam      = apvts.getRawParameterValue ("numSteps");
    rateParam          = apvts.getRawParameterValue ("rate");
    gateLengthParam    = apvts.getRawParameterValue ("gateLength");
    gateThresholdParam = apvts.getRawParameterValue ("gateThreshold");
    rateModParam       = apvts.getRawParameterValue ("rate_mod");
    gateLengthModParam = apvts.getRawParameterValue ("gateLength_mod");
    numStepsModParam   = apvts.getRawParameterValue ("numSteps_mod");
    stepsModMaxParam   = apvts.getRawParameterValue ("numSteps_max");
    
    pitchParams.resize (MAX_STEPS);
	stepModParams.resize(MAX_STEPS);
	stepTrigParams.resize(MAX_STEPS);
    stepTrigModParams.resize(MAX_STEPS);
    stepGateParams.resize(MAX_STEPS);
	for (int i = 0; i < MAX_STEPS; ++i)
	{
        pitchParams[i] = apvts.getRawParameterValue ("step" + juce::String (i + 1));
		stepModParams[i] = apvts.getRawParameterValue("step" + juce::String(i + 1) + "_mod");
		stepTrigParams[i] = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("step" + juce::String(i + 1) + "_trig"));
        stepTrigModParams[i] = apvts.getRawParameterValue("step" + juce::String(i + 1) + "_trig_mod");
        stepGateParams[i] = apvts.getRawParameterValue("step" + juce::String(i + 1) + "_gate");
	}

    // <<< CHANGE 2: Initialize for all 54 outputs
    for (int i = 0; i < 6 + (MAX_STEPS * 3); ++i)
		lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MultiSequencerModuleProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
	phase = 0.0;
}

void MultiSequencerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // --- PART 1: The original, working StepSequencer logic for "Live" outputs ---
    juce::ignoreUnused (midi);
    if (rateParam == nullptr || numStepsParam == nullptr || gateLengthParam == nullptr)
    {
        buffer.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const auto& inputBus = getBusBuffer(buffer, true, 0);
    auto* pitchOut       = buffer.getWritePointer(0);
    auto* gateOut        = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    auto* gateNuancedOut = buffer.getNumChannels() > 2 ? buffer.getWritePointer(2) : nullptr;
    auto* velocityOut    = buffer.getNumChannels() > 3 ? buffer.getWritePointer(3) : nullptr;
    auto* modOut         = buffer.getNumChannels() > 4 ? buffer.getWritePointer(4) : nullptr;
    auto* trigOut        = buffer.getNumChannels() > 5 ? buffer.getWritePointer(5) : nullptr;
    
    const bool isRateMod = isParamInputConnected("rate_mod");
    const bool isGateLenMod = isParamInputConnected("gateLength_mod");
    const bool isStepsMod = isParamInputConnected("numSteps_mod");
    const float* rateCV = isRateMod && inputBus.getNumChannels() > 2 ? inputBus.getReadPointer(2) : nullptr;
    const float* gateLenCV = isGateLenMod && inputBus.getNumChannels() > 3 ? inputBus.getReadPointer(3) : nullptr;
    const float* stepsCV = isStepsMod && inputBus.getNumChannels() > 4 ? inputBus.getReadPointer(4) : nullptr;
    
    const float baseRate = rateParam->load();
    const float baseGate = gateLengthParam->load();
    const int baseSteps = (int) numStepsParam->load();
    const int boundMax = stepsModMaxParam != nullptr ? juce::jlimit (1, MAX_STEPS, (int) stepsModMaxParam->load()) : MAX_STEPS;
    const float gateThreshold = gateThresholdParam != nullptr ? juce::jlimit(0.0f, 1.0f, gateThresholdParam->load()) : 0.5f;

    // Start of Logging Block
    static int logCounter = 0;
    const bool shouldLog = (logCounter++ % 300 == 0);
    if (shouldLog) juce::Logger::writeToLog("--- MultiSequencer::processBlock ---");
    // End of Logging Block

    // --- UI Telemetry Bootstrap (from old StepSequencer) ---
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
            if (hasCh && isParamInputConnected("step" + juce::String(si + 1) + "_mod"))
            {
                const float cv0 = inputBus.getReadPointer(ch)[0];
                live = juce::jlimit(0.0f, 1.0f, base + (cv0 - 0.5f));
            }
            setLiveParamValue("step_live_" + juce::String(si + 1), live);

            // Per-step gate live values: channels 38..53
            const int gateCh = 38 + si;
            const bool hasGateCh = totalCh > gateCh;
            const float baseGateVal = (stepGateParams.size() > (size_t) si && stepGateParams[si] != nullptr) ? stepGateParams[si]->load() : 0.8f;
            float liveGate = baseGateVal;
            if (hasGateCh && isParamInputConnected("step" + juce::String(si + 1) + "_gate_mod"))
            {
                const float cv0 = inputBus.getReadPointer(gateCh)[0];
                liveGate = juce::jlimit(0.0f, 1.0f, baseGateVal + (cv0 - 0.5f));
            }
            setLiveParamValue("gate_live_" + juce::String(si + 1), liveGate);

            // Per-step trigger live values: channels 22..37
            const int trigCh = 22 + si;
            const bool hasTrigCh = totalCh > trigCh;
            const bool baseTrig = (stepTrigParams.size() > (size_t) si && stepTrigParams[si] != nullptr) ? (bool)(*stepTrigParams[si]) : false;
            bool liveTrig = baseTrig;
            if (hasTrigCh && isParamInputConnected("step" + juce::String(si + 1) + "_trig_mod"))
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
        int activeSteps = baseSteps;
        if (isStepsMod && stepsCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, stepsCV[i]);
            const int mapped = 1 + (int) std::round(cv * (MAX_STEPS - 1));
            activeSteps = juce::jlimit(1, boundMax, mapped);
        }
        if (currentStep.load() >= activeSteps)
            currentStep.store(0);
        
        float rate = baseRate;
        if (isRateMod && rateCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            const float modRateHz = 0.01f + cv * (50.0f - 0.01f);
            rate = modRateHz;
        }
        lastRateLive = rate;
        
        float gateLen = baseGate;
        if (isGateLenMod && gateLenCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, gateLenCV[i]);
            gateLen = juce::jlimit(0.0f, 1.0f, cv);
        }
        lastGateLive = gateLen;
        
        float gateThresholdLive = gateThreshold;
        if (isGateLenMod && gateLenCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, gateLenCV[i]);
            gateThresholdLive = juce::jlimit(0.0f, 1.0f, cv);
        }
        lastGateThresholdLive = gateThresholdLive;
        
        const double phaseInc = (sampleRate > 0.0 ? (double) rate / sampleRate : 0.0);
        
        phase += phaseInc;
		if (phase >= 1.0)
		{
            phase -= 1.0;
            const int next = (currentStep.load() + 1) % juce::jlimit (1, MAX_STEPS, activeSteps);
            if (shouldLog) juce::Logger::writeToLog("[M-SEQ LOG] Step Advanced: " + juce::String(currentStep.load()) + " -> " + juce::String(next));
            currentStep.store(next);
            stepAdvanced = true;
        }
        lastStepsLive = activeSteps;

        const int currentStepIndex = currentStep.load();
        const float sliderValue = pitchParams[currentStepIndex] != nullptr ? pitchParams[currentStepIndex]->load() : 0.0f;
        
        float rawModValue = 0.5f;
        const auto stepModId = "step" + juce::String(currentStepIndex + 1) + "_mod";
        if (isParamInputConnected(stepModId))
        {
            const int modChannel = 6 + currentStepIndex;
            if (inputBus.getNumChannels() > modChannel)
                rawModValue = inputBus.getReadPointer(modChannel)[i];
        }
        const float modValue = rawModValue - 0.5f;
        const float pitchValue = juce::jlimit (0.0f, 1.0f, sliderValue + modValue);
        
        float stepGateLevel = (stepGateParams[currentStepIndex] != nullptr) ? stepGateParams[currentStepIndex]->load() : 0.8f;
        const auto gateModId = "step" + juce::String(currentStepIndex + 1) + "_gate_mod";
        if (isParamInputConnected(gateModId))
        {
            const int gateModChannel = 38 + currentStepIndex;
            if (inputBus.getNumChannels() > gateModChannel)
            {
                const float cv = inputBus.getReadPointer(gateModChannel)[i];
                stepGateLevel = juce::jlimit(0.0f, 1.0f, stepGateLevel + (cv - 0.5f));
            }
        }

        const bool isGateOn = (stepGateLevel >= gateThresholdLive);
        if (isGateOn && !previousGateOn) {
            gateFadeProgress = 0.0f;
        } else if (!isGateOn && previousGateOn) {
            gateFadeProgress = 0.0f;
        }
        
        const float fadeIncrement = sampleRate > 0.0f ? (1000.0f / GATE_FADE_TIME_MS) / sampleRate : 0.0f;
        gateFadeProgress = juce::jmin(1.0f, gateFadeProgress + fadeIncrement);
        const float fadeMultiplier = isGateOn ? gateFadeProgress : (1.0f - gateFadeProgress);
        
        const float gateBinaryValue = (phase < gateLen) && isGateOn ? fadeMultiplier : 0.0f;
        const float gateNuancedValue = (phase < gateLen) && isGateOn ? (stepGateLevel * fadeMultiplier) : 0.0f;
        previousGateOn = isGateOn;
        
        bool trigActive = (stepTrigParams[currentStepIndex]) ? (bool)(*stepTrigParams[currentStepIndex]) : false;
        const auto trigModId = "step" + juce::String(currentStepIndex + 1) + "_trig_mod";
        if (isParamInputConnected(trigModId))
        {
            const int trigModChannel = 22 + currentStepIndex;
            if (inputBus.getNumChannels() > trigModChannel)
            {
                if (inputBus.getReadPointer(trigModChannel)[i] > 0.5f) trigActive = true;
            }
        }
        
        if (stepAdvanced) {
            if (shouldLog && trigActive) juce::Logger::writeToLog("[M-SEQ LOG] Trigger pulse armed for step " + juce::String(currentStepIndex));
            pendingTriggerSamples = trigActive ? (int) std::round (0.001 * sampleRate) : 0;
            stepAdvanced = false;
        }

        pitchOut[i] = pitchValue;
        if (gateOut) gateOut[i] = gateBinaryValue;
        if (gateNuancedOut) gateNuancedOut[i] = gateNuancedValue;
        if (velocityOut) velocityOut[i] = 0.85f;
        if (modOut) modOut[i] = 0.0f;
        if (trigOut) {
            trigOut[i] = (pendingTriggerSamples > 0) ? 1.0f : 0.0f;
            if (pendingTriggerSamples > 0) --pendingTriggerSamples;
        }
    }
    if (shouldLog)
    {
        juce::Logger::writeToLog("[M-SEQ LOG] Rate: " + juce::String(lastRateLive, 2) + "Hz | Steps: " + juce::String(lastStepsLive));
        juce::Logger::writeToLog("[M-SEQ LOG] Final Output (last sample): Pitch=" + juce::String(pitchOut[numSamples-1], 3) + ", Gate=" + juce::String(gateOut ? gateOut[numSamples-1] : -1.0f, 3));
    }
    
    setLiveParamValue("rate_live", lastRateLive);
    setLiveParamValue("gateLength_live", lastGateLive);
    setLiveParamValue("gateThreshold_live", lastGateThresholdLive);
    setLiveParamValue("steps_live", (float) lastStepsLive);

    // --- PART 2: NEW Logic to populate the parallel static outputs ---
    auto outBus = getBusBuffer(buffer, false, 0);
    for (int step = 0; step < lastStepsLive; ++step)
    {
        const float baseValue = pitchParams[step] ? pitchParams[step]->load() : 0.0f;
        float liveValue = baseValue;
        const int modChannel = 6 + step;
        if (isParamInputConnected("step" + juce::String(step + 1) + "_mod") && inputBus.getNumChannels() > modChannel) {
            liveValue = juce::jlimit(0.0f, 1.0f, baseValue + (inputBus.getReadPointer(modChannel)[0] - 0.5f));
        }

        const bool baseTrig = stepTrigParams[step] ? (bool)(*stepTrigParams[step]) : false;
        bool liveTrig = baseTrig;
        const int trigModChannel = 22 + step;
        if (isParamInputConnected("step" + juce::String(step + 1) + "_trig_mod") && inputBus.getNumChannels() > trigModChannel) {
            liveTrig = inputBus.getReadPointer(trigModChannel)[0] > 0.5f;
        }

        // --- THE FIX IS HERE ---
        // The trigger is only high if it's enabled AND the playhead is on this step.
        const float trigOutputValue = (liveTrig && step == currentStep.load()) ? 1.0f : 0.0f;

        float gateLevel = stepGateParams[step] ? stepGateParams[step]->load() : 0.8f;
        const int gateModChannel = 38 + step;
        if (isParamInputConnected("step" + juce::String(step + 1) + "_gate_mod") && inputBus.getNumChannels() > gateModChannel) {
             const float cv = inputBus.getReadPointer(gateModChannel)[0];
             gateLevel = juce::jlimit(0.0f, 1.0f, gateLevel + (cv - 0.5f));
        }

        int pitchOutChannel = 6 + step * 3 + 0;
        int gateOutChannel  = 6 + step * 3 + 1;
        int trigOutChannel  = 6 + step * 3 + 2;

        // Fill the buffers with the correct values
        if (pitchOutChannel < outBus.getNumChannels())
            juce::FloatVectorOperations::fill(outBus.getWritePointer(pitchOutChannel), liveValue, numSamples);
            
        if (gateOutChannel < outBus.getNumChannels())
            juce::FloatVectorOperations::fill(outBus.getWritePointer(gateOutChannel), gateLevel, numSamples);

        if (trigOutChannel < outBus.getNumChannels())
            juce::FloatVectorOperations::fill(outBus.getWritePointer(trigOutChannel), trigOutputValue, numSamples);
    }
    
    if (lastOutputValues.size() >= (size_t)outBus.getNumChannels()) {
        for (int ch = 0; ch < outBus.getNumChannels(); ++ch)
            if (lastOutputValues[ch]) lastOutputValues[ch]->store(outBus.getSample(ch, numSamples - 1));
    }
}


#if defined(PRESET_CREATOR_UI)
// ... The rest of your file (drawParametersInNode, etc.) remains unchanged ...
void MultiSequencerModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // This is the full, working UI code from the original StepSequencer, with buttons added.
    auto& ap = getAPVTS();
    int activeSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    const int boundMaxUi = stepsModMaxParam ? juce::jlimit(1, MAX_STEPS, (int)stepsModMaxParam->load()) : MAX_STEPS;
    const bool stepsAreModulated = isParamInputConnected("numSteps_mod");
    if (stepsAreModulated) {
        activeSteps = juce::jlimit(1, boundMaxUi, (int)std::round(getLiveParamValueFor("numSteps_mod", "steps_live", (float)activeSteps)));
    }
    int displayedSteps = activeSteps;
    if (stepsAreModulated) ImGui::BeginDisabled();
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderInt("Steps", &displayedSteps, 1, boundMaxUi)) {
        if (!stepsAreModulated) if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter("numSteps"))) *p = displayedSteps;
    }
    if (!stepsAreModulated) adjustParamOnWheel(ap.getParameter("numSteps"), "numSteps", (float)displayedSteps);
    if (ImGui::IsItemDeactivatedAfterEdit() && !stepsAreModulated) onModificationEnded();
    ImGui::PopItemWidth();
    if (stepsAreModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    const int shown = juce::jlimit(1, MAX_STEPS, displayedSteps);
    const float sliderW = itemWidth / (float)juce::jmax(8, shown) * 0.8f;
    
    ImGui::PushItemWidth(sliderW);
    for (int i = 0; i < shown; ++i) {
        if (i > 0) ImGui::SameLine();
        float baseValue = (pitchParams[i]) ? pitchParams[i]->load() : 0.5f;
        const auto modPid = "step" + juce::String(i + 1) + "_mod";
        const bool modConnected = isParamInputConnected(modPid);
        float liveValue = getLiveParamValueFor(modPid, "step_live_" + juce::String(i + 1), baseValue);
        float sliderValue = modConnected ? liveValue : baseValue;
        const bool isActive = (i == currentStep.load());
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        }
        ImGui::PushID(i);
        if (modConnected) ImGui::BeginDisabled();
        if (ImGui::VSliderFloat("##s", ImVec2(sliderW, 60.0f), &sliderValue, 0.0f, 1.0f, "")) {
            if (!modConnected) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("step" + juce::String(i + 1)))) *p = sliderValue;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        if (!modConnected) {
            if (ImGui::IsItemHovered()) {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) {
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("step" + juce::String(i + 1)))) *p = juce::jlimit(0.0f, 1.0f, baseValue + (wheel > 0 ? 0.05f : -0.05f));
                }
            }
        }
        if (modConnected) ImGui::EndDisabled();
        if (isActive) ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(sliderW);
    ImVec2 gate_sliders_p0 = ImGui::GetCursorScreenPos();
    for (int i = 0; i < shown; ++i) {
		if (i > 0) ImGui::SameLine();
        ImGui::PushID(2000 + i);
        float baseGateValue = (stepGateParams[i]) ? stepGateParams[i]->load() : 0.8f;
        const auto modPid = "step" + juce::String(i + 1) + "_gate_mod";
        const bool modConnected = isParamInputConnected(modPid);
        float sliderValue = modConnected ? getLiveParamValueFor(modPid, "gate_live_" + juce::String(i + 1), baseGateValue) : baseGateValue;
        const bool isActive = (i == currentStep.load());
        if (isActive) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        if (modConnected) ImGui::BeginDisabled();
        if (ImGui::VSliderFloat("##g", ImVec2(sliderW, 60.0f), &sliderValue, 0.0f, 1.0f, "")) {
            if (!modConnected && stepGateParams[i]) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("step" + juce::String(i + 1) + "_gate"))) *p = sliderValue;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        if (!modConnected) {
            if (ImGui::IsItemHovered()) {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("step" + juce::String(i + 1) + "_gate"))) *p = juce::jlimit(0.0f, 1.0f, sliderValue + (wheel > 0 ? 0.05f : -0.05f));
            }
        }
        if (modConnected) ImGui::EndDisabled();
		if (isActive) ImGui::PopStyleColor();
		ImGui::PopID();
	}
	ImGui::PopItemWidth();
    
    const bool gtIsModulatedForLine = isParamInputConnected("gateLength_mod");
    const float threshold_value = gtIsModulatedForLine ? getLiveParamValueFor("gateLength_mod", "gateThreshold_live", (gateThresholdParam ? gateThresholdParam->load() : 0.5f)) : (gateThresholdParam ? gateThresholdParam->load() : 0.5f);
    const float slider_height = 60.0f;
    const float row_width = (sliderW * shown) + (ImGui::GetStyle().ItemSpacing.x * (shown - 1));
    const float line_y = gate_sliders_p0.y + (1.0f - threshold_value) * slider_height;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(gate_sliders_p0.x, line_y), ImVec2(gate_sliders_p0.x + row_width, line_y), IM_COL32(255, 255, 0, 200), 2.0f);
    
    ImGui::Text("Current Step: %d", currentStep.load() + 1);
    
    const bool isRateModulated = isParamInputConnected("rate_mod");
    float rateDisplay = isRateModulated ? getLiveParamValueFor("rate_mod", "rate_live", rateParam->load()) : rateParam->load();
    ImGui::PushItemWidth(itemWidth);
    if (isRateModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate (Hz)", &rateDisplay, 0.1f, 20.0f, "%.2f")) {
        if (!isRateModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("rate"))) *p = rateDisplay;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (!isRateModulated) adjustParamOnWheel(ap.getParameter("rate"), "rate", rateDisplay);
    if (isRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    const bool gtIsModulated = isParamInputConnected("gateLength_mod");
    float gtEff = gtIsModulated ? getLiveParamValueFor("gateLength_mod", "gateThreshold_live", (gateThresholdParam ? gateThresholdParam->load() : 0.5f)) : (gateThresholdParam ? gateThresholdParam->load() : 0.5f);
    if (gtIsModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Gate Threshold", &gtEff, 0.0f, 1.0f)) {
        if (!gtIsModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gateThreshold"))) *p = gtEff;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (!gtIsModulated) adjustParamOnWheel(ap.getParameter("gateThreshold"), "gateThreshold", gtEff);
    if (gtIsModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::PopItemWidth();

    {
        for (int i = 0; i < shown; ++i) {
            if (i > 0) ImGui::SameLine();
            bool baseTrig = (stepTrigParams.size() > (size_t)i && stepTrigParams[i]) ? (bool)(*stepTrigParams[i]) : false;
            const auto trigModId = "step" + juce::String(i + 1) + "_trig_mod";
            const bool trigIsModulated = isParamInputConnected(trigModId);
            bool displayTrig = trigIsModulated ? getLiveParamValueFor(trigModId, "trig_live_" + juce::String(i + 1), baseTrig ? 1.0f : 0.0f) > 0.5f : baseTrig;
            if (trigIsModulated) ImGui::BeginDisabled();
            ImGui::PushID(1000 + i);
            ImGui::SetNextItemWidth(sliderW);
            ImGui::PushItemWidth(sliderW);
            if (ImGui::Checkbox("##trig", &displayTrig) && !trigIsModulated && stepTrigParams[i]) *stepTrigParams[i] = displayTrig;
            ImGui::PopItemWidth();
            float used = ImGui::GetItemRectSize().x;
            if (used < sliderW) { ImGui::SameLine(0.0f, 0.0f); ImGui::Dummy(ImVec2(sliderW - used, 0.0f)); }
            if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
            ImGui::PopID();
            if (trigIsModulated) ImGui::EndDisabled();
        }
    }
    
    // ADDED: Auto-connect buttons
    if (ImGui::Button("Connect to Samplers", ImVec2(itemWidth, 0))) { autoConnectSamplersTriggered = true; }
    if (ImGui::Button("Connect to PolyVCO", ImVec2(itemWidth, 0))) { autoConnectVCOTriggered = true; }
}

void MultiSequencerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    const int boundMaxPins = stepsModMaxParam ? juce::jlimit(1, MAX_STEPS, (int)stepsModMaxParam->load()) : MAX_STEPS;
    int activeSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    if (isParamInputConnected("numSteps_mod")) {
        activeSteps = juce::jlimit(1, boundMaxPins, (int)std::round(getLiveParamValueFor("numSteps_mod", "steps_live", (float)activeSteps)));
    } else {
        activeSteps = juce::jlimit(1, boundMaxPins, activeSteps);
    }
    
    // --- Section 1: Global Inputs & Live Outputs (In Parallel) ---
    helpers.drawParallelPins("Mod In L", 0, "Pitch", 0);
    helpers.drawParallelPins("Mod In R", 1, "Gate", 1);
    helpers.drawParallelPins("Rate Mod", 2, "Gate Nuanced", 2);
    helpers.drawParallelPins("Gate Mod", 3, "Velocity", 3);
    helpers.drawParallelPins("Steps Mod", 4, "Mod", 4);
    
    // This output has no corresponding global input; use nullptr for input
    helpers.drawParallelPins(nullptr, -1, "Trigger", 5);

    // --- Section 2: Per-Step Inputs & Outputs (In Parallel) ---
    for (int i = 0; i < activeSteps; ++i)
    {
        const juce::String stepStr = " " + juce::String(i + 1);

        // Pitch I/O for this step
        helpers.drawParallelPins(("Step" + stepStr + " Mod").toRawUTF8(), 6 + i, 
                                ("Pitch" + stepStr).toRawUTF8(), 6 + i * 3 + 0);
        
        // Gate I/O for this step
        helpers.drawParallelPins(("Step" + stepStr + " Gate Mod").toRawUTF8(), 38 + i, 
                                ("Gate" + stepStr).toRawUTF8(), 6 + i * 3 + 1);

        // Trigger I/O for this step
        helpers.drawParallelPins(("Step" + stepStr + " Trig Mod").toRawUTF8(), 22 + i, 
                                ("Trig" + stepStr).toRawUTF8(), 6 + i * 3 + 2);
	}
}
#endif

juce::String MultiSequencerModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel) {
        case 0: return "Pitch";
        case 1: return "Gate";
        case 2: return "Gate Nuanced";
        case 3: return "Velocity";
        case 4: return "Mod";
        case 5: return "Trigger";
    }
    int stepChannel = channel - 6;
    if (stepChannel >= 0 && stepChannel < MAX_STEPS * 3) {
        int step = (stepChannel / 3) + 1;
        int outputType = stepChannel % 3;
        switch (outputType) {
            case 0: return "Pitch " + juce::String(step);
            case 1: return "Gate " + juce::String(step);
            case 2: return "Trig " + juce::String(step);
        }
    }
	return {};
}

juce::String MultiSequencerModuleProcessor::getAudioInputLabel(int channel) const {
    switch (channel) {
        case 0: return "Mod In L";
        case 1: return "Mod In R";
        case 2: return "Rate Mod";
        case 3: return "Gate Mod";
        case 4: return "Steps Mod";
    }
    if (channel >= 6 && channel < 6 + MAX_STEPS) return "Step " + juce::String(channel - 5) + " Mod";
    if (channel >= 22 && channel < 22 + MAX_STEPS) return "Step " + juce::String(channel - 21) + " Trig Mod";
    if (channel >= 38 && channel < 38 + MAX_STEPS) return "Step " + juce::String(channel - 37) + " Gate Mod";
	return {};
}

bool MultiSequencerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const {
	outBusIndex = 0;
    if (paramId == "rate_mod") { outChannelIndexInBus = 2; return true; }
    if (paramId == "gateLength_mod") { outChannelIndexInBus = 3; return true; }
    if (paramId == "numSteps_mod") { outChannelIndexInBus = 4; return true; }
    if (paramId.startsWith("step") && paramId.endsWith("_mod") && !paramId.endsWith("_trig_mod") && !paramId.endsWith("_gate_mod")) {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false).upToFirstOccurrenceOf("_mod", false, false).getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS) { outChannelIndexInBus = 6 + (stepNum - 1); return true; }
    }
    if (paramId.startsWith("step") && paramId.endsWith("_trig_mod")) {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false).upToFirstOccurrenceOf("_trig_mod", false, false).getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS) { outChannelIndexInBus = 22 + (stepNum - 1); return true; }
    }
    if (paramId.startsWith("step") && paramId.endsWith("_gate_mod")) {
        int stepNum = paramId.fromFirstOccurrenceOf("step", false, false).upToFirstOccurrenceOf("_gate_mod", false, false).getIntValue();
        if (stepNum > 0 && stepNum <= MAX_STEPS) { outChannelIndexInBus = 38 + (stepNum - 1); return true; }
	}
	return false;
}
