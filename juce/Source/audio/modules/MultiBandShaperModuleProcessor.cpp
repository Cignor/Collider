#include "MultiBandShaperModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include "../../preset_creator/ControllerPresetManager.h"
#endif
#include <cmath>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MultiBandShaperModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Create a Drive parameter for each band (0 = mute, 100 = max drive)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "drive_" + juce::String(i + 1);
        auto paramName = "Drive " + juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            paramId, paramName,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f, 0.3f), 1.0f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    
    // Relative modulation parameters
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "relativeDriveMod_" + juce::String(i + 1);
        auto paramName = "Relative Drive Mod " + juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterBool>(paramId, paramName, true));
    }
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeGainMod", "Relative Gain Mod", true));

    return { params.begin(), params.end() };
}

MultiBandShaperModuleProcessor::MultiBandShaperModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(2 + NUM_BANDS + 2), true) // 0-1: Audio In, 2-9: Drive Mods, 10: Gain Mod, 11: Mix Mod
          .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MultiBandShaperParams", createParameterLayout())
{
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        driveParams[i] = apvts.getRawParameterValue("drive_" + juce::String(i + 1));
        relativeDriveModParams[i] = apvts.getRawParameterValue("relativeDriveMod_" + juce::String(i + 1));
    }
    outputGainParam = apvts.getRawParameterValue("outputGain");
    mixParam = apvts.getRawParameterValue("mix");
    relativeGainModParam = apvts.getRawParameterValue("relativeGainMod");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MultiBandShaperModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    smoothedMix.reset(sampleRate, 0.01);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 1 }; // Mono spec for each filter

    const float q = 1.41f; // Standard Q value for reasonable band separation

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            filters[band][ch].prepare(spec);
            filters[band][ch].reset(); // Ensure clean state
            filters[band][ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(
                sampleRate, centerFreqs[band], q);
        }
    }
    
    bandBuffer.setSize(2, samplesPerBlock, false, true, true); // Clear and allocate
    sumBuffer.setSize(2, samplesPerBlock, false, true, true); // Clear and allocate

#if defined(PRESET_CREATOR_UI)
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        vizAccumInput[band] = 0.0;
        vizAccumOutput[band] = 0.0;
        vizData.bandInputDb[band].store(-60.0f);
        vizData.bandOutputDb[band].store(-60.0f);
        vizData.bandDriveValue[band].store(0.0f);
    }
    vizData.outputLevelDb.store(-60.0f);
#endif
}

void MultiBandShaperModuleProcessor::releaseResources()
{
    // Reset all filters to ensure clean state when stopped
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            filters[band][ch].reset();
        }
    }
}

void MultiBandShaperModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Get pointers to modulation CV inputs from unified input bus
    const bool isMixMod = isParamInputConnected("mix");
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    // SAFE: Read input pointers BEFORE any output operations
    const int mixModChannel = 2 + NUM_BANDS + 1; // Channel 11
    const float* mixCV = isMixMod && inBus.getNumChannels() > mixModChannel ? inBus.getReadPointer(mixModChannel) : nullptr;
    
    // Get base parameter values ONCE
    const float baseMix = mixParam != nullptr ? mixParam->load() : 1.0f;

    const int numSamples = buffer.getNumSamples();
    
    // Store dry signal for mixing
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.setSize(2, numSamples, false, false, true);
    for (int ch = 0; ch < juce::jmin(2, inBus.getNumChannels()); ++ch)
        dryBuffer.copyFrom(ch, 0, inBus, ch, 0, numSamples);
    
    sumBuffer.clear();

#if defined(PRESET_CREATOR_UI)
    std::array<float, NUM_BANDS> driveValuesUsed {};
#endif

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        // 1. Filter the original signal to isolate this band
        for (int ch = 0; ch < 2; ++ch)
        {
            bandBuffer.copyFrom(ch, 0, inBus, ch, 0, numSamples);
        }

        // Apply filters per channel
        for (int ch = 0; ch < 2; ++ch)
        {
            float* data = bandBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = filters[band][ch].processSample(data[i]);
            }
        }

#if defined(PRESET_CREATOR_UI)
        double inputEnergy = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* data = bandBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                inputEnergy += static_cast<double>(data[i]) * static_cast<double>(data[i]);
        }
        vizAccumInput[band] = inputEnergy;
