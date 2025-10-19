#include "FunctionGeneratorModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

// <<< FIX: Removed all "_mod" parameters. They should NOT be part of the APVTS.
juce::AudioProcessorValueTreeState::ParameterLayout FunctionGeneratorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdRate, "Rate",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.01f, 0.25f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdMode, "Mode",
        juce::StringArray{"Free (Hz)", "Sync"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramIdLoop, "Loop", true)); // <<< FIX: Changed default to true for more immediate sound

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdSlew, "Slew",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdGateThresh, "Gate Thresh",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdTrigThresh, "Trig Thresh",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdPitchBase, "Pitch Base (st)",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdValueMult, "Value Mult",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f, 0.5f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdCurveSelect, "Curve Select",
        juce::StringArray{"Blue", "Red", "Green"}, 0));

    return { params.begin(), params.end() };
}

FunctionGeneratorModuleProcessor::FunctionGeneratorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(10), true)
                          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(13), true)),
      apvts(*this, nullptr, "FunctionGeneratorParams", createParameterLayout())
{
    // Initialize all three curves to default shapes
    for (int curveIndex = 0; curveIndex < 3; ++curveIndex)
    {
        curves[curveIndex].resize(CURVE_RESOLUTION);
        for (int i = 0; i < CURVE_RESOLUTION; ++i)
        {
            float x = (float)i / (float)(CURVE_RESOLUTION - 1);
            switch (curveIndex)
            {
                case 0: curves[curveIndex][i] = x; break; // Blue curve - ramp up
                case 1: curves[curveIndex][i] = 1.0f - x; break; // Red curve - ramp down
                case 2: curves[curveIndex][i] = 0.5f + 0.5f * std::sin(x * juce::MathConstants<float>::twoPi); break; // Green curve - sine wave
            }
        }
    }

    // Cache parameter pointers
    rateParam = apvts.getRawParameterValue(paramIdRate);
    modeParam = apvts.getRawParameterValue(paramIdMode);
    loopParam = apvts.getRawParameterValue(paramIdLoop);
    slewParam = apvts.getRawParameterValue(paramIdSlew);
    gateThreshParam = apvts.getRawParameterValue(paramIdGateThresh);
    trigThreshParam = apvts.getRawParameterValue(paramIdTrigThresh);
    pitchBaseParam = apvts.getRawParameterValue(paramIdPitchBase);
    valueMultParam = apvts.getRawParameterValue(paramIdValueMult);
    curveSelectParam = apvts.getRawParameterValue(paramIdCurveSelect);

    for (int i = 0; i < 13; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void FunctionGeneratorModuleProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr;
    phase = 0.0;
    
    smoothedSlew.reset(sampleRate, 0.01);
    smoothedRate.reset(sampleRate, 0.01);
    smoothedGateThresh.reset(sampleRate, 0.001);
    smoothedTrigThresh.reset(sampleRate, 0.001);
    smoothedPitchBase.reset(sampleRate, 0.01);
    smoothedValueMult.reset(sampleRate, 0.01);
}

void FunctionGeneratorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isSlewMod = isParamInputConnected(paramIdSlewMod);
    const bool isGateThreshMod = isParamInputConnected(paramIdGateThreshMod);
    const bool isGateConnected = isParamInputConnected(paramIdGateIn);
    const bool isTrigThreshMod = isParamInputConnected(paramIdTrigThreshMod);
    const bool isPitchBaseMod = isParamInputConnected(paramIdPitchBaseMod);
    const bool isValueMultMod = isParamInputConnected(paramIdValueMultMod);
    const bool isCurveSelectMod = isParamInputConnected(paramIdCurveSelectMod);

    const float* gateIn = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* triggerIn = inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* syncIn = inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* rateCV = isRateMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* slewCV = isSlewMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;
    const float* gateThreshCV = isGateThreshMod && inBus.getNumChannels() > 5 ? inBus.getReadPointer(5) : nullptr;
    const float* trigThreshCV = isTrigThreshMod && inBus.getNumChannels() > 6 ? inBus.getReadPointer(6) : nullptr;
    const float* pitchBaseCV = isPitchBaseMod && inBus.getNumChannels() > 7 ? inBus.getReadPointer(7) : nullptr;
    const float* valueMultCV = isValueMultMod && inBus.getNumChannels() > 8 ? inBus.getReadPointer(8) : nullptr;
    const float* curveSelectCV = isCurveSelectMod && inBus.getNumChannels() > 9 ? inBus.getReadPointer(9) : nullptr;

    const float baseRate = rateParam->load();
    const int baseMode = static_cast<int>(modeParam->load());
    const bool baseLoop = loopParam->load() > 0.5f;
    const float baseSlew = slewParam->load();
    const float baseGateThresh = gateThreshParam->load();
    const float baseTrigThresh = trigThreshParam->load();
    const float basePitchBase = pitchBaseParam->load();
    const float baseValueMult = valueMultParam->load();
    const int baseCurveSelect = static_cast<int>(curveSelectParam->load());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float effectiveRate = baseRate;
        if (isRateMod && rateCV) {
            const float cv = juce::jlimit(0.0f, 1.0f, rateCV[i]);
            effectiveRate = juce::jmap(cv, 0.1f, 100.0f);
        }

        float effectiveSlew = baseSlew;
        if (isSlewMod && slewCV) effectiveSlew = juce::jlimit(0.0f, 1.0f, slewCV[i]);

        float effectiveGateThresh = baseGateThresh;
        if (isGateThreshMod && gateThreshCV) effectiveGateThresh = juce::jlimit(0.0f, 1.0f, gateThreshCV[i]);

        float effectiveTrigThresh = baseTrigThresh;
        if (isTrigThreshMod && trigThreshCV) effectiveTrigThresh = juce::jlimit(0.0f, 1.0f, trigThreshCV[i]);

        float effectivePitchBase = basePitchBase;
        if (isPitchBaseMod && pitchBaseCV) effectivePitchBase = juce::jlimit(-24.0f, 24.0f, pitchBaseCV[i] * 48.0f - 24.0f);

        float effectiveValueMult = baseValueMult;
        if (isValueMultMod && valueMultCV) effectiveValueMult = juce::jlimit(0.0f, 10.0f, valueMultCV[i] * 10.0f);

        int effectiveCurveSelect = baseCurveSelect;
        if (isCurveSelectMod && curveSelectCV) {
            const float cv = juce::jlimit(0.0f, 1.0f, curveSelectCV[i]);
            effectiveCurveSelect = static_cast<int>(cv * 2.99f);
        }

        smoothedRate.setTargetValue(effectiveRate);
        smoothedSlew.setTargetValue(effectiveSlew);
        smoothedGateThresh.setTargetValue(effectiveGateThresh);
        smoothedTrigThresh.setTargetValue(effectiveTrigThresh);
        smoothedPitchBase.setTargetValue(effectivePitchBase);
        smoothedValueMult.setTargetValue(effectiveValueMult);

        // Get the NEXT smoothed value for all parameters that need it. THIS IS THE FIX.
        const float smoothedRateValue = smoothedRate.getNextValue();
        const float smoothedSlewValue = smoothedSlew.getNextValue();
        const float smoothedGateThreshValue = smoothedGateThresh.getNextValue();
        const float smoothedTrigThreshValue = smoothedTrigThresh.getNextValue();
        const float smoothedPitchBaseValue = smoothedPitchBase.getNextValue();
        const float smoothedValueMultValue = smoothedValueMult.getNextValue();

        bool currentGateState = true; // Default to 'true' (always running)
        if (isGateConnected && gateIn)
        {
            // If a cable is connected, let it take control
            currentGateState = (gateIn[i] > smoothedTrigThreshValue);
        }
        bool triggerRising = triggerIn && (triggerIn[i] > smoothedTrigThreshValue) && !lastTriggerState;
        bool syncRising = syncIn && (syncIn[i] > 0.5f) && !lastSyncState;
        
        bool endOfCycle = false;

        if (baseMode == 0) // Free (Hz) mode
        {
            if (currentGateState) {
                phase += smoothedRateValue / sampleRate;
            }
            if (syncRising) {
                phase = 0.0;
            }
            if (phase >= 1.0) {
                if (baseLoop) {
                    phase = std::fmod(phase, 1.0);
                    endOfCycle = true;
                } else {
                    phase = 1.0;
                }
            }
        }
        else // Sync mode
        {
            if (triggerRising) {
                phase = 0.0;
                isRunning = true;
            }
            
            if (isRunning) {
                // In sync mode, rate still controls speed of the one-shot
                phase += smoothedRateValue / sampleRate;
                if (phase >= 1.0) {
                    isRunning = baseLoop; // If looping, stay running and wrap
                    phase = baseLoop ? std::fmod(phase, 1.0) : 1.0;
                    endOfCycle = true;
                }
            }
        }
        
        // Look up all three curves, plus the selected one for slewing
        float blueValue = interpolateCurve(0, static_cast<float>(phase));
        float redValue = interpolateCurve(1, static_cast<float>(phase));
        float greenValue = interpolateCurve(2, static_cast<float>(phase));

        targetValue = interpolateCurve(effectiveCurveSelect, static_cast<float>(phase));

        // Apply slew using a simplified, standard one-pole filter logic
        float slewCoeff = 1.0f - std::exp(-1.0f / (0.001f + smoothedSlewValue * smoothedSlewValue * (float)sampleRate));
        currentValue += (targetValue - currentValue) * slewCoeff;
        
        float outputs[13];
        generateOutputs(currentValue, blueValue, redValue, greenValue, endOfCycle, outputs, smoothedGateThreshValue, smoothedPitchBaseValue, smoothedValueMultValue);

        for (int ch = 0; ch < 13 && ch < outBus.getNumChannels(); ++ch) {
            outBus.getWritePointer(ch)[i] = outputs[ch];
        }

        lastTriggerState = triggerIn && (triggerIn[i] > smoothedTrigThreshValue);
        lastGateState = currentGateState;
        lastSyncState = syncIn && (syncIn[i] > 0.5f);

        if ((i & 63) == 0) {
            setLiveParamValue("rate_live", smoothedRate.getCurrentValue());
            setLiveParamValue("slew_live", smoothedSlew.getCurrentValue());
            setLiveParamValue("gateThresh_live", smoothedGateThresh.getCurrentValue());
            setLiveParamValue("trigThresh_live", smoothedTrigThresh.getCurrentValue());
            setLiveParamValue("pitchBase_live", smoothedPitchBase.getCurrentValue());
            setLiveParamValue("valueMult_live", smoothedValueMult.getCurrentValue());
            setLiveParamValue("curveSelect_live", static_cast<float>(effectiveCurveSelect));
        }
    }

    if (lastOutputValues.size() >= 13) {
        for (int ch = 0; ch < 13 && ch < outBus.getNumChannels(); ++ch) {
            if (lastOutputValues[ch]) {
                lastOutputValues[ch]->store(outBus.getSample(ch, buffer.getNumSamples() - 1));
            }
        }
    }
}

