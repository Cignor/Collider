#include "SnapshotSequencerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/theme/ThemeManager.h"
#endif

SnapshotSequencerModuleProcessor::SnapshotSequencerModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)), // Dummy output
      apvts (*this, nullptr, "SnapshotSeqParams", createParameterLayout())
{
    numStepsParam = apvts.getRawParameterValue ("numSteps");
    
    // Initialize empty snapshots
    for (auto& snapshot : snapshots)
    {
        snapshot.reset();
    }
    
    // Inspector value tracking (no outputs to track)
    lastOutputValues.clear();
}

juce::AudioProcessorValueTreeState::ParameterLayout SnapshotSequencerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back (std::make_unique<juce::AudioParameterInt> ("numSteps", "Num Steps", 1, MAX_STEPS, 8));
    
    return { params.begin(), params.end() };
}

std::optional<RhythmInfo> SnapshotSequencerModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;
    
    // Build display name with logical ID
    info.displayName = "Snapshot Seq #" + juce::String(getLogicalId());
    info.sourceType = "snapshot_sequencer";
    
    // Snapshot Sequencer is clock-driven (no internal rate)
    // It advances steps based on external clock input, so we can't determine
    // BPM without analyzing the clock signal. For now, we'll indicate it's
    // active but BPM is unknown (0.0f).
    
    // Read LIVE transport state to check if system is playing
    TransportState transport;
    bool hasTransport = false;
    if (getParent())
    {
        transport = getParent()->getTransportState();
        hasTransport = true;
    }
    
    // Snapshot Sequencer is not synced to transport (it's clock-driven)
    info.isSynced = false;
    
    // Consider it active if transport is playing (clock might be running)
    // In practice, it's active whenever clock input is connected and triggering
    info.isActive = hasTransport ? transport.isPlaying : true;
    
    // BPM is unknown - depends on external clock source
    // Could be enhanced in the future to analyze clock input frequency
    info.bpm = 0.0f;
    
    return info;
}

void SnapshotSequencerModuleProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    currentStep.store(0);
    lastClockHigh = false;
    lastResetHigh = false;
}

void SnapshotSequencerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    if (numStepsParam == nullptr)
    {
        buffer.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numSteps = juce::jlimit(1, MAX_STEPS, (int)numStepsParam->load());
    
    // Get clock and reset inputs
    const bool hasClockInput = buffer.getNumChannels() > 0;
    const bool hasResetInput = buffer.getNumChannels() > 1;
    
    const float* clockIn = hasClockInput ? buffer.getReadPointer(0) : nullptr;
    const float* resetIn = hasResetInput ? buffer.getReadPointer(1) : nullptr;
    
    // Process sample by sample to detect triggers
    bool clockDetected = false;
    bool resetDetected = false;
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Check for reset trigger (rising edge above 0.5V)
        if (resetIn != nullptr)
        {
            bool resetHigh = resetIn[i] > 0.5f;
            if (resetHigh && !lastResetHigh)
            {
                // Reset trigger detected
                currentStep.store(0);
                resetDetected = true;
                
                // Load snapshot for step 0 if it exists
                if (isSnapshotStored(0) && commandBus != nullptr && parentVoiceId != 0)
                {
                    Command cmd;
                    cmd.type = Command::Type::LoadPatchState;
                    cmd.voiceId = parentVoiceId;
                    cmd.patchState = snapshots[0];
                    commandBus->enqueue(cmd);
                    juce::Logger::writeToLog("[SnapshotSeq] Reset: Loading snapshot for step 0");
                }
            }
            lastResetHigh = resetHigh;
        }
        
        // Check for clock trigger (rising edge above 0.5V)
        if (clockIn != nullptr)
        {
            bool clockHigh = clockIn[i] > 0.5f;
            if (clockHigh && !lastClockHigh)
            {
                // Clock trigger detected - advance to next step
                int oldStep = currentStep.load();
                int newStep = (oldStep + 1) % numSteps;
                currentStep.store(newStep);
                clockDetected = true;
                
                juce::Logger::writeToLog("[SnapshotSeq] Step " + juce::String(oldStep) + " -> " + juce::String(newStep));
                
                // Load snapshot for the new step if it exists
                if (isSnapshotStored(newStep) && commandBus != nullptr && parentVoiceId != 0)
                {
                    Command cmd;
                    cmd.type = Command::Type::LoadPatchState;
                    cmd.voiceId = parentVoiceId;
                    cmd.patchState = snapshots[newStep];
                    commandBus->enqueue(cmd);
                    juce::Logger::writeToLog("[SnapshotSeq] Loading snapshot for step " + juce::String(newStep));
                }
            }
            lastClockHigh = clockHigh;
        }
    }
    