#endif

        // 2. Apply waveshaping to the filtered band
        float baseDrive = driveParams[band]->load();
        float drive = baseDrive;
        const bool relativeDriveMode = relativeDriveModParams[band] && relativeDriveModParams[band]->load() > 0.5f;
        
        // Check for modulation input from unified input bus (channels 2-9)
        if (isParamInputConnected("drive_" + juce::String(band + 1)))
        {
            int modChannel = 2 + band; // Channels 2-9 are drive mods for bands 0-7
            if (inBus.getNumChannels() > modChannel)
            {
                const float cv = juce::jlimit(0.0f, 1.0f, inBus.getSample(modChannel, 0));
                if (relativeDriveMode) {
                    // RELATIVE: ±3 octaves (0.125x to 8x)
                    const float octaveOffset = (cv - 0.5f) * 6.0f;
                    drive = baseDrive * std::pow(2.0f, octaveOffset);
                } else {
                    // ABSOLUTE: CV directly sets drive (0-100)
                    drive = juce::jmap(cv, 0.0f, 100.0f);
                }
                drive = juce::jlimit(0.0f, 100.0f, drive);
            }
        }
        setLiveParamValue("drive_" + juce::String(band + 1) + "_live", drive);
#if defined(PRESET_CREATOR_UI)
        driveValuesUsed[band] = drive;
#endif

        // Skip processing if drive is zero (band is muted)
        if (drive > 0.001f) // Use a small threshold instead of 0.0f
        {
            // Apply tanh waveshaping
            for (int ch = 0; ch < 2; ++ch)
            {
                float* data = bandBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    data[i] = std::tanh(data[i] * drive);
                }
            }

#if defined(PRESET_CREATOR_UI)
            double outputEnergy = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* data = bandBuffer.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    outputEnergy += static_cast<double>(data[i]) * static_cast<double>(data[i]);
            }
            vizAccumOutput[band] = outputEnergy;
#endif

            // 3. Add the shaped band to the final mix
            sumBuffer.addFrom(0, 0, bandBuffer, 0, 0, numSamples);
            sumBuffer.addFrom(1, 0, bandBuffer, 1, 0, numSamples);
        }
#if defined(PRESET_CREATOR_UI)
        else
        {
            vizAccumOutput[band] = 0.0;
        }
#endif
    }

    // 4. Apply output gain and copy to the final output bus
    float baseGainDb = outputGainParam->load();
    float gainDb = baseGainDb;
    const bool relativeGainMode = relativeGainModParam && relativeGainModParam->load() > 0.5f;
    
    // Check for modulation on output gain from unified input bus
    if (isParamInputConnected("outputGain"))
    {
        int gainModChannel = 2 + NUM_BANDS; // Channel 10
        if (inBus.getNumChannels() > gainModChannel)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, inBus.getSample(gainModChannel, 0));
            if (relativeGainMode) {
                // RELATIVE: ±24dB offset
                const float offset = (cv - 0.5f) * 48.0f;
                gainDb = baseGainDb + offset;
            } else {
                // ABSOLUTE: CV directly sets gain (-24dB to +24dB)
                gainDb = juce::jmap(cv, -24.0f, 24.0f);
            }
            gainDb = juce::jlimit(-24.0f, 24.0f, gainDb);
        }
    }
    setLiveParamValue("outputGain_live", gainDb);
    
    const float finalGain = juce::Decibels::decibelsToGain(gainDb);
    outBus.copyFrom(0, 0, sumBuffer, 0, 0, numSamples);
    outBus.copyFrom(1, 0, sumBuffer, 1, 0, numSamples);
    outBus.applyGain(finalGain);
    
    // Apply dry/wet mix
    for (int i = 0; i < numSamples; ++i)
    {
        float currentMix = baseMix;
        if (isMixMod && mixCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, mixCV[i]);
            currentMix = cv;
        }
        smoothedMix.setTargetValue(currentMix);
        const float mix = smoothedMix.getNextValue();
        const float dry = 1.0f - mix;
        
        for (int ch = 0; ch < juce::jmin(2, outBus.getNumChannels()); ++ch)
        {
            const float drySample = dryBuffer.getSample(ch, i);
            const float wetSample = outBus.getSample(ch, i);
            outBus.setSample(ch, i, dry * drySample + mix * wetSample);
        }
    }
    
    // Update live parameter value for telemetry
    if (numSamples > 0) {
        float finalMix = baseMix;
        if (isMixMod && mixCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, mixCV[numSamples - 1]);
            finalMix = cv;
        }
        setLiveParamValue("mix_live", finalMix);
    }
    
    if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, numSamples - 1));
    if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, numSamples - 1));