float FunctionGeneratorModuleProcessor::interpolateCurve(int curveIndex, float p)
{
    if (curveIndex < 0 || curveIndex >= 3) return 0.0f;
    const auto& curve = curves[curveIndex];
    float scaledPhase = p * (CURVE_RESOLUTION - 1);
    int index = static_cast<int>(scaledPhase);
    float fraction = scaledPhase - index;
    if (index >= CURVE_RESOLUTION - 1) return curve[CURVE_RESOLUTION - 1];
    float y1 = curve[index];
    float y2 = curve[index + 1];
    return y1 + fraction * (y2 - y1);
}

void FunctionGeneratorModuleProcessor::generateOutputs(float selectedValue, float blueValue, float redValue, float greenValue, bool eoc, float* outs, float gateThresh, float pitchBase, float valueMult)
{
    // --- Existing Outputs (based on selected curve) ---
    outs[0] = selectedValue; // Value
    outs[1] = 1.0f - selectedValue; // Inverted
    outs[2] = selectedValue * 2.0f - 1.0f; // Bipolar
    
    float pitchBaseOffset = pitchBase / 12.0f;
    outs[3] = pitchBaseOffset + selectedValue * valueMult; // Pitch
    
    bool gateHigh = selectedValue > gateThresh;
    outs[4] = gateHigh ? 1.0f : 0.0f; // Gate
    
    if (gateHigh && !lastGateOut) outs[5] = 1.0f; // Trigger
    else outs[5] = 0.0f;
    lastGateOut = gateHigh;
    
    if (eoc) { // End of Cycle
        eocPulseRemaining = static_cast<int>(sampleRate * 0.001); // 1ms pulse
    }
    outs[6] = (eocPulseRemaining > 0) ? 1.0f : 0.0f;
    if (eocPulseRemaining > 0) --eocPulseRemaining;

    // --- New Dedicated Curve Outputs ---
    outs[7] = blueValue;
    outs[8] = pitchBaseOffset + blueValue * valueMult;

    outs[9] = redValue;
    outs[10] = pitchBaseOffset + redValue * valueMult;

    outs[11] = greenValue;
    outs[12] = pitchBaseOffset + greenValue * valueMult;
}

