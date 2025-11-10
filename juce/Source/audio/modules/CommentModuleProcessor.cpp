#include "CommentModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include <imnodes.h>
#endif

CommentModuleProcessor::CommentModuleProcessor()
    : ModuleProcessor(BusesProperties()), // No audio inputs or outputs
      apvts(*this, nullptr, "CommentParams", createParameterLayout())
{
    // Initialize text buffers
    snprintf(titleBuffer, sizeof(titleBuffer), "Comment");
    textBuffer[0] = '\0';
}

juce::AudioProcessorValueTreeState::ParameterLayout CommentModuleProcessor::createParameterLayout() {
    return {}; // No audio parameters are needed
}

void CommentModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    buffer.clear(); // This module produces no sound
}

// Save the comment's content and dimensions to the preset file
juce::ValueTree CommentModuleProcessor::getExtraStateTree() const {
    juce::ValueTree vt("CommentState");
    vt.setProperty("title", juce::String(titleBuffer), nullptr);
    vt.setProperty("text", juce::String(textBuffer), nullptr);
    vt.setProperty("width", nodeWidth, nullptr);
    vt.setProperty("height", nodeHeight, nullptr);
    return vt;
}

// Load the comment's content and dimensions from a preset file
void CommentModuleProcessor::setExtraStateTree(const juce::ValueTree& vt) {
    if (vt.hasType("CommentState")) {
        strncpy(titleBuffer, vt.getProperty("title", "Comment").toString().toRawUTF8(), sizeof(titleBuffer) - 1);
        strncpy(textBuffer, vt.getProperty("text", "").toString().toRawUTF8(), sizeof(textBuffer) - 1);
        nodeWidth = (float)vt.getProperty("width", 250.0);
        nodeHeight = (float)vt.getProperty("height", 150.0);
        // Note: Node dimensions will be applied in drawParametersInNode when the UI is rendered
    }
}

#if defined(PRESET_CREATOR_UI)
void CommentModuleProcessor::drawParametersInNode(float, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    // Clamp to reasonable bounds (defensive)
    nodeWidth = juce::jlimit(150.0f, 800.0f, nodeWidth);
    nodeHeight = juce::jlimit(100.0f, 600.0f, nodeHeight);

    // Draw content in a child with our exact size
    ImGui::BeginChild("CommentContent", ImVec2(nodeWidth, nodeHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Title Input - ensure null termination
    titleBuffer[sizeof(titleBuffer) - 1] = '\0';
    if (ImGui::InputText("##title", titleBuffer, sizeof(titleBuffer)))
    {
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    }

    // Body Input - ensure null termination and use our exact size
    textBuffer[sizeof(textBuffer) - 1] = '\0';
    const ImVec2 textAreaSize(nodeWidth - 16.0f, nodeHeight - 70.0f);
    if (ImGui::InputTextMultiline("##text", textBuffer, sizeof(textBuffer), textAreaSize))
    {
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    }

    // Draw resize handle in bottom-right corner
    const ImVec2 resizeHandleSize(16.0f, 16.0f);
    ImVec2 drawPos = ImGui::GetCursorScreenPos();
    drawPos.x += ImGui::GetContentRegionAvail().x - resizeHandleSize.x;
    drawPos.y += ImGui::GetContentRegionAvail().y - resizeHandleSize.y;

    ImGui::SetCursorScreenPos(drawPos);
    ImGui::InvisibleButton("##resize", resizeHandleSize);
    const bool isResizing = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

    if (isResizing)
    {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        nodeWidth = juce::jlimit(150.0f, 800.0f, nodeWidth + delta.x);
        nodeHeight = juce::jlimit(100.0f, 600.0f, nodeHeight + delta.y);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        wasBeingResizedLastFrame = true;
    }
    else if (wasBeingResizedLastFrame)
    {
        // Just finished resizing, trigger undo snapshot
        wasBeingResizedLastFrame = false;
        onModificationEnded();
    }

    // Draw resize handle indicator
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(drawPos.x + 4.0f, drawPos.y + resizeHandleSize.y - 4.0f),
        ImVec2(drawPos.x + resizeHandleSize.x - 4.0f, drawPos.y + resizeHandleSize.y - 4.0f),
        ImVec2(drawPos.x + resizeHandleSize.x - 4.0f, drawPos.y + 4.0f),
        ImGui::GetColorU32(ImGuiCol_ResizeGrip));

    // <<< FIX 1: Satisfy the assertion by forcing ImGui to update its boundaries >>>
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    // <<< FIX 2: Correctly end the child window we started >>>
    ImGui::EndChild();

}
#endif
