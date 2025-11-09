#include "MixerModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

// Corrected constructor with two separate stereo inputs
MixerModuleProcessor::MixerModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("In A", juce::AudioChannelSet::stereo(), true)  // Bus 0
        .withInput ("In B", juce::AudioChannelSet::stereo(), true)  // Bus 1
        .withInput ("Gain Mod", juce::AudioChannelSet::mono(), true)
        .withInput ("Pan Mod", juce::AudioChannelSet::mono(), true)
        .withInput ("X-Fade Mod", juce::AudioChannelSet::mono(), true)
        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MixerParams", createParameterLayout())
{
    gainParam      = apvts.getRawParameterValue ("gain");
    panParam       = apvts.getRawParameterValue ("pan");
    crossfadeParam = apvts.getRawParameterValue ("crossfade"); // Get the new parameter

    // Initialize value tooltips for the stereo output
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

// Updated parameter layout with the new crossfade slider
juce::AudioProcessorValueTreeState::ParameterLayout MixerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("gain", "Gain", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("pan",  "Pan",  juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("crossfade",  "Crossfade",  juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f)); // A <-> B
    return { p.begin(), p.end() };
}

void MixerModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

// Completely rewritten processBlock for crossfading
void MixerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    auto inA = getBusBuffer(buffer, true, 0);
    auto inB = getBusBuffer(buffer, true, 1);
    auto out = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = out.getNumChannels();

    // Read CV from input buses (if connected)
    float gainModCV = 0.0f;
    float panModCV = 0.0f;
    float crossfadeModCV = 0.0f;
    
    // Check if gain mod bus is connected and read CV
    if (isParamInputConnected("gain"))
    {
        const auto& gainModBus = getBusBuffer(buffer, true, 2);
        if (gainModBus.getNumChannels() > 0)
            gainModCV = gainModBus.getReadPointer(0)[0]; // Read first sample
    }
    
    // Check if pan mod bus is connected and read CV
    if (isParamInputConnected("pan"))
    {
        const auto& panModBus = getBusBuffer(buffer, true, 3);
        if (panModBus.getNumChannels() > 0)
            panModCV = panModBus.getReadPointer(0)[0]; // Read first sample
    }
    
    // Check if crossfade mod bus is connected and read CV
    if (isParamInputConnected("x-fade"))
    {
        const auto& crossfadeModBus = getBusBuffer(buffer, true, 4);
        if (crossfadeModBus.getNumChannels() > 0)
            crossfadeModCV = crossfadeModBus.getReadPointer(0)[0]; // Read first sample
    }

    // Apply modulation or use parameter values
    float crossfade = 0.0f;
    if (isParamInputConnected("x-fade"))
    {
        // Map CV [0,1] to crossfade [-1, 1]
        crossfade = -1.0f + crossfadeModCV * (1.0f - (-1.0f));
    }
    else
    {
        crossfade = crossfadeParam != nullptr ? crossfadeParam->load() : 0.0f;
    }

    // Use a constant power law for a smooth crossfade without volume dips
    const float mixAngle = (crossfade * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
    const float gainA = std::cos(mixAngle);
    const float gainB = std::sin(mixAngle);

    // Perform the crossfade into the output buffer
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* srcA = (ch < inA.getNumChannels()) ? inA.getReadPointer(ch) : nullptr;
        const float* srcB = (ch < inB.getNumChannels()) ? inB.getReadPointer(ch) : nullptr;
        float* dst = out.getWritePointer(ch);
        
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = srcA ? srcA[i] : 0.0f;
            const float b = srcB ? srcB[i] : 0.0f;
            dst[i] = a * gainA + b * gainB;
        }
    }

    // Now, apply the master gain and pan to the mixed signal in the output buffer
    float masterGain = 0.0f;
    if (isParamInputConnected("gain"))
    {
        // Map CV [0,1] to gain [-60, 6] dB
        float gainDb = -60.0f + gainModCV * (6.0f - (-60.0f));
        masterGain = juce::Decibels::decibelsToGain(gainDb);
    }
    else
    {
        masterGain = juce::Decibels::decibelsToGain(gainParam != nullptr ? gainParam->load() : 0.0f);
    }
    
    float pan = 0.0f;
    if (isParamInputConnected("pan"))
    {
        // Map CV [0,1] to pan [-1, 1]
        pan = -1.0f + panModCV * (1.0f - (-1.0f));
    }
    else
    {
        pan = panParam != nullptr ? panParam->load() : 0.0f;
    }
    const float panAngleMaster = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
    const float lGain = masterGain * std::cos(panAngleMaster);
    const float rGain = masterGain * std::sin(panAngleMaster);

    out.applyGain(0, 0, numSamples, lGain);
    if (numChannels > 1)
        out.applyGain(1, 0, numSamples, rGain);

    // Store live modulated values for UI display
    setLiveParamValue("crossfade_live", crossfade);
    setLiveParamValue("gain_live", isParamInputConnected("gain") ? (-60.0f + gainModCV * 66.0f) : (gainParam != nullptr ? gainParam->load() : 0.0f));
    setLiveParamValue("pan_live", pan);

    // Update tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getSample(0, numSamples - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(out.getSample(1, numSamples - 1));
    }
}