#if defined(PRESET_CREATOR_UI)
void FunctionGeneratorModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    // Rate
    const bool rateIsMod = isParamModulated(paramIdRateMod);
    float rate = rateIsMod ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
    if (rateIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.1f, 100.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic)) {
        if (!rateIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
    }
    if (!rateIsMod) adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
    if (ImGui::IsItemDeactivatedAfterEdit() && !rateIsMod) onModificationEnded();
    if (rateIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Mode (No modulation for this parameter)
    int mode = (int)modeParam->load();
    if (ImGui::Combo("Mode", &mode, "Free (Hz)\0Sync\0\0")) {
        *dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdMode)) = mode;
        onModificationEnded();
    }
    
    // Loop (No modulation for this parameter)
    bool loop = loopParam->load() > 0.5f;
    if (ImGui::Checkbox("Loop", &loop)) {
        *dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramIdLoop)) = loop;
        onModificationEnded();
    }
    
    // Slew
    const bool slewIsMod = isParamModulated(paramIdSlewMod);
    float slew = slewIsMod ? getLiveParamValueFor(paramIdSlewMod, "slew_live", slewParam->load()) : slewParam->load();
    if (slewIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Slew", &slew, 0.0f, 1.0f, "%.3f")) {
        if (!slewIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSlew)) = slew;
    }
    if (!slewIsMod) adjustParamOnWheel(ap.getParameter(paramIdSlew), "slew", slew);
    if (ImGui::IsItemDeactivatedAfterEdit() && !slewIsMod) onModificationEnded();
    if (slewIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Gate Thresh
    const bool gateThreshIsMod = isParamModulated(paramIdGateThreshMod);
    float gateThresh = gateThreshIsMod ? getLiveParamValueFor(paramIdGateThreshMod, "gateThresh_live", gateThreshParam->load()) : gateThreshParam->load();
    if (gateThreshIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Gate Thr", &gateThresh, 0.0f, 1.0f, "%.2f")) {
        if (!gateThreshIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdGateThresh)) = gateThresh;
    }
    if (!gateThreshIsMod) adjustParamOnWheel(ap.getParameter(paramIdGateThresh), "gateThresh", gateThresh);
    if (ImGui::IsItemDeactivatedAfterEdit() && !gateThreshIsMod) onModificationEnded();
    if (gateThreshIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Trig Thresh
    const bool trigThreshIsMod = isParamModulated(paramIdTrigThreshMod);
    float trigThresh = trigThreshIsMod ? getLiveParamValueFor(paramIdTrigThreshMod, "trigThresh_live", trigThreshParam->load()) : trigThreshParam->load();
    if (trigThreshIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Trig Thr", &trigThresh, 0.0f, 1.0f, "%.2f")) {
        if (!trigThreshIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdTrigThresh)) = trigThresh;
    }
    if (!trigThreshIsMod) adjustParamOnWheel(ap.getParameter(paramIdTrigThresh), "trigThresh", trigThresh);
    if (ImGui::IsItemDeactivatedAfterEdit() && !trigThreshIsMod) onModificationEnded();
    if (trigThreshIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Pitch Base
    const bool pitchBaseIsMod = isParamModulated(paramIdPitchBaseMod);
    float pitchBase = pitchBaseIsMod ? getLiveParamValueFor(paramIdPitchBaseMod, "pitchBase_live", pitchBaseParam->load()) : pitchBaseParam->load();
    if (pitchBaseIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Pitch Base", &pitchBase, -24.0f, 24.0f, "%.1f st")) {
        if (!pitchBaseIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdPitchBase)) = pitchBase;
    }
    if (!pitchBaseIsMod) adjustParamOnWheel(ap.getParameter(paramIdPitchBase), "pitchBase", pitchBase);
    if (ImGui::IsItemDeactivatedAfterEdit() && !pitchBaseIsMod) onModificationEnded();
    if (pitchBaseIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Value Mult
    const bool valueMultIsMod = isParamModulated(paramIdValueMultMod);
    float valueMult = valueMultIsMod ? getLiveParamValueFor(paramIdValueMultMod, "valueMult_live", valueMultParam->load()) : valueMultParam->load();
    if (valueMultIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Value Mult", &valueMult, 0.0f, 10.0f, "%.2f")) {
        if (!valueMultIsMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdValueMult)) = valueMult;
    }
    if (!valueMultIsMod) adjustParamOnWheel(ap.getParameter(paramIdValueMult), "valueMult", valueMult);
    if (ImGui::IsItemDeactivatedAfterEdit() && !valueMultIsMod) onModificationEnded();
    if (valueMultIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // --- CURVE CANVAS LOGIC (Moved to the end to fix UI interaction) ---

    // Curve Selection
    int activeEditorCurve = static_cast<int>(curveSelectParam->load());
    if (isParamInputConnected(paramIdCurveSelectMod)) {
        activeEditorCurve = static_cast<int>(getLiveParamValueFor(paramIdCurveSelectMod, "curveSelect_live", (float)activeEditorCurve));
    }
    if (ImGui::Button("Blue")) { if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdCurveSelect))) *p = 0; onModificationEnded(); }
    ImGui::SameLine();
    if (ImGui::Button("Red")) { if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdCurveSelect))) *p = 1; onModificationEnded(); }
    ImGui::SameLine();
    if (ImGui::Button("Green")) { if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdCurveSelect))) *p = 2; onModificationEnded(); }
    
    // Canvas Setup
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(itemWidth, 150.0f);
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(30, 30, 30, 255));
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(150, 150, 150, 255));
    
    // Mouse interaction for drawing on the canvas
    ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft);
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isDragging = true;
        lastMousePosInCanvas = ImVec2(ImGui::GetIO().MousePos.x - canvas_p0.x, ImGui::GetIO().MousePos.y - canvas_p0.y);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (isDragging) onModificationEnded();
        isDragging = false;
        lastMousePosInCanvas = ImVec2(-1, -1);
    }
    if (isDragging && is_active) {
        ImVec2 current_pos = ImVec2(ImGui::GetIO().MousePos.x - canvas_p0.x, ImGui::GetIO().MousePos.y - canvas_p0.y);
        int idx0 = static_cast<int>((lastMousePosInCanvas.x / canvas_sz.x) * CURVE_RESOLUTION);
        int idx1 = static_cast<int>((current_pos.x / canvas_sz.x) * CURVE_RESOLUTION);
        idx0 = juce::jlimit(0, CURVE_RESOLUTION - 1, idx0);
        idx1 = juce::jlimit(0, CURVE_RESOLUTION - 1, idx1);
        if (idx0 > idx1) std::swap(idx0, idx1);
        for (int i = idx0; i <= idx1; ++i) {
            float t = (idx1 == idx0) ? 1.0f : (float)(i - idx0) / (float)(idx1 - idx0);
            float y_pos = juce::jmap(t, lastMousePosInCanvas.y, current_pos.y);
            curves[activeEditorCurve][i] = 1.0f - juce::jlimit(0.0f, 1.0f, y_pos / canvas_sz.y);
        }
        lastMousePosInCanvas = current_pos;
    }
    
    // Draw curves
    const ImU32 colors[] = { IM_COL32(100, 150, 255, 255), IM_COL32(255, 100, 100, 255), IM_COL32(100, 255, 150, 255) };
    for (int c = 0; c < 3; ++c) {
        ImU32 color = colors[c];
        if (c != activeEditorCurve) color = (color & 0x00FFFFFF) | (100 << 24);
        for (int i = 0; i < CURVE_RESOLUTION - 1; ++i) {
            ImVec2 p1 = ImVec2(canvas_p0.x + ((float)i / (CURVE_RESOLUTION - 1)) * canvas_sz.x, canvas_p0.y + (1.0f - curves[c][i]) * canvas_sz.y);
            ImVec2 p2 = ImVec2(canvas_p0.x + ((float)(i + 1) / (CURVE_RESOLUTION - 1)) * canvas_sz.x, canvas_p0.y + (1.0f - curves[c][i+1]) * canvas_sz.y);
            draw_list->AddLine(p1, p2, color, 2.0f);
        }
    }
    
    // Draw Gate Threshold line (Yellow)
    const float gate_line_y = canvas_p0.y + (1.0f - gateThresh) * canvas_sz.y;
    draw_list->AddLine(ImVec2(canvas_p0.x, gate_line_y), ImVec2(canvas_p1.x, gate_line_y), IM_COL32(255, 255, 0, 200), 2.0f);

    // Draw Trigger Threshold line (Red)
    const float trig_line_y = canvas_p0.y + (1.0f - trigThresh) * canvas_sz.y;
    draw_list->AddLine(ImVec2(canvas_p0.x, trig_line_y), ImVec2(canvas_p1.x, trig_line_y), IM_COL32(255, 0, 0, 200), 2.0f);
    
    // Draw playhead
    float playhead_x = canvas_p0.x + phase * canvas_sz.x;
    draw_list->AddLine(ImVec2(playhead_x, canvas_p0.y), ImVec2(playhead_x, canvas_p1.y), IM_COL32(255, 255, 0, 200));

    ImGui::Dummy(canvas_sz);
    
    ImGui::PopItemWidth(); // <<< FIX: Added to match the PushItemWidth at the top.
}

void FunctionGeneratorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Input pins
    helpers.drawAudioInputPin("Gate In", 0);
    helpers.drawAudioInputPin("Trigger In", 1);
    helpers.drawAudioInputPin("Sync In", 2);
    helpers.drawAudioInputPin("Rate Mod", 3);
    helpers.drawAudioInputPin("Slew Mod", 4);
    helpers.drawAudioInputPin("Gate Thresh Mod", 5);
    helpers.drawAudioInputPin("Trig Thresh Mod", 6);
    helpers.drawAudioInputPin("Pitch Base Mod", 7);
    helpers.drawAudioInputPin("Value Mult Mod", 8);
    helpers.drawAudioInputPin("Curve Select Mod", 9);

    // Selected Outputs
    helpers.drawAudioOutputPin("Value", 0);
    helpers.drawAudioOutputPin("Inverted", 1);
    helpers.drawAudioOutputPin("Bipolar", 2);
    helpers.drawAudioOutputPin("Pitch", 3);
    helpers.drawAudioOutputPin("Gate", 4);
    helpers.drawAudioOutputPin("Trigger", 5);
    helpers.drawAudioOutputPin("End of Cycle", 6);
    // Dedicated Outputs
    helpers.drawAudioOutputPin("Blue Value", 7);
    helpers.drawAudioOutputPin("Blue Pitch", 8);
    helpers.drawAudioOutputPin("Red Value", 9);
    helpers.drawAudioOutputPin("Red Pitch", 10);
    helpers.drawAudioOutputPin("Green Value", 11);
    helpers.drawAudioOutputPin("Green Pitch", 12);
}

