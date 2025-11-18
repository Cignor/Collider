#include "MIDIKnobsModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ControllerPresetManager.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MIDIKnobsModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterInt>("numKnobs", "Number of Knobs", 1, MAX_KNOBS, 8));
    layout.add(std::make_unique<juce::AudioParameterInt>("midiChannel", "MIDI Channel", 0, 16, 0)); // 0 = Omni
    
    // Device selection (simplified - device enumeration not available in this context)
    juce::StringArray deviceOptions;
    deviceOptions.add("All Devices");
    layout.add(std::make_unique<juce::AudioParameterChoice>("midiDevice", "MIDI Device", deviceOptions, 0));
    
    return layout;
}

MIDIKnobsModuleProcessor::MIDIKnobsModuleProcessor()
    : ModuleProcessor(BusesProperties().withOutput("Outputs", juce::AudioChannelSet::discreteChannels(MAX_KNOBS), true)),
      apvts(*this, nullptr, "MIDIKnobsParams", createParameterLayout())
{
    numKnobsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numKnobs"));
    midiChannelParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midiChannel"));
    deviceFilterParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("midiDevice"));
    
    for (int i = 0; i < MAX_KNOBS; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MIDIKnobsModuleProcessor::prepareToPlay(double, int)
{
    learningIndex = -1;  // Reset learn state
}

void MIDIKnobsModuleProcessor::releaseResources()
{
}

void MIDIKnobsModuleProcessor::handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages)
{
    int numActive = numKnobsParam ? numKnobsParam->get() : MAX_KNOBS;
    int deviceFilter = deviceFilterParam ? deviceFilterParam->getIndex() : 0;
    int channelFilter = midiChannelParam ? midiChannelParam->get() : 0;
    
    for (const auto& msg : midiMessages)
    {
        // DEVICE FILTERING (0 = All Devices, 1+ = specific device)
        if (deviceFilter != 0 && msg.deviceIndex != (deviceFilter - 1))
            continue;
        
        // CHANNEL FILTERING (0 = All Channels, 1-16 = specific channel)
        if (channelFilter != 0 && msg.message.getChannel() != channelFilter)
            continue;
        
        if (!msg.message.isController())
            continue;
        
        int ccNumber = msg.message.getControllerNumber();
        float ccValue = msg.message.getControllerValue() / 127.0f;
        
        // Handle MIDI Learn
        if (learningIndex != -1 && learningIndex < numActive)
        {
            mappings[learningIndex].midiCC = ccNumber;
            learningIndex = -1;
        }
        
        // Update mapped knobs
        for (int i = 0; i < numActive; ++i)
        {
            if (mappings[i].midiCC == ccNumber)
            {
                mappings[i].currentValue = juce::jmap(ccValue, mappings[i].minVal, mappings[i].maxVal);
            }
        }
    }
}

void MIDIKnobsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages); // MIDI already processed in handleDeviceSpecificMidi
    
    int numActive = numKnobsParam ? numKnobsParam->get() : MAX_KNOBS;
    
    // Note: MIDI CC messages are processed in handleDeviceSpecificMidi() which is called BEFORE processBlock
    // This method just generates CV outputs from the current state
    
    // Write current values to output buffer
    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        float val = (i < numActive) ? mappings[i].currentValue : 0.0f;
        buffer.setSample(i, 0, val);
        
        // Fill rest of block with same value
        if (buffer.getNumSamples() > 1)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(i) + 1, val, buffer.getNumSamples() - 1);
        
        lastOutputValues[i]->store(val);
    }
}

juce::ValueTree MIDIKnobsModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIKnobsState");
    
    // Save the name of the controller preset that is currently active
    #if defined(PRESET_CREATOR_UI)
    vt.setProperty("controllerPreset", activeControllerPresetName, nullptr);
    #endif
    
    // Save the MIDI device filter
    if (deviceFilterParam)
        vt.setProperty("deviceFilter", deviceFilterParam->getIndex(), nullptr);
    
    // Save the MIDI channel from the APVTS parameter
    if (midiChannelParam)
        vt.setProperty("midiChannel", midiChannelParam->get(), nullptr);
    
    // Save the actual mapping data
    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        juce::ValueTree mapping("Mapping");
        mapping.setProperty("index", i, nullptr);
        mapping.setProperty("cc", mappings[i].midiCC, nullptr);
        mapping.setProperty("min", mappings[i].minVal, nullptr);
        mapping.setProperty("max", mappings[i].maxVal, nullptr);
        vt.addChild(mapping, -1, nullptr);
    }
    return vt;
}

void MIDIKnobsModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIKnobsState"))
    {
        // Load the name of the controller preset
        #if defined(PRESET_CREATOR_UI)
        activeControllerPresetName = vt.getProperty("controllerPreset", "").toString();
        #endif
        
        // Load the MIDI device filter
        if (deviceFilterParam && vt.hasProperty("deviceFilter"))
        {
            int deviceIndex = vt.getProperty("deviceFilter", 0);
            deviceFilterParam->setValueNotifyingHost(deviceFilterParam->convertTo0to1((float)deviceIndex));
        }
        
        // Load the MIDI channel and update the APVTS parameter
        if (midiChannelParam)
            *midiChannelParam = (int)vt.getProperty("midiChannel", 0);
        
        // Load the actual mapping data
        for (const auto& child : vt)
        {
            if (child.hasType("Mapping"))
            {
                int index = child.getProperty("index", -1);
                if (index >= 0 && index < MAX_KNOBS)
                {
                    mappings[index].midiCC = child.getProperty("cc", -1);
                    mappings[index].minVal = child.getProperty("min", 0.0f);
                    mappings[index].maxVal = child.getProperty("max", 1.0f);
                }
            }
        }
    }
}

std::vector<DynamicPinInfo> MIDIKnobsModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    int numActive = numKnobsParam ? numKnobsParam->get() : MAX_KNOBS;
    
    for (int i = 0; i < numActive; ++i)
    {
        juce::String label = "Knob " + juce::String(i + 1);
        pins.push_back(DynamicPinInfo(label, i, PinDataType::CV));
    }
    
    return pins;
}

#if defined(PRESET_CREATOR_UI)

