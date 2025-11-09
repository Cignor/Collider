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

void ClockDividerModuleProcessor::prepareToPlay(double sr, int)
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
    }
    
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
    ImGui::Spacing();
    
    // === SECTION: Clock Monitor ===
    ThemeText("CLOCK MONITOR", theme.text.section_header);
    
    double bpm = (currentClockInterval > 0.0) ? (60.0 * sampleRate / currentClockInterval) : 0.0;
    ImGui::Text("Clock Rate: %.1f BPM", bpm);
    
    ImGui::PopItemWidth();
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
