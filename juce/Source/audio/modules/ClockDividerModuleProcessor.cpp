#include "ClockDividerModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ClockDividerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hysteresis", "Hysteresis", juce::NormalisableRange<float>(0.0f, 0.5f, 0.0001f), 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pulseWidth", "Pulse Width", juce::NormalisableRange<float>(0.01f, 1.0f, 0.0001f), 0.5f));
    return { params.begin(), params.end() };
}

ClockDividerModuleProcessor::ClockDividerModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Clock In", juce::AudioChannelSet::mono(), true)
                        .withInput("Reset", juce::AudioChannelSet::mono(), true)
                        .withOutput("Out", juce::AudioChannelSet::discreteChannels(6), true)),
      apvts(*this, nullptr, "ClockDivParams", createParameterLayout())
{
    gateThresholdParam = apvts.getRawParameterValue("gateThreshold");
    hysteresisParam    = apvts.getRawParameterValue("hysteresis");
    pulseWidthParam    = apvts.getRawParameterValue("pulseWidth");
    // ADD THIS BLOCK:
    for (int i = 0; i < 6; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void ClockDividerModuleProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    clockCount = 0;
    div2State = div4State = div8State = false;
    currentClockInterval = sampleRate; // Default to 1 second
    samplesSinceLastClock = 0;
    multiplierPhase[0] = multiplierPhase[1] = multiplierPhase[2] = 0.0;
    lastInputState = false;
    schmittStateClock = false;
    schmittStateReset = false;
    for (int i = 0; i < 6; ++i) pulseSamplesRemaining[i] = 0;
    
#if defined(PRESET_CREATOR_UI)
    captureBuffer.setSize(8, samplesPerBlock); // 0=Clock, 1=Reset, 2-7=Outputs
    captureBuffer.clear();
    for (auto& v : vizData.clockInputWaveform) v.store(0.0f);
    for (auto& v : vizData.resetInputWaveform) v.store(0.0f);
    for (auto& v : vizData.div2Waveform) v.store(0.0f);
    for (auto& v : vizData.div4Waveform) v.store(0.0f);
    for (auto& v : vizData.div8Waveform) v.store(0.0f);
    for (auto& v : vizData.mul2Waveform) v.store(0.0f);
    for (auto& v : vizData.mul3Waveform) v.store(0.0f);
    for (auto& v : vizData.mul4Waveform) v.store(0.0f);
    vizData.writeIndex.store(0);
    vizData.currentBpm.store(0.0);
    vizData.clockInterval.store(0.0);
    vizData.clockCountLive.store(0);
#endif
}

void ClockDividerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto inClock = getBusBuffer(buffer, true, 0);
    auto inReset = getBusBuffer(buffer, true, 1);
    auto out = getBusBuffer(buffer, false, 0);

    const float* clockIn = inClock.getReadPointer(0);
    const float* resetIn = inReset.getNumChannels() > 0 ? inReset.getReadPointer(0) : nullptr;
    float* div2Out = out.getWritePointer(0);
    float* div4Out = out.getWritePointer(1);
    float* div8Out = out.getWritePointer(2);
    float* mul2Out = out.getWritePointer(3);
    float* mul3Out = out.getWritePointer(4);
    float* mul4Out = out.getWritePointer(5);

    const float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    const float hyst = hysteresisParam != nullptr ? hysteresisParam->load() : 0.05f;
    const float highThresh = juce::jlimit(0.0f, 1.0f, gateThresh + hyst);
    const float lowThresh  = juce::jlimit(0.0f, 1.0f, gateThresh - hyst);
    const float pulseWidth = pulseWidthParam != nullptr ? pulseWidthParam->load() : 0.5f;

#if defined(PRESET_CREATOR_UI)
    // Prepare capture buffer
    const int numSamples = buffer.getNumSamples();
    if (captureBuffer.getNumSamples() < numSamples)
        captureBuffer.setSize(8, numSamples, false, false, true);
#endif

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Schmitt trigger for clock
        const float vin = clockIn[i];
        if (!schmittStateClock && vin >= highThresh) schmittStateClock = true;
        else if (schmittStateClock && vin <= lowThresh) schmittStateClock = false;

        // Optional reset
        bool doReset = false;
        if (resetIn != nullptr)
        {
            const float vr = resetIn[i];
            if (!schmittStateReset && vr >= highThresh) { schmittStateReset = true; doReset = true; }
            else if (schmittStateReset && vr <= lowThresh) schmittStateReset = false;
        }

        if (doReset)
        {
            clockCount = 0;
            div2State = div4State = div8State = false;
            samplesSinceLastClock = 0;
            multiplierPhase[0] = multiplierPhase[1] = multiplierPhase[2] = 0.0;
            for (int k = 0; k < 6; ++k) pulseSamplesRemaining[k] = 0;
        }

        samplesSinceLastClock++;

        // --- Division on rising edge ---
        if (schmittStateClock && !lastInputState)
        {
            currentClockInterval = samplesSinceLastClock;
            samplesSinceLastClock = 0;
            
            clockCount++;
            if (clockCount % 2 == 0) { div2State = !div2State; pulseSamplesRemaining[0] = (int) juce::jmax(1.0, currentClockInterval * 0.5 * pulseWidth); }
            if (clockCount % 4 == 0) { div4State = !div4State; pulseSamplesRemaining[1] = (int) juce::jmax(1.0, currentClockInterval * 1.0 * pulseWidth); }
            if (clockCount % 8 == 0) { div8State = !div8State; pulseSamplesRemaining[2] = (int) juce::jmax(1.0, currentClockInterval * 2.0 * pulseWidth); }
        }
        lastInputState = schmittStateClock;

        // Gate/trigger shaping using pulse width
        div2Out[i] = pulseSamplesRemaining[0]-- > 0 ? 1.0f : 0.0f;
        div4Out[i] = pulseSamplesRemaining[1]-- > 0 ? 1.0f : 0.0f;
        div8Out[i] = pulseSamplesRemaining[2]-- > 0 ? 1.0f : 0.0f;

        // --- Multiplication via phase ---
        if (currentClockInterval > 0)
        {
            double phaseInc = 1.0 / currentClockInterval;
            
            // x2
            multiplierPhase[0] += phaseInc * 2.0;
            if (multiplierPhase[0] >= 1.0) multiplierPhase[0] -= 1.0;
            mul2Out[i] = (multiplierPhase[0] < pulseWidth) ? 1.0f : 0.0f;

            // x3
            multiplierPhase[1] += phaseInc * 3.0;
            if (multiplierPhase[1] >= 1.0) multiplierPhase[1] -= 1.0;
            mul3Out[i] = (multiplierPhase[1] < pulseWidth) ? 1.0f : 0.0f;

            // x4
            multiplierPhase[2] += phaseInc * 4.0;
            if (multiplierPhase[2] >= 1.0) multiplierPhase[2] -= 1.0;
            mul4Out[i] = (multiplierPhase[2] < pulseWidth) ? 1.0f : 0.0f;
        }
        else
        {
            // No valid clock interval - output zero
            mul2Out[i] = 0.0f;
            mul3Out[i] = 0.0f;
            mul4Out[i] = 0.0f;
        }
        
#if defined(PRESET_CREATOR_UI)
        // Capture for visualization
        captureBuffer.setSample(0, i, clockIn[i]);
        captureBuffer.setSample(1, i, resetIn != nullptr ? resetIn[i] : 0.0f);
        captureBuffer.setSample(2, i, div2Out[i]);
        captureBuffer.setSample(3, i, div4Out[i]);
        captureBuffer.setSample(4, i, div8Out[i]);
        captureBuffer.setSample(5, i, mul2Out[i]);
        captureBuffer.setSample(6, i, mul3Out[i]);
        captureBuffer.setSample(7, i, mul4Out[i]);
#endif
    }
    
