#include "MIDIFadersModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MIDIFadersModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterInt>("numFaders", "Number of Faders", 1, MAX_FADERS, 8));
    return layout;
}

MIDIFadersModuleProcessor::MIDIFadersModuleProcessor()
    : ModuleProcessor(BusesProperties().withOutput("Outputs", juce::AudioChannelSet::discreteChannels(MAX_FADERS), true)),
      apvts(*this, nullptr, "MIDIFadersParams", createParameterLayout())
{
    numFadersParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numFaders"));
    
    for (int i = 0; i < MAX_FADERS; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MIDIFadersModuleProcessor::prepareToPlay(double, int)
{
    learningIndex = -1;  // Reset learn state
}

void MIDIFadersModuleProcessor::releaseResources()
{
}

void MIDIFadersModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    int numActive = numFadersParam ? numFadersParam->get() : MAX_FADERS;
    
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
        
        // Update mapped faders
        for (int i = 0; i < numActive; ++i)
        {
            if (mappings[i].midiCC == ccNumber)
            {
                mappings[i].currentValue = juce::jmap(ccValue, mappings[i].minVal, mappings[i].maxVal);
            }
        }
    }
    
    // Write current values to output buffer
    for (int i = 0; i < MAX_FADERS; ++i)
    {
        float val = (i < numActive) ? mappings[i].currentValue : 0.0f;
        buffer.setSample(i, 0, val);
        
        // Fill rest of block with same value
        if (buffer.getNumSamples() > 1)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(i) + 1, val, buffer.getNumSamples() - 1);
        
        lastOutputValues[i]->store(val);
    }
}

juce::ValueTree MIDIFadersModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIFadersState");
    for (int i = 0; i < MAX_FADERS; ++i)
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

void MIDIFadersModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIFadersState"))
    {
        for (const auto& child : vt)
        {
            if (child.hasType("Mapping"))
            {
                int index = child.getProperty("index", -1);
                if (index >= 0 && index < MAX_FADERS)
                {
                    mappings[index].midiCC = child.getProperty("cc", -1);
                    mappings[index].minVal = child.getProperty("min", 0.0f);
                    mappings[index].maxVal = child.getProperty("max", 1.0f);
                }
            }
        }
    }
}

std::vector<DynamicPinInfo> MIDIFadersModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    int numActive = numFadersParam ? numFadersParam->get() : MAX_FADERS;
    
    for (int i = 0; i < numActive; ++i)
    {
        juce::String label = "Fader " + juce::String(i + 1);
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

void MIDIFadersModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // === HEADER SECTION ===
    // (No SeparatorText - node already has title bar, and it extends beyond bounds)
    
    // Number of faders control
    if (numFadersParam)
    {
        int numFaders = numFadersParam->get();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##numfaders", &numFaders, 1, MAX_FADERS))
        {
            *numFadersParam = numFaders;
            onModificationEnded();
        }
        ImGui::SameLine();
        ImGui::Text("Faders"); 
        ImGui::SameLine();
        HelpMarker("Number of active faders (1-16). Drag to adjust.");
    }
    
    // View mode selector
    ImGui::Spacing();
    if (ImGui::RadioButton("Visual", viewMode == ViewMode::Visual)) viewMode = ViewMode::Visual;
    ImGui::SameLine();
    if (ImGui::RadioButton("Compact", viewMode == ViewMode::Compact)) viewMode = ViewMode::Compact;
    ImGui::SameLine();
    if (ImGui::RadioButton("Table", viewMode == ViewMode::Table)) viewMode = ViewMode::Table;
    ImGui::SameLine();
    HelpMarker("Visual: Vertical sliders with color coding\nCompact: Linear list view\nTable: Detailed table view");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === DRAW SELECTED VIEW ===
    int numActive = numFadersParam ? numFadersParam->get() : MAX_FADERS;
    
    switch (viewMode)
    {
        case ViewMode::Visual:
            drawVisualFaders(numActive, onModificationEnded);
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

void MIDIFadersModuleProcessor::drawVisualFaders(int numActive, const std::function<void()>& onModificationEnded)
{
    // Draw vertical sliders in rows (8 per row)
    const int fadersPerRow = 8;
    const float faderWidth = 22.0f;
    const float faderHeight = 140.0f;
    const float spacing = 4.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    
    for (int row = 0; row < (numActive + fadersPerRow - 1) / fadersPerRow; ++row)
    {
        if (row > 0) ImGui::Spacing();
        
        ImGui::BeginGroup();
        
        // Draw faders in this row
        for (int col = 0; col < fadersPerRow; ++col)
        {
            int i = row * fadersPerRow + col;
            if (i >= numActive) break;
            
            if (col > 0) ImGui::SameLine();
            
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            // Color coding using HSV
            float hue = static_cast<float>(i) / static_cast<float>(MAX_FADERS);
            ImVec4 colorBg = ImColor::HSV(hue, 0.5f, 0.5f);
            ImVec4 colorHovered = ImColor::HSV(hue, 0.6f, 0.6f);
            ImVec4 colorActive = ImColor::HSV(hue, 0.7f, 0.7f);
            ImVec4 colorGrab = ImColor::HSV(hue, 0.9f, 0.9f);
            
            // Special color for learning state
            if (learningIndex == i)
            {
                colorBg = ImVec4(1.0f, 0.5f, 0.0f, 0.8f);      // Orange
                colorHovered = ImVec4(1.0f, 0.6f, 0.1f, 0.9f);
                colorActive = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
                colorGrab = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            }
            
            ImGui::PushStyleColor(ImGuiCol_FrameBg, colorBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, colorHovered);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, colorActive);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, colorGrab);
            
            // Vertical slider
            float displayValue = map.currentValue;
            bool hasMapping = (map.midiCC != -1);
            
            if (!hasMapping)
                ImGui::BeginDisabled();
            
            if (ImGui::VSliderFloat("##fader", ImVec2(faderWidth, faderHeight), &displayValue, 
                                     map.minVal, map.maxVal, ""))
            {
                // Manual control (not recommended for MIDI input, but allows testing)
                map.currentValue = displayValue;
            }
            
            // Tooltip showing value and CC
            if (ImGui::IsItemActive() || ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Fader %d\nCC: %d\nValue: %.3f\nRange: %.1f - %.1f", 
                                  i + 1, map.midiCC, map.currentValue, map.minVal, map.maxVal);
            }
            
            if (!hasMapping)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Fader %d\nNo MIDI CC assigned\nClick Learn button below", i + 1);
            }
            
            ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
        
        ImGui::EndGroup();
        
        // Draw labels and learn buttons below faders
        for (int col = 0; col < fadersPerRow; ++col)
        {
            int i = row * fadersPerRow + col;
            if (i >= numActive) break;
            
            if (col > 0) ImGui::SameLine();
            
            auto& map = mappings[i];
            ImGui::PushID(i + 1000); // Different ID space
            
            ImGui::BeginGroup();
            
            // Label with CC number
            if (map.midiCC != -1)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, 1.0f)); // Light green
                ImGui::Text("F%d", i + 1);
                ImGui::Text("CC%d", map.midiCC);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextDisabled("F%d", i + 1);
                ImGui::TextDisabled("---");
            }
            
            // Learn button (smaller for visual mode)
            if (learningIndex == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
                if (ImGui::Button("Stop##btn", ImVec2(faderWidth, 0)))
                    learningIndex = -1;
                ImGui::PopStyleColor(2);
            }
            else
            {
                if (ImGui::Button("Lrn##btn", ImVec2(faderWidth, 0)))
                    learningIndex = i;
            }
            
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
    
    ImGui::PopStyleVar();
}

