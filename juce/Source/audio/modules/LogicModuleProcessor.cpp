#include "LogicModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout LogicModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Operation selector: 0=AND, 1=OR, 2=XOR, 3=NOT A
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "operation", "Operation", juce::StringArray({"AND", "OR", "XOR", "NOT A"}), 0));

    // Gate threshold for interpreting gates
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    return {params.begin(), params.end()};
}

LogicModuleProcessor::LogicModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("In A", juce::AudioChannelSet::mono(), true)
                          .withInput("In B", juce::AudioChannelSet::mono(), true)
                          .withOutput("AND", juce::AudioChannelSet::mono(), true)
                          .withOutput("OR", juce::AudioChannelSet::mono(), true)
                          .withOutput("XOR", juce::AudioChannelSet::mono(), true)
                          .withOutput("NOT A", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "LogicParams", createParameterLayout())
{
    operationParam = apvts.getRawParameterValue("operation");
    gateThresholdParam = apvts.getRawParameterValue("gateThreshold");
    
    // Initialize lastOutputValues for cable inspector (4 outputs)
    for (int i = 0; i < 4; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void LogicModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);

#if defined(PRESET_CREATOR_UI)
    vizInputABuffer.setSize(1, vizBufferSize, false, true, true);
    vizInputBBuffer.setSize(1, vizBufferSize, false, true, true);
    vizAndOutputBuffer.setSize(1, vizBufferSize, false, true, true);
    vizOrOutputBuffer.setSize(1, vizBufferSize, false, true, true);
    vizXorOutputBuffer.setSize(1, vizBufferSize, false, true, true);
    vizNotAOutputBuffer.setSize(1, vizBufferSize, false, true, true);
    vizWritePos = 0;
    for (auto& v : vizData.inputAWaveform) v.store(0.0f);
    for (auto& v : vizData.inputBWaveform) v.store(0.0f);
    for (auto& v : vizData.andOutputWaveform) v.store(0.0f);
    for (auto& v : vizData.orOutputWaveform) v.store(0.0f);
    for (auto& v : vizData.xorOutputWaveform) v.store(0.0f);
    for (auto& v : vizData.notAOutputWaveform) v.store(0.0f);
    vizData.currentGateThreshold.store(0.5f);
    vizData.currentOperation.store(0);
    vizData.inputAState.store(false);
    vizData.inputBState.store(false);
    vizData.andState.store(false);
    vizData.orState.store(false);
    vizData.xorState.store(false);
    vizData.notAState.store(false);
#endif
}

void LogicModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inA = getBusBuffer(buffer, true, 0);  // Input A
    auto inB = getBusBuffer(buffer, true, 1);  // Input B
    auto outAND = getBusBuffer(buffer, false, 0);  // AND output
    auto outOR = getBusBuffer(buffer, false, 1);   // OR output
    auto outXOR = getBusBuffer(buffer, false, 2);  // XOR output
    auto outNOTA = getBusBuffer(buffer, false, 3); // NOT A output

    const int numSamples = buffer.getNumSamples();
    const float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    const float* aData = inA.getReadPointer(0);
    const float* bData = inB.getReadPointer(0);

    float* andData = outAND.getWritePointer(0);
    float* orData = outOR.getWritePointer(0);
    float* xorData = outXOR.getWritePointer(0);
    float* notAData = outNOTA.getWritePointer(0);

#if defined(PRESET_CREATOR_UI)
    // Capture input audio for visualization (before processing)
    const int samplesToCopy = juce::jmin(numSamples, vizBufferSize);
    if (vizInputABuffer.getNumSamples() > 0 && inA.getNumChannels() > 0)
    {
        for (int i = 0; i < samplesToCopy; ++i)
        {
            const int writeIdx = (vizWritePos + i) % vizBufferSize;
            vizInputABuffer.setSample(0, writeIdx, aData[i]);
        }
    }
    if (vizInputBBuffer.getNumSamples() > 0 && inB.getNumChannels() > 0)
    {
        for (int i = 0; i < samplesToCopy; ++i)
        {
            const int writeIdx = (vizWritePos + i) % vizBufferSize;
            vizInputBBuffer.setSample(0, writeIdx, bData[i]);
        }
    }
