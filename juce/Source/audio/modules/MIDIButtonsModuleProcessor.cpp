#include "MIDIButtonsModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MIDIButtonsModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterInt>("numButtons", "Number of Buttons", 1, MAX_BUTTONS, 16));
    return layout;
}

MIDIButtonsModuleProcessor::MIDIButtonsModuleProcessor()
    : ModuleProcessor(BusesProperties().withOutput("Outputs", juce::AudioChannelSet::discreteChannels(MAX_BUTTONS), true)),
      apvts(*this, nullptr, "MIDIButtonsParams", createParameterLayout())
{
    numButtonsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numButtons"));
    
    for (int i = 0; i < MAX_BUTTONS; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MIDIButtonsModuleProcessor::prepareToPlay(double, int)
{
    learningIndex = -1;  // Reset learn state
}

void MIDIButtonsModuleProcessor::releaseResources()
{
}

void MIDIButtonsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    int numActive = numButtonsParam ? numButtonsParam->get() : MAX_BUTTONS;
    
    // Process incoming MIDI CC messages
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (!msg.isController()) continue;
        
        int ccNumber = msg.getControllerNumber();
        float ccValue = msg.getControllerValue() / 127.0f;
        
        // Handle MIDI Learn
        if (learningIndex != -1 && learningIndex < numActive)
        {
            mappings[learningIndex].midiCC = ccNumber;
            learningIndex = -1;  // Stop learning
        }
        
        // Update mapped buttons
        for (int i = 0; i < numActive; ++i)
        {
            if (mappings[i].midiCC == ccNumber)
            {
                bool isPressed = ccValue > 0.5f;
                
                switch (mappings[i].mode)
                {
                    case ButtonMode::Gate:
                        mappings[i].currentValue = isPressed ? 1.0f : 0.0f;
                        break;
                        
                    case ButtonMode::Toggle:
                        if (isPressed && !mappings[i].toggleState)  // Rising edge
                            mappings[i].currentValue = 1.0f - mappings[i].currentValue;
                        mappings[i].toggleState = isPressed;
                        break;
                        
                    case ButtonMode::Trigger:
                        if (isPressed && mappings[i].triggerSamplesRemaining <= 0)
                            mappings[i].triggerSamplesRemaining = (int)(getSampleRate() * 0.005);  // 5ms pulse
                        break;
                }
            }
        }
    }
    
    // Write current values to output buffer
    for (int i = 0; i < MAX_BUTTONS; ++i)
    {
        float val = 0.0f;
        
        if (i < numActive)
        {
            if (mappings[i].mode == ButtonMode::Trigger)
            {
                val = (mappings[i].triggerSamplesRemaining > 0) ? 1.0f : 0.0f;
                if (mappings[i].triggerSamplesRemaining > 0)
                    mappings[i].triggerSamplesRemaining--;
            }
            else
            {
                val = mappings[i].currentValue;
            }
        }
        
        buffer.setSample(i, 0, val);
        
        // Fill rest of block with same value
        if (buffer.getNumSamples() > 1)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(i) + 1, val, buffer.getNumSamples() - 1);
        
        lastOutputValues[i]->store(val);
    }
}

juce::ValueTree MIDIButtonsModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIButtonsState");
    for (int i = 0; i < MAX_BUTTONS; ++i)
    {
        juce::ValueTree mapping("Mapping");
        mapping.setProperty("index", i, nullptr);
        mapping.setProperty("cc", mappings[i].midiCC, nullptr);
        mapping.setProperty("mode", (int)mappings[i].mode, nullptr);
        vt.addChild(mapping, -1, nullptr);
    }
    return vt;
}

void MIDIButtonsModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIButtonsState"))
    {
        for (const auto& child : vt)
        {
            if (child.hasType("Mapping"))
            {
                int index = child.getProperty("index", -1);
                if (index >= 0 && index < MAX_BUTTONS)
                {
                    mappings[index].midiCC = child.getProperty("cc", -1);
                    mappings[index].mode = (ButtonMode)(int)child.getProperty("mode", 0);
                }
            }
        }
    }
}