juce::String FunctionGeneratorModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Gate In";
        case 1: return "Trigger In";
        case 2: return "Sync In";
        case 3: return "Rate Mod";
        case 4: return "Slew Mod";
        case 5: return "Gate Thresh Mod";
        case 6: return "Trig Thresh Mod";
        case 7: return "Pitch Base Mod";
        case 8: return "Value Mult Mod";
        case 9: return "Curve Select Mod";
        default: return {};
    }
}

juce::String FunctionGeneratorModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Value";
        case 1: return "Inverted";
        case 2: return "Bipolar";
        case 3: return "Pitch";
        case 4: return "Gate";
        case 5: return "Trigger";
        case 6: return "End of Cycle";
        case 7: return "Blue Value";
        case 8: return "Blue Pitch";
        case 9: return "Red Value";
        case 10: return "Red Pitch";
        case 11: return "Green Value";
        case 12: return "Green Pitch";
        default: return {};
    }
}

#endif

bool FunctionGeneratorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == paramIdGateIn) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdRateMod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdSlewMod) { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdGateThreshMod) { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdTrigThreshMod) { outChannelIndexInBus = 6; return true; }
    if (paramId == paramIdPitchBaseMod) { outChannelIndexInBus = 7; return true; }
    if (paramId == paramIdValueMultMod) { outChannelIndexInBus = 8; return true; }
    if (paramId == paramIdCurveSelectMod) { outChannelIndexInBus = 9; return true; }
    
    return false;
}

// --- State management functions for saving/loading ---
juce::ValueTree FunctionGeneratorModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("FunctionGeneratorState");
    for (int c = 0; c < curves.size(); ++c)
    {
        juce::ValueTree points("CurvePoints_" + juce::String(c));
        for (int i = 0; i < curves[c].size(); ++i)
        {
            points.setProperty("p" + juce::String(i), curves[c][i], nullptr);
        }
        vt.addChild(points, -1, nullptr);
    }
    return vt;
}

void FunctionGeneratorModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("FunctionGeneratorState"))
    {
        for (int c = 0; c < curves.size(); ++c)
        {
            auto points = vt.getChildWithName("CurvePoints_" + juce::String(c));
            if (points.isValid())
            {
                curves[c].resize(CURVE_RESOLUTION);
                for (int i = 0; i < CURVE_RESOLUTION; ++i)
                {
                    curves[c][i] = (float)points.getProperty("p" + juce::String(i), 0.0);
                }
            }
        }
    }
}