#if defined(PRESET_CREATOR_UI)
    // Update visualization data (thread-safe)
    vizData.currentStep.store(currentStep.load());
    vizData.clockActive.store(clockDetected);
    vizData.resetActive.store(resetDetected);
#endif
    
    // Clear output buffer (this module has no audio output)
    buffer.clear();
}

void SnapshotSequencerModuleProcessor::setSnapshotForStep(int stepIndex, const juce::MemoryBlock& state)
{
    if (stepIndex >= 0 && stepIndex < MAX_STEPS)
    {
        snapshots[stepIndex] = state;
        juce::Logger::writeToLog("[SnapshotSeq] Stored snapshot for step " + juce::String(stepIndex) + 
                                " (size: " + juce::String(state.getSize()) + " bytes)");
    }
}

const juce::MemoryBlock& SnapshotSequencerModuleProcessor::getSnapshotForStep(int stepIndex) const
{
    static juce::MemoryBlock emptyBlock;
    if (stepIndex >= 0 && stepIndex < MAX_STEPS)
        return snapshots[stepIndex];
    return emptyBlock;
}

void SnapshotSequencerModuleProcessor::clearSnapshotForStep(int stepIndex)
{
    if (stepIndex >= 0 && stepIndex < MAX_STEPS)
    {
        snapshots[stepIndex].reset();
        juce::Logger::writeToLog("[SnapshotSeq] Cleared snapshot for step " + juce::String(stepIndex));
    }
}

bool SnapshotSequencerModuleProcessor::isSnapshotStored(int stepIndex) const
{
    if (stepIndex >= 0 && stepIndex < MAX_STEPS)
        return snapshots[stepIndex].getSize() > 0;
    return false;
}

juce::ValueTree SnapshotSequencerModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree tree("SnapshotSeqState");
    
    // Save each snapshot as Base64-encoded string
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        if (snapshots[i].getSize() > 0)
        {
            juce::ValueTree stepTree("Step");
            stepTree.setProperty("index", i, nullptr);
            stepTree.setProperty("data", snapshots[i].toBase64Encoding(), nullptr);
            tree.appendChild(stepTree, nullptr);
        }
    }
    
    return tree;
}

void SnapshotSequencerModuleProcessor::setExtraStateTree(const juce::ValueTree& tree)
{
    if (!tree.hasType("SnapshotSeqState"))
        return;
    
    // Clear all snapshots first
    for (auto& snapshot : snapshots)
        snapshot.reset();
    
    // Load snapshots from tree
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto stepTree = tree.getChild(i);
        if (stepTree.hasType("Step"))
        {
            int index = stepTree.getProperty("index", -1);
            juce::String dataStr = stepTree.getProperty("data", "").toString();
            
            if (index >= 0 && index < MAX_STEPS && dataStr.isNotEmpty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding(dataStr))
                {
                    snapshots[index] = mb;
                    juce::Logger::writeToLog("[SnapshotSeq] Restored snapshot for step " + juce::String(index) +
                                           " (size: " + juce::String(mb.getSize()) + " bytes)");
                }
            }
        }
    }
}