// Helper function for tooltip with help marker
static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void MIDIKnobsModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);
    
    // === MULTI-MIDI DEVICE FILTERING ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "MIDI Routing");
    ImGui::Text("Device: All Devices (filtering active in background)");
    ImGui::TextDisabled("Note: Check MIDI Device Manager window for device list");
    
    // Channel selector
    if (midiChannelParam)
    {
        int channel = midiChannelParam->get();
        const char* items[] = {"All Channels", "1", "2", "3", "4", "5", "6", "7", "8",
                               "9", "10", "11", "12", "13", "14", "15", "16"};
        if (ImGui::Combo("Channel", &channel, items, 17))
        {
            midiChannelParam->setValueNotifyingHost(
                midiChannelParam->getNormalisableRange().convertTo0to1(channel));
        }
    }
    
    ImGui::Spacing();
    
    // === PRESET MANAGEMENT UI ===
    auto& presetManager = ControllerPresetManager::get();
    const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::Knobs);
    
    // UI SYNCHRONIZATION: On first draw after loading, find the index for the saved preset name
    if (activeControllerPresetName.isNotEmpty())
    {
        selectedPresetIndex = presetNames.indexOf(activeControllerPresetName);
        activeControllerPresetName = ""; // Clear so we only do this once
    }
    
    ImGui::Text("Controller Preset");
    
    // Create a C-style array of char pointers for the ImGui combo box
    std::vector<const char*> names;
    for (const auto& name : presetNames)
        names.push_back(name.toRawUTF8());
    
    // Draw the dropdown menu
    if (ImGui::Combo("##PresetCombo", &selectedPresetIndex, names.data(), (int)names.size()))
    {
        // When a preset is selected, load it and update our state
        if (selectedPresetIndex >= 0 && selectedPresetIndex < (int)presetNames.size())
        {
            activeControllerPresetName = presetNames[selectedPresetIndex];
            juce::ValueTree presetData = presetManager.loadPreset(ControllerPresetManager::ModuleType::Knobs, activeControllerPresetName);
            setExtraStateTree(presetData);
            onModificationEnded(); // Create an undo state
        }
    }
    
    // "Save" button and text input popup
    ImGui::SameLine();
    if (ImGui::Button("Save##preset"))
    {
        ImGui::OpenPopup("Save Knob Preset");
    }
    
    // "Delete" button
    ImGui::SameLine();
    if (ImGui::Button("Delete##preset"))
    {
        if (selectedPresetIndex >= 0 && selectedPresetIndex < (int)presetNames.size())
        {
            presetManager.deletePreset(ControllerPresetManager::ModuleType::Knobs, presetNames[selectedPresetIndex]);
            selectedPresetIndex = -1; // Deselect
            activeControllerPresetName = ""; // Clear active name
        }
    }
    
    if (ImGui::BeginPopup("Save Knob Preset"))
    {
        ImGui::InputText("Preset Name", presetNameBuffer, sizeof(presetNameBuffer));
        if (ImGui::Button("Save New##confirm"))
        {
            juce::String name(presetNameBuffer);
            if (name.isNotEmpty())
            {
                presetManager.savePreset(ControllerPresetManager::ModuleType::Knobs, name, getExtraStateTree());
                activeControllerPresetName = name; // Mark this new preset as active
                selectedPresetIndex = presetNames.indexOf(activeControllerPresetName); // Resync UI
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##preset"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === HEADER SECTION ===
    // Number of knobs control
    if (numKnobsParam)
    {
        int numKnobs = numKnobsParam->get();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##numknobs", &numKnobs, 1, MAX_KNOBS))
        {
            *numKnobsParam = numKnobs;
            onModificationEnded();
        }
        ImGui::SameLine();
        ImGui::Text("Knobs");
        ImGui::SameLine();
        HelpMarker("Number of active knobs (1-16). Drag to adjust.");
    }
    
    // MIDI Channel filter control
    if (midiChannelParam)
    {
        int channel = midiChannelParam->get();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##midichannel", &channel, 0, 16))
        {
            *midiChannelParam = channel;
            onModificationEnded();
        }
        ImGui::SameLine();
        ImGui::Text(channel == 0 ? "Ch: Omni (All)" : juce::String("Ch: " + juce::String(channel)).toRawUTF8());
        ImGui::SameLine();
        HelpMarker("MIDI Channel filter. 0 = Omni (all channels), 1-16 = specific channel only.");
    }
    
    // View mode selector
    ImGui::Spacing();
    if (ImGui::RadioButton("Visual", viewMode == ViewMode::Visual)) viewMode = ViewMode::Visual;
    ImGui::SameLine();
    if (ImGui::RadioButton("Compact", viewMode == ViewMode::Compact)) viewMode = ViewMode::Compact;
    ImGui::SameLine();
    if (ImGui::RadioButton("Table", viewMode == ViewMode::Table)) viewMode = ViewMode::Table;
    ImGui::SameLine();
    HelpMarker("Visual: Horizontal sliders with color coding\nCompact: Linear list view\nTable: Detailed table view");
    
    ImGui::Spacing();
    ImGui::Spacing();  // Double spacing for visual separation
    
    // === DRAW SELECTED VIEW ===
    int numActive = numKnobsParam ? numKnobsParam->get() : MAX_KNOBS;
    
    switch (viewMode)
    {
        case ViewMode::Visual:
            drawVisualKnobs(numActive, onModificationEnded);
            break;
        case ViewMode::Compact:
            drawCompactList(numActive, onModificationEnded);
            break;
        case ViewMode::Table:
            drawTableView(numActive, onModificationEnded);
            break;
    }
    
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void MIDIKnobsModuleProcessor::drawVisualKnobs(int numActive, const std::function<void()>& onModificationEnded)
{
    // Draw horizontal sliders in a grid (4 per row)
    const int knobsPerRow = 4;
    const float sliderWidth = 120.0f;
    const float sliderHeight = 18.0f;
    const float spacing = 8.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    
    for (int row = 0; row < (numActive + knobsPerRow - 1) / knobsPerRow; ++row)
    {
        if (row > 0) ImGui::Spacing();
        
        for (int col = 0; col < knobsPerRow; ++col)
        {
            int i = row * knobsPerRow + col;
            if (i >= numActive) break;
            
            if (col > 0) ImGui::SameLine();
            
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            ImGui::BeginGroup();
            
            // Label with CC number
            float hue = static_cast<float>(i) / static_cast<float>(MAX_KNOBS);
            if (map.midiCC != -1)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.8f, 1.0f).Value);
                ImGui::Text("K%d:CC%d", i + 1, map.midiCC);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextDisabled("K%d:---", i + 1);
            }
            
            // Horizontal slider with color coding
            ImVec4 colorBg = ImColor::HSV(hue, 0.5f, 0.5f);
            ImVec4 colorHovered = ImColor::HSV(hue, 0.6f, 0.6f);
            ImVec4 colorActive = ImColor::HSV(hue, 0.7f, 0.7f);
            ImVec4 colorGrab = ImColor::HSV(hue, 0.9f, 0.9f);
            
            // Special color for learning state
            if (learningIndex == i)
            {
                colorBg = ImVec4(1.0f, 0.5f, 0.0f, 0.8f);
                colorHovered = ImVec4(1.0f, 0.6f, 0.1f, 0.9f);
                colorActive = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
                colorGrab = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            }
            
            ImGui::PushStyleColor(ImGuiCol_FrameBg, colorBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, colorHovered);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, colorActive);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, colorGrab);
            
            float displayValue = map.currentValue;
            bool hasMapping = (map.midiCC != -1);
            
            if (!hasMapping)
                ImGui::BeginDisabled();
            
            ImGui::SetNextItemWidth(sliderWidth);
            if (ImGui::SliderFloat("##slider", &displayValue, map.minVal, map.maxVal, "%.2f"))
            {
                map.currentValue = displayValue;
            }
            
            if (ImGui::IsItemActive() || ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Knob %d\nCC: %d\nValue: %.3f\nRange: %.1f - %.1f",
                                  i + 1, map.midiCC, map.currentValue, map.minVal, map.maxVal);
            }
            
            if (!hasMapping)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Knob %d\nNo MIDI CC assigned\nClick Learn button below", i + 1);
            }
            
            ImGui::PopStyleColor(4);
            
            // Learn button below slider
            if (learningIndex == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
                if (ImGui::Button("Stop##btn", ImVec2(sliderWidth, 0)))
                    learningIndex = -1;
                ImGui::PopStyleColor(2);
            }
            else
            {
                if (ImGui::Button("Learn##btn", ImVec2(sliderWidth, 0)))
                    learningIndex = i;
            }
            
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
    
    ImGui::PopStyleVar();
}

