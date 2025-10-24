#include "MIDIJogWheelModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ControllerPresetManager.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MIDIJogWheelModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>("increment", "Increment", juce::StringArray{"0.001", "0.01", "0.1", "1.0", "10.0", "100.0"}, 2)); // Default to 0.1
    layout.add(std::make_unique<juce::AudioParameterFloat>("resetValue", "Reset Value", -100000.0f, 100000.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterInt>("midiChannel", "MIDI Channel", 0, 16, 1)); // Default to Channel 1
    
    // Device selection (simplified - device enumeration not available in this context)
    juce::StringArray deviceOptions;
    deviceOptions.add("All Devices");
    layout.add(std::make_unique<juce::AudioParameterChoice>("midiDevice", "MIDI Device", deviceOptions, 0));
    
    return layout;
}

MIDIJogWheelModuleProcessor::MIDIJogWheelModuleProcessor()
    : ModuleProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "MIDIJogWheelParams", createParameterLayout())
{
    incrementParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("increment"));
    resetValueParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("resetValue"));
    midiChannelParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midiChannel"));
    deviceFilterParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("midiDevice"));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MIDIJogWheelModuleProcessor::prepareToPlay(double, int)
{
    isLearning = false;
    mapping.lastRelativeValue = -1;
}

void MIDIJogWheelModuleProcessor::handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages)
{
    int deviceFilter = deviceFilterParam ? deviceFilterParam->getIndex() : 0;
    int channelFilter = midiChannelParam ? midiChannelParam->get() : 0;
    static const float increments[] = {0.001f, 0.01f, 0.1f, 1.0f, 10.0f, 100.0f};
    float incrementSize = increments[incrementParam ? incrementParam->getIndex() : 2];

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
        int value = msg.message.getControllerValue();
        
        // Learn mode: capture the first CC we see
        if (isLearning)
        {
            mapping.midiCC = ccNumber;
            isLearning = false;
            mapping.lastRelativeValue = -1;
        }
        
        // Process the learned/assigned CC
        if (mapping.midiCC != -1 && ccNumber == mapping.midiCC)
        {
            // DELTA CALCULATION for encoders
            if (mapping.lastRelativeValue != -1)
            {
                int delta = value - mapping.lastRelativeValue;
                
                // Handle wraparound
                if (delta > 64)
                    delta -= 128;
                else if (delta < -64)
                    delta += 128;
                
                mapping.currentValue += (float)delta * incrementSize;
            }
            
            mapping.lastRelativeValue = value;
        }
    }
}

void MIDIJogWheelModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages); // MIDI already processed in handleDeviceSpecificMidi
    
    // Note: MIDI is processed in handleDeviceSpecificMidi() which is called BEFORE processBlock
    // This method just generates CV output from the current state
    
    // Write output
    buffer.setSample(0, 0, mapping.currentValue);
    if (buffer.getNumSamples() > 1)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0) + 1, mapping.currentValue, buffer.getNumSamples() - 1);
    
    lastOutputValues[0]->store(mapping.currentValue);
}

juce::ValueTree MIDIJogWheelModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIJogWheelState");
    
    #if defined(PRESET_CREATOR_UI)
    vt.setProperty("controllerPreset", activeControllerPresetName, nullptr);
    #endif
    
    if (midiChannelParam)
        vt.setProperty("midiChannel", midiChannelParam->get(), nullptr);
    
    vt.setProperty("midiCC", mapping.midiCC, nullptr);
    vt.setProperty("currentValue", mapping.currentValue, nullptr);
    return vt;
}

void MIDIJogWheelModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIJogWheelState"))
    {
        #if defined(PRESET_CREATOR_UI)
        activeControllerPresetName = vt.getProperty("controllerPreset", "").toString();
        #endif
        
        if (midiChannelParam)
            *midiChannelParam = (int)vt.getProperty("midiChannel", 1);
        
        mapping.midiCC = vt.getProperty("midiCC", -1);
        mapping.currentValue = vt.getProperty("currentValue", 0.0f);
        mapping.lastRelativeValue = -1; // Reset delta tracking on load
    }
}

std::vector<DynamicPinInfo> MIDIJogWheelModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    pins.push_back(DynamicPinInfo("Value", 0, PinDataType::CV));
    return pins;
}

#if defined(PRESET_CREATOR_UI)

