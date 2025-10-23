#include "RandomModuleProcessor.h"
#include <cmath>

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
    
    // Transport sync parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("sync", "Sync to Transport", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("rate_division", "Division",
        juce::StringArray{ "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" }, 3));
    
    return { params.begin(), params.end() };
}

RandomModuleProcessor::RandomModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::disabled(), true)
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
    for (int i = 0; i < 5; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void RandomModuleProcessor::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    phase = 1.0;
    lastScaledBeats = 0.0;
    trigPulseRemaining = 0;
    smoothedSlew.reset(newSampleRate, 0.01);
    smoothedSlew.setCurrentAndTargetValue(slewParam->load());
    // Initialize with a random value
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    targetValue = currentValue = minVal + rng.nextFloat() * (maxVal - minVal);
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    targetValueCV = currentValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
}

void RandomModuleProcessor::setTimingInfo(const TransportState& state)
{
    m_currentTransport = state;
}

void RandomModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto outBus = getBusBuffer(buffer, false, 0);
    outBus.clear();
    const int numSamples = buffer.getNumSamples();

    const float baseRate = rateParam->load();
    const float baseSlew = slewParam->load();
    const float minVal = minParam->load();
    const float maxVal = maxParam->load();
    const float cvMinVal = cvMinParam->load();
    const float cvMaxVal = cvMaxParam->load();
    const float normMinVal = normMinParam->load();
    const float normMaxVal = normMaxParam->load();
    const float trigThreshold = trigThresholdParam->load();

    // Get sync parameters
    const bool syncEnabled = apvts.getRawParameterValue("sync")->load() > 0.5f;
    const int divisionIndex = (int)apvts.getRawParameterValue("rate_division")->load();
    static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
    const double beatDivision = divisions[juce::jlimit(0, 8, divisionIndex)];

    auto* normOut = outBus.getWritePointer(0);
    auto* rawOut  = outBus.getWritePointer(1);
    auto* cvOut   = outBus.getWritePointer(2);
    auto* boolOut = outBus.getWritePointer(3);
    auto* trigOut = outBus.getWritePointer(4);

    for (int i = 0; i < numSamples; ++i)
    {
        bool triggerNewValue = false;
        if (syncEnabled && m_currentTransport.isPlaying)
        {
            // SYNC MODE
            double beatsNow = m_currentTransport.songPositionBeats + (i / sampleRate / 60.0 * m_currentTransport.bpm);
            double scaledBeats = beatsNow * beatDivision;
            if (static_cast<long long>(scaledBeats) > static_cast<long long>(lastScaledBeats))
            {
                triggerNewValue = true;
            }
            lastScaledBeats = scaledBeats;
        }
        else
        {
            // FREE-RUNNING MODE
            phase += (double)baseRate / sampleRate;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                triggerNewValue = true;
            }
        }

        if (triggerNewValue)
        {
            targetValue = minVal + rng.nextFloat() * (maxVal - minVal);
            targetValueCV = cvMinVal + rng.nextFloat() * (cvMaxVal - cvMinVal);
            if (currentValueCV >= trigThreshold) {
                trigPulseRemaining = (int) std::max(1.0, sampleRate * 0.001);
            }
        }

        smoothedSlew.setTargetValue(baseSlew);
        float effectiveSlew = smoothedSlew.getNextValue();
        if (effectiveSlew <= 0.0001f) {
            currentValue = targetValue;
            currentValueCV = targetValueCV;
        } else {
            const float slewCoeff = (float)(1.0 - std::exp(-1.0 / (effectiveSlew * (float)sampleRate)));
            currentValue += (targetValue - currentValue) * slewCoeff;
            currentValueCV += (targetValueCV - currentValueCV) * slewCoeff;
        }
        
        float tempNormalized = (std::abs(maxVal - minVal) < 1e-6f) ? 0.5f : (currentValue - minVal) / (maxVal - minVal);
        float normalizedValue = juce::jmap(tempNormalized, 0.0f, 1.0f, normMinVal, normMaxVal);
        
        normOut[i] = normalizedValue;
        rawOut[i]  = currentValue;
        cvOut[i]   = currentValueCV;
        boolOut[i] = (currentValueCV >= trigThreshold) ? 1.0f : 0.0f;
        
        trigOut[i] = (trigPulseRemaining > 0) ? 1.0f : 0.0f;
        if (trigPulseRemaining > 0) --trigPulseRemaining;
    }
    
    lastNormalizedOutputValue.store(normOut[numSamples - 1]);
    lastOutputValue.store(rawOut[numSamples - 1]);
    lastCvOutputValue.store(cvOut[numSamples - 1]);
    lastBoolOutputValue.store(boolOut[numSamples - 1]);
    lastTrigOutputValue.store(trigOut[numSamples - 1]);
    
    // Update lastOutputValues for cable inspector
    if (lastOutputValues.size() >= 5)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(normOut[numSamples - 1]);
        if (lastOutputValues[1]) lastOutputValues[1]->store(rawOut[numSamples - 1]);
        if (lastOutputValues[2]) lastOutputValues[2]->store(cvOut[numSamples - 1]);
        if (lastOutputValues[3]) lastOutputValues[3]->store(boolOut[numSamples - 1]);
        if (lastOutputValues[4]) lastOutputValues[4]->store(trigOut[numSamples - 1]);
    }
}

