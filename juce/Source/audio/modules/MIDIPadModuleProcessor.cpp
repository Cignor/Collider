#include "MIDIPadModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MIDIPadModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    // === NUMBER OF PADS ===
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "numPads", "Number of Pads", 1, MAX_PADS, 16));
    
    // === MIDI ROUTING ===
    
    // Device selection (simplified)
    juce::StringArray deviceOptions;
    deviceOptions.add("All Devices");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "midiDevice", "MIDI Device", deviceOptions, 0));
    
    // Channel filter (0 = All Channels, 1-16 = specific channel)
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "midiChannel", "MIDI Channel", 0, 16, 0));
    
    // === TRIGGER BEHAVIOR ===
    
    // Trigger mode
    juce::StringArray triggerModes;
    triggerModes.add("Trigger");  // Brief pulse
    triggerModes.add("Gate");     // Hold until note-off
    triggerModes.add("Toggle");   // Toggle on/off
    triggerModes.add("Latch");    // Hold until another pad
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "triggerMode", "Trigger Mode", triggerModes, 0));
    
    // Trigger length (for Trigger mode)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "triggerLength", "Trigger Length",
        juce::NormalisableRange<float>(1.0f, 500.0f, 1.0f), 10.0f));
    
    // Velocity curve
    juce::StringArray curves;
    curves.add("Linear");
    curves.add("Exponential");
    curves.add("Logarithmic");
    curves.add("Fixed");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "velocityCurve", "Velocity Curve", curves, 0));
    
    // === VISUAL ===
    
    // Color mode
    juce::StringArray colorModes;
    colorModes.add("Velocity");    // Brightness = velocity
    colorModes.add("Row Colors");  // Each row different color
    colorModes.add("Fixed");       // All same color
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "colorMode", "Color Mode", colorModes, 0));
    
    return layout;
}

MIDIPadModuleProcessor::MIDIPadModuleProcessor()
    : ModuleProcessor(
        juce::AudioProcessor::BusesProperties()
            .withOutput("Main", juce::AudioChannelSet::discreteChannels(33), true)
            .withOutput("Mod", juce::AudioChannelSet::discreteChannels(64), true)
    ),
      apvts(*this, nullptr, "MIDIPadParams", createParameterLayout())
{
    // Get parameter pointers
    numPadsParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numPads"));
    deviceFilterParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("midiDevice"));
    midiChannelFilterParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midiChannel"));
    triggerModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("triggerMode"));
    triggerLengthParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("triggerLength"));
    velocityCurveParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("velocityCurve"));
    colorModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("colorMode"));
    
    // Initialize last output values for telemetry (33 outputs)
    lastOutputValues.resize(33);
    for (auto& val : lastOutputValues)
        val = std::make_unique<std::atomic<float>>(0.0f);
}

void MIDIPadModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;
    
    // Reset all pad states
    for (auto& pad : padMappings)
    {
        pad.gateHigh = false;
        pad.velocity = 0.0f;
        pad.triggerStartTime = 0.0;
        pad.toggleState = false;
    }
    
    lastGlobalVelocity = 0.0f;
    lastHitPad = -1;
    activePadCount = 0;
    latchedPad = -1;
    learningIndex = -1;
    
    juce::Logger::writeToLog("[MIDI Pads] Prepared to play at " + juce::String(sampleRate) + " Hz");
}

void MIDIPadModuleProcessor::releaseResources()
{
}

int MIDIPadModuleProcessor::midiNoteToPadIndex(int noteNumber) const
{
    // Find which pad (if any) is mapped to this MIDI note
    int numActive = numPadsParam ? numPadsParam->get() : MAX_PADS;
    
    for (int i = 0; i < numActive; ++i)
    {
        if (padMappings[i].midiNote == noteNumber)
            return i;
    }
    
    return -1;  // Note not mapped to any pad
}

float MIDIPadModuleProcessor::applyVelocityCurve(float rawVelocity) const
{
    if (!velocityCurveParam)
        return rawVelocity;
    
    int curve = velocityCurveParam->getIndex();
    
    switch (curve)
    {
        case 0:  // Linear
            return rawVelocity;
            
        case 1:  // Exponential
            return rawVelocity * rawVelocity;
            
        case 2:  // Logarithmic
            return std::log(1.0f + 9.0f * rawVelocity) / std::log(10.0f);
            
        case 3:  // Fixed
            return 1.0f;
            
        default:
            return rawVelocity;
    }
}

