#include "ChorusModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ChorusModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate", 0.05f, 5.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDepth, "Depth", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMix, "Mix", 0.0f, 1.0f, 0.5f));
    
    // Relative modulation parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeRateMod", "Relative Rate Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeDepthMod", "Relative Depth Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeMixMod", "Relative Mix Mod", true));
    
    return { params.begin(), params.end() };
}

ChorusModuleProcessor::ChorusModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // 0-1: Audio In, 2: Rate Mod, 3: Depth Mod, 4: Mix Mod
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChorusParams", createParameterLayout())
{
    rateParam = apvts.getRawParameterValue(paramIdRate);
    depthParam = apvts.getRawParameterValue(paramIdDepth);
    mixParam = apvts.getRawParameterValue(paramIdMix);
    relativeRateModParam = apvts.getRawParameterValue("relativeRateMod");
    relativeDepthModParam = apvts.getRawParameterValue("relativeDepthMod");
    relativeMixModParam = apvts.getRawParameterValue("relativeMixMod");

    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void ChorusModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 2; // Process in stereo

    chorus.prepare(spec);
    chorus.reset();
}

void ChorusModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Get handles to the input and output buses
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    // The Chorus DSP object works in-place, so we first copy the dry input to the output.
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

    // --- Get Modulation CVs from unified input bus ---
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isDepthMod = isParamInputConnected(paramIdDepthMod);
    const bool isMixMod = isParamInputConnected(paramIdMixMod);

    const float* rateCV = isRateMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* depthCV = isDepthMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* mixCV = isMixMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    // --- Get Base Parameter Values ---
    const float baseRate = rateParam->load();
    const float baseDepth = depthParam->load();
    const float baseMix = mixParam->load();
    const bool relativeRateMode = relativeRateModParam && relativeRateModParam->load() > 0.5f;
    const bool relativeDepthMode = relativeDepthModParam && relativeDepthModParam->load() > 0.5f;
    const bool relativeMixMode = relativeMixModParam && relativeMixModParam->load() > 0.5f;

    // Apply CV modulation with relative/absolute modes
    float finalRate = baseRate;
    if (isRateMod && rateCV) {
        const float cv = juce::jlimit(0.0f, 1.0f, rateCV[0]);
        if (relativeRateMode) {
            // RELATIVE: Modulate ±2 octaves (0.25x to 4x)
            const float octaveOffset = (cv - 0.5f) * 4.0f; // ±2 octaves
            finalRate = baseRate * std::pow(2.0f, octaveOffset);
        } else {
            // ABSOLUTE: CV directly sets rate (0.05-5 Hz)
            finalRate = juce::jmap(cv, 0.05f, 5.0f);
        }
    }

    float finalDepth = baseDepth;
    if (isDepthMod && depthCV) {
        const float cv = juce::jlimit(0.0f, 1.0f, depthCV[0]);
        if (relativeDepthMode) {
            // RELATIVE: CV adds offset to base depth (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            finalDepth = baseDepth + offset;
        } else {
            // ABSOLUTE: CV directly sets depth
            finalDepth = cv;
        }
        finalDepth = juce::jlimit(0.0f, 1.0f, finalDepth);
    }

    float finalMix = baseMix;
    if (isMixMod && mixCV) {
        const float cv = juce::jlimit(0.0f, 1.0f, mixCV[0]);
        if (relativeMixMode) {
            // RELATIVE: CV adds offset to base mix (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            finalMix = baseMix + offset;
        } else {
            // ABSOLUTE: CV directly sets mix
            finalMix = cv;
        }
        finalMix = juce::jlimit(0.0f, 1.0f, finalMix);
    }
    
    // --- Update the DSP Object ---
    chorus.setRate(juce::jlimit(0.05f, 5.0f, finalRate));
    chorus.setDepth(juce::jlimit(0.0f, 1.0f, finalDepth));
    chorus.setMix(juce::jlimit(0.0f, 1.0f, finalMix));

    // --- Process the Audio ---
    // The context works on the output buffer, which now contains the dry signal.
    juce::dsp::AudioBlock<float> block(outBus);
    juce::dsp::ProcessContextReplacing<float> context(block);
    chorus.process(context);

    // --- Update UI Telemetry ---
    setLiveParamValue("rate_live", finalRate);
    setLiveParamValue("depth_live", finalDepth);
    setLiveParamValue("mix_live", finalMix);
    
    // --- Update Tooltips ---
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool ChorusModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus

    if (paramId == paramIdRateMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdDepthMod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdMixMod) { outChannelIndexInBus = 4; return true; }
    return false;
}

