#include "RateModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

RateModuleProcessor::RateModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Rate Mod", juce::AudioChannelSet::mono(), true)
                        .withOutput("Out", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "RateParams", createParameterLayout())
{
    baseRateParam = apvts.getRawParameterValue("baseRate");
    multiplierParam = apvts.getRawParameterValue("multiplier");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out
}

juce::AudioProcessorValueTreeState::ParameterLayout RateModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("baseRate", "Base Rate", juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("multiplier", "Multiplier", juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));
    return { params.begin(), params.end() };
}

void RateModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void RateModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    
    const float* src = in.getNumChannels() > 0 ? in.getReadPointer(0) : nullptr;
    float* dst = out.getWritePointer(0);
    
    const float baseRate = baseRateParam->load();
    const float multiplier = multiplierParam->load();
    
    float sumOutput = 0.0f;
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float modulation = 0.0f;
        if (src != nullptr)
        {
            // Convert audio input (-1 to 1) to modulation range (-0.5 to +0.5)
            modulation = src[i] * 0.5f;
        }
        
        // Calculate final rate: baseRate * multiplier * (1 + modulation)
        float finalRate = baseRate * multiplier * (1.0f + modulation);
        finalRate = juce::jlimit(0.01f, 50.0f, finalRate); // Clamp to reasonable range
        
        // Update telemetry for live UI feedback (throttled to every 64 samples)
        if ((i & 0x3F) == 0) {
            setLiveParamValue("baseRate_live", baseRate);
            setLiveParamValue("multiplier_live", multiplier);
        }
        
        // Normalize the rate to 0.0..1.0 for modulation routing
        // Map 0.01..50.0 Hz -> 0.0..1.0
        const float normalizedRate = juce::jlimit (0.0f, 1.0f, (finalRate - 0.01f) / (50.0f - 0.01f));
        dst[i] = normalizedRate;
        sumOutput += finalRate;
    }
    
    lastOutputValue.store(sumOutput / (float) buffer.getNumSamples());
    
    // Update output values for tooltips
    if (!lastOutputValues.empty() && lastOutputValues[0])
    {
        lastOutputValues[0]->store(out.getSample(0, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void RateModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    float baseRate = baseRateParam->load();
    float multiplier = multiplierParam->load();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    ImGui::PushItemWidth(itemWidth);
    
    // === SECTION: Rate Control ===
    ThemeText("RATE CONTROL", theme.text.section_header);
    
    bool isBaseRateModulated = isParamModulated("baseRate");
    if (isBaseRateModulated) {
        baseRate = getLiveParamValueFor("baseRate", "baseRate_live", baseRate);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Base Rate", &baseRate, 0.1f, 20.0f, "%.2f Hz")) {
        if (!isBaseRateModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("baseRate"))) *p = baseRate;
        }
    }
    if (!isBaseRateModulated) adjustParamOnWheel(ap.getParameter("baseRate"), "baseRate", baseRate);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isBaseRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ThemeText("(mod)", theme.text.active); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base frequency in Hz");

    bool isMultiplierModulated = isParamModulated("multiplier");
    if (isMultiplierModulated) {
        multiplier = getLiveParamValueFor("multiplier", "multiplier_live", multiplier);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Multiplier", &multiplier, 0.1f, 10.0f, "%.2fx")) {
        if (!isMultiplierModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("multiplier"))) *p = multiplier;
        }
    }
    if (!isMultiplierModulated) adjustParamOnWheel(ap.getParameter("multiplier"), "multiplier", multiplier);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isMultiplierModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ThemeText("(mod)", theme.text.active); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rate multiplier");

    ImGui::Spacing();
    ImGui::Spacing();
    
    // === SECTION: Output ===
    ThemeText("OUTPUT", theme.text.section_header);
    
    float outputHz = getLastOutputValue();
    ImGui::Text("Frequency: %.2f Hz", outputHz);
    
    ImGui::PopItemWidth();
}
#endif

void RateModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Mod In", 0);
    helpers.drawAudioOutputPin("Out", 0);
}

float RateModuleProcessor::getLastOutputValue() const
{
    return lastOutputValue.load();
}

// Parameter bus contract implementation
bool RateModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "baseRate") { outBusIndex = 1; outChannelIndexInBus = 0; return true; }
    return false;
}