#if defined(PRESET_CREATOR_UI)
void SnapshotSequencerModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded);
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    ImGui::PushItemWidth(itemWidth);
    
    // Number of steps parameter
    if (auto* param = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numSteps")))
    {
        int steps = param->get();
        if (ImGui::SliderInt("Steps", &steps, 1, MAX_STEPS))
        {
            param->beginChangeGesture();
            *param = steps;
            param->endChangeGesture();
        }
    }
    
    ImGui::Spacing();
    ThemeText("Step Sequence", theme.text.section_header);
    ImGui::Spacing();
    
    ImGui::PushID(this); // Unique ID for this node's UI
    
    // Read visualization data (thread-safe) - BEFORE BeginChild
    const int currentStepIndex = vizData.currentStep.load();
    const bool clockActive = vizData.clockActive.load();
    const bool resetActive = vizData.resetActive.load();
    const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    
    // Read snapshot states
    bool stepStored[MAX_STEPS];
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        stepStored[i] = isSnapshotStored(i);
    }
    
    // Step grid visualization in child window
    const float stepGridHeight = 180.0f;
    const ImVec2 graphSize(itemWidth, stepGridHeight);
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    if (ImGui::BeginChild("SnapshotSeqViz", graphSize, false, childFlags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        // Calculate step grid layout (4 columns max)
        const int cols = 4;
        const int rows = (numSteps + cols - 1) / cols;
        const float stepWidth = (graphSize.x - 20.0f) / cols;
        const float stepHeight = (graphSize.y - 40.0f) / rows;
        const float stepSpacing = 4.0f;
        const float stepSize = juce::jmin(stepWidth - stepSpacing, stepHeight - stepSpacing);
        
        // Colors
        const ImU32 stepEmptyColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        const ImU32 stepStoredColor = ImGui::ColorConvertFloat4ToU32(theme.accent);
        const ImU32 stepCurrentColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.amplitude);
        const ImU32 stepBorderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
        
        // Draw step grid
        for (int i = 0; i < numSteps; ++i)
        {
            const int col = i % cols;
            const int row = i / cols;
            const float x = p0.x + 10.0f + col * stepWidth;
            const float y = p0.y + 30.0f + row * stepHeight;
            const float centerX = x + stepWidth * 0.5f;
            const float centerY = y + stepHeight * 0.5f;
            
            const bool isCurrent = (i == currentStepIndex);
            const bool isStored = stepStored[i];
            
            // Step background
            ImU32 fillColor = stepEmptyColor;
            if (isCurrent)
                fillColor = stepCurrentColor;
            else if (isStored)
                fillColor = stepStoredColor;
            
            const float rectX = centerX - stepSize * 0.5f;
            const float rectY = centerY - stepSize * 0.5f;
            const float rectX2 = centerX + stepSize * 0.5f;
            const float rectY2 = centerY + stepSize * 0.5f;
            
            drawList->AddRectFilled(ImVec2(rectX, rectY), ImVec2(rectX2, rectY2), fillColor, 2.0f);
            drawList->AddRect(ImVec2(rectX, rectY), ImVec2(rectX2, rectY2), stepBorderColor, 2.0f, 0, 1.5f);
            
            // Step number text
            const juce::String stepNum = juce::String(i + 1);
            const ImVec2 textSize = ImGui::CalcTextSize(stepNum.toRawUTF8());
            drawList->AddText(ImVec2(centerX - textSize.x * 0.5f, centerY - textSize.y * 0.5f), textColor, stepNum.toRawUTF8());
        }
        
        // Clock/Reset activity indicators
        if (clockActive || resetActive)
        {
            const float indicatorY = p0.y + 8.0f;
            if (clockActive)
            {
                const ImU32 clockColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                drawList->AddCircleFilled(ImVec2(p0.x + 12.0f, indicatorY), 4.0f, clockColor);
            }
            if (resetActive)
            {
                const ImU32 resetColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                drawList->AddCircleFilled(ImVec2(p0.x + 24.0f, indicatorY), 4.0f, resetColor);
            }
        }
        
        drawList->PopClipRect();
        
        // Info overlay
        ImGui::SetCursorPos(ImVec2(4, 4));
        if (clockActive || resetActive)
        {
            if (clockActive)
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "CLK");
            if (resetActive)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "RST");
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.6f), "Step %d/%d", currentStepIndex + 1, numSteps);
        }
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##snapshotSeqVizDrag", graphSize);
    }
    ImGui::EndChild(); // CRITICAL: Must be OUTSIDE the if block!
    
    ImGui::PopID(); // End unique ID
    
    ImGui::Spacing();
    
    // Step status list (compact)
    for (int i = 0; i < numSteps; ++i)
    {
        ImGui::PushID(i);
        
        const juce::String label = "Step " + juce::String(i + 1) + ":";
        if (i == currentStepIndex)
            ThemeText(label.toRawUTF8(), theme.text.section_header);
        else
            ThemeText(label.toRawUTF8());

        ImGui::SameLine();
        
        bool stored = stepStored[i];
        
        // Status indicator
        if (stored)
        {
            ThemeText("[STORED]", theme.text.success);
        }
        else
        {
            ThemeText("[EMPTY]", theme.text.disabled);
        }
        
        // Note: Capture and Clear buttons are handled by the ImGuiNodeEditorComponent
        // since it needs access to the synth's getStateInformation method
        
        ImGui::PopID();
    }
    
    ImGui::Spacing();
    ImGui::TextWrapped("Connect a clock to advance steps. Each step can store a complete patch state.");
    ImGui::PopItemWidth();
}
#endif