// Dynamic pin interface - preserves exact same modulation system
std::vector<DynamicPinInfo> ChorusModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;

    // Audio inputs (always present)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});

    // Modulation inputs - exactly matching current static layout
    pins.push_back({"Rate Mod", 2, PinDataType::CV});
    pins.push_back({"Depth Mod", 3, PinDataType::CV});
    pins.push_back({"Mix Mod", 4, PinDataType::CV});

    return pins;
}

std::vector<DynamicPinInfo> ChorusModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;

    // Audio outputs - exactly matching current static layout
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});

    return pins;
}

juce::String ChorusModuleProcessor::getAudioInputLabel(int channel) const
{
    // Bus 0 (Stereo Audio In)
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    // Bus 1-3 (Mono Mod In) - We need to calculate the absolute channel index
    if (channel == 2) return "Rate Mod";
    if (channel == 3) return "Depth Mod";
    if (channel == 4) return "Mix Mod";
    return {};
}

juce::String ChorusModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void ChorusModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    // Helper for tooltips
    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // === CHORUS PARAMETERS SECTION ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Chorus Parameters");
    ImGui::Spacing();

    // Rate Slider
    bool isRateMod = isParamModulated(paramIdRateMod);
    float rate = isRateMod ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
    if (isRateMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Rate", &rate, 0.05f, 5.0f, "%.2f Hz"))
        if (!isRateMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdRate)) = rate;
    if (!isRateMod) adjustParamOnWheel(ap.getParameter(paramIdRate), "rate", rate);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isRateMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("LFO modulation rate (0.05-5 Hz)\nControls how fast the chorus effect sweeps");

    // Depth Slider
    bool isDepthMod = isParamModulated(paramIdDepthMod);
    float depth = isDepthMod ? getLiveParamValueFor(paramIdDepthMod, "depth_live", depthParam->load()) : depthParam->load();
    if (isDepthMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Depth", &depth, 0.0f, 1.0f, "%.2f"))
        if (!isDepthMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDepth)) = depth;
    if (!isDepthMod) adjustParamOnWheel(ap.getParameter(paramIdDepth), "depth", depth);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isDepthMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Modulation depth (0-1)\nControls intensity of pitch/time variation");

    // Mix Slider
    bool isMixMod = isParamModulated(paramIdMixMod);
    float mix = isMixMod ? getLiveParamValueFor(paramIdMixMod, "mix_live", mixParam->load()) : mixParam->load();
    if (isMixMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f, "%.2f"))
        if (!isMixMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMix)) = mix;
    if (!isMixMod) adjustParamOnWheel(ap.getParameter(paramIdMix), "mix", mix);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isMixMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Dry/wet mix (0-1)\n0 = dry only, 1 = fully chorused");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === RELATIVE MODULATION SECTION ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CV Input Modes");
    ImGui::Spacing();
    
    // Relative Rate Mod checkbox
    bool relativeRateMod = relativeRateModParam != nullptr && relativeRateModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Rate Mod", &relativeRateMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeRateMod")))
            *p = relativeRateMod;
        juce::Logger::writeToLog("[Chorus UI] Relative Rate Mod: " + juce::String(relativeRateMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±2 octaves)\nOFF: CV directly sets rate (0.05-5 Hz)");
    }
    
    // Relative Depth Mod checkbox
    bool relativeDepthMod = relativeDepthModParam != nullptr && relativeDepthModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Depth Mod", &relativeDepthMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDepthMod")))
            *p = relativeDepthMod;
        juce::Logger::writeToLog("[Chorus UI] Relative Depth Mod: " + juce::String(relativeDepthMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±0.5)\nOFF: CV directly sets depth (0-1)");
    }
    
    // Relative Mix Mod checkbox
    bool relativeMixMod = relativeMixModParam != nullptr && relativeMixModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Mix Mod", &relativeMixMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeMixMod")))
            *p = relativeMixMod;
        juce::Logger::writeToLog("[Chorus UI] Relative Mix Mod: " + juce::String(relativeMixMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±0.5)\nOFF: CV directly sets mix (0-1)");
    }

    ImGui::PopItemWidth();
}

void ChorusModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Use dynamic pins - this preserves the exact same modulation system
    // The channel indices match getParamRouting() exactly
    auto dynamicInputs = getDynamicInputPins();
    auto dynamicOutputs = getDynamicOutputPins();

    // Draw inputs using dynamic pin system
    for (const auto& pin : dynamicInputs)
    {
        helpers.drawAudioInputPin(pin.name.toRawUTF8(), pin.channel);
    }

    // Draw outputs using dynamic pin system
    for (const auto& pin : dynamicOutputs)
    {
        helpers.drawAudioOutputPin(pin.name.toRawUTF8(), pin.channel);
    }
}
#endif