#if defined(PRESET_CREATOR_UI)
    const double norm = juce::jmax(1, numSamples * 2);
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        const double inRms = std::sqrt(vizAccumInput[band] / norm);
        const double outRms = std::sqrt(vizAccumOutput[band] / norm);
        vizData.bandInputDb[band].store(juce::Decibels::gainToDecibels(static_cast<float>(juce::jmax(inRms, 1.0e-6)), -60.0f));
        vizData.bandOutputDb[band].store(juce::Decibels::gainToDecibels(static_cast<float>(juce::jmax(outRms, 1.0e-6)), -60.0f));
        vizData.bandDriveValue[band].store(driveValuesUsed[band]);
    }
    const float outRmsFinal = outBus.getRMSLevel(0, 0, numSamples);
    vizData.outputLevelDb.store(juce::Decibels::gainToDecibels(juce::jmax(outRmsFinal, 1.0e-6f), -60.0f));
#endif
}

bool MultiBandShaperModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId.startsWith("drive_"))
    {
        int bandNum = paramId.getTrailingIntValue(); // e.g., "drive_5" -> 5
        if (bandNum > 0 && bandNum <= NUM_BANDS)
        {
            outChannelIndexInBus = 2 + (bandNum - 1); // Channels 2-9 for drives 1-8
            return true;
        }
    }
    if (paramId == "outputGain")
    {
        outChannelIndexInBus = 2 + NUM_BANDS; // Channel 10 for output gain
        return true;
    }
    if (paramId == "mix")
    {
        outChannelIndexInBus = 2 + NUM_BANDS + 1; // Channel 11 for mix
        return true;
    }
    return false;
}

juce::String MultiBandShaperModuleProcessor::getAudioInputLabel(int channel) const
{
    // Bus 0: Audio In
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    
    // Bus 1: Mod In (channels are relative to the start of the bus)
    // Absolute channel index = channel - numChannelsInBus0
    int modChannel = channel - 2; // Bus 0 has 2 channels (stereo)
    if (modChannel >= 0 && modChannel < NUM_BANDS)
    {
        return "Drive " + juce::String(modChannel + 1) + " Mod";
    }
    if (modChannel == NUM_BANDS)
    {
        return "Gain Mod";
    }
    if (modChannel == NUM_BANDS + 1)
    {
        return "Mix Mod";
    }
    
    return {};
}

