#include "RandomModuleProcessor.h"
#include <cmath>

// --- Parameter Layout Definition ---
juce::AudioProcessorValueTreeState::ParameterLayout RandomModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdCvMin, "CV Min", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdCvMax, "CV Max", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdNormMin, "Norm Min", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdNormMax, "Norm Max", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMin, "Min", -100.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMax, "Max", -100.0f, 100.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSlew, "Slew", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate", juce::NormalisableRange<float>(0.1f, 50.0f, 0.01f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdTrigThreshold, "Trig Threshold", 0.0f, 1.0f, 0.5f));
    
    return { params.begin(), params.end() };
}

// --- Constructor ---
RandomModuleProcessor::RandomModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        // THE FIX: Define one input bus with 3 channels for Trigger, Rate, and Slew.
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true)
                        .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(5), true)),
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
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    targetValue = currentValue = minVal + rng.nextFloat() * (maxVal - minVal);
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    targetValueCV = currentValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
    
    phase = 1.0; // Force trigger on first sample
    trigPulseRemaining = 0;
    lastTriggerState = false;

    smoothedRate.reset(newSampleRate, 0.01);
    smoothedSlew.reset(newSampleRate, 0.01);
}

// --- Main Audio Processing Block ---
void RandomModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Removed verbose one-shot diagnostics; lifecycle is fixed in graph
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    outBus.clear();
    
    const int numSamples = buffer.getNumSamples();

    const bool isTrigIn = isParamInputConnected(paramIdTriggerIn);
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isSlewMod = isParamInputConnected(paramIdSlewMod);

    const float* trigCV = isTrigIn && inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* rateCV = isRateMod && inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* slewCV = isSlewMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;

    const float baseRate = rateParam->load();
    const float baseSlew = slewParam->load();
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    const float normMinVal = normMinParam->load();
    const float normMaxVal = normMaxParam->load();
    const float trigThreshold = trigThresholdParam->load();

    auto* normOut = outBus.getWritePointer(0);
    auto* rawOut  = outBus.getWritePointer(1);
    auto* cvOut   = outBus.getWritePointer(2);
    auto* boolOut = outBus.getWritePointer(3);
    auto* trigOut = outBus.getWritePointer(4);

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Calculate effective parameters for this sample
        float effectiveRate = baseRate;
        if (isRateMod && rateCV) {
            const float cv = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            effectiveRate = 0.1f * std::pow(500.0f, cv); // Logarithmic mapping
            static int logCounter = 0;
            if (cv > 0.001f && (logCounter++ % 400) == 0 && i == 0)
                juce::Logger::writeToLog("[RANDOM-NODE-FIXED] Received non-zero Rate CV: " + juce::String(cv, 4));
        }

        float effectiveSlew = baseSlew;
        if (isSlewMod && slewCV) {
            effectiveSlew = juce::jlimit(0.0f, 1.0f, slewCV[i]);
        }

        smoothedRate.setTargetValue(effectiveRate);
        smoothedSlew.setTargetValue(effectiveSlew);
        const float smoothedRateValue = smoothedRate.getNextValue();
        const float smoothedSlewValue = smoothedSlew.getNextValue();

        // 2. Check for trigger condition
        bool shouldGenerateNew = false;
        if (isTrigIn && trigCV) { // External trigger has priority
            const bool trigHigh = trigCV[i] > 0.5f;
            if (trigHigh && !lastTriggerState) {
                shouldGenerateNew = true;
            }
            lastTriggerState = trigHigh;
        } else { // Use internal clock
            phase += smoothedRateValue / sampleRate;
            if (phase >= 1.0) {
                phase -= 1.0;
                shouldGenerateNew = true;
            }
        }

        // 3. Generate new target values if triggered
        if (shouldGenerateNew)
        {
            targetValue = minVal + rng.nextFloat() * (maxVal - minVal);
            targetValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
            if (currentValueCV >= trigThreshold) {
                trigPulseRemaining = (int) std::max(1.0, sampleRate * 0.001); // 1ms pulse
            }
        }

        // 4. Apply slew
        if (smoothedSlewValue <= 0.0001f) {
            currentValue = targetValue;
            currentValueCV = targetValueCV;
        } else {
            const float slewCoeff = (float)(1.0 - std::exp(-1.0 / (smoothedSlewValue * (float)sampleRate)));
            currentValue += (targetValue - currentValue) * slewCoeff;
            currentValueCV += (targetValueCV - currentValueCV) * slewCoeff;
        }

        // 5. Calculate and write all outputs
        float tempNormalized;
        const float range = maxVal - minVal;
        if (std::abs(range) < 1e-6f) {
            tempNormalized = 0.5f;
        } else {
            tempNormalized = (currentValue - minVal) / range;
        }
        float normalizedValue = juce::jmap(tempNormalized, 0.0f, 1.0f, normMinVal, normMaxVal);
        
        normOut[i] = normalizedValue;
        rawOut[i]  = currentValue;
        cvOut[i]   = currentValueCV;
        boolOut[i] = (currentValueCV >= trigThreshold) ? 1.0f : 0.0f;
        
        trigOut[i] = (trigPulseRemaining > 0) ? 1.0f : 0.0f;
        if (trigPulseRemaining > 0) --trigPulseRemaining;

        // 6. Update telemetry for UI (throttled)
        if ((i & 0x3F) == 0) {
            setLiveParamValue("rate_live", smoothedRateValue);
            setLiveParamValue("slew_live", smoothedSlewValue);
        }
    }
    
    // Update inspector values
    lastNormalizedOutputValue.store(normOut[numSamples - 1]);
    lastOutputValue.store(rawOut[numSamples - 1]);
    lastCvOutputValue.store(cvOut[numSamples - 1]);
    lastBoolOutputValue.store(boolOut[numSamples - 1]);
    lastTrigOutputValue.store(trigOut[numSamples - 1]);
}