void MIDIKnobsModuleProcessor::drawCompactList(int numActive, const std::function<void()>& onModificationEnded)
{
    ImGui::TextDisabled("Click 'Learn' then move a MIDI control");
    ImGui::Spacing();
    
    for (int i = 0; i < numActive; ++i)
    {
        auto& map = mappings[i];
        ImGui::PushID(i);
        
        // Knob label with live value indicator
        float normalizedValue = (map.maxVal != map.minVal) ?
            (map.currentValue - map.minVal) / (map.maxVal - map.minVal) : 0.0f;
        normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
        
        ImGui::Text("K%d", i + 1);
        ImGui::SameLine();
        
        // Value progress bar
        ImGui::SetNextItemWidth(60);
        float hue = static_cast<float>(i) / static_cast<float>(MAX_KNOBS);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(hue, 0.7f, 0.7f).Value);
        ImGui::ProgressBar(normalizedValue, ImVec2(0, 0), "");
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        ImGui::Text("CC:%3d", map.midiCC != -1 ? map.midiCC : 0);
        if (map.midiCC == -1)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(unassigned)");
        }
        
        ImGui::SameLine();
        
        // Learn button with visual feedback
        if (learningIndex == i)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
            if (ImGui::Button("Learning...##btn", ImVec2(90, 0)))
                learningIndex = -1;
            ImGui::PopStyleColor(2);
        }
        else
        {
            if (ImGui::Button("Learn##btn", ImVec2(90, 0)))
                learningIndex = i;
        }
        
        // Range control on same line
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
        if (ImGui::DragFloatRange2("##range", &map.minVal, &map.maxVal, 0.01f, -10.0f, 10.0f,
                                    "%.1f", "%.1f", flags))
        {
            onModificationEnded();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min: %.2f, Max: %.2f", map.minVal, map.maxVal);
        
        ImGui::PopID();
    }
}

void MIDIKnobsModuleProcessor::drawTableView(int numActive, const std::function<void()>& onModificationEnded)
{
    ImGui::TextDisabled("Detailed view with all parameters");
    ImGui::Spacing();
    
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit |
                            ImGuiTableFlags_NoHostExtendX |
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable;
    
    float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4;
    float tableHeight = rowHeight * (numActive + 1.5f);
    
    if (ImGui::BeginTable("##knobs_table", 6, flags, ImVec2(0, tableHeight)))
    {
        ImGui::TableSetupColumn("Knob", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("CC", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Learn", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableHeadersRow();
        
        for (int i = 0; i < numActive; ++i)
        {
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            ImGui::TableNextRow();
            
            // Column 0: Knob number
            ImGui::TableNextColumn();
            float hue = static_cast<float>(i) / static_cast<float>(MAX_KNOBS);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.8f, 1.0f).Value);
            ImGui::Text("Knob %d", i + 1);
            ImGui::PopStyleColor();
            
            // Column 1: CC number
            ImGui::TableNextColumn();
            if (map.midiCC != -1)
                ImGui::Text("%d", map.midiCC);
            else
                ImGui::TextDisabled("--");
            
            // Column 2: Current value
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", map.currentValue);
            
            // Column 3: Learn button
            ImGui::TableNextColumn();
            if (learningIndex == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                if (ImGui::Button("Learning##btn"))
                    learningIndex = -1;
                ImGui::PopStyleColor();
            }
            else
            {
                if (ImGui::Button("Learn##btn"))
                    learningIndex = i;
            }
            
            // Column 4: Min value
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##min", &map.minVal, 0.01f, -10.0f, map.maxVal, "%.1f"))
                onModificationEnded();
            ImGui::PopItemWidth();
            
            // Column 5: Max value
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##max", &map.maxVal, 0.01f, map.minVal, 10.0f, "%.1f"))
                onModificationEnded();
            ImGui::PopItemWidth();
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void MIDIKnobsModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    juce::ignoreUnused(helpers);
}
#endif