#if defined(PRESET_CREATOR_UI)
void MultiBandShaperModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String&)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();
    ImGui::PushID(this);
    
    auto& presetManager = ControllerPresetManager::get();
    
    // Define 12 standard presets (drive values for 8 bands: 60Hz, 150Hz, 400Hz, 1kHz, 2.4kHz, 5kHz, 10kHz, 16kHz)
    struct StandardPreset
    {
        const char* name;
        std::array<float, NUM_BANDS> drives;
    };
    const StandardPreset standardPresets[] = {
        { "Flat", { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } },
        { "Bass Heavy", { 50.0f, 40.0f, 20.0f, 5.0f, 2.0f, 1.0f, 1.0f, 1.0f } },
        { "Bright", { 1.0f, 1.0f, 2.0f, 5.0f, 20.0f, 40.0f, 50.0f, 45.0f } },
        { "Warm", { 15.0f, 25.0f, 30.0f, 20.0f, 10.0f, 5.0f, 2.0f, 1.0f } },
        { "Aggressive", { 60.0f, 60.0f, 60.0f, 60.0f, 60.0f, 60.0f, 60.0f, 60.0f } },
        { "Subtle", { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f } },
        { "Mid Focus", { 2.0f, 5.0f, 40.0f, 50.0f, 45.0f, 30.0f, 10.0f, 5.0f } },
        { "Vintage", { 10.0f, 15.0f, 35.0f, 40.0f, 35.0f, 20.0f, 10.0f, 5.0f } },
        { "Modern", { 45.0f, 35.0f, 15.0f, 10.0f, 15.0f, 30.0f, 45.0f, 50.0f } },
        { "Smooth", { 8.0f, 12.0f, 15.0f, 18.0f, 15.0f, 12.0f, 8.0f, 5.0f } },
        { "Punchy", { 30.0f, 45.0f, 50.0f, 40.0f, 20.0f, 10.0f, 5.0f, 2.0f } },
        { "Airy", { 1.0f, 1.0f, 2.0f, 3.0f, 5.0f, 25.0f, 45.0f, 50.0f } }
    };
    const int numStandardPresets = sizeof(standardPresets) / sizeof(standardPresets[0]);
    
    // Get saved presets
    const auto& savedPresetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::MultiBandShaper);
    
    // UI SYNCHRONIZATION: On first draw after loading, find the index for the saved preset name
    if (activePresetName.isNotEmpty())
    {
        selectedPresetIndex = savedPresetNames.indexOf(activePresetName);
        if (selectedPresetIndex >= 0)
        {
            // Found saved preset, clear standard preset selection
            selectedStandardPresetIndex = -1;
        }
        else
        {
            // Saved preset not found, clear it
            activePresetName = "";
            selectedPresetIndex = -1;
        }
        activePresetName = ""; // Clear so we only do this once
    }
    
    // Create combined list: standard presets + saved presets
    juce::StringArray allPresetNames;
    for (int i = 0; i < numStandardPresets; ++i)
        allPresetNames.add(standardPresets[i].name);
    allPresetNames.add("---"); // Separator
    for (const auto& name : savedPresetNames)
        allPresetNames.add(name);
    
    // Create C-style array for ImGui combo
    std::vector<const char*> comboItems;
    for (const auto& name : allPresetNames)
        comboItems.push_back(name.toRawUTF8());
    
    if (comboItems.empty())
        comboItems.push_back("<no presets>");
    
    // Preset dropdown with scroll-edit support
    // Calculate current selection based on saved state
    int comboSelection = 0;
    if (selectedPresetIndex >= 0 && selectedPresetIndex < savedPresetNames.size())
    {
        // A saved preset is selected
        comboSelection = numStandardPresets + 1 + selectedPresetIndex; // +1 for separator
    }
    else
    {
        // Standard preset is selected (or default to first)
        comboSelection = juce::jlimit(0, numStandardPresets - 1, selectedStandardPresetIndex);
    }
    
    // Helper lambda to apply preset selection
    auto applyPresetSelection = [&](int selection)
    {
        if (selection < numStandardPresets)
        {
            // Standard preset selected
            const auto& preset = standardPresets[selection];
            for (int i = 0; i < NUM_BANDS; ++i)
            {
                juce::String paramId = "drive_" + juce::String(i + 1);
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
                    *p = preset.drives[i];
            }
            selectedPresetIndex = -1; // Clear saved preset selection
            selectedStandardPresetIndex = selection; // Track which standard preset is selected
            activePresetName = "";
            onModificationEnded();
        }
        else if (selection > numStandardPresets) // Skip separator
        {
            // Saved preset selected
            int savedIndex = selection - numStandardPresets - 1;
            if (savedIndex >= 0 && savedIndex < savedPresetNames.size())
            {
                activePresetName = savedPresetNames[savedIndex];
                juce::ValueTree presetData = presetManager.loadPreset(ControllerPresetManager::ModuleType::MultiBandShaper, activePresetName);
                setExtraStateTree(presetData);
                selectedPresetIndex = savedIndex;
                selectedStandardPresetIndex = -1; // Clear standard preset selection
                onModificationEnded();
            }
        }
    };
    
    ThemeText("PRESETS", theme.text.section_header);
    ImGui::SetNextItemWidth(itemWidth * 0.5f); // Constrain dropdown width to 50% of node width
    if (ImGui::Combo("##MultiBandPreset", &comboSelection, comboItems.data(), (int)comboItems.size()))
    {
        applyPresetSelection(comboSelection);
    }
    
    // Scroll-edit support for preset dropdown
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int maxIndex = (int)comboItems.size() - 1;
            // Follow guide pattern: wheel > 0.0f (scroll down) gives -1, wheel < 0.0f (scroll up) gives +1
            const int delta = wheel > 0.0f ? -1 : 1;
            int newSelection = juce::jlimit(0, maxIndex, comboSelection + delta);
            
            // Skip separator when scrolling
            if (newSelection == numStandardPresets)
            {
                newSelection = wheel > 0.0f ? numStandardPresets - 1 : numStandardPresets + 1;
                newSelection = juce::jlimit(0, maxIndex, newSelection);
            }
            
            if (newSelection != comboSelection)
            {
                comboSelection = newSelection;
                applyPresetSelection(comboSelection);
            }
        }
    }
    
    // "Save" button - on same line but constrained
    ImGui::SameLine(0, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Save##multibandpreset"))
    {
        ImGui::OpenPopup("Save MultiBand Preset");
    }
    ImGui::PopStyleColor(3);
    
    // "Delete" button - on same line
    if (selectedPresetIndex >= 0 && selectedPresetIndex < savedPresetNames.size())
    {
        ImGui::SameLine(0, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("Delete##multibandpreset"))
        {
            presetManager.deletePreset(ControllerPresetManager::ModuleType::MultiBandShaper, savedPresetNames[selectedPresetIndex]);
            selectedPresetIndex = -1;
            activePresetName = "";
        }
        ImGui::PopStyleColor(3);
    }
    
    // Save popup
    if (ImGui::BeginPopup("Save MultiBand Preset"))
    {
        ImGui::InputText("Preset Name", presetNameBuffer, sizeof(presetNameBuffer));
        if (ImGui::Button("Save New##confirm"))
        {
            juce::String name(presetNameBuffer);
            if (name.isNotEmpty())
            {
                presetManager.savePreset(ControllerPresetManager::ModuleType::MultiBandShaper, name, getExtraStateTree());
                activePresetName = name;
                selectedPresetIndex = savedPresetNames.indexOf(activePresetName);
                if (selectedPresetIndex < 0)
                {
                    // Preset was just created, need to refresh
                    const auto& updatedNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::MultiBandShaper);
                    selectedPresetIndex = updatedNames.indexOf(activePresetName);
                }
                memset(presetNameBuffer, 0, sizeof(presetNameBuffer));
                ImGui::CloseCurrentPopup();
                onModificationEnded();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##multibandpreset"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();

    const ImU32 panelBg = ThemeManager::getInstance().getCanvasBackground();
    const ImU32 inputColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
    const ImU32 outputColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
    const ImU32 driveColor = ImGui::ColorConvertFloat4ToU32(theme.accent);

    ImGui::Spacing();
    ThemeText("Band Sculpting", theme.text.section_header);
    ImGui::Spacing();

    const float laneHeight = 160.0f;
    const float childHeight = laneHeight + 36.0f;
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginChild("MultiBandViz", ImVec2(itemWidth, childHeight), false, childFlags))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        const ImVec2 childPos = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        const ImVec2 rectMax = ImVec2(childPos.x + childSize.x, childPos.y + childSize.y);
        drawList->AddRectFilled(childPos, rectMax, panelBg, 4.0f);
        ImGui::PushClipRect(childPos, rectMax, true);

        const float padding = 8.0f;
        const float laneWidth = juce::jmax(12.0f, (childSize.x - padding * (NUM_BANDS + 1)) / (float)NUM_BANDS);
        const float laneTop = childPos.y + 12.0f;
        const float laneBottom = childPos.y + laneHeight;
        const float laneUsable = laneBottom - laneTop;

        float inputLineXs[NUM_BANDS];
        float inputLineYs[NUM_BANDS];
        float outputLineYs[NUM_BANDS];

        for (int band = 0; band < NUM_BANDS; ++band)
        {
            ImGui::PushID(band);
            const float x0 = childPos.x + padding + band * (laneWidth + padding);
            const float x1 = x0 + laneWidth;
            const float centerX = (x0 + x1) * 0.5f;
            const float driveValue = vizData.bandDriveValue[band].load();
            const float driveNorm = juce::jlimit(0.0f, 1.0f, driveValue / 100.0f);
            const float inputDb = vizData.bandInputDb[band].load();
            const float outputDb = vizData.bandOutputDb[band].load();
            const float inputNorm = juce::jlimit(0.0f, 1.0f, (inputDb + 60.0f) / 60.0f);
            const float outputNorm = juce::jlimit(0.0f, 1.0f, (outputDb + 60.0f) / 60.0f);

            const float driveTop = laneBottom - driveNorm * laneUsable;
            drawList->AddRect(ImVec2(x0, laneTop), ImVec2(x1, laneBottom), IM_COL32(255, 255, 255, 35), 3.0f);
            drawList->AddRectFilled(ImVec2(x0 + 1.5f, driveTop), ImVec2(x1 - 1.5f, laneBottom), driveColor, 3.0f);

            const float inputY = laneBottom - inputNorm * laneUsable;
            const float outputY = laneBottom - outputNorm * laneUsable;
            inputLineXs[band] = centerX;
            inputLineYs[band] = inputY;
            outputLineYs[band] = outputY;

            ImGui::SetCursorScreenPos(ImVec2(x0, laneTop));
            const ImVec2 buttonSize(laneWidth, laneUsable);
            const juce::String paramId = "drive_" + juce::String(band + 1);
            const bool isMod = isParamModulated(paramId);
            if (!isMod)
            {
                ImGui::InvisibleButton("DriveDrag", buttonSize, ImGuiButtonFlags_MouseButtonLeft);
                if (ImGui::IsItemActive())
                {
                    const float mouseY = ImGui::GetIO().MousePos.y;
                    const float clamped = juce::jlimit(laneTop, laneBottom, mouseY);
                    const float normalized = 1.0f - (clamped - laneTop) / laneUsable;
                    const float newDrive = juce::jlimit(0.0f, 100.0f, normalized * 100.0f);
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
                        *p = newDrive;
                    vizData.bandDriveValue[band].store(newDrive);
                }
                if (ImGui::IsItemDeactivated())
                    onModificationEnded();
                if (ImGui::IsItemHovered())
                {
                    const juce::String tooltip = juce::String((int)centerFreqs[band]) + " Hz\nDrive: " + juce::String(vizData.bandDriveValue[band].load(), 1) + "\nIn: " + juce::String(inputDb, 1) + " dB\nOut: " + juce::String(outputDb, 1) + " dB";
                    ImGui::SetTooltip("%s", tooltip.toRawUTF8());
                    
                    // Scroll-edit support: handle mouse wheel when hovered
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
                        {
                            adjustParamOnWheel(p, paramId, vizData.bandDriveValue[band].load());
                            vizData.bandDriveValue[band].store(p->get());
                            onModificationEnded();
                        }
                    }
                }
            }
            else
            {
                ImGui::InvisibleButton("DriveDragDisabled", buttonSize, ImGuiButtonFlags_MouseButtonLeft);
                if (ImGui::IsItemHovered())
                {
                    const juce::String tooltip = juce::String((int)centerFreqs[band]) + " Hz (modulated)";
                    ImGui::SetTooltip("%s", tooltip.toRawUTF8());
                }
            }

            const juce::String driveLabel = juce::String(vizData.bandDriveValue[band].load(), 1);
            const float driveLabelWidth = ImGui::CalcTextSize(driveLabel.toRawUTF8()).x;
            drawList->AddText(ImVec2(centerX - driveLabelWidth * 0.5f, laneBottom + 2.0f),
                              IM_COL32(200, 200, 200, 200), driveLabel.toRawUTF8());
            const juce::String freqLabel = (centerFreqs[band] < 1000.0f) ? juce::String((int)centerFreqs[band]) : juce::String(centerFreqs[band] / 1000.0f, 1) + "k";
            const float freqLabelWidth = ImGui::CalcTextSize(freqLabel.toRawUTF8()).x;
            drawList->AddText(ImVec2(centerX - freqLabelWidth * 0.5f, laneBottom + 16.0f),
                              IM_COL32(180, 180, 180, 200), freqLabel.toRawUTF8());
            ImGui::PopID();
        }

        for (int band = 1; band < NUM_BANDS; ++band)
        {
            drawList->AddLine(ImVec2(inputLineXs[band - 1], inputLineYs[band - 1]),
                              ImVec2(inputLineXs[band], inputLineYs[band]), inputColor, 1.8f);
            drawList->AddLine(ImVec2(inputLineXs[band - 1], outputLineYs[band - 1]),
                              ImVec2(inputLineXs[band], outputLineYs[band]), outputColor, 2.0f);
        }

        ImGui::PopClipRect();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    const float outputDb = vizData.outputLevelDb.load();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, driveColor);
    ImGui::ProgressBar(juce::jlimit(0.0f, 1.0f, (outputDb + 60.0f) / 60.0f),
                       ImVec2(itemWidth * 0.55f, 0), (juce::String(outputDb, 1) + " dB").toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    auto* gainParamPtr = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outputGain"));
    const bool isGainModulated = isParamModulated("outputGain");
    float gain = isGainModulated ? getLiveParamValueFor("outputGain", "outputGain_live", gainParamPtr->get()) : gainParamPtr->get();
    ImGui::PushItemWidth(itemWidth);
    if (isGainModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Output (dB)", &gain, -24.0f, 24.0f, "%.1f dB"))
    {
        if (!isGainModulated) *gainParamPtr = gain;
    }
    if (!isGainModulated) adjustParamOnWheel(gainParamPtr, "outputGain", gain);
    if (ImGui::IsItemDeactivatedAfterEdit() && !isGainModulated) onModificationEnded();
    if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ThemeText("CV Input Modes", theme.modulation.frequency);
    ImGui::Spacing();

    const int numColumns = 2;
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "relativeDriveMod_" + juce::String(i + 1);
        bool relDriveMod = relativeDriveModParams[i] != nullptr && relativeDriveModParams[i]->load() > 0.5f;
        juce::String checkboxLabel = "Band " + juce::String(i + 1) + " Rel";
        if (ImGui::Checkbox((checkboxLabel + "##rel" + juce::String(i)).toRawUTF8(), &relDriveMod))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramId)))
                *p = relDriveMod;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Band %d: ON = CV modulates ±3 oct\nOFF = CV sets drive (0-100)", i + 1);
        if ((i + 1) % numColumns != 0 && i < NUM_BANDS - 1)
            ImGui::SameLine();
    }

    ImGui::Spacing();
    bool relativeGainMod = relativeGainModParam != nullptr && relativeGainModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Gain Mod", &relativeGainMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeGainMod")))
            *p = relativeGainMod;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ON: CV modulates ±24 dB around slider\nOFF: CV sets gain directly (-24 to +24 dB)");

    ImGui::Spacing();
    ThemeText("Mix", theme.text.section_header);
    ImGui::Spacing();
    
    auto* mixParamPtr = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("mix"));
    const bool isMixModulated = isParamModulated("mix");
    float mix = mixParamPtr != nullptr ? mixParamPtr->get() : 1.0f;
    if (isMixModulated) {
        mix = getLiveParamValueFor("mix", "mix_live", mix);
        ImGui::BeginDisabled();
    }
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f, "%.2f"))
    {
        if (!isMixModulated && mixParamPtr != nullptr) *mixParamPtr = mix;
    }
    if (!isMixModulated && mixParamPtr != nullptr) adjustParamOnWheel(mixParamPtr, "mix", mix);
    if (ImGui::IsItemDeactivatedAfterEdit() && !isMixModulated) onModificationEnded();
    if (isMixModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };
    HelpMarker("Wet/dry balance (0=dry, 1=wet)");

    ImGui::PopID();
}

void MultiBandShaperModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In L", 0, "Out L", 0);
    helpers.drawParallelPins("In R", 1, "Out R", 1);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "drive_" + juce::String(i + 1);
        int busIdx, chanInBus;
        if (getParamRouting(paramId, busIdx, chanInBus))
        {
            helpers.drawParallelPins(("Drive " + juce::String(i + 1) + " Mod").toRawUTF8(),
                                     getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus),
                                     nullptr,
                                     -1);
        }
    }
    
    // Draw output gain modulation input
    int busIdx, chanInBus;
    if (getParamRouting("outputGain", busIdx, chanInBus))
    {
        helpers.drawParallelPins("Gain Mod",
                                 getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus),
                                 nullptr,
                                 -1);
    }
    
    // Draw mix modulation input
    if (getParamRouting("mix", busIdx, chanInBus))
    {
        helpers.drawParallelPins("Mix Mod",
                                 getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus),
                                 nullptr,
                                 -1);
    }
}
#endif

std::vector<DynamicPinInfo> MultiBandShaperModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Drive modulation inputs for each band (channels 2-9)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        pins.push_back({"Drive " + juce::String(i + 1) + " Mod", 2 + i, PinDataType::CV});
    }
    
    // Output gain modulation (channel 10)
    pins.push_back({"Gain Mod", 2 + NUM_BANDS, PinDataType::CV});
    
    // Mix modulation (channel 11)
    pins.push_back({"Mix Mod", 2 + NUM_BANDS + 1, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> MultiBandShaperModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}

juce::ValueTree MultiBandShaperModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree state("MultiBandShaperState");
    
#if defined(PRESET_CREATOR_UI)
    // Save the active preset name (for saved presets)
    if (selectedPresetIndex >= 0)
    {
        auto& presetManager = ControllerPresetManager::get();
        const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::MultiBandShaper);
        if (selectedPresetIndex < presetNames.size())
            state.setProperty("preset", presetNames[selectedPresetIndex], nullptr);
    }
    
    // Save which standard preset is selected (for standard presets)
    if (selectedStandardPresetIndex >= 0 && selectedStandardPresetIndex < 12)
    {
        state.setProperty("selectedStandardPresetIndex", selectedStandardPresetIndex, nullptr);
    }
    
    // Save all 8 band drive values
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String paramId = "drive_" + juce::String(i + 1);
        if (driveParams[i] != nullptr)
            state.setProperty(paramId, driveParams[i]->load(), nullptr);
    }
    
    // Save output gain
    if (outputGainParam != nullptr)
        state.setProperty("outputGain", outputGainParam->load(), nullptr);
    
    // Save mix
    if (mixParam != nullptr)
        state.setProperty("mix", mixParam->load(), nullptr);
    
    // Save relative modulation parameters
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String paramId = "relativeDriveMod_" + juce::String(i + 1);
        if (relativeDriveModParams[i] != nullptr)
            state.setProperty(paramId, relativeDriveModParams[i]->load() > 0.5f, nullptr);
    }
    if (relativeGainModParam != nullptr)
        state.setProperty("relativeGainMod", relativeGainModParam->load() > 0.5f, nullptr);