void MIDIPadModuleProcessor::handlePadHit(int padIdx, float velocity)
{
    if (padIdx < 0 || padIdx >= MAX_PADS)
        return;
    
    // Apply velocity curve
    float processedVelocity = applyVelocityCurve(velocity);
    
    // Update pad state
    padMappings[padIdx].velocity = processedVelocity;
    padMappings[padIdx].triggerStartTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    
    // Update global state
    lastGlobalVelocity = processedVelocity;
    lastHitPad = padIdx;
    
    // Handle trigger mode
    int mode = triggerModeParam ? triggerModeParam->getIndex() : 0;
    
    switch (mode)
    {
        case 0:  // Trigger mode
            padMappings[padIdx].gateHigh = true;
            // Will be turned off in updateTriggerStates() after triggerLength ms
            break;
            
        case 1:  // Gate mode
            padMappings[padIdx].gateHigh = true;
            break;
            
        case 2:  // Toggle mode
            padMappings[padIdx].toggleState = !padMappings[padIdx].toggleState.load();
            padMappings[padIdx].gateHigh = padMappings[padIdx].toggleState.load();
            break;
            
        case 3:  // Latch mode
            // Turn off previously latched pad
            int prevLatch = latchedPad.load();
            if (prevLatch >= 0 && prevLatch < MAX_PADS)
                padMappings[prevLatch].gateHigh = false;
            
            // Latch this pad
            padMappings[padIdx].gateHigh = true;
            latchedPad = padIdx;
            break;
    }
}

void MIDIPadModuleProcessor::handlePadRelease(int padIdx)
{
    if (padIdx < 0 || padIdx >= MAX_PADS)
        return;
    
    // Only matters for Gate mode
    int mode = triggerModeParam ? triggerModeParam->getIndex() : 0;
    
    if (mode == 1)  // Gate mode
    {
        padMappings[padIdx].gateHigh = false;
    }
}

void MIDIPadModuleProcessor::handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages)
{
    // Get user's filter settings
    int deviceFilter = deviceFilterParam ? deviceFilterParam->getIndex() : 0;
    int channelFilter = midiChannelFilterParam ? midiChannelFilterParam->get() : 0;
    int numActive = numPadsParam ? numPadsParam->get() : MAX_PADS;
    
    for (const auto& msg : midiMessages)
    {
        // DEVICE FILTERING
        if (deviceFilter != 0 && msg.deviceIndex != (deviceFilter - 1))
            continue;
        
        // CHANNEL FILTERING
        if (channelFilter != 0 && msg.message.getChannel() != channelFilter)
            continue;
        
        // PROCESS FILTERED MESSAGE
        if (msg.message.isNoteOn())
        {
            int noteNumber = msg.message.getNoteNumber();
            float velocity = msg.message.getVelocity();
            
            // Check for velocity 0 (some devices send note-on with vel=0 as note-off)
            if (velocity == 0.0f)
            {
                int padIdx = midiNoteToPadIndex(noteNumber);
                handlePadRelease(padIdx);
            }
            else
            {
                // Handle MIDI Learn
                if (learningIndex >= 0 && learningIndex < numActive)
                {
                    padMappings[learningIndex].midiNote = noteNumber;
                    learningIndex = -1;  // Exit learn mode
                }
                
                // Process normal pad hit
                int padIdx = midiNoteToPadIndex(noteNumber);
                if (padIdx >= 0)
                {
                    handlePadHit(padIdx, velocity);
                }
            }
        }
        else if (msg.message.isNoteOff())
        {
            int noteNumber = msg.message.getNoteNumber();
            int padIdx = midiNoteToPadIndex(noteNumber);
            handlePadRelease(padIdx);
        }
    }
}

void MIDIPadModuleProcessor::updateTriggerStates(juce::AudioBuffer<float>& buffer)
{
    // For Trigger mode: turn off gates after triggerLength ms
    int mode = triggerModeParam ? triggerModeParam->getIndex() : 0;
    
    if (mode != 0)  // Only for Trigger mode
        return;
    
    float triggerLengthMs = triggerLengthParam ? triggerLengthParam->get() : 10.0f;
    double triggerLengthSec = triggerLengthMs / 1000.0;
    double currentTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    
    for (int i = 0; i < MAX_PADS; ++i)
    {
        if (padMappings[i].gateHigh.load())
        {
            double elapsed = currentTime - padMappings[i].triggerStartTime.load();
            if (elapsed >= triggerLengthSec)
            {
                padMappings[i].gateHigh = false;
            }
        }
    }
}

void MIDIPadModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);  // MIDI already processed in handleDeviceSpecificMidi
    
    if (buffer.getNumChannels() < 33)
    {
        buffer.clear();
        return;
    }
    
    // Update trigger states (for Trigger mode timing)
    updateTriggerStates(buffer);
    
    const int numSamples = buffer.getNumSamples();
    int numActive = numPadsParam ? numPadsParam->get() : MAX_PADS;
    
    // Count active pads
    int activeCount = 0;
    for (int i = 0; i < numActive; ++i)
    {
        if (padMappings[i].gateHigh.load())
            activeCount++;
    }
    activePadCount = activeCount;
    
    // Generate outputs for all pads
    for (int padIdx = 0; padIdx < MAX_PADS; ++padIdx)
    {
        // Gate output (channels 0-15)
        float gateValue = (padIdx < numActive && padMappings[padIdx].gateHigh.load()) ? 1.0f : 0.0f;
        buffer.clear(padIdx, 0, numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(padIdx), gateValue, numSamples);
        
        // Velocity output (channels 16-31)
        float velocityValue = (padIdx < numActive) ? padMappings[padIdx].velocity.load() : 0.0f;
        buffer.clear(16 + padIdx, 0, numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(16 + padIdx), velocityValue, numSamples);
        
        // Update telemetry
        if (lastOutputValues.size() > padIdx)
            lastOutputValues[padIdx]->store(gateValue);
        if (lastOutputValues.size() > 16 + padIdx)
            lastOutputValues[16 + padIdx]->store(velocityValue);
    }
    
    // Global velocity output (channel 32)
    float globalVel = lastGlobalVelocity.load();
    buffer.clear(32, 0, numSamples);
    juce::FloatVectorOperations::fill(buffer.getWritePointer(32), globalVel, numSamples);
    
    if (lastOutputValues.size() > 32)
        lastOutputValues[32]->store(globalVel);
}

std::vector<DynamicPinInfo> MIDIPadModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    int numActive = numPadsParam ? numPadsParam->get() : MAX_PADS;
    
    for (int i = 0; i < numActive; ++i)
    {
        // Gate output
        juce::String gateLabel = "Pad " + juce::String(i + 1) + " Gate";
        pins.push_back(DynamicPinInfo(gateLabel, i, PinDataType::Gate));
        
        // Velocity output
        juce::String velLabel = "Pad " + juce::String(i + 1) + " Vel";
        pins.push_back(DynamicPinInfo(velLabel, 16 + i, PinDataType::CV));
    }
    
    // Global velocity
    pins.push_back(DynamicPinInfo("Global Vel", 32, PinDataType::CV));
    
    return pins;
}

juce::ValueTree MIDIPadModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MIDIPadsState");
    
    // Save MIDI mappings
    for (int i = 0; i < MAX_PADS; ++i)
    {
        juce::ValueTree mapping("Mapping");
        mapping.setProperty("index", i, nullptr);
        mapping.setProperty("note", padMappings[i].midiNote, nullptr);
        vt.addChild(mapping, -1, nullptr);
    }
    
    return vt;
}

void MIDIPadModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MIDIPadsState"))
    {
        // Load MIDI mappings
        for (const auto& child : vt)
        {
            if (child.hasType("Mapping"))
            {
                int index = child.getProperty("index", -1);
                if (index >= 0 && index < MAX_PADS)
                {
                    padMappings[index].midiNote = child.getProperty("note", -1);
                }
            }
        }
    }
}

#if defined(PRESET_CREATOR_UI)

ImVec4 MIDIPadModuleProcessor::getPadColor(int padIdx, float brightness) const
{
    int colorMode = colorModeParam ? colorModeParam->getIndex() : 0;
    int row = padIdx / 4;
    
    if (colorMode == 1)  // Row colors
    {
        float hue = row * 0.25f;  // 0, 0.25, 0.5, 0.75
        return ImColor::HSV(hue, 0.8f, brightness).Value;
    }
    else  // Velocity or Fixed mode
    {
        // Red-ish color
        return ImVec4(brightness, brightness/4, brightness/4, 1.0f);
    }
}

