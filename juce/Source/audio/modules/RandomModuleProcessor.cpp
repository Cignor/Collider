#include "RandomModuleProcessor.h"
#include <cmath>

// --- Parameter Layout Definition ---
juce::AudioProcessorValueTreeState::ParameterLayout RandomModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Parameters for the "CV Out" signal (0-1 range)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdCvMin, "CV Min", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdCvMax, "CV Max", 0.0f, 1.0f, 1.0f));

    // Parameters for the "Raw Out" signal (wide bipolar range)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMin, "Min", -100.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMax, "Max", -100.0f, 100.0f, 1.0f));
    
    // Parameters for the "Norm Out" signal (0-1 range)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdNormMin, "Norm Min", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdNormMax, "Norm Max", 0.0f, 1.0f, 1.0f));

    // Control parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSlew, "Slew", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate", juce::NormalisableRange<float>(0.1f, 50.0f, 0.01f, 0.3f), 1.0f));
    
    // Boolean threshold parameter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdTrigThreshold, "Trig Threshold", 0.0f, 1.0f, 0.5f));
    
    // Note: No real APVTS parameters are needed for mod inputs, their IDs are just for routing.
    
    return { params.begin(), params.end() };
}

// --- Constructor ---
RandomModuleProcessor::RandomModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // 0:Trig, 1:Rate Mod, 2:Slew Mod
                        .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(5), true)), // 0:Norm, 1:Raw, 2:CV, 3:Bool, 4:Trig
      apvts(*this, nullptr, "RandomParams", createParameterLayout())
{
    minParam = apvts.getRawParameterValue(paramIdMin);
    maxParam = apvts.getRawParameterValue(paramIdMax);
    cvMinParam = apvts.getRawParameterValue(paramIdCvMin);
    cvMaxParam = apvts.getRawParameterValue(paramIdCvMax);
    normMinParam = apvts.getRawParameterValue(paramIdNormMin);
    normMaxParam = apvts.getRawParameterValue(paramIdNormMax);
    slewParam = apvts.getRawParameterValue(paramIdSlew);
    rateParam = apvts.getRawParameterValue(paramIdRate);
    trigThresholdParam = apvts.getRawParameterValue(paramIdTrigThreshold);

    // Initialize storage for the five output pins for cable inspector
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

// --- Audio Processing Setup ---
void RandomModuleProcessor::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    // Initialize with a random value to avoid starting at 0
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    targetValue = currentValue = minVal + rng.nextFloat() * (maxVal - minVal);
    
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    targetValueCV = currentValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
    
    lastTrig = 0.0f;
    phase = 0.0;
    samplesUntilNext = (int) std::max(1.0, std::floor(sampleRate / std::max(0.1, (double) rateParam->load())));
    trigPulseRemaining = 0;
    
    // CRITICAL FIX: Initialize smoothed values (like BestPracticeNodeProcessor)
    smoothedRate.reset(newSampleRate, 0.01);  // 10ms smoothing time
    smoothedSlew.reset(newSampleRate, 0.01);
    smoothedRate.setCurrentAndTargetValue(rateParam->load());
    smoothedSlew.setCurrentAndTargetValue(slewParam->load());
}