#endif
    
    return state;
}

void MultiBandShaperModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (state.hasType("MultiBandShaperState"))
    {
#if defined(PRESET_CREATOR_UI)
        // Load the preset name for UI synchronization
        activePresetName = state.getProperty("preset", "").toString();
        
        // Load which standard preset is selected
        selectedStandardPresetIndex = state.getProperty("selectedStandardPresetIndex", 0);
        if (selectedStandardPresetIndex < 0 || selectedStandardPresetIndex >= 12)
            selectedStandardPresetIndex = 0; // Default to Flat if invalid
        
        // If a saved preset name was loaded, try to find its index
        if (activePresetName.isNotEmpty())
        {
            auto& presetManager = ControllerPresetManager::get();
            const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::MultiBandShaper);
            selectedPresetIndex = presetNames.indexOf(activePresetName);
            if (selectedPresetIndex >= 0)
            {
                // Found saved preset, clear standard preset selection
                selectedStandardPresetIndex = -1;
            }
            else
            {
                // Saved preset not found, clear it
                activePresetName = "";
                selectedPresetIndex = -1;
            }
        }
        else
        {
            // No saved preset, use standard preset
            selectedPresetIndex = -1;
        }
        
        // Load all 8 band drive values
        for (int i = 0; i < NUM_BANDS; ++i)
        {
            juce::String paramId = "drive_" + juce::String(i + 1);
            float value = state.getProperty(paramId, 1.0f);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
                *p = value;
        }
        
        // Load output gain
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("outputGain")))
            *p = state.getProperty("outputGain", 0.0f);
        
        // Load mix
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("mix")))
            *p = state.getProperty("mix", 1.0f);
        
        // Load relative modulation parameters
        for (int i = 0; i < NUM_BANDS; ++i)
        {
            juce::String paramId = "relativeDriveMod_" + juce::String(i + 1);
            bool value = state.getProperty(paramId, true);
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId)))
                *p = value;
        }
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("relativeGainMod")))
            *p = state.getProperty("relativeGainMod", true);
        
        juce::Logger::writeToLog("[MultiBandShaper] Loaded state - selectedStandardPresetIndex: " + juce::String(selectedStandardPresetIndex) + 
                                 ", selectedPresetIndex: " + juce::String(selectedPresetIndex) + 
                                 ", activePresetName: " + activePresetName);
#endif
    }
}

