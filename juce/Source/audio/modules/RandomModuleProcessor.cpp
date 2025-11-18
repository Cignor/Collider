#include "RandomModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include <imgui.h>
#endif
#include "../graph/ModularSynthProcessor.h"
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

#if defined(PRESET_CREATOR_UI)
    vizRawBuffer.setSize(1, vizBufferSize, false, true, true);
    vizCvBuffer.setSize(1, vizBufferSize, false, true, true);
    vizTrigBuffer.setSize(1, vizBufferSize, false, true, true);
    vizWritePos = 0;
    for (auto& v : vizData.rawWaveform) v.store(0.0f);
    for (auto& v : vizData.cvWaveform) v.store(0.0f);
    for (auto& v : vizData.triggerMarkers) v.store(0.0f);
    vizData.currentMin.store(minVal);
    vizData.currentMax.store(maxVal);
    vizData.currentCvMin.store(cvMinVal);
    vizData.currentCvMax.store(cvMaxVal);
    vizData.currentTrigThreshold.store(trigThresholdParam->load());
    vizData.currentRate.store(rateParam->load());
    vizData.currentSlew.store(slewParam->load());
#endif
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
    int divisionIndex = (int)apvts.getRawParameterValue("rate_division")->load();
    // Use global division if a Tempo Clock has override enabled
    // IMPORTANT: Read from parent's LIVE transport state, not cached copy (which is stale)
    if (syncEnabled && getParent())
    {
        int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
        if (globalDiv >= 0)
            divisionIndex = globalDiv;
    }
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

#if defined(PRESET_CREATOR_UI)
        // Capture waveforms into circular buffers
        if (vizRawBuffer.getNumSamples() > 0 && vizCvBuffer.getNumSamples() > 0 && vizTrigBuffer.getNumSamples() > 0)
        {
            vizRawBuffer.setSample(0, vizWritePos, currentValue);
            vizCvBuffer.setSample(0, vizWritePos, currentValueCV);
            vizTrigBuffer.setSample(0, vizWritePos, trigOut[i]);
            vizWritePos = (vizWritePos + 1) % vizBufferSize;
        }
#endif
    }
    
    lastNormalizedOutputValue.store(normOut[numSamples - 1]);
    lastOutputValue.store(rawOut[numSamples - 1]);
    lastCvOutputValue.store(cvOut[numSamples - 1]);
    lastBoolOutputValue.store(boolOut[numSamples - 1]);
    lastTrigOutputValue.store(trigOut[numSamples - 1]);

#if defined(PRESET_CREATOR_UI)
    // Update visualization data (thread-safe)
    // Downsample waveforms from circular buffers
    const int stride = vizBufferSize / VizData::waveformPoints;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const int readIdx = (vizWritePos - VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
        const float rawSample = vizRawBuffer.getSample(0, readIdx);
        const float cvSample = vizCvBuffer.getSample(0, readIdx);
        const float trigSample = vizTrigBuffer.getSample(0, readIdx);
        vizData.rawWaveform[i].store(rawSample);
        vizData.cvWaveform[i].store(cvSample);
        vizData.triggerMarkers[i].store(trigSample);
    }

    // Update current parameter states
    vizData.currentMin.store(minVal);
    vizData.currentMax.store(maxVal);
    vizData.currentCvMin.store(cvMinVal);
    vizData.currentCvMax.store(cvMaxVal);
    vizData.currentTrigThreshold.store(trigThreshold);
    vizData.currentRate.store(baseRate);
    vizData.currentSlew.store(baseSlew);