std::vector<DynamicPinInfo> MIDIButtonsModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    int numActive = numButtonsParam ? numButtonsParam->get() : MAX_BUTTONS;
    
    for (int i = 0; i < numActive; ++i)
    {
        juce::String label = "Button " + juce::String(i + 1);
        pins.push_back(DynamicPinInfo(label, i, PinDataType::Gate));
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

ImVec4 MIDIButtonsModuleProcessor::getModeColor(ButtonMode mode, float brightness) const
{
    // Gate: Green, Toggle: Blue, Trigger: Orange
    switch (mode)
    {
        case ButtonMode::Gate:
            return ImColor::HSV(0.33f, brightness, brightness).Value;  // Green
        case ButtonMode::Toggle:
            return ImColor::HSV(0.60f, brightness, brightness).Value;  // Blue
        case ButtonMode::Trigger:
            return ImColor::HSV(0.08f, brightness, brightness).Value;  // Orange
        default:
            return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    }
}

void MIDIButtonsModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // === HEADER SECTION ===
    if (numButtonsParam)
    {
        int numButtons = numButtonsParam->get();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##numbuttons", &numButtons, 1, MAX_BUTTONS))
        {
            *numButtonsParam = numButtons;
            onModificationEnded();
        }
        ImGui::SameLine();
        ImGui::Text("Buttons");
        ImGui::SameLine();
        HelpMarker("Number of active buttons (1-32). Drag to adjust.");
    }
    
    // View mode selector
    ImGui::Spacing();
    if (ImGui::RadioButton("Visual", viewMode == ViewMode::Visual)) viewMode = ViewMode::Visual;
    ImGui::SameLine();
    if (ImGui::RadioButton("Compact", viewMode == ViewMode::Compact)) viewMode = ViewMode::Compact;
    ImGui::SameLine();
    if (ImGui::RadioButton("Table", viewMode == ViewMode::Table)) viewMode = ViewMode::Table;
    ImGui::SameLine();
    HelpMarker("Visual: Button grid with mode colors\nCompact: Linear list view\nTable: Detailed table view\n\nColors: Green=Gate, Blue=Toggle, Orange=Trigger");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === DRAW SELECTED VIEW ===
    int numActive = numButtonsParam ? numButtonsParam->get() : MAX_BUTTONS;
    
    switch (viewMode)
    {
        case ViewMode::Visual:
            drawVisualButtons(numActive, onModificationEnded);
            break;
        case ViewMode::Compact:
            drawCompactList(numActive, onModificationEnded);
            break;
        case ViewMode::Table:
            drawTableView(numActive, onModificationEnded);
            break;
    }
    
    ImGui::PopItemWidth();
}

void MIDIButtonsModuleProcessor::drawVisualButtons(int numActive, const std::function<void()>& onModificationEnded)
{
    // Draw buttons in grid (8 per row)
    const int buttonsPerRow = 8;
    const float buttonSize = 32.0f;
    const float spacing = 4.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    
    for (int row = 0; row < (numActive + buttonsPerRow - 1) / buttonsPerRow; ++row)
    {
        if (row > 0) ImGui::Spacing();
        
        for (int col = 0; col < buttonsPerRow; ++col)
        {
            int i = row * buttonsPerRow + col;
            if (i >= numActive) break;
            
            if (col > 0) ImGui::SameLine();
            
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            bool hasMapping = (map.midiCC != -1);
            
            // Button colors based on state
            ImVec4 color, colorHovered, colorActive;
            
            // Override with orange if learning
            if (learningIndex == i)
            {
                color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
                colorHovered = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
                colorActive = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
            }
            // Dimmed gray for unassigned (but still clickable!)
            else if (!hasMapping)
            {
                color = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);       // Dark gray
                colorHovered = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Lighter on hover
                colorActive = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Even lighter when clicked
            }
            // Highlight if button is ON
            else if (map.currentValue > 0.5f)
            {
                color = getModeColor(map.mode, 0.9f);
                colorHovered = getModeColor(map.mode, 1.0f);
                colorActive = getModeColor(map.mode, 1.0f);
            }
            // Normal assigned button
            else
            {
                color = getModeColor(map.mode, 0.6f);
                colorHovered = getModeColor(map.mode, 0.7f);
                colorActive = getModeColor(map.mode, 0.8f);
            }
            
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorActive);
            
            // Button label showing number
            char label[16];
            snprintf(label, sizeof(label), "%d##btn", i + 1);
            
            // Left-click enters learn mode (ALWAYS clickable, even if unassigned!)
            if (ImGui::Button(label, ImVec2(buttonSize, buttonSize)))
            {
                learningIndex = i;
            }
            
            // Tooltips
            if (learningIndex == i)
            {
                ImGui::SetTooltip("Learning Button %d...\nPress a MIDI button to assign", i + 1);
            }
            else if (ImGui::IsItemHovered())
            {
                if (hasMapping)
                {
                    const char* modeName = map.mode == ButtonMode::Gate ? "Gate" :
                                           map.mode == ButtonMode::Toggle ? "Toggle" : "Trigger";
                    ImGui::SetTooltip("Button %d\nCC: %d\nMode: %s\nValue: %.1f\n\nClick to learn new CC",
                                      i + 1, map.midiCC, modeName, map.currentValue);
                }
                else
                {
                    ImGui::SetTooltip("Button %d\nNo MIDI CC assigned\n\nClick to learn CC", i + 1);
                }
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
    }
    
    ImGui::PopStyleVar();
    
    // Learning and mode controls below buttons
    if (learningIndex >= 0 && learningIndex < numActive)
    {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Learning Button %d...", learningIndex + 1);
        if (ImGui::Button("Cancel Learning##cancel", ImVec2(150, 0)))
            learningIndex = -1;
        ImGui::SameLine();
        
        // Mode selector while learning
        auto& map = mappings[learningIndex];
        const char* modes[] = { "Gate", "Toggle", "Trigger" };
        int currentMode = (int)map.mode;
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##learnmode", &currentMode, modes, 3))
        {
            map.mode = (ButtonMode)currentMode;
            onModificationEnded();
        }
    }
    else
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Click a button to learn its MIDI CC");
    }
}