#if defined(PRESET_CREATOR_UI)
    // Update visualization data
    const int numSamplesForViz = buffer.getNumSamples();
    const int stride = juce::jmax(1, numSamplesForViz / VizData::waveformPoints);
    
    for (int i = 0; i < VizData::waveformPoints && (i * stride) < numSamplesForViz; ++i)
    {
        const int sampleIdx = i * stride;
        vizData.clockInputWaveform[i].store(captureBuffer.getSample(0, sampleIdx));
        vizData.resetInputWaveform[i].store(captureBuffer.getSample(1, sampleIdx));
        vizData.div2Waveform[i].store(captureBuffer.getSample(2, sampleIdx));
        vizData.div4Waveform[i].store(captureBuffer.getSample(3, sampleIdx));
        vizData.div8Waveform[i].store(captureBuffer.getSample(4, sampleIdx));
        vizData.mul2Waveform[i].store(captureBuffer.getSample(5, sampleIdx));
        vizData.mul3Waveform[i].store(captureBuffer.getSample(6, sampleIdx));
        vizData.mul4Waveform[i].store(captureBuffer.getSample(7, sampleIdx));
    }
    
    // Update BPM and clock info
    const double bpm = (currentClockInterval > 0.0) ? (60.0 * sampleRate / currentClockInterval) : 0.0;
    vizData.currentBpm.store(bpm);
    vizData.clockInterval.store(currentClockInterval);
    vizData.clockCountLive.store(clockCount);