void MIDIPadModuleProcessor::drawParametersInNode(float itemWidth, 
                                                   const std::function<bool(const juce::String&)>&, 
                                                   const std::function<void()>& onModificationEnded)
{
    ImGui::PushID(this);
    // HelpMarker helper function
    auto HelpMarker = [](const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    
    ImGui::PushItemWidth(itemWidth);
    
    // === HEADER ===
    if (numPadsParam)
    {
        int numPads = numPadsParam->get();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##numpads", &numPads, 1, MAX_PADS))
        {
            *numPadsParam = numPads;
            onModificationEnded();
        }
        ImGui::SameLine();
        ImGui::Text("Pads");
        HelpMarker("Number of active pads (1-16)");
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === MIDI ROUTING ===
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "MIDI Routing");
    ImGui::Spacing();
    
    ImGui::Text("Device: All Devices");
    HelpMarker("Multi-device filtering active.\nDevice selection managed by MidiDeviceManager.");
    
    ImGui::Spacing();
    
    // Channel selector
    if (midiChannelFilterParam)
    {
        int channel = midiChannelFilterParam->get();
        const char* items[] = {"All Channels", "1", "2", "3", "4", "5", "6", "7", "8",
                               "9", "10", "11", "12", "13", "14", "15", "16"};
        if (ImGui::Combo("##channel", &channel, items, 17))
        {
            midiChannelFilterParam->setValueNotifyingHost(
                midiChannelFilterParam->getNormalisableRange().convertTo0to1(channel));
        }
        ImGui::SameLine();
        ImGui::Text("Channel");
        HelpMarker("Filter MIDI by channel.\n0 = All Channels, 1-16 = specific channel.");
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === PAD GRID ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Pad Grid (4x4)");
    ImGui::Spacing();
    
    int numActive = numPadsParam ? numPadsParam->get() : MAX_PADS;
    
    // Draw 4x4 grid with custom ImDrawList
    const float cellSize = (itemWidth - 16.0f) / 4.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 gridStart = ImGui::GetCursorScreenPos();
    
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            int padIdx = row * 4 + col;
            bool isActive = (padIdx < numActive) && padMappings[padIdx].isActive();
            bool hasMapping = (padIdx < numActive) && (padMappings[padIdx].midiNote != -1);
            float velocity = (padIdx < numActive) ? padMappings[padIdx].velocity.load() : 0.0f;
            bool isLearning = (learningIndex == padIdx);
            
            ImVec2 cellPos(
                gridStart.x + col * (cellSize + 4.0f) + 2.0f,
                gridStart.y + row * (cellSize + 4.0f) + 2.0f
            );
            
            ImVec2 cellEnd(
                cellPos.x + cellSize,
                cellPos.y + cellSize
            );
            
            // Background color
            ImU32 bgColor;
            if (padIdx >= numActive)
            {
                bgColor = IM_COL32(20, 20, 20, 255);  // Inactive (dark)
            }
            else if (isLearning)
            {
                bgColor = IM_COL32(255, 128, 0, 255);  // Orange for learning
            }
            else if (!hasMapping)
            {
                bgColor = IM_COL32(60, 60, 60, 255);  // Gray for unassigned
            }
            else
            {
                bgColor = IM_COL32(40, 40, 40, 255);  // Normal
            }
            
            drawList->AddRectFilled(cellPos, cellEnd, bgColor, 3.0f);
            
            // Active indicator (with pulsing animation)
            if (isActive && hasMapping)
            {
                float pulse = 0.6f + 0.4f * std::sin((float)ImGui::GetTime() * 8.0f);
                ImVec4 color = getPadColor(padIdx, velocity * pulse);
                ImU32 activeColor = ImGui::ColorConvertFloat4ToU32(color);
                drawList->AddRectFilled(cellPos, cellEnd, activeColor, 3.0f);
            }
            
            // Border
            ImU32 borderColor;
            if (isLearning)
            {
                borderColor = IM_COL32(255, 200, 0, 255);
            }
            else if (isActive)
            {
                borderColor = IM_COL32(255, 100, 100, 255);
            }
            else if (hasMapping)
            {
                borderColor = IM_COL32(100, 100, 100, 255);
            }
            else
            {
                borderColor = IM_COL32(60, 60, 60, 255);
            }
            drawList->AddRect(cellPos, cellEnd, borderColor, 3.0f, 0, 2.0f);
            
            // Make clickable for learn mode
            ImGui::SetCursorScreenPos(cellPos);
            ImGui::PushID(padIdx);
            ImGui::InvisibleButton("##pad", ImVec2(cellSize, cellSize));
            if (ImGui::IsItemClicked() && padIdx < numActive)
            {
                learningIndex = padIdx;
            }
            
            // Tooltip
            if (ImGui::IsItemHovered() && padIdx < numActive)
            {
                if (isLearning)
                {
                    ImGui::SetTooltip("Learning Pad %d...\nHit a MIDI pad to assign", padIdx + 1);
                }
                else if (hasMapping)
                {
                    ImGui::SetTooltip("Pad %d\nMIDI Note: %d\nClick to reassign", padIdx + 1, padMappings[padIdx].midiNote);
                }
                else
                {
                    ImGui::SetTooltip("Pad %d\nUnassigned\nClick to learn", padIdx + 1);
                }
            }
            ImGui::PopID();
            
            // Label
            char label[8];
            snprintf(label, sizeof(label), "%d", padIdx + 1);
            ImVec2 textSize = ImGui::CalcTextSize(label);
            ImVec2 textPos(
                cellPos.x + (cellSize - textSize.x) * 0.5f,
                cellPos.y + (cellSize - textSize.y) * 0.5f
            );
            ImU32 textColor = (isActive || isLearning) ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 200);
            drawList->AddText(textPos, textColor, label);
        }
    }
    
    // Reserve space for grid
    float gridHeight = 4.0f * (cellSize + 4.0f) + 4.0f;
    ImGui::SetCursorScreenPos(ImVec2(gridStart.x, gridStart.y + gridHeight));
    ImGui::Dummy(ImVec2(itemWidth, 0));
    
    HelpMarker("Click a pad to assign MIDI note.\nOrange = Learning\nGray = Unassigned\nPulsing = Active");
    
    // Learning status
    if (learningIndex >= 0 && learningIndex < numActive)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Learning Pad %d... Hit a MIDI pad", learningIndex + 1);
        if (ImGui::Button("Cancel Learning", ImVec2(150, 0)))
        {
            learningIndex = -1;
        }
    }
    else
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Click a pad to learn MIDI note");
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === SETTINGS ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Settings");
    ImGui::Spacing();
    
    // Trigger mode
    if (triggerModeParam)
    {
        const char* modes[] = {"Trigger", "Gate", "Toggle", "Latch"};
        int mode = triggerModeParam->getIndex();
        if (ImGui::Combo("##mode", &mode, modes, 4))
        {
            triggerModeParam->setValueNotifyingHost(
                triggerModeParam->getNormalisableRange().convertTo0to1(mode));
        }
        ImGui::SameLine();
        ImGui::Text("Mode");
        HelpMarker("Trigger: Brief pulse\nGate: Hold until release\nToggle: Each hit toggles\nLatch: Hold until another pad");
    }
    
    // Velocity curve
    if (velocityCurveParam)
    {
        const char* curves[] = {"Linear", "Exponential", "Logarithmic", "Fixed"};
        int curve = velocityCurveParam->getIndex();
        if (ImGui::Combo("##curve", &curve, curves, 4))
        {
            velocityCurveParam->setValueNotifyingHost(
                velocityCurveParam->getNormalisableRange().convertTo0to1(curve));
        }
        ImGui::SameLine();
        ImGui::Text("Velocity Curve");
        HelpMarker("Linear: 1:1 mapping\nExponential: More dynamic\nLogarithmic: Compressed\nFixed: Ignore velocity");
    }
    
    // Trigger length (only relevant for Trigger mode)
    if (triggerLengthParam)
    {
        float trigLen = triggerLengthParam->get();
        if (ImGui::SliderFloat("##triglen", &trigLen, 1.0f, 100.0f, "%.0f ms"))
        {
            *triggerLengthParam = trigLen;
        }
        ImGui::SameLine();
        ImGui::Text("Trigger Length");
        HelpMarker("Duration of gate pulse in Trigger mode.\nRange: 1-100 milliseconds.");
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === STATISTICS ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Statistics");
    ImGui::Spacing();
    
    int activeCount = activePadCount.load();
    ImGui::Text("Active Pads: %d/%d", activeCount, numActive);
    HelpMarker("Number of pads currently outputting gate=1");
    
    int lastPad = lastHitPad.load();
    float lastVel = lastGlobalVelocity.load();
    if (lastPad >= 0)
    {
        ImGui::Text("Last Hit: Pad %d (vel: %.2f)", lastPad + 1, lastVel);
    }
    else
    {
        ImGui::TextDisabled("Last Hit: None");
    }
    HelpMarker("Most recently triggered pad and its velocity");
    
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void MIDIPadModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    juce::ignoreUnused(helpers);
    // Pins are drawn dynamically via getDynamicOutputPins()
}
#endif