static void HelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void MIDIJogWheelModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // === MULTI-MIDI DEVICE FILTERING ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "MIDI Routing");
    
    // Device selector
    if (deviceFilterParam)
    {
        int deviceIdx = deviceFilterParam->getIndex();
        const char* deviceName = deviceFilterParam->getCurrentChoiceName().toRawUTF8();
        if (ImGui::BeginCombo("Device", deviceName))
        {
            for (int i = 0; i < deviceFilterParam->choices.size(); ++i)
            {
                bool isSelected = (deviceIdx == i);
                if (ImGui::Selectable(deviceFilterParam->choices[i].toRawUTF8(), isSelected))
                {
                    deviceFilterParam->setValueNotifyingHost(
                        deviceFilterParam->getNormalisableRange().convertTo0to1(i));
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    
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
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // === PRESET MANAGEMENT UI ===
    auto& presetManager = ControllerPresetManager::get();
    const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::JogWheel);
    
    if (activeControllerPresetName.isNotEmpty())
    {
        selectedPresetIndex = presetNames.indexOf(activeControllerPresetName);
        activeControllerPresetName = "";
    }
    
    ImGui::Text("Controller Preset");
    
    std::vector<const char*> names;
    for (const auto& name : presetNames)
        names.push_back(name.toRawUTF8());
    
    if (ImGui::Combo("##PresetCombo", &selectedPresetIndex, names.data(), (int)names.size()))
    {
        if (selectedPresetIndex >= 0 && selectedPresetIndex < (int)presetNames.size())
        {
            activeControllerPresetName = presetNames[selectedPresetIndex];
            juce::ValueTree presetData = presetManager.loadPreset(ControllerPresetManager::ModuleType::JogWheel, activeControllerPresetName);
            setExtraStateTree(presetData);
            onModificationEnded();
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save##preset"))
        ImGui::OpenPopup("Save JogWheel Preset");
    
    ImGui::SameLine();
    if (ImGui::Button("Delete##preset"))
    {
        if (selectedPresetIndex >= 0 && selectedPresetIndex < (int)presetNames.size())
        {
            presetManager.deletePreset(ControllerPresetManager::ModuleType::JogWheel, presetNames[selectedPresetIndex]);
            selectedPresetIndex = -1;
            activeControllerPresetName = "";
        }
    }
    
    if (ImGui::BeginPopup("Save JogWheel Preset"))
    {
        ImGui::InputText("Preset Name", presetNameBuffer, sizeof(presetNameBuffer));
        if (ImGui::Button("Save New##confirm"))
        {
            juce::String name(presetNameBuffer);
            if (name.isNotEmpty())
            {
                presetManager.savePreset(ControllerPresetManager::ModuleType::JogWheel, name, getExtraStateTree());
                activeControllerPresetName = name;
                selectedPresetIndex = presetNames.indexOf(activeControllerPresetName);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##preset"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === HEADER ===
    ImGui::Text("MIDI Jog Wheel / Infinite Encoder");
    ImGui::SameLine();
    HelpMarker("Uses delta calculation for smooth infinite rotation.\nWorks with encoders that send changing CC values.");
    
    // === STATUS & LEARN ===
    if (mapping.midiCC != -1)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
        ImGui::Text("Assigned to CC %d", mapping.midiCC);
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
        ImGui::Text("Not Assigned");
        ImGui::PopStyleColor();
    }
    
    ImGui::Spacing();
    
    if (isLearning)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
        if (ImGui::Button("Learning... (turn jog wheel)", ImVec2(itemWidth, 0)))
            isLearning = false;
        ImGui::PopStyleColor();
    }
    else
    {
        if (ImGui::Button("Learn MIDI CC", ImVec2(itemWidth, 0)))
        {
            isLearning = true;
            mapping.lastRelativeValue = -1;
        }
    }
    ImGui::SameLine();
    HelpMarker("Click, then turn your jog wheel to assign it.\nWorks with any encoder CC (82, 86, etc.)");
    
    // MIDI Channel
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
        HelpMarker("MIDI Channel. 0 = Omni, 1-16 = specific channel.");
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === CONFIGURATION ===
    ImGui::Text("Configuration");
    
    int incrementIdx = incrementParam->getIndex();
    const char* incrementNames[] = { "0.001", "0.01", "0.1", "1.0", "10.0", "100.0" };
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::Combo("Increment", &incrementIdx, incrementNames, 6))
    {
        *incrementParam = incrementIdx;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarker("Step size per tick. Start with 0.1 for testing.");
    
    float resetVal = resetValueParam->get();
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::InputFloat("Reset Value", &resetVal))
        *resetValueParam = resetVal;
    if (ImGui::IsItemDeactivatedAfterEdit())
        onModificationEnded();
    
    if (ImGui::Button("Reset to Value", ImVec2(itemWidth, 0)))
    {
        mapping.currentValue = resetValueParam->get();
        onModificationEnded();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === VALUE DISPLAY ===
    ImGui::Separator();
    ImGui::Text("Current Value: %.3f", mapping.currentValue);
    
    // Circular display
    const float canvasWidth = juce::jmin(itemWidth, 150.0f);
    const ImVec2 canvasSize(canvasWidth, canvasWidth);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 center(p0.x + canvasSize.x * 0.5f, p0.y + canvasSize.y * 0.5f);
    float radius = canvasSize.x * 0.42f;
    auto* drawList = ImGui::GetWindowDrawList();
    
    drawList->PushClipRect(p0, ImVec2(p0.x + canvasSize.x, p0.y + canvasSize.y), true);
    drawList->AddCircleFilled(center, radius + 4.0f, IM_COL32(30, 30, 30, 255), 64);
    drawList->AddCircle(center, radius, IM_COL32(100, 100, 100, 255), 64, 2.0f);
    
    float normalizedValue = std::fmod(mapping.currentValue, 1.0f);
    if (normalizedValue < 0.0f) normalizedValue += 1.0f;
    
    float angle = normalizedValue * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
    ImVec2 handEnd(center.x + std::cos(angle) * radius * 0.85f, 
                   center.y + std::sin(angle) * radius * 0.85f);
    drawList->AddLine(center, handEnd, IM_COL32(100, 180, 255, 255), 4.0f);
    drawList->AddCircleFilled(center, 6.0f, IM_COL32(100, 180, 255, 255));
    
    for (int i = 0; i < 4; ++i)
    {
        float tickAngle = (i * juce::MathConstants<float>::halfPi) - juce::MathConstants<float>::halfPi;
        ImVec2 tickStart(center.x + std::cos(tickAngle) * (radius - 8.0f),
                        center.y + std::sin(tickAngle) * (radius - 8.0f));
        ImVec2 tickEnd(center.x + std::cos(tickAngle) * radius,
                      center.y + std::sin(tickAngle) * radius);
        drawList->AddLine(tickStart, tickEnd, IM_COL32(120, 120, 120, 255), 2.0f);
    }
    
    drawList->PopClipRect();
    ImGui::InvisibleButton("##jogwheel", canvasSize);
    
    ImGui::PopItemWidth();
}

void MIDIJogWheelModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Value", 0);
}

#endif