void MIDIFadersModuleProcessor::drawCompactList(int numActive, const std::function<void()>& onModificationEnded)
{
    ImGui::TextDisabled("Click 'Learn' then move a MIDI control");
    ImGui::Spacing();
    
    for (int i = 0; i < numActive; ++i)
    {
        auto& map = mappings[i];
        ImGui::PushID(i);
        
        // Fader label with live value indicator
        float normalizedValue = (map.maxVal != map.minVal) ? 
            (map.currentValue - map.minVal) / (map.maxVal - map.minVal) : 0.0f;
        normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
        
        ImGui::Text("F%d", i + 1);
        ImGui::SameLine();
        
        // Value progress bar
        ImGui::SetNextItemWidth(60);
        float hue = static_cast<float>(i) / static_cast<float>(MAX_FADERS);
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

void MIDIFadersModuleProcessor::drawTableView(int numActive, const std::function<void()>& onModificationEnded)
{
    ImGui::TextDisabled("Detailed view with all parameters");
    ImGui::Spacing();
    
    // CRITICAL: NoHostExtendX requires NO ScrollX/ScrollY!
    // Solution: Use fixed outer height, no -FLT_MIN in cells
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit |     // Auto-fit columns to content
                            ImGuiTableFlags_NoHostExtendX |      // Don't extend beyond available width  
                            ImGuiTableFlags_Borders | 
                            ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_Resizable;
    
    // Fixed height outer size (NO ScrollY flag, just clip content)
    float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4;
    float tableHeight = rowHeight * (numActive + 1.5f);  // +1.5 for header + padding
    
    if (ImGui::BeginTable("##faders_table", 6, flags, ImVec2(0, tableHeight)))
    {
        // All columns use WidthFixed (required for NoHostExtendX to work)
        ImGui::TableSetupColumn("Fader", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("CC", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Learn", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableHeadersRow();
        
        // Draw rows
        for (int i = 0; i < numActive; ++i)
        {
            auto& map = mappings[i];
            ImGui::PushID(i);
            
            ImGui::TableNextRow();
            
            // Column 0: Fader number
            ImGui::TableNextColumn();
            float hue = static_cast<float>(i) / static_cast<float>(MAX_FADERS);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.8f, 1.0f).Value);
            ImGui::Text("Fader %d", i + 1);
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
            
            // Column 3: Learn button (NO -FLT_MIN, let column width control it)
            ImGui::TableNextColumn();
            if (learningIndex == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                if (ImGui::Button("Learning##btn"))  // ##btn ensures unique ID with PushID(i)
                    learningIndex = -1;
                ImGui::PopStyleColor();
            }
            else
            {
                if (ImGui::Button("Learn##btn"))  // ##btn ensures unique ID with PushID(i)
                    learningIndex = i;
            }
            
            // Column 4: Min value (NO SetNextItemWidth, let column control it)
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(-1);  // -1 = fill available width in THIS cell
            if (ImGui::DragFloat("##min", &map.minVal, 0.01f, -10.0f, map.maxVal, "%.1f"))
                onModificationEnded();
            ImGui::PopItemWidth();
            
            // Column 5: Max value (NO SetNextItemWidth, let column control it)
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(-1);  // -1 = fill available width in THIS cell
            if (ImGui::DragFloat("##max", &map.maxVal, 0.01f, map.minVal, 10.0f, "%.1f"))
                onModificationEnded();
            ImGui::PopItemWidth();
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void MIDIFadersModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Pins are drawn by the node editor automatically based on getIoPins()
    // This function is intentionally empty to avoid duplicate pin rendering
    juce::ignoreUnused(helpers);
}
#endif