void MIDIButtonsModuleProcessor::drawCompactList(int numActive, const std::function<void()>& onModificationEnded)
{
    ImGui::TextDisabled("Click 'Learn' then press a MIDI button/pad");
    ImGui::Spacing();
    
    for (int i = 0; i < numActive; ++i)
    {
        auto& map = mappings[i];
        ImGui::PushID(i);
        
        // Button indicator with mode color
        ImVec4 color = getModeColor(map.mode, 0.8f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Text(map.currentValue > 0.5f ? "[X]" : "[ ]");
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        ImGui::Text("B%d", i + 1);
        ImGui::SameLine();
        ImGui::Text("CC:%3d", map.midiCC != -1 ? map.midiCC : 0);
        if (map.midiCC == -1)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(unassigned)");
        }
        
        ImGui::SameLine();
        
        // Learn button
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
        
        // Mode combo
        ImGui::SameLine();
        const char* modes[] = { "Gate", "Toggle", "Trigger" };
        int currentMode = (int)map.mode;
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##mode", &currentMode, modes, 3))
        {
            map.mode = (ButtonMode)currentMode;
            onModificationEnded();
        }
        
        ImGui::PopID();
    }
}

void MIDIButtonsModuleProcessor::drawTableView(int numActive, const std::function<void()>& onModificationEnded)
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
    
    if (ImGui::BeginTable("##buttons_table", 5, flags, ImVec2(0, tableHeight)))
    {
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("CC", ImGuiTableColumnFlags_WidthFixed, 35);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Learn", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        
        for (int i = 0; i < numActive; ++i)
        {
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            ImGui::TableNextRow();
            
            // Column 0: Button number
            ImGui::TableNextColumn();
            ImVec4 color = getModeColor(map.mode, 0.9f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("Button %d", i + 1);
            ImGui::PopStyleColor();
            
            // Column 1: CC number
            ImGui::TableNextColumn();
            if (map.midiCC != -1)
                ImGui::Text("%d", map.midiCC);
            else
                ImGui::TextDisabled("--");
            
            // Column 2: State
            ImGui::TableNextColumn();
            ImGui::Text(map.currentValue > 0.5f ? "ON" : "OFF");
            
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
            
            // Column 4: Mode
            ImGui::TableNextColumn();
            const char* modes[] = { "Gate", "Toggle", "Trigger" };
            int currentMode = (int)map.mode;
            ImGui::PushItemWidth(-1);
            if (ImGui::Combo("##mode", &currentMode, modes, 3))
            {
                map.mode = (ButtonMode)currentMode;
                onModificationEnded();
            }
            ImGui::PopItemWidth();
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void MIDIButtonsModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    juce::ignoreUnused(helpers);
}
#endif