#endif
    
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
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
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
    // Note: Parent already manages ID scope with PushID(lid), so we don't push another ID here

    // === SECTION: Timing ===
    ThemeText("TIMING", theme.text.section_header);
    
    bool sync = apvts.getRawParameterValue("sync")->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("sync")))
            *p = sync;
        onModificationEnded();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock random generation to host tempo");
    
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
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("rate_division")))
                    *p = division;
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
                ThemeText("Tempo Clock Division Override Active", theme.text.warning);
                ImGui::TextUnformatted("A Tempo Clock node with 'Division Override' enabled is controlling the global division.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        else
        {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Note division for synced random generation");
        }
    }
    else
    {
        if (ImGui::SliderFloat("Rate", &rate, 0.1f, 50.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic))
        {
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Random value generation rate");
    }

    if (ImGui::SliderFloat("Slew", &slew, 0.0f, 1.0f, "%.3f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSlew)) = slew;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdSlew), "slew", slew);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smoothness of transitions between random values");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === SECTION: Range Controls ===
    ThemeText("RANGE CONTROLS", theme.text.section_header);
    
    if (ImGui::SliderFloat("Min", &minVal, -100.0f, 100.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMin)) = minVal;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdMin), "min", minVal);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum raw output value");

    if (ImGui::SliderFloat("Max", &maxVal, -100.0f, 100.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMax)) = maxVal;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdMax), "max", maxVal);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum raw output value");
    
    if (ImGui::SliderFloat("CV Min", &cvMin, 0.0f, 1.0f, "%.3f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMin)) = cvMin;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMin), "cvMin", cvMin);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum CV output value (0-1)");

    if (ImGui::SliderFloat("CV Max", &cvMax, 0.0f, 1.0f, "%.3f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdCvMax)) = cvMax;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdCvMax), "cvMax", cvMax);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum CV output value (0-1)");

    if (ImGui::SliderFloat("Norm Min", &normMin, 0.0f, 1.0f, "%.3f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdNormMin)) = normMin;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdNormMin), "normMin", normMin);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum normalized output value");

    if (ImGui::SliderFloat("Norm Max", &normMax, 0.0f, 1.0f, "%.3f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdNormMax)) = normMax;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdNormMax), "normMax", normMax);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum normalized output value");
    
    if (ImGui::SliderFloat("Trig Thr", &trigThreshold, 0.0f, 1.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdTrigThreshold)) = trigThreshold;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter(paramIdTrigThreshold), "trigThreshold", trigThreshold);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Threshold for trigger/gate outputs");

    ImGui::Spacing();
    ImGui::Spacing();
    
    // === SECTION: Random Value Visualization ===
    ThemeText("RANDOM OUTPUT", theme.text.section_header);
    ImGui::Spacing();

    ImGui::PushID(this); // Unique ID for this node's UI
    
    // Read visualization data (thread-safe) - BEFORE BeginChild
    float rawWaveform[VizData::waveformPoints];
    float cvWaveform[VizData::waveformPoints];
    float triggerMarkers[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        rawWaveform[i] = vizData.rawWaveform[i].load();
        cvWaveform[i] = vizData.cvWaveform[i].load();
        triggerMarkers[i] = vizData.triggerMarkers[i].load();
    }
    const float currentMin = vizData.currentMin.load();
    const float currentMax = vizData.currentMax.load();
    const float currentCvMin = vizData.currentCvMin.load();
    const float currentCvMax = vizData.currentCvMax.load();
    const float currentTrigThreshold = vizData.currentTrigThreshold.load();
    const float currentRate = vizData.currentRate.load();
    const float currentSlew = vizData.currentSlew.load();

    // Calculate scaling for Raw output (normalize to 0-1 range for display)
    const float rawRange = currentMax - currentMin;
    const float rawScale = (std::abs(rawRange) < 1e-6f) ? 1.0f : 1.0f / rawRange;

    // Calculate scaling for CV output (already 0-1, but we'll show it normalized)
    const float cvRange = currentCvMax - currentCvMin;
    const float cvScale = (std::abs(cvRange) < 1e-6f) ? 1.0f : 1.0f / cvRange;

    // Waveform visualization in child window
    const float waveHeight = 120.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    
    if (ImGui::BeginChild("RandomViz", graphSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        const ImU32 rawColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
        const ImU32 cvColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
        const ImU32 triggerColor = ImGui::ColorConvertFloat4ToU32(theme.accent);
        const ImU32 centerLineColor = IM_COL32(150, 150, 150, 100);
        const ImU32 thresholdLineColor = IM_COL32(255, 255, 255, 120);

        // Draw output waveforms
        const float midY = p0.y + graphSize.y * 0.5f;
        const float scaleY = graphSize.y * 0.45f;
        const float stepX = graphSize.x / (float)(VizData::waveformPoints - 1);

        // Draw center line (zero reference for normalized view)
        drawList->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), centerLineColor, 1.0f);

        // Draw trigger threshold line (for CV output)
        const float trigThresholdNorm = (currentTrigThreshold - currentCvMin) * cvScale;
        const float trigY = midY - (trigThresholdNorm - 0.5f) * scaleY * 2.0f;
        const float clampedTrigY = juce::jlimit(p0.y + 2.0f, p1.y - 2.0f, trigY);
        drawList->AddLine(ImVec2(p0.x, clampedTrigY), ImVec2(p1.x, clampedTrigY), thresholdLineColor, 1.0f);
        drawList->AddText(ImVec2(p0.x + 4.0f, clampedTrigY - 14.0f), thresholdLineColor, "Trig Thr");

        // Draw CV waveform (background, shows normalized 0-1 range)
        float prevX = p0.x;
        float prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float cvNormalized = (cvWaveform[i] - currentCvMin) * cvScale;
            const float sample = juce::jlimit(0.0f, 1.0f, cvNormalized);
            const float x = p0.x + i * stepX;
            const float y = midY - (sample - 0.5f) * scaleY * 2.0f;
            if (i > 0)
            {
                ImVec4 colorVec4 = ImGui::ColorConvertU32ToFloat4(cvColor);
                colorVec4.w = 0.4f; // More transparent for background
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(colorVec4), 1.8f);
            }
            prevX = x;
            prevY = y;
        }

        // Draw Raw waveform (foreground, shows actual value range with slew smoothing visible)
        prevX = p0.x;
        prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float rawNormalized = (rawWaveform[i] - currentMin) * rawScale;
            const float sample = juce::jlimit(0.0f, 1.0f, rawNormalized);
            const float x = p0.x + i * stepX;
            const float y = midY - (sample - 0.5f) * scaleY * 2.0f;
            if (i > 0)
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), rawColor, 2.5f);
            prevX = x;
            prevY = y;
        }

        // Draw trigger markers (vertical lines where triggers occur)
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            if (triggerMarkers[i] > 0.5f)
            {
                const float x = p0.x + i * stepX;
                drawList->AddLine(ImVec2(x, p0.y + 2.0f), ImVec2(x, p0.y + 8.0f), triggerColor, 2.0f);
            }
        }

        drawList->PopClipRect();
        
        // Live parameter values overlay
        ImGui::SetCursorPos(ImVec2(4, 4));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "Rate: %.2f Hz | Slew: %.3f | Range: [%.2f, %.2f]", currentRate, currentSlew, currentMin, currentMax);
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##randomVizDrag", graphSize);
    }
    ImGui::EndChild();

    ImGui::PopID(); // End unique ID
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === SECTION: Live Output ===
    ThemeText("LIVE OUTPUT", theme.text.section_header);
    
    const float rawOut = getLastOutputValue();
    const float normOut = getLastNormalizedOutputValue();
    const float cvOut = getLastCvOutputValue();
    const bool boolOut = getLastBoolOutputValue() > 0.5f;
    const bool trigOut = getLastTrigOutputValue() > 0.5f;
    
    // Calculate fixed width for progress bars using actual text measurements
    const float labelTextWidth = ImGui::CalcTextSize("Norm").x;  // "Norm" is the widest label
    const float valueTextWidth = ImGui::CalcTextSize("-99.99").x;  // Max expected width based on -100 to 100 range
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float barWidth = itemWidth - labelTextWidth - valueTextWidth - (spacing * 2.0f);
    
    // Raw output with bar
    ImGui::Text("Raw");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.accent);
    ImGui::ProgressBar((rawOut - minVal) / (maxVal - minVal + 0.0001f), ImVec2(barWidth, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.2f", rawOut);
    
    // Normalized output with bar
    ImGui::Text("Norm");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.accent);
    ImGui::ProgressBar(normOut, ImVec2(barWidth, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.2f", normOut);
    
    // CV output with bar
    ImGui::Text("CV");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.accent);
    ImGui::ProgressBar(cvOut, ImVec2(barWidth, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.2f", cvOut);
    
    // Bool indicator (LED style)
    ImGui::Text("Bool:");
    ImGui::SameLine();
    ThemeText(boolOut ? "ON" : "OFF", boolOut ? theme.text.success : theme.text.disabled);
    
    // Trig indicator (LED style)
    ImGui::Text("Trig:");
    ImGui::SameLine();
    ThemeText(trigOut ? "TRIG" : "---", trigOut ? theme.text.warning : theme.text.disabled);

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