#endif

    // Process each sample
    bool lastAState = false;
    bool lastBState = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = aData[i] > gateThresh ? 1.0f : 0.0f;  // Gate threshold
        const float b = bData[i] > gateThresh ? 1.0f : 0.0f;  // Gate threshold

        // Perform all logical operations
        andData[i] = (a > 0.5f && b > 0.5f) ? 1.0f : 0.0f;   // A AND B
        orData[i] = (a > 0.5f || b > 0.5f) ? 1.0f : 0.0f;    // A OR B
        xorData[i] = ((a > 0.5f) != (b > 0.5f)) ? 1.0f : 0.0f; // A XOR B
        notAData[i] = (a > 0.5f) ? 0.0f : 1.0f;              // NOT A

#if defined(PRESET_CREATOR_UI)
        // Capture output audio for visualization (after processing)
        const int writeIdx = (vizWritePos + i) % vizBufferSize;
        if (vizAndOutputBuffer.getNumSamples() > 0)
            vizAndOutputBuffer.setSample(0, writeIdx, andData[i]);
        if (vizOrOutputBuffer.getNumSamples() > 0)
            vizOrOutputBuffer.setSample(0, writeIdx, orData[i]);
        if (vizXorOutputBuffer.getNumSamples() > 0)
            vizXorOutputBuffer.setSample(0, writeIdx, xorData[i]);
        if (vizNotAOutputBuffer.getNumSamples() > 0)
            vizNotAOutputBuffer.setSample(0, writeIdx, notAData[i]);

        // Track current state (use last sample for live display)
        if (i == numSamples - 1)
        {
            lastAState = (a > 0.5f);
            lastBState = (b > 0.5f);
        }
#endif
    }

#if defined(PRESET_CREATOR_UI)
    vizWritePos = (vizWritePos + numSamples) % vizBufferSize;

    // Update visualization data (thread-safe)
    // Downsample waveforms from circular buffers
    const int stride = vizBufferSize / VizData::waveformPoints;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const int readIdx = (vizWritePos - VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
        if (vizInputABuffer.getNumSamples() > 0)
            vizData.inputAWaveform[i].store(vizInputABuffer.getSample(0, readIdx));
        if (vizInputBBuffer.getNumSamples() > 0)
            vizData.inputBWaveform[i].store(vizInputBBuffer.getSample(0, readIdx));
        if (vizAndOutputBuffer.getNumSamples() > 0)
            vizData.andOutputWaveform[i].store(vizAndOutputBuffer.getSample(0, readIdx));
        if (vizOrOutputBuffer.getNumSamples() > 0)
            vizData.orOutputWaveform[i].store(vizOrOutputBuffer.getSample(0, readIdx));
        if (vizXorOutputBuffer.getNumSamples() > 0)
            vizData.xorOutputWaveform[i].store(vizXorOutputBuffer.getSample(0, readIdx));
        if (vizNotAOutputBuffer.getNumSamples() > 0)
            vizData.notAOutputWaveform[i].store(vizNotAOutputBuffer.getSample(0, readIdx));
    }

    // Update current states
    vizData.currentGateThreshold.store(gateThresh);
    vizData.currentOperation.store(static_cast<int>(operationParam != nullptr ? operationParam->load() : 0.0f));
    vizData.inputAState.store(lastAState);
    vizData.inputBState.store(lastBState);
    vizData.andState.store(lastAState && lastBState);
    vizData.orState.store(lastAState || lastBState);
    vizData.xorState.store(lastAState != lastBState);
    vizData.notAState.store(!lastAState);
#endif
}

void LogicModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void LogicModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

