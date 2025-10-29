#include "LimiterModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout LimiterModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdThreshold, "Threshold", -20.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRelease, "Release", 1.0f, 200.0f, 10.0f));
    
    // Relative modulation parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeThresholdMod", "Relative Threshold Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeReleaseMod", "Relative Release Mod", true));
    
    return { params.begin(), params.end() };
}

LimiterModuleProcessor::LimiterModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(4), true) // 0-1: Audio In, 2: Threshold Mod, 3: Release Mod
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "LimiterParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    releaseParam = apvts.getRawParameterValue(paramIdRelease);
    relativeThresholdModParam = apvts.getRawParameterValue("relativeThresholdMod");
    relativeReleaseModParam = apvts.getRawParameterValue("relativeReleaseMod");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void LimiterModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 2;

    limiter.prepare(spec);
    limiter.reset();
}

void LimiterModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    // Copy input to output for in-place processing
    const int numInputChannels = inBus.getNumChannels();
    const int numOutputChannels = outBus.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numInputChannels > 0)
    {
        // If input is mono, copy it to both left and right outputs.
        if (numInputChannels == 1 && numOutputChannels > 1)
        {
            outBus.copyFrom(0, 0, inBus, 0, 0, numSamples);
            outBus.copyFrom(1, 0, inBus, 0, 0, numSamples);
        }
        // Otherwise, perform a standard stereo copy.
        else
        {
            const int channelsToCopy = juce::jmin(numInputChannels, numOutputChannels);
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                outBus.copyFrom(ch, 0, inBus, ch, 0, numSamples);
            }
        }
    }
    else
    {
        // If no input is connected, ensure the output is silent.
        outBus.clear();
    }

    // --- Get base parameter values and relative modes ---
    const float baseThreshold = thresholdParam->load();
    const float baseRelease = releaseParam->load();
    const bool relativeThresholdMode = relativeThresholdModParam && relativeThresholdModParam->load() > 0.5f;
    const bool relativeReleaseMode = relativeReleaseModParam && relativeReleaseModParam->load() > 0.5f;
    
    // --- Update DSP Parameters from unified input bus (once per block) ---
    float finalThreshold = baseThreshold;
    if (isParamInputConnected(paramIdThresholdMod) && inBus.getNumChannels() > 2) {
        const float cv = juce::jlimit(0.0f, 1.0f, inBus.getSample(2, 0));
        if (relativeThresholdMode) {
            // RELATIVE: ±10dB offset
            const float offset = (cv - 0.5f) * 20.0f;
            finalThreshold = baseThreshold + offset;
        } else {
            // ABSOLUTE: CV directly sets threshold
            finalThreshold = juce::jmap(cv, -20.0f, 0.0f);
        }
        finalThreshold = juce::jlimit(-20.0f, 0.0f, finalThreshold);
    }
        
    float finalRelease = baseRelease;
    if (isParamInputConnected(paramIdReleaseMod) && inBus.getNumChannels() > 3) {
        const float cv = juce::jlimit(0.0f, 1.0f, inBus.getSample(3, 0));
        if (relativeReleaseMode) {
            // RELATIVE: 0.25x to 4x time scaling
            const float octaveOffset = (cv - 0.5f) * 4.0f;
            finalRelease = baseRelease * std::pow(2.0f, octaveOffset);
        } else {
            // ABSOLUTE: CV directly sets release
            finalRelease = juce::jmap(cv, 1.0f, 200.0f);
        }
        finalRelease = juce::jlimit(1.0f, 200.0f, finalRelease);
    }

    limiter.setThreshold(finalThreshold);
    limiter.setRelease(finalRelease);
    
    // --- Process the Audio ---
    juce::dsp::AudioBlock<float> block(outBus);
    juce::dsp::ProcessContextReplacing<float> context(block);
    limiter.process(context);

    // --- Update UI Telemetry & Tooltips ---
    setLiveParamValue("threshold_live", finalThreshold);
    setLiveParamValue("release_live", finalRelease);
    
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool LimiterModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == paramIdThresholdMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdReleaseMod)   { outChannelIndexInBus = 3; return true; }
    return false;
}

juce::String LimiterModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    if (channel == 2) return "Thresh Mod";
    if (channel == 3) return "Release Mod";
    return {};
}

juce::String LimiterModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void LimiterModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };

    auto drawSlider = [&](const char* label, const juce::String& paramId, const juce::String& modId, float min, float max, const char* format, const char* tooltip) {
        bool isMod = isParamModulated(modId);
        float value = isMod ? getLiveParamValueFor(modId, paramId + "_live", ap.getRawParameterValue(paramId)->load())
                            : ap.getRawParameterValue(paramId)->load();
        
        if (isMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
        if (tooltip) { ImGui::SameLine(); HelpMarker(tooltip); }
    };

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Limiter Parameters");
    ImGui::Spacing();

    drawSlider("Threshold", paramIdThreshold, paramIdThresholdMod, -20.0f, 0.0f, "%.1f dB", "Maximum output level (-20 to 0 dB)\nSignal peaks above this are limited");
    drawSlider("Release", paramIdRelease, paramIdReleaseMod, 1.0f, 200.0f, "%.0f ms", "Release time (1-200 ms)\nHow fast the limiter recovers");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === RELATIVE MODULATION SECTION ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CV Input Modes");
    ImGui::Spacing();
    
    // Relative Threshold Mod checkbox
    bool relativeThresholdMod = relativeThresholdModParam != nullptr && relativeThresholdModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Threshold Mod", &relativeThresholdMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeThresholdMod")))
            *p = relativeThresholdMod;
        juce::Logger::writeToLog("[Limiter UI] Relative Threshold Mod: " + juce::String(relativeThresholdMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±10dB)\nOFF: CV directly sets threshold (-20dB to 0dB)");
    }
    
    // Relative Release Mod checkbox
    bool relativeReleaseMod = relativeReleaseModParam != nullptr && relativeReleaseModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Release Mod", &relativeReleaseMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeReleaseMod")))
            *p = relativeReleaseMod;
        juce::Logger::writeToLog("[Limiter UI] Relative Release Mod: " + juce::String(relativeReleaseMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (0.25x to 4x)\nOFF: CV directly sets release (1-200ms)");
    }

    ImGui::PopItemWidth();
}

void LimiterModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Thresh Mod", 2);
    helpers.drawAudioInputPin("Release Mod", 3);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

std::vector<DynamicPinInfo> LimiterModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (channels 2-3)
    pins.push_back({"Thresh Mod", 2, PinDataType::CV});
    pins.push_back({"Release Mod", 3, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> LimiterModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}