#if defined(PRESET_CREATOR_UI)
// --- UI Drawing and Pin Definitions ---
void RandomModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    float cvMin = cvMinParam->load();
    float cvMax = cvMaxParam->load();
    float normMin = normMinParam->load();
    float normMax = normMaxParam->load();
    float minVal = minParam->load();
    float maxVal = maxParam->load();
    float trigThreshold = trigThresholdParam->load();

    const bool isRateMod = isParamModulated(paramIdRateMod);
    float rate = isRateMod ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
    
    const bool isSlewMod = isParamModulated(paramIdSlewMod);
    float slew = isSlewMod ? getLiveParamValueFor(paramIdSlewMod, "slew_live", slewParam->load()) : slewParam->load();

    ImGui::PushItemWidth(itemWidth);

    if (isRateMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.1f, 50.0f, "%.3f Hz", ImGuiSliderFlags_Logarithmic)) {
        if (!isRateMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate))) *p = rate;
    }
    if (!isRateMod) adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isRateMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (isSlewMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Slew", &slew, 0.0f, 1.0f, "%.3f")) {
        if (!isSlewMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSlew))) *p = slew;
    }
    if (!isSlewMod) adjustParamOnWheel(ap.getParameter(paramIdSlew), "slew", slew);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isSlewMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (ImGui::SliderFloat("CV Min", &cvMin, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMin)) = cvMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMin), "cvMin", cvMin);

    if (ImGui::SliderFloat("CV Max", &cvMax, 0.0f, 1.0f)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMax)) = cvMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMax), "cvMax", cvMax);
    
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
    
    if (ImGui::SliderFloat("Trig Threshold", &trigThreshold, 0.0f, 1.0f, "%.2f")) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdTrigThreshold)) = trigThreshold;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdTrigThreshold), "trigThreshold", trigThreshold);

    ImGui::Text("Raw Output:        %.2f", getLastOutputValue());
    ImGui::Text("Normalized Output: %.2f", getLastNormalizedOutputValue());
    ImGui::Text("CV Output:         %.2f", getLastCvOutputValue());
    ImGui::Text("Bool Output:       %s", (getLastBoolOutputValue() > 0.5f) ? "On" : "Off");
    ImGui::Text("Trig Output:       %s", (getLastTrigOutputValue() > 0.5f) ? "On" : "Off");

    ImGui::PopItemWidth();
}

void RandomModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // --- DEBUG LOG: REASON 4 ---
    juce::Logger::writeToLog("[DEBUG-RANDOM-UI] Drawing Input Pins:");
    juce::Logger::writeToLog("  - 'Trigger In' on channel 0");
    juce::Logger::writeToLog("  - 'Rate Mod' on channel 1");
    juce::Logger::writeToLog("  - 'Slew Mod' on channel 2");
    // --- END DEBUG LOG ---

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
    switch (channel) {
        case 0: return "Trigger In";
        case 1: return "Rate Mod";
        case 2: return "Slew Mod";
        default: return {};
    }
}

juce::String RandomModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel) {
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
    outBusIndex = 0; // All inputs are on the single input bus.
    if (paramId == paramIdTriggerIn) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdRateMod) { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdSlewMod) { outChannelIndexInBus = 2; return true; }
    return false;
}
#endif

// --- UI Display Helpers ---
float RandomModuleProcessor::getLastOutputValue() const { return lastOutputValue.load(); }
float RandomModuleProcessor::getLastNormalizedOutputValue() const { return lastNormalizedOutputValue.load(); }
float RandomModuleProcessor::getLastCvOutputValue() const { return lastCvOutputValue.load(); }
float RandomModuleProcessor::getLastBoolOutputValue() const { return lastBoolOutputValue.load(); }
float RandomModuleProcessor::getLastTrigOutputValue() const { return lastTrigOutputValue.load(); }