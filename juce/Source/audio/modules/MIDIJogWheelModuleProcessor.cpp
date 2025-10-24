#include "MIDIJogWheelModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MIDIJogWheelModuleProcessor::createParameterLayout()
{
    return juce::AudioProcessorValueTreeState::ParameterLayout();
}

MIDIJogWheelModuleProcessor::MIDIJogWheelModuleProcessor()
    : ModuleProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "MIDIJogWheelParams", createParameterLayout())
{
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MIDIJogWheelModuleProcessor::prepareToPlay(double, int)
{
    isLearning = false;  // Reset learn state
}

void MIDIJogWheelModuleProcessor::releaseResources()
{
}

void MIDIJogWheelModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Process incoming MIDI CC messages
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (!msg.isController()) continue;
        
        int ccNumber = msg.getControllerNumber();
        float ccValue = msg.getControllerValue() / 127.0f;
        
        // Handle MIDI Learn
        if (isLearning)
        {
            midiCC = ccNumber;
            isLearning = false;
        }
        
        // Update jog wheel value
        if (midiCC == ccNumber)
        {
            currentValue = juce::jmap(ccValue, minVal, maxVal);
        }
    }
    
    // Write current value to output buffer
    buffer.setSample(0, 0, currentValue);
    
    // Fill rest of block with same value
    if (buffer.getNumSamples() > 1)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0) + 1, currentValue, buffer.getNumSamples() - 1);
    
    lastOutputValues[0]->store(currentValue);
}

juce::ValueTree MIDIJogWheelModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIJogWheelState");
    vt.setProperty("cc", midiCC, nullptr);
    vt.setProperty("min", minVal, nullptr);
    vt.setProperty("max", maxVal, nullptr);
    return vt;
}

void MIDIJogWheelModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIJogWheelState"))
    {
        midiCC = vt.getProperty("cc", -1);
        minVal = vt.getProperty("min", 0.0f);
        maxVal = vt.getProperty("max", 1.0f);
    }
}

std::vector<DynamicPinInfo> MIDIJogWheelModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    pins.push_back(DynamicPinInfo("Value", 0, PinDataType::CV));
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

void MIDIJogWheelModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // === HEADER ===
    ImGui::Text("MIDI Jog Wheel");
    ImGui::SameLine();
    HelpMarker("Single rotary control for MIDI CC input.\nPerfect for jog wheels, endless encoders, or rotary knobs.");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === STATUS ===
    if (midiCC != -1)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));  // Light green
        ImGui::Text("Assigned to CC %d", midiCC);
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("No MIDI CC assigned");
    }
    
    ImGui::Spacing();
    
    // === VISUAL DISPLAY ===
    // Endless rotation indicator (jog wheels rotate infinitely, no fixed start/end)
    float normalizedValue = (maxVal != minVal) ? 
        (currentValue - minVal) / (maxVal - minVal) : 0.0f;
    
    // For endless rotation, we wrap around [0, 1] instead of clamping
    normalizedValue = normalizedValue - floorf(normalizedValue);  // Keep fractional part only
    
    // Define canvas size - use itemWidth to constrain it
    const float canvasWidth = juce::jmin(itemWidth, 160.0f);
    const ImVec2 canvasSize = ImVec2(canvasWidth, canvasWidth);
    
    // Get screen position BEFORE any widget (critical for correct positioning)
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
    ImVec2 center = ImVec2(canvasP0.x + canvasSize.x * 0.5f, canvasP0.y + canvasSize.y * 0.5f);
    
    // Get draw list and set up clipping (following imgui_demo.cpp pattern)
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasP0, canvasP1, true);
    
    // Draw background rectangle
    drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(30, 30, 35, 255));
    
    // Draw circular rotation indicator
    float radius = canvasSize.x * 0.35f;  // Scale with canvas size
    
    // Full circle outline (no start/end - it's endless!)
    ImU32 circleColor = isLearning ? 
        IM_COL32(255, 127, 0, 255) :  // Orange when learning
        IM_COL32(80, 80, 80, 255);     // Gray normally
    drawList->AddCircle(center, radius, circleColor, 64, 2.0f);
    
    // Rotating indicator "hand" pointing to current position
    // Angle: 0 = top (12 o'clock), rotates clockwise
    float indicatorAngle = normalizedValue * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
    
    ImU32 indicatorColor = isLearning ? 
        IM_COL32(255, 127, 0, 255) :  // Orange when learning
        IM_COL32(76, 178, 255, 255);   // Blue normally
    
    // Draw indicator line from center to edge
    ImVec2 indicatorEnd = ImVec2(
        center.x + cosf(indicatorAngle) * radius,
        center.y + sinf(indicatorAngle) * radius
    );
    drawList->AddLine(center, indicatorEnd, indicatorColor, 4.0f);
    
    // Draw indicator dot at the end
    drawList->AddCircleFilled(indicatorEnd, 6.0f, indicatorColor);
    
    // Center dot
    drawList->AddCircleFilled(center, 5.0f, indicatorColor);
    
    // Value text in center
    char valueText[32];
    snprintf(valueText, sizeof(valueText), "%.2f", currentValue);
    ImVec2 textSize = ImGui::CalcTextSize(valueText);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                      IM_COL32(255, 255, 255, 255), valueText);
    
    // Pop clipping
    drawList->PopClipRect();
    
    // Reserve space with InvisibleButton (AFTER drawing, following imgui_demo.cpp)
    ImGui::InvisibleButton("##jogwheel_canvas", canvasSize);
    
    ImGui::Spacing();
    
    // === CONTROLS ===
    // Learn button (use itemWidth to prevent infinite scaling)
    if (isLearning)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        if (ImGui::Button("Learning... (click to cancel)##btn", ImVec2(itemWidth, 0)))
            isLearning = false;
        ImGui::PopStyleColor(2);
        ImGui::TextDisabled("Move your jog wheel or rotary encoder...");
    }
    else
    {
        if (ImGui::Button("Learn MIDI CC##btn", ImVec2(itemWidth, 0)))
            isLearning = true;
    }
    
    ImGui::Spacing();
    
    // Range controls
    ImGui::Text("Output Range:");
    ImGui::SetNextItemWidth(itemWidth);
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
    if (ImGui::DragFloatRange2("##range", &minVal, &maxVal, 0.01f, -10.0f, 10.0f,
                                "Min: %.2f", "Max: %.2f", flags))
    {
        onModificationEnded();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag to adjust output range.\nIncoming MIDI values (0-127) will be mapped to this range.");
    
    // Quick range presets
    ImGui::Spacing();
    ImGui::TextDisabled("Presets:");
    const float presetBtnWidth = (itemWidth - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    if (ImGui::Button("0 to 1##preset1", ImVec2(presetBtnWidth, 0))) { minVal = 0.0f; maxVal = 1.0f; onModificationEnded(); }
    ImGui::SameLine();
    if (ImGui::Button("-1 to 1##preset2", ImVec2(presetBtnWidth, 0))) { minVal = -1.0f; maxVal = 1.0f; onModificationEnded(); }
    ImGui::SameLine();
    if (ImGui::Button("0 to 10##preset3", ImVec2(presetBtnWidth, 0))) { minVal = 0.0f; maxVal = 10.0f; onModificationEnded(); }
    
    ImGui::PopItemWidth();
}

void MIDIJogWheelModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    juce::ignoreUnused(helpers);
}
#endif

