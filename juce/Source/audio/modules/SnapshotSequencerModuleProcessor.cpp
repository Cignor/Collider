#include "SnapshotSequencerModuleProcessor.h"

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
    ImGui::PushID(this);
    
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
    
    ThemeText("Snapshots:", theme.modules.sequencer_section_header);
    
    // Display current step
    int currentStepIndex = currentStep.load();
    ImGui::Text("Current Step: %d", currentStepIndex + 1);
    
    
    // Draw snapshot slots
    const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    
    for (int i = 0; i < numSteps; ++i)
    {
        ImGui::PushID(i);
        
        const juce::String label = "Step " + juce::String(i + 1) + ":";
        if (i == currentStepIndex)
            ThemeText(label.toRawUTF8(), theme.text.section_header);
        else
            ThemeText(label.toRawUTF8());

        ImGui::SameLine();
        
        bool stored = isSnapshotStored(i);
        
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
    
    ImGui::TextWrapped("Connect a clock to advance steps. Each step can store a complete patch state.");
    ImGui::PopItemWidth();
    ImGui::PopID();
}
#endif