bool LogicModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "operation") { outBusIndex = 2; outChannelIndexInBus = 0; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void LogicModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    int operation = static_cast<int>(operationParam->load());
    const char* operationNames[] = {"AND", "OR", "XOR", "NOT A"};
    
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::Combo("Operation", &operation, operationNames, 4))
    {
        *operationParam = static_cast<float>(operation);
        onModificationEnded();
    }
    float gateThresh = gateThresholdParam != nullptr ? gateThresholdParam->load() : 0.5f;
    if (ImGui::SliderFloat("Gate Thresh", &gateThresh, 0.0f, 1.0f, "%.3f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gateThreshold"))) *p = gateThresh;
        onModificationEnded();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === SECTION: Logic Visualization ===
    ThemeText("Logic Activity", theme.text.section_header);
    ImGui::Spacing();

    ImGui::PushID(this); // Unique ID for this node's UI
    
    // Read visualization data (thread-safe) - BEFORE BeginChild
    float inputAWaveform[VizData::waveformPoints];
    float inputBWaveform[VizData::waveformPoints];
    float andOutputWaveform[VizData::waveformPoints];
    float orOutputWaveform[VizData::waveformPoints];
    float xorOutputWaveform[VizData::waveformPoints];
    float notAOutputWaveform[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        inputAWaveform[i] = vizData.inputAWaveform[i].load();
        inputBWaveform[i] = vizData.inputBWaveform[i].load();
        andOutputWaveform[i] = vizData.andOutputWaveform[i].load();
        orOutputWaveform[i] = vizData.orOutputWaveform[i].load();
        xorOutputWaveform[i] = vizData.xorOutputWaveform[i].load();
        notAOutputWaveform[i] = vizData.notAOutputWaveform[i].load();
    }
    const float currentGateThreshold = vizData.currentGateThreshold.load();
    const bool inputAState = vizData.inputAState.load();
    const bool inputBState = vizData.inputBState.load();
    const bool andState = vizData.andState.load();
    const bool orState = vizData.orState.load();
    const bool xorState = vizData.xorState.load();
    const bool notAState = vizData.notAState.load();

    // Waveform visualization in child window
    const float waveHeight = 160.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("LogicViz", graphSize, false, childFlags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        const ImU32 inputAColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);  // Cyan
        const ImU32 inputBColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);      // Orange/Yellow
        const ImU32 andColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.amplitude);       // Magenta/Pink
        const ImU32 orColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.filter);          // Green
        const ImU32 xorColor = ImGui::ColorConvertFloat4ToU32(theme.accent);                     // Accent
        const ImU32 notAColor = IM_COL32(180, 180, 255, 255);  // Light blue
        const ImU32 thresholdLineColor = IM_COL32(255, 255, 255, 120);
        const ImU32 centerLineColor = IM_COL32(150, 150, 150, 100);

        // Divide visualization into sections: Inputs (top), Outputs (bottom)
        const float midY = p0.y + graphSize.y * 0.5f;
        const float inputSectionHeight = graphSize.y * 0.45f;
        const float outputSectionHeight = graphSize.y * 0.45f;
        const float inputTopY = p0.y + 8.0f;
        const float inputBottomY = inputTopY + inputSectionHeight;
        const float outputTopY = midY + 4.0f;
        const float outputBottomY = outputTopY + outputSectionHeight;
        const float stepX = graphSize.x / (float)(VizData::waveformPoints - 1);

        // Draw center separator line
        drawList->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), centerLineColor, 1.5f);

        // Draw gate threshold line (for inputs)
        const float thresholdY = inputTopY + (1.0f - currentGateThreshold) * inputSectionHeight;
        const float clampedThresholdY = juce::jlimit(inputTopY + 2.0f, inputBottomY - 2.0f, thresholdY);
        drawList->AddLine(ImVec2(p0.x, clampedThresholdY), ImVec2(p1.x, clampedThresholdY), thresholdLineColor, 1.0f);
        drawList->AddText(ImVec2(p0.x + 4.0f, clampedThresholdY - 14.0f), thresholdLineColor, "Thresh");

        // Draw input waveforms (as gate signals - square waves)
        auto drawGateWaveform = [&](const float* waveform, ImU32 color, float topY, float bottomY, float alpha)
        {
            ImVec4 colorVec4 = ImGui::ColorConvertU32ToFloat4(color);
            colorVec4.w = alpha;
            const ImU32 drawColor = ImGui::ColorConvertFloat4ToU32(colorVec4);
            
            float prevX = p0.x;
            float prevGateState = (juce::jlimit(0.0f, 1.0f, waveform[0]) > currentGateThreshold) ? 1.0f : 0.0f;
            float prevY = bottomY - prevGateState * (bottomY - topY);
            
            for (int i = 1; i < VizData::waveformPoints; ++i)
            {
                const float sample = juce::jlimit(0.0f, 1.0f, waveform[i]);
                const float gateState = (sample > currentGateThreshold) ? 1.0f : 0.0f;
                const float x = p0.x + i * stepX;
                const float y = bottomY - gateState * (bottomY - topY);
                
                // Draw horizontal line for previous gate state
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, prevY), drawColor, 2.0f);
                
                // Draw vertical transition if state changed
                if (std::abs(gateState - prevGateState) > 0.5f)
                {
                    drawList->AddLine(ImVec2(x, prevY), ImVec2(x, y), drawColor, 2.0f);
                }
                
                prevX = x;
                prevY = y;
                prevGateState = gateState;
            }
            
            // Draw final horizontal segment
            const float finalX = p0.x + (VizData::waveformPoints - 1) * stepX;
            drawList->AddLine(ImVec2(prevX, prevY), ImVec2(finalX, prevY), drawColor, 2.0f);
        };

        // Draw input A (top half of input section)
        const float inputAMidY = inputTopY + inputSectionHeight * 0.25f;
        drawGateWaveform(inputAWaveform, inputAColor, inputTopY, inputAMidY, 0.6f);

        // Draw input B (bottom half of input section)
        const float inputBMidY = inputTopY + inputSectionHeight * 0.75f;
        drawGateWaveform(inputBWaveform, inputBColor, inputAMidY, inputBMidY, 0.6f);

        // Draw output waveforms (as gate signals in output section)
        const float outputRowHeight = outputSectionHeight / 4.0f;
        drawGateWaveform(andOutputWaveform, andColor, outputTopY, outputTopY + outputRowHeight, 0.8f);
        drawGateWaveform(orOutputWaveform, orColor, outputTopY + outputRowHeight, outputTopY + outputRowHeight * 2.0f, 0.8f);
        drawGateWaveform(xorOutputWaveform, xorColor, outputTopY + outputRowHeight * 2.0f, outputTopY + outputRowHeight * 3.0f, 0.8f);
        drawGateWaveform(notAOutputWaveform, notAColor, outputTopY + outputRowHeight * 3.0f, outputBottomY, 0.8f);

        // Add labels for outputs
        const char* outputLabels[] = {"AND", "OR", "XOR", "NOT A"};
        ImU32 outputColors[] = {andColor, orColor, xorColor, notAColor};
        for (int i = 0; i < 4; ++i)
        {
            const float labelY = outputTopY + outputRowHeight * i + outputRowHeight * 0.5f - 8.0f;
            drawList->AddText(ImVec2(p0.x + 4.0f, labelY), outputColors[i], outputLabels[i]);
        }

        drawList->PopClipRect();

        // Current state indicators overlay
        ImGui::SetCursorPos(ImVec2(4, waveHeight + 6));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "Inputs:");
        ImGui::SameLine();
        
        auto drawStateLED = [&](const char* label, bool state, ImU32 activeColor)
        {
            const ImVec2 pos = ImGui::GetCursorPos();
            const float radius = 5.0f;
            const ImU32 ledColor = state ? activeColor : IM_COL32(60, 60, 60, 200);
            drawList->AddCircleFilled(ImVec2(p0.x + pos.x + radius, p0.y + pos.y + radius), radius, ledColor, 16);
            ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        };
        
        drawStateLED("A", inputAState, inputAColor);
        ImGui::SameLine();
        drawStateLED("B", inputBState, inputBColor);

        ImGui::SetCursorPos(ImVec2(4, waveHeight + 24));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "Outputs:");
        ImGui::SameLine();
        drawStateLED("AND", andState, andColor);
        ImGui::SameLine();
        drawStateLED("OR", orState, orColor);
        ImGui::SameLine();
        drawStateLED("XOR", xorState, xorColor);
        ImGui::SameLine();
        drawStateLED("NOT A", notAState, notAColor);

        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##logicVizDrag", graphSize);
    }
    ImGui::EndChild();

    ImGui::PopID(); // End unique ID

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::PopItemWidth();
}

void LogicModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In A", 0, "AND", 0);
    helpers.drawParallelPins("In B", 1, "OR", 1);
    helpers.drawParallelPins(nullptr, -1, "XOR", 2);
    helpers.drawParallelPins(nullptr, -1, "NOT A", 3);
}
#endif