juce::ValueTree RandomModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("RandomState");
    vt.setProperty("sync", apvts.getRawParameterValue("sync")->load(), nullptr);
    vt.setProperty("rate_division", apvts.getRawParameterValue("rate_division")->load(), nullptr);
    return vt;
}

void RandomModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("RandomState"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("sync")))
            *p = (bool)vt.getProperty("sync", false);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("rate_division")))
            *p = (int)vt.getProperty("rate_division", 3);
    }
}

#if defined(PRESET_CREATOR_UI)
void RandomModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    
    auto& ap = getAPVTS();
    float cvMin = cvMinParam->load();
    float cvMax = cvMaxParam->load();
    float normMin = normMinParam->load();
    float normMax = normMaxParam->load();
    float minVal = minParam->load();
    float maxVal = maxParam->load();
    float slew = slewParam->load();
    float rate = rateParam->load();
    float trigThreshold = trigThresholdParam->load();

    ImGui::PushItemWidth(itemWidth);

    // --- SYNC CONTROLS ---
    bool sync = apvts.getRawParameterValue("sync")->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("sync")))
            *p = sync;
        onModificationEnded();
    }
    
    if (sync)
    {
        int division = (int)apvts.getRawParameterValue("rate_division")->load();
        if (ImGui::Combo("Division", &division, "1/32\0""1/16\0""1/8\0""1/4\0""1/2\0""1\0""2\0""4\0""8\0\0"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("rate_division")))
                *p = division;
            onModificationEnded();
        }
    }
    else
    {
        // Rate slider (only show in free-running mode)
        if (ImGui::SliderFloat("Rate", &rate, 0.1f, 50.0f, "%.3f Hz", ImGuiSliderFlags_Logarithmic)) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
    }

    if (ImGui::SliderFloat("Slew", &slew, 0.0f, 1.0f, "%.3f")) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSlew)) = slew;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdSlew), "slew", slew);
    
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
    helpers.drawAudioOutputPin("Norm Out", 0);
    helpers.drawAudioOutputPin("Raw Out", 1);
    helpers.drawAudioOutputPin("CV Out", 2);
    helpers.drawAudioOutputPin("Bool Out", 3);
    helpers.drawAudioOutputPin("Trig Out", 4);
}

juce::String RandomModuleProcessor::getAudioInputLabel(int) const { return {}; }

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

bool RandomModuleProcessor::getParamRouting(const juce::String&, int&, int&) const { return false; }
#endif

float RandomModuleProcessor::getLastOutputValue() const { return lastOutputValue.load(); }
float RandomModuleProcessor::getLastNormalizedOutputValue() const { return lastNormalizedOutputValue.load(); }
float RandomModuleProcessor::getLastCvOutputValue() const { return lastCvOutputValue.load(); }
float RandomModuleProcessor::getLastBoolOutputValue() const { return lastBoolOutputValue.load(); }
float RandomModuleProcessor::getLastTrigOutputValue() const { return lastTrigOutputValue.load(); }
