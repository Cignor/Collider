#include "WaveshaperModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif
#include <cmath> // For std::tanh

juce::AudioProcessorValueTreeState::ParameterLayout WaveshaperModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Drive", 
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.01f, 0.3f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("type", "Type",
        juce::StringArray{ "Soft Clip (tanh)", "Hard Clip", "Foldback" }, 0));
    
    // Relative modulation parameters
    p.push_back(std::make_unique<juce::AudioParameterBool>("relativeDriveMod", "Relative Drive Mod", true));
    
    return { p.begin(), p.end() };
}

WaveshaperModuleProcessor::WaveshaperModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(4), true) // 0-1: Audio In, 2: Drive Mod, 3: Type Mod
                        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "WaveshaperParams", createParameterLayout())
{
    driveParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("drive"));
    typeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("type"));
    relativeDriveModParam = apvts.getRawParameterValue("relativeDriveMod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

void WaveshaperModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Get pointers to modulation CV inputs from unified input bus
    const bool isDriveMod = isParamInputConnected("drive");
    const bool isTypeMod = isParamInputConnected("type");
    auto inBus = getBusBuffer(buffer, true, 0);
    const float* driveCV = isDriveMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* typeCV = isTypeMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;

    // Get base parameter values ONCE
    const float baseDrive = driveParam != nullptr ? driveParam->get() : 1.0f;
    const int baseType = typeParam != nullptr ? typeParam->getIndex() : 0;
    const bool relativeDriveMode = relativeDriveModParam && relativeDriveModParam->load() > 0.5f;
    
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // PER-SAMPLE FIX: Calculate effective drive FOR THIS SAMPLE
            float drive = baseDrive;
            if (isDriveMod && driveCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, driveCV[i]);
                if (relativeDriveMode) {
                    // RELATIVE: ±3 octaves (0.125x to 8x)
                    const float octaveRange = 3.0f;
                    const float octaveOffset = (cv - 0.5f) * (octaveRange * 2.0f);
                    drive = baseDrive * std::pow(2.0f, octaveOffset);
                } else {
                    // ABSOLUTE: CV directly sets drive (1-100)
                    drive = juce::jmap(cv, 1.0f, 100.0f);
                }
                drive = juce::jlimit(1.0f, 100.0f, drive);
            }
            
            // PER-SAMPLE FIX: Calculate effective type FOR THIS SAMPLE
            int type = baseType;
            if (isTypeMod && typeCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, typeCV[i]);
                // Map CV [0,1] to type [0,2] with wrapping
                type = static_cast<int>(cv * 3.0f) % 3;
            }
            
            float s = data[i] * drive;
            
            switch (type)
            {
                case 0: // Soft Clip (tanh)
                    data[i] = std::tanh(s);
                    break;
                case 1: // Hard Clip
                    data[i] = juce::jlimit(-1.0f, 1.0f, s);
                    break;
                case 2: // Foldback
                    data[i] = std::abs(std::abs(std::fmod(s - 1.0f, 4.0f)) - 2.0f) - 1.0f;
                    break;
            }
        }
    }
    
    // Store live modulated values for UI display (use last sample's values)
    float finalDrive = baseDrive;
    if (isDriveMod && driveCV != nullptr) {
        const float cv = juce::jlimit(0.0f, 1.0f, driveCV[buffer.getNumSamples() - 1]);
        if (relativeDriveMode) {
            // RELATIVE: ±3 octaves
            const float octaveRange = 3.0f;
            const float octaveOffset = (cv - 0.5f) * (octaveRange * 2.0f);
            finalDrive = baseDrive * std::pow(2.0f, octaveOffset);
        } else {
            // ABSOLUTE: CV directly sets drive
            finalDrive = juce::jmap(cv, 1.0f, 100.0f);
        }
        finalDrive = juce::jlimit(1.0f, 100.0f, finalDrive);
    }
    setLiveParamValue("drive_live", finalDrive);
    
    int finalType = baseType;
    if (isTypeMod && typeCV != nullptr) {
        const float cv = juce::jlimit(0.0f, 1.0f, typeCV[buffer.getNumSamples() - 1]);
        finalType = static_cast<int>(cv * 3.0f) % 3;
    }
    setLiveParamValue("type_live", static_cast<float>(finalType));

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(buffer.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(buffer.getSample(1, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void WaveshaperModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();
    
    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };
    
    float drive = 1.0f; if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("drive"))) drive = *p;
    int type = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("type"))) type = p->getIndex();

    ImGui::PushItemWidth(itemWidth);

    ThemeText("Waveshaper Parameters", theme.text.section_header);
    ImGui::Spacing();

    // Drive
    bool isDriveModulated = isParamModulated("drive");
    if (isDriveModulated) {
        drive = getLiveParamValueFor("drive", "drive_live", drive);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Drive", &drive, 1.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) if (!isDriveModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("drive"))) *p = drive;
    if (!isDriveModulated) adjustParamOnWheel(ap.getParameter("drive"), "drive", drive);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isDriveModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Drive amount (1-100)\nLogarithmic scale for fine control");

    // Type
    bool isTypeModulated = isParamModulated("type");
    if (isTypeModulated) {
        type = static_cast<int>(getLiveParamValueFor("type", "type_live", static_cast<float>(type)));
        ImGui::BeginDisabled();
    }
    if (ImGui::Combo("Type", &type, "Soft Clip\0Hard Clip\0Foldback\0\0")) if (!isTypeModulated) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("type"))) *p = type;
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isTypeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Shaping algorithm:\nSoft Clip = smooth saturation\nHard Clip = digital clipping\nFoldback = wave folding distortion");

    ImGui::Spacing();
    ImGui::Spacing();

    // === RELATIVE MODULATION SECTION ===
    ThemeText("CV Input Modes", theme.modulation.frequency);
    ImGui::Spacing();
    
    // Relative Drive Mod checkbox
    bool relativeDriveMod = relativeDriveModParam != nullptr && relativeDriveModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Drive Mod", &relativeDriveMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDriveMod")))
            *p = relativeDriveMod;
        juce::Logger::writeToLog("[Waveshaper UI] Relative Drive Mod: " + juce::String(relativeDriveMod ? "ON" : "OFF"));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±3 octaves)\nOFF: CV directly sets drive (1-100)");
    }

    ImGui::PopItemWidth();
}

void WaveshaperModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);

    // CORRECTED MODULATION PINS - Use absolute channel index
    int busIdx, chanInBus;
    if (getParamRouting("drive", busIdx, chanInBus))
        helpers.drawAudioInputPin("Drive Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("type", busIdx, chanInBus))
        helpers.drawAudioInputPin("Type Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));

    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

std::vector<DynamicPinInfo> WaveshaperModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (channels 2-3)
    pins.push_back({"Drive Mod", 2, PinDataType::CV});
    pins.push_back({"Type Mod", 3, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> WaveshaperModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}

// Parameter bus contract implementation
bool WaveshaperModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "drive") { outChannelIndexInBus = 2; return true; }
    if (paramId == "type") { outChannelIndexInBus = 3; return true; }
    return false;
}