// Updated UI drawing code
#if defined(PRESET_CREATOR_UI)
void MixerModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    // Helper for tooltips
    auto HelpMarkerMixer = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    
    float gainDb = gainParam != nullptr ? gainParam->load() : 0.0f;
    float pan = panParam != nullptr ? panParam->load() : 0.0f;
    float crossfade = crossfadeParam != nullptr ? crossfadeParam->load() : 0.0f;

    ImGui::PushItemWidth(itemWidth);

    // === CROSSFADE SECTION ===
    ThemeText("Crossfade", theme.text.section_header);
    ImGui::Spacing();

    // Crossfade Slider
    bool isXfModulated = isParamModulated("x-fade");
    if (isXfModulated) {
        crossfade = getLiveParamValueFor("x-fade", "crossfade_live", crossfade);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("A <-> B", &crossfade, -1.0f, 1.0f)) if (!isXfModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("crossfade"))) *p = crossfade;
    if (!isXfModulated) adjustParamOnWheel(ap.getParameter("crossfade"), "crossfade", crossfade);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isXfModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ThemeText("(mod)", theme.text.active); }
    ImGui::SameLine();
    HelpMarkerMixer("Crossfade between inputs A and B\n-1 = A only, 0 = equal mix, +1 = B only");

    // Visual crossfade indicator
    float aLevel = (1.0f - crossfade) / 2.0f;
    float bLevel = (1.0f + crossfade) / 2.0f;
    ImGui::Text("A: %.1f%%", aLevel * 100.0f);
    ImGui::SameLine(itemWidth * 0.5f);
    ImGui::Text("B: %.1f%%", bLevel * 100.0f);

    ImGui::Spacing();
    ImGui::Spacing();

    // === MASTER CONTROLS SECTION ===
    ThemeText("Master Controls", theme.text.section_header);
    ImGui::Spacing();

    // Gain Slider
    bool isGainModulated = isParamModulated("gain");
    if (isGainModulated) {
        gainDb = getLiveParamValueFor("gain", "gain_live", gainDb);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Gain dB", &gainDb, -60.0f, 6.0f)) if (!isGainModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gain"))) *p = gainDb;
    if (!isGainModulated) adjustParamOnWheel(ap.getParameter("gain"), "gainDb", gainDb);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ThemeText("(mod)", theme.text.active); }
    ImGui::SameLine();
    HelpMarkerMixer("Master output gain (-60 to +6 dB)");

    // Pan Slider
    bool isPanModulated = isParamModulated("pan");
    if (isPanModulated) {
        pan = getLiveParamValueFor("pan", "pan_live", pan);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Pan", &pan, -1.0f, 1.0f)) if (!isPanModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("pan"))) *p = pan;
    if (!isPanModulated) adjustParamOnWheel(ap.getParameter("pan"), "pan", pan);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isPanModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ThemeText("(mod)", theme.text.active); }
    ImGui::SameLine();
    HelpMarkerMixer("Stereo panning\n-1 = full left, 0 = center, +1 = full right");

    // Visual pan indicator
    const char* panLabel = (pan < -0.3f) ? "L" : (pan > 0.3f) ? "R" : "C";
    ImGui::Text("Position: %s (%.2f)", panLabel, pan);

    ImGui::PopItemWidth();
}
#endif

void MixerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In A L", 0);
    helpers.drawAudioInputPin("In A R", 1);
    helpers.drawAudioInputPin("In B L", 2);
    helpers.drawAudioInputPin("In B R", 3);

    int busIdx, chanInBus;
    if (getParamRouting("gain", busIdx, chanInBus))
        helpers.drawAudioInputPin("Gain Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("pan", busIdx, chanInBus))
        helpers.drawAudioInputPin("Pan Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("crossfade", busIdx, chanInBus))
        helpers.drawAudioInputPin("X-Fade Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));

    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}

bool MixerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outChannelIndexInBus = 0;
    if (paramId == "gain")      { outBusIndex = 2; return true; }
    if (paramId == "pan")       { outBusIndex = 3; return true; }
    if (paramId == "crossfade" || paramId == "x-fade") { outBusIndex = 4; return true; }
    return false;
}