#endif
    
    // ADD THIS BLOCK:
    if (lastOutputValues.size() >= 6)
    {
        for (int i = 0; i < 6; ++i)
            if (lastOutputValues[i])
                lastOutputValues[i]->store(out.getSample(i, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void ClockDividerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);
    
    // === SECTION: Clock Settings ===
    ThemeText("CLOCK SETTINGS", theme.text.section_header);
    
    float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    if (ImGui::SliderFloat("Gate Thresh", &gateThresh, 0.0f, 1.0f, "%.3f")) { 
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gateThreshold"))) *p = gateThresh; 
        onModificationEnded(); 
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Threshold for detecting clock pulses");
    
    float hyst = hysteresisParam != nullptr ? hysteresisParam->load() : 0.05f;
    if (ImGui::SliderFloat("Hysteresis", &hyst, 0.0f, 0.5f, "%.4f")) { 
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("hysteresis"))) *p = hyst; 
        onModificationEnded(); 
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Noise immunity for clock detection");
    
    float pw = pulseWidthParam != nullptr ? pulseWidthParam->load() : 0.5f;
    if (ImGui::SliderFloat("Pulse Width", &pw, 0.01f, 1.0f, "%.3f")) { 
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("pulseWidth"))) *p = pw; 
        onModificationEnded(); 
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Output pulse width (0-1)");
    
    ImGui::Spacing();
    
    // === SECTION: Clock Monitor ===
    ThemeText("CLOCK MONITOR", theme.text.section_header);
    
    // BPM and clock info
    const double bpm = vizData.currentBpm.load();
    const double interval = vizData.clockInterval.load();
    const int count = vizData.clockCountLive.load();
    ImGui::Text("BPM: %.1f", bpm);
    ImGui::SameLine();
    ImGui::Text(" | Interval: %.0f samples", interval);
    ImGui::Text("Clock Count: %d", count);
    
    ImGui::Spacing();
    
    // Waveform Visualization
    const auto& freqColors = theme.modules.frequency_graph;
    const auto resolveColor = [](ImU32 value, ImU32 fallback) { return value != 0 ? value : fallback; };
    const float graphHeight = 180.0f;
    if (ImGui::BeginChild("ClockDividerWaveform", ImVec2(itemWidth, graphHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        const ImVec2 p1 = ImVec2(p0.x + childSize.x, p0.y + childSize.y);
        
        // Background
        const ImU32 bgColor = resolveColor(freqColors.background, IM_COL32(18, 20, 24, 255));
        drawList->AddRectFilled(p0, p1, bgColor);
        
        // Grid lines
        const ImU32 gridColor = resolveColor(freqColors.grid, IM_COL32(50, 55, 65, 255));
        const float centerY = p0.y + childSize.y * 0.5f;
        drawList->AddLine(ImVec2(p0.x, centerY), ImVec2(p1.x, centerY), gridColor, 1.0f);
        drawList->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), gridColor, 1.0f);
        drawList->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), gridColor, 1.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        // Read waveform data
        std::array<float, VizData::waveformPoints> clockIn, resetIn, div2, div4, div8, mul2, mul3, mul4;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            clockIn[i] = vizData.clockInputWaveform[i].load();
            resetIn[i] = vizData.resetInputWaveform[i].load();
            div2[i] = vizData.div2Waveform[i].load();
            div4[i] = vizData.div4Waveform[i].load();
            div8[i] = vizData.div8Waveform[i].load();
            mul2[i] = vizData.mul2Waveform[i].load();
            mul3[i] = vizData.mul3Waveform[i].load();
            mul4[i] = vizData.mul4Waveform[i].load();
        }
        
        // Draw waveforms with different vertical offsets
        const float halfHeight = childSize.y * 0.5f;
        const float scale = halfHeight * 0.35f; // Scale for visibility
        const float yOffset = childSize.y / 8.0f; // Vertical spacing between traces
        const float channelHeight = yOffset;
        
        // Clock Input (top, white/cyan)
        ImU32 colorClock = IM_COL32(100, 200, 255, 255);
        float yBase = p0.y + yOffset;
        for (int i = 1; i < VizData::waveformPoints; ++i)
        {
            float x0 = p0.x + (float)(i - 1) / (float)(VizData::waveformPoints - 1) * childSize.x;
            float x1 = p0.x + (float)i / (float)(VizData::waveformPoints - 1) * childSize.x;
            float y0 = juce::jlimit(p0.y, p1.y, yBase - clockIn[i - 1] * scale);
            float y1 = juce::jlimit(p0.y, p1.y, yBase - clockIn[i] * scale);
            drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), colorClock, 2.0f);
        }
        
        // Reset Input (if active, red/orange)
        ImU32 colorReset = IM_COL32(255, 100, 80, 180);
        float yBaseReset = p0.y + yOffset * 2.0f;
        for (int i = 1; i < VizData::waveformPoints; ++i)
        {
            if (resetIn[i] > 0.01f || resetIn[i-1] > 0.01f) // Only draw if there's signal
            {
                float x0 = p0.x + (float)(i - 1) / (float)(VizData::waveformPoints - 1) * childSize.x;
                float x1 = p0.x + (float)i / (float)(VizData::waveformPoints - 1) * childSize.x;
                float y0 = juce::jlimit(p0.y, p1.y, yBaseReset - resetIn[i - 1] * scale);
                float y1 = juce::jlimit(p0.y, p1.y, yBaseReset - resetIn[i] * scale);
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), colorReset, 1.5f);
            }
        }
        
        // Divider outputs (/2, /4, /8) - green/cyan shades
        ImU32 colorDiv2 = IM_COL32(80, 255, 120, 255);
        ImU32 colorDiv4 = IM_COL32(80, 200, 255, 255);
        ImU32 colorDiv8 = IM_COL32(120, 180, 255, 255);
        
        float yBaseDiv2 = p0.y + yOffset * 3.5f;
        float yBaseDiv4 = p0.y + yOffset * 4.5f;
        float yBaseDiv8 = p0.y + yOffset * 5.5f;
        
        for (int i = 1; i < VizData::waveformPoints; ++i)
        {
            float x0 = p0.x + (float)(i - 1) / (float)(VizData::waveformPoints - 1) * childSize.x;
            float x1 = p0.x + (float)i / (float)(VizData::waveformPoints - 1) * childSize.x;
            
            float y0_div2 = juce::jlimit(p0.y, p1.y, yBaseDiv2 - div2[i - 1] * scale);
            float y1_div2 = juce::jlimit(p0.y, p1.y, yBaseDiv2 - div2[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_div2), ImVec2(x1, y1_div2), colorDiv2, 1.5f);
            
            float y0_div4 = juce::jlimit(p0.y, p1.y, yBaseDiv4 - div4[i - 1] * scale);
            float y1_div4 = juce::jlimit(p0.y, p1.y, yBaseDiv4 - div4[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_div4), ImVec2(x1, y1_div4), colorDiv4, 1.5f);
            
            float y0_div8 = juce::jlimit(p0.y, p1.y, yBaseDiv8 - div8[i - 1] * scale);
            float y1_div8 = juce::jlimit(p0.y, p1.y, yBaseDiv8 - div8[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_div8), ImVec2(x1, y1_div8), colorDiv8, 1.5f);
        }
        
        // Multiplier outputs (x2, x3, x4) - yellow/magenta shades
        ImU32 colorMul2 = IM_COL32(255, 220, 80, 255);
        ImU32 colorMul3 = IM_COL32(255, 150, 200, 255);
        ImU32 colorMul4 = IM_COL32(255, 100, 255, 255);
        
        float yBaseMul2 = p0.y + yOffset * 6.5f;
        float yBaseMul3 = p0.y + yOffset * 7.0f;
        float yBaseMul4 = p0.y + yOffset * 7.5f;
        
        for (int i = 1; i < VizData::waveformPoints; ++i)
        {
            float x0 = p0.x + (float)(i - 1) / (float)(VizData::waveformPoints - 1) * childSize.x;
            float x1 = p0.x + (float)i / (float)(VizData::waveformPoints - 1) * childSize.x;
            
            float y0_mul2 = juce::jlimit(p0.y, p1.y, yBaseMul2 - mul2[i - 1] * scale);
            float y1_mul2 = juce::jlimit(p0.y, p1.y, yBaseMul2 - mul2[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_mul2), ImVec2(x1, y1_mul2), colorMul2, 1.5f);
            
            float y0_mul3 = juce::jlimit(p0.y, p1.y, yBaseMul3 - mul3[i - 1] * scale);
            float y1_mul3 = juce::jlimit(p0.y, p1.y, yBaseMul3 - mul3[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_mul3), ImVec2(x1, y1_mul3), colorMul3, 1.5f);
            
            float y0_mul4 = juce::jlimit(p0.y, p1.y, yBaseMul4 - mul4[i - 1] * scale);
            float y1_mul4 = juce::jlimit(p0.y, p1.y, yBaseMul4 - mul4[i] * scale);
            drawList->AddLine(ImVec2(x0, y0_mul4), ImVec2(x1, y1_mul4), colorMul4, 1.5f);
        }
        
        const char* channelLabels[8] = { "Clock", "Reset", "/2", "/4", "/8", "x2", "x3", "x4" };
        const float channelCenters[8] = {
            p0.y + yOffset,
            p0.y + yOffset * 2.0f,
            p0.y + yOffset * 3.5f,
            p0.y + yOffset * 4.5f,
            p0.y + yOffset * 5.5f,
            p0.y + yOffset * 6.5f,
            p0.y + yOffset * 7.0f,
            p0.y + yOffset * 7.5f
        };
        const ImU32 channelColors[8] = {
            colorClock,
            colorReset,
            colorDiv2,
            colorDiv4,
            colorDiv8,
            colorMul2,
            colorMul3,
            colorMul4
        };
        
        drawList->PopClipRect();
        
        // Label text per channel using drawList to avoid cursor jumps
        for (int ch = 0; ch < 8; ++ch)
        {
            const float labelY = juce::jlimit(p0.y, p1.y, channelCenters[ch] - channelHeight * 0.45f);
            drawList->AddText(ImVec2(p0.x + 6.0f, labelY), channelColors[ch], channelLabels[ch]);
        }
        
        // Threshold label
        const float thresholdDisplay = gateThresholdParam != nullptr ? gateThresholdParam->load() : gateThresh;
        auto thresholdText = juce::String::formatted("Threshold: %.4f", thresholdDisplay);
        drawList->AddText(ImVec2(p1.x - 150.0f, p0.y + 4.0f), IM_COL32(255, 160, 160, 255), thresholdText.toRawUTF8());
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##clockDividerWaveformDrag", childSize);
    }
    ImGui::EndChild();
    
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void ClockDividerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Clock In", 0);
    helpers.drawAudioInputPin("Reset", 1);
    helpers.drawAudioOutputPin("/2", 0);
    helpers.drawAudioOutputPin("/4", 1);
    helpers.drawAudioOutputPin("/8", 2);
    helpers.drawAudioOutputPin("x2", 3);
    helpers.drawAudioOutputPin("x3", 4);
    helpers.drawAudioOutputPin("x4", 5);
}
#endif

std::optional<RhythmInfo> ClockDividerModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;
    
    // Build display name with logical ID
    info.displayName = "Clock Divider #" + juce::String(getLogicalId());
    info.sourceType = "clock_divider";
    
    // Clock Divider is clock-driven (not synced to transport)
    info.isSynced = false;
    
#if defined(PRESET_CREATOR_UI)
    // Get detected BPM from visualization data (calculated in processBlock)
    const double detectedBPM = vizData.currentBpm.load();
    const double clockInterval = vizData.clockInterval.load();
    
    // Active if we're receiving clock input (interval > 0 means clock detected)
    info.isActive = clockInterval > 0.0;
    
    // BPM is the detected input clock BPM
    info.bpm = static_cast<float>(detectedBPM);
#else
    // In non-UI builds, we can't access vizData, so return unknown
    info.isActive = false;
    info.bpm = 0.0f;
#endif
    
    // Validate BPM before returning
    if (!std::isfinite(info.bpm) || info.bpm < 0.0f)
        info.bpm = 0.0f;
    
    return info;
}