// --- Main Audio Processing Block ---
void RandomModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Get dedicated handles for input and output buses
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    // Clear ONLY the output bus to start fresh
    outBus.clear();
    
    const int numSamples = buffer.getNumSamples();

    // Check for modulation using virtual _mod parameter IDs (Pattern B)
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isSlewMod = isParamInputConnected(paramIdSlewMod);

    // Get modulation CVs from the single input bus
    const float* trigIn = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* rateCV = isRateMod && inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* slewCV = isSlewMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;

    // Get base parameter values
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    const float normMinVal = normMinParam->load();
    const float normMaxVal = normMaxParam->load();
    const float baseSlew = slewParam->load();
    const float baseRate = rateParam->load();
    const float trigThreshold = trigThresholdParam->load();

    // Get write pointers FROM THE CORRECT OUTPUT BUS
    auto* normOut = outBus.getNumChannels() > 0 ? outBus.getWritePointer(0) : nullptr;
    auto* rawOut  = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : nullptr;
    auto* cvOut   = outBus.getNumChannels() > 2 ? outBus.getWritePointer(2) : nullptr;
    auto* boolOut = outBus.getNumChannels() > 3 ? outBus.getWritePointer(3) : nullptr;
    auto* trigOut = outBus.getNumChannels() > 4 ? outBus.getWritePointer(4) : nullptr;

    // --- The entire per-sample processing loop (FIXED WITH SMOOTHING) ---
    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Calculate effective parameter values for this sample (with smoothing like BestPracticeNodeProcessor)
        float targetRate = baseRate;
        if (rateCV != nullptr) {
            const float cv01 = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            constexpr float fMin = 0.1f;
            constexpr float fMax = 50.0f;
            const float spanOct = std::log2(fMax / fMin);
            targetRate = fMin * std::pow(2.0f, cv01 * spanOct);
        }
        smoothedRate.setTargetValue(targetRate);
        float effectiveRate = smoothedRate.getNextValue();

        float targetSlew = baseSlew;
        if (slewCV != nullptr) {
            targetSlew = juce::jlimit(0.0f, 1.0f, slewCV[i]);
        }
        smoothedSlew.setTargetValue(targetSlew);
        float effectiveSlew = smoothedSlew.getNextValue();

        // 2. Check for triggers
        bool shouldGenerateNew = false;
        const int periodSamples = (int) std::max(1.0, std::floor(sampleRate / std::max(0.1, (double) effectiveRate)));
        if (samplesUntilNext <= 0)
        {
            samplesUntilNext = periodSamples;
            shouldGenerateNew = true;
        }
        --samplesUntilNext;
        if (trigIn != nullptr)
        {
            const bool triggered = trigIn[i] > 0.5f && lastTrig <= 0.5f;
            lastTrig = trigIn[i];
            if (triggered) shouldGenerateNew = true;
        }

        // 3. Generate new target values if triggered
        if (shouldGenerateNew)
        {
            targetValue = minVal + rng.nextFloat() * (maxVal - minVal);
            targetValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
            if (targetValueCV >= trigThreshold) {
                trigPulseRemaining = (int) std::max(1.0, sampleRate * 0.001);
            }
        }

        // 4. Apply slew
        if (effectiveSlew <= 0.0001f)
        {
            currentValue = targetValue;
            currentValueCV = targetValueCV;
        }
        else
        {
            const float slewSeconds = effectiveSlew;
            const float slewCoeff = (float)(1.0 - std::exp(-1.0 / (slewSeconds * sampleRate)));
            currentValue += (targetValue - currentValue) * slewCoeff;
            currentValueCV += (targetValueCV - currentValueCV) * slewCoeff;
        }

        // 5. Calculate normalized output
        float tempNormalized;
        const float range = maxVal - minVal;
        if (std::abs(range) < 1e-6f) {
            tempNormalized = 0.5f;
        } else {
            tempNormalized = (currentValue - minVal) / range;
        }
        float normalizedValue = juce::jmap(tempNormalized, 0.0f, 1.0f, normMinVal, normMaxVal);
        
        // 6. Write to output channels (pointers are now correct)
        if (normOut) normOut[i] = normalizedValue;
        if (rawOut)  rawOut[i]  = currentValue;
        if (cvOut)   cvOut[i]   = currentValueCV;
        if (boolOut) boolOut[i] = (tempNormalized >= trigThreshold) ? 1.0f : 0.0f;
        if (trigOut) {
            trigOut[i] = (trigPulseRemaining > 0) ? 1.0f : 0.0f;
            if (trigPulseRemaining > 0)
                --trigPulseRemaining;
        }

        // 7. Update telemetry for UI (throttled) - CRITICAL FIX: Use smoothed current values
        if ((i & 0x3F) == 0) {
            setLiveParamValue("rate_live", smoothedRate.getCurrentValue());
            setLiveParamValue("slew_live", smoothedSlew.getCurrentValue());
        }
    }
    
    // --- Update Inspector and UI Display Values from the correct output bus ---
    lastNormalizedOutputValue.store(normOut ? normOut[numSamples - 1] : 0.0f);
    lastOutputValue.store(rawOut ? rawOut[numSamples - 1] : 0.0f);
    lastCvOutputValue.store(cvOut ? cvOut[numSamples - 1] : 0.0f);
    lastBoolOutputValue.store(boolOut ? boolOut[numSamples - 1] : 0.0f);
    lastTrigOutputValue.store(trigOut ? trigOut[numSamples - 1] : 0.0f);

    if (lastOutputValues.size() >= 5)
    {
        if (lastOutputValues[0] && normOut) lastOutputValues[0]->store(normOut[numSamples - 1]);
        if (lastOutputValues[1] && rawOut)  lastOutputValues[1]->store(rawOut[numSamples - 1]);
        if (lastOutputValues[2] && cvOut)   lastOutputValues[2]->store(cvOut[numSamples - 1]);
        if (lastOutputValues[3] && boolOut) lastOutputValues[3]->store(boolOut[numSamples - 1]);
        if (lastOutputValues[4] && trigOut) lastOutputValues[4]->store(trigOut[numSamples - 1]);
    }
}


#if defined(PRESET_CREATOR_UI)
// --- UI Drawing and Pin Definitions ---

void RandomModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    // Check for modulation using virtual _mod parameter IDs (Pattern B)
    bool slewIsMod = isParamModulated(paramIdSlewMod);
    bool rateIsMod = isParamModulated(paramIdRateMod);

    float cvMin = cvMinParam->load();
    float cvMax = cvMaxParam->load();
    float minVal = minParam->load();
    float maxVal = maxParam->load();
    // --- Get live values if modulated, otherwise get base parameter values ---
    float slew = slewIsMod ? getLiveParamValueFor(paramIdSlew, "slew_live", slewParam->load()) : slewParam->load();
    float rate = rateIsMod ? getLiveParamValueFor(paramIdRate, "rate_live", rateParam->load()) : rateParam->load();
    float trigThreshold = trigThresholdParam->load();

    ImGui::PushItemWidth(itemWidth);

    if (ImGui::SliderFloat("CV Min", &cvMin, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMin)) = cvMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMin), "cvMin", cvMin);

    if (ImGui::SliderFloat("CV Max", &cvMax, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMax)) = cvMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMax), "cvMax", cvMax);

    float normMin = normMinParam->load();
    float normMax = normMaxParam->load();

    if (ImGui::SliderFloat("Norm Min", &normMin, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdNormMin)) = normMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdNormMin), "normMin", normMin);

    if (ImGui::SliderFloat("Norm Max", &normMax, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdNormMax)) = normMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdNormMax), "normMax", normMax);

    if (ImGui::SliderFloat("Min", &minVal, -100.0f, 100.0f, "%.3f")) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMin)) = minVal;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdMin), "min", minVal);

    if (ImGui::SliderFloat("Max", &maxVal, -100.0f, 100.0f, "%.3f")) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMax)) = maxVal;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdMax), "max", maxVal);

    // --- Slew Slider with Modulation Feedback ---
    if (slewIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Slew", &slew, 0.0f, 1.0f, "%.3f")) if (!slewIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSlew)) = slew;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (!slewIsMod) adjustParamOnWheel(ap.getParameter(paramIdSlew), "slew", slew);
    if (slewIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // --- Rate Slider with Modulation Feedback ---
    if (rateIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.1f, 50.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) if (!rateIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (!rateIsMod) adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
    if (rateIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (ImGui::SliderFloat("Trig Threshold", &trigThreshold, 0.0f, 1.0f, "%.2f")) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdTrigThreshold)) = trigThreshold;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdTrigThreshold), "trigThreshold", trigThreshold);

    // Output value displays
    ImGui::Text("Raw Output:        %.2f", getLastOutputValue());
    ImGui::Text("Normalized Output: %.2f", getLastNormalizedOutputValue());
    ImGui::Text("CV Output:         %.2f", getLastCvOutputValue());
    ImGui::Text("Bool Output:       %s", (getLastBoolOutputValue() > 0.5f) ? "On" : "Off");
    ImGui::Text("Trig Output:       %s", (getLastTrigOutputValue() > 0.5f) ? "On" : "Off");

    ImGui::PopItemWidth();
}

void RandomModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Trigger In", 0);
    helpers.drawAudioInputPin("Rate Mod", 1);
    helpers.drawAudioInputPin("Slew Mod", 2);
    helpers.drawAudioOutputPin("Norm Out", 0);
    helpers.drawAudioOutputPin("Raw Out", 1);
    helpers.drawAudioOutputPin("CV Out", 2);
    helpers.drawAudioOutputPin("Bool Out", 3);
    helpers.drawAudioOutputPin("Trig Out", 4);
}

juce::String RandomModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Trigger In";
        case 1: return "Rate Mod";
        case 2: return "Slew Mod";
        default: return {};
    }
}

juce::String RandomModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Norm Out";
        case 1: return "Raw Out";
        case 2: return "CV Out";
        case 3: return "Bool Out";
        case 4: return "Trig Out";
        default: return {};
    }
}

bool RandomModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    // Map virtual _mod parameter IDs to physical channels (Pattern B)
    if (paramId == paramIdRateMod) { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdSlewMod) { outChannelIndexInBus = 2; return true; }
    // Note: The trigger input doesn't need routing as it's not tied to a parameter
    return false;
}
#endif

// --- UI Display Helpers ---
float RandomModuleProcessor::getLastOutputValue() const { return lastOutputValue.load(); }
float RandomModuleProcessor::getLastNormalizedOutputValue() const { return lastNormalizedOutputValue.load(); }
float RandomModuleProcessor::getLastCvOutputValue() const { return lastCvOutputValue.load(); }
float RandomModuleProcessor::getLastBoolOutputValue() const { return lastBoolOutputValue.load(); }
float RandomModuleProcessor::getLastTrigOutputValue() const { return lastTrigOutputValue.load(); }