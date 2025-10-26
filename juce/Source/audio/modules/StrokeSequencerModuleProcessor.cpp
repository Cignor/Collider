#include "StrokeSequencerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h" // For ImGui calls
#endif

StrokeSequencerModuleProcessor::StrokeSequencerModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // Reset, Rate, 3x Thresholds
                          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(4), true)), // 3x Triggers, 1x CV
      apvts(*this, nullptr, "StrokeSeqParams", createParameterLayout())
{
    for (auto& val : triggerOutputValues)
        val = 0.0f;
    
    // Initialize cached parameter pointers
    rateParam = apvts.getRawParameterValue(paramIdRate);
    floorYParam = apvts.getRawParameterValue(paramIdFloorY);
    midYParam = apvts.getRawParameterValue(paramIdMidY);
    ceilingYParam = apvts.getRawParameterValue(paramIdCeilingY);
    
    startTimerHz(30); // For UI updates
}

StrokeSequencerModuleProcessor::~StrokeSequencerModuleProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout StrokeSequencerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate", juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdSync, "Sync to Transport", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdRateDivision, "Division", 
        juce::StringArray{ "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8" }, 3)); // Default: 1/4 note
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdFloorY, "Floor Y", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMidY, "Mid Y", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdCeilingY, "Ceiling Y", 0.0f, 1.0f, 0.75f));

    return { params.begin(), params.end() };
}

void StrokeSequencerModuleProcessor::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    phase = 0.0;
}

void StrokeSequencerModuleProcessor::releaseResources() {}

void StrokeSequencerModuleProcessor::setTimingInfo(const TransportState& state)
{
    // Check if the transport has just started playing
    if (state.isPlaying && !wasPlaying)
    {
        // Reset to the beginning when play is pressed
        playheadPosition = 0.0;
        phase = 0.0;
    }
    wasPlaying = state.isPlaying;
    
    m_currentTransport = state;
}

std::vector<DynamicPinInfo> StrokeSequencerModuleProcessor::getDynamicInputPins() const
{
    return {
        { "Reset In",       0, PinDataType::Gate },
        { "Rate Mod In",    1, PinDataType::CV },
        { "Floor Mod In",   2, PinDataType::CV },
        { "Mid Mod In",     3, PinDataType::CV },
        { "Ceiling Mod In", 4, PinDataType::CV }
    };
}

std::vector<DynamicPinInfo> StrokeSequencerModuleProcessor::getDynamicOutputPins() const
{
    return {
        { "Floor Trig Out", 0, PinDataType::Gate },
        { "Mid Trig Out",   1, PinDataType::Gate },
        { "Ceiling Trig Out", 2, PinDataType::Gate },
        { "Value Out",      3, PinDataType::CV }
    };
}

bool StrokeSequencerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdRateMod)     { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdFloorYMod)   { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdMidYMod)     { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdCeilingYMod) { outChannelIndexInBus = 4; return true; }
    return false;
}

void StrokeSequencerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // --- Thread-safe Update Block ---
    if (strokeDataDirty.load())
    {
        audioStrokePoints.clear();
        for (const auto& stroke : userStrokes)
        {
            audioStrokePoints.insert(audioStrokePoints.end(), stroke.begin(), stroke.end());
        }
        std::sort(audioStrokePoints.begin(), audioStrokePoints.end(), [](const StrokePoint& a, const StrokePoint& b) { return a.x < b.x; });
        strokeDataDirty = false;
    }
    // --- End Block ---

    const int numSamples = buffer.getNumSamples();
    const auto inBus = getBusBuffer(buffer, true, 0);

    // --- Get Modulation CVs ---
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isFloorMod = isParamInputConnected(paramIdFloorYMod);
    const bool isMidMod = isParamInputConnected(paramIdMidYMod);
    const bool isCeilingMod = isParamInputConnected(paramIdCeilingYMod);

    const float* rateCV = isRateMod && inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* floorCV = isFloorMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* midCV = isMidMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* ceilingCV = isCeilingMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    // --- Get Base Parameter Values ---
    const bool sync = apvts.getRawParameterValue(paramIdSync)->load() > 0.5f;
    const float baseRate = rateParam->load();
    float finalThresholds[3] = { floorYParam->load(), midYParam->load(), ceilingYParam->load() };
    
    // --- Calculate Final Values (per-block for simplicity) ---
    float finalRate = baseRate;
    if (isRateMod && rateCV)
    {
        // CV (0..1) scales the rate from its minimum to its maximum
        auto range = apvts.getParameterRange(paramIdRate);
        finalRate = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, rateCV[0]));
    }

    if (isFloorMod && floorCV)   finalThresholds[0] = juce::jlimit(0.0f, 1.0f, floorCV[0]);
    if (isMidMod && midCV)       finalThresholds[1] = juce::jlimit(0.0f, 1.0f, midCV[0]);
    if (isCeilingMod && ceilingCV) finalThresholds[2] = juce::jlimit(0.0f, 1.0f, ceilingCV[0]);

    // --- Update UI Telemetry ---
    setLiveParamValue("rate_live", finalRate);
    setLiveParamValue("floorY_live", finalThresholds[0]);
    setLiveParamValue("midY_live", finalThresholds[1]);
    setLiveParamValue("ceilingY_live", finalThresholds[2]);

    // Check for reset trigger
    if (inBus.getNumChannels() > 0 && inBus.getReadPointer(0)[0] > 0.5f)
    {
        playheadPosition = 0.0;
        phase = 0.0;
    }

    // --- Transport Sync Logic ---
    if (sync && m_currentTransport.isPlaying)
    {
        // SYNC MODE: Use the global beat position
        int divisionIndex = (int)apvts.getRawParameterValue(paramIdRateDivision)->load();
        
        // Use global division if a Tempo Clock has override enabled
        if (getParent())
        {
            int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
            if (globalDiv >= 0)
                divisionIndex = globalDiv;
        }
        
        static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
        const double beatDivision = divisions[juce::jlimit(0, 8, divisionIndex)];
        
        // Calculate playhead position based on song position
        playheadPosition = std::fmod(m_currentTransport.songPositionBeats * beatDivision, 1.0);
    }
    else
    {
        // FREE-RUNNING MODE: Use the internal phase clock
        const double phaseInc = (sampleRate > 0.0 ? (double)finalRate / sampleRate : 0.0);
        phase += phaseInc * numSamples;
        if (phase >= 1.0)
            phase -= std::floor(phase);
        playheadPosition = phase;
    }

    // --- Main processing loop ---
    auto* valueOut = buffer.getWritePointer(3);
    for (int i = 0; i < numSamples; ++i)
    {
        // Find Stroke Value at Playhead
        float currentStrokeY = 0.5f; // Default to center if no points
        if (audioStrokePoints.size() > 1)
        {
            // Find the two points the playhead is between
            auto it = std::lower_bound(audioStrokePoints.begin(), audioStrokePoints.end(), (float)playheadPosition, 
                                       [](const StrokePoint& p, float val) { return p.x < val; });
            
            if (it == audioStrokePoints.begin()) {
                currentStrokeY = it->y;
            } else if (it == audioStrokePoints.end()) {
                currentStrokeY = (audioStrokePoints.end() - 1)->y;
            } else {
                // Linear interpolation
                const auto p1 = *(it - 1);
                const auto p2 = *it;
                const float t = (playheadPosition - p1.x) / (p2.x - p1.x);
                currentStrokeY = juce::jmap(t, p1.y, p2.y);
            }
        }
        else if (!audioStrokePoints.empty()) {
            currentStrokeY = audioStrokePoints[0].y;
        }

        currentStrokeYValue.store(currentStrokeY);
        valueOut[i] = currentStrokeY;

        // Intersection Logic
        for (int t = 0; t < 3; ++t)
        {
            const bool isAbove = currentStrokeY >= finalThresholds[t];
            if (isAbove && !wasAboveThreshold[t])
            {
                triggerOutputValues[t] = 1.0f; // Fire trigger
            }
            wasAboveThreshold[t] = isAbove;
        }
    }

    // Write Triggers to Output Buffers
    for (int t = 0; t < 3; ++t)
    {
        if (triggerOutputValues[t].load() > 0.5f)
        {
            buffer.setSample(t, 0, 1.0f);
            triggerOutputValues[t] = 0.0f; // Reset for next block
        }
    }
}

void StrokeSequencerModuleProcessor::timerCallback()
{
    // This timer is just for the UI, to force an update
    // It's a simple way to get the playhead to animate smoothly
    if (auto* editor = getActiveEditor())
        editor->repaint();
}

#if defined(PRESET_CREATOR_UI)
void StrokeSequencerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // --- SYNC CONTROLS ---
    bool sync = apvts.getRawParameterValue(paramIdSync)->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSync))) *p = sync;
        onModificationEnded();
    }
    
    ImGui::PushItemWidth(itemWidth);
    if (sync)
    {
        // Check if global division is active (Tempo Clock override)
        int globalDiv = getParent() ? getParent()->getTransportState().globalDivisionIndex.load() : -1;
        bool isGlobalDivisionActive = globalDiv >= 0;
        int division = isGlobalDivisionActive ? globalDiv : (int)apvts.getRawParameterValue(paramIdRateDivision)->load();
        
        // Grey out if controlled by Tempo Clock
        if (isGlobalDivisionActive) ImGui::BeginDisabled();
        
        if (ImGui::Combo("Division", &division, "1/32\0""1/16\0""1/8\0""1/4\0""1/2\0""1\0""2\0""4\0""8\0\0"))
        {
            if (!isGlobalDivisionActive)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdRateDivision))) *p = division;
                onModificationEnded();
            }
        }
        
        if (isGlobalDivisionActive)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Tempo Clock Division Override Active");
                ImGui::TextUnformatted("A Tempo Clock node with 'Division Override' enabled is controlling the global division.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
    else
    {
        // FREE-RUNNING MODE: Show rate control
        bool isRateMod = isParamModulated(paramIdRateMod);
        float rateValue = isRateMod ? getLiveParamValueFor(paramIdRateMod, "rate_live", rateParam->load()) : rateParam->load();
        if (isRateMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Rate", &rateValue, 0.1f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic))
            if (!isRateMod) *rateParam = rateValue;
        if (ImGui::IsItemDeactivatedAfterEdit() && !isRateMod) onModificationEnded();
        if (isRateMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    }
    ImGui::PopItemWidth();
    ImGui::Separator();

    // --- Canvas & Controls Layout ---
    ImGui::BeginGroup(); // Group canvas and sliders together

    // --- Canvas ---
    ImVec2 canvas_size(840.0f, 360.0f);
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);
    auto* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(20, 20, 20, 255));
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(100, 100, 100, 255));
    
    ImGui::InvisibleButton("##canvas", canvas_size);
    const bool is_hovered = ImGui::IsItemHovered();
    ImVec2 mouse_pos_in_canvas = ImVec2(ImGui::GetIO().MousePos.x - canvas_p0.x, ImGui::GetIO().MousePos.y - canvas_p0.y);

    // Mouse Input
    // --- Eraser Mode ---
    if (is_hovered && isErasing && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float eraseRadius = 15.0f; // Radius of the eraser brush
        juce::Point<float> mousePos = { mouse_pos_in_canvas.x, mouse_pos_in_canvas.y };
        bool strokeModified = false;

        // Iterate through each stroke
        for (auto& stroke : userStrokes)
        {
            // Remove points from the current stroke that are within the eraseRadius
            auto initialSize = stroke.size();
            std::erase_if(stroke, [&](const StrokePoint& p) {
                // Convert normalized point back to canvas coordinates for distance check
                juce::Point<float> pCanvas = { p.x * canvas_size.x, (1.0f - p.y) * canvas_size.y };
                return mousePos.getDistanceFrom(pCanvas) < eraseRadius;
            });
            if (stroke.size() < initialSize)
                strokeModified = true;
        }

        // Remove any strokes that became empty after erasing
        auto initialStrokesSize = userStrokes.size();
        std::erase_if(userStrokes, [](const std::vector<StrokePoint>& stroke) {
            return stroke.empty();
        });
        if (userStrokes.size() < initialStrokesSize)
            strokeModified = true;

        if (strokeModified)
            strokeDataDirty = true; // Signal the audio thread to update its copy
    }
    // --- End Eraser Mode ---

    // DRAWING LOGIC (ensure this comes AFTER the eraser logic)
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isErasing)
    {
        isDrawing = true;
        userStrokes.emplace_back(); // Add a new, empty stroke to our list
    }
    if (isDrawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDrawing = false;
        strokeDataDirty = true; // Signal the audio thread to update its copy
    }
    if (isDrawing && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !userStrokes.empty())
    {
        float x = juce::jlimit(0.0f, 1.0f, mouse_pos_in_canvas.x / canvas_size.x);
        float y = 1.0f - juce::jlimit(0.0f, 1.0f, mouse_pos_in_canvas.y / canvas_size.y); // Invert Y
        userStrokes.back().push_back({x, y}); // Add point to the newest stroke
    }

    // --- Rendering ---
    draw_list->PushClipRect(canvas_p0, canvas_p1, true);

    // Draw all Strokes
    for (const auto& stroke : userStrokes)
    {
        if (stroke.size() > 1)
        {
            std::vector<ImVec2> pointsForImGui;
            for (const auto& p : stroke)
                pointsForImGui.emplace_back(canvas_p0.x + p.x * canvas_size.x, canvas_p0.y + (1.0f - p.y) * canvas_size.y);
            draw_list->AddPolyline(pointsForImGui.data(), pointsForImGui.size(), IM_COL32(255, 255, 0, 255), 0, 2.0f);
        }
    }

    // Draw Threshold Lines using live modulated values
    float liveThresholds[3] = {
        isParamModulated(paramIdFloorYMod) ? getLiveParamValueFor(paramIdFloorYMod, "floorY_live", floorYParam->load()) : floorYParam->load(),
        isParamModulated(paramIdMidYMod)   ? getLiveParamValueFor(paramIdMidYMod, "midY_live", midYParam->load())     : midYParam->load(),
        isParamModulated(paramIdCeilingYMod)? getLiveParamValueFor(paramIdCeilingYMod, "ceilingY_live", ceilingYParam->load()) : ceilingYParam->load()
    };
    ImU32 colors[3] = { IM_COL32(255, 100, 100, 150), IM_COL32(100, 255, 100, 150), IM_COL32(100, 100, 255, 150) };
    for (int t = 0; t < 3; ++t) {
        float y = canvas_p0.y + (1.0f - liveThresholds[t]) * canvas_size.y;
        draw_list->AddLine({canvas_p0.x, y}, {canvas_p1.x, y}, colors[t]);
    }

    // Draw Playhead
    float playheadX = canvas_p0.x + (float)playheadPosition * canvas_size.x;
    draw_list->AddLine({playheadX, canvas_p0.y}, {playheadX, canvas_p1.y}, IM_COL32(255, 255, 255, 200), 2.0f);

    // Eraser Visual Feedback
    if (is_hovered && isErasing)
    {
        const float eraseRadius = 15.0f;
        ImVec2 center = ImVec2(canvas_p0.x + mouse_pos_in_canvas.x, canvas_p0.y + mouse_pos_in_canvas.y);
        draw_list->AddCircle(center, eraseRadius, IM_COL32(255, 0, 0, 100), 0, 2.0f); // Red translucent circle
    }

    draw_list->PopClipRect();
    
    ImGui::EndGroup(); // End the canvas group

    // --- Vertical Sliders to the Right ---
    ImGui::SameLine(); // Place sliders to the right of the canvas group

    ImGui::BeginGroup();
    {
        const ImVec2 sliderSize(18.0f, canvas_size.y);
        
        // Helper lambda for a vertical modulated slider
        auto createV_Slider = [&](const char* label, const juce::String& paramID, const juce::String& modID, std::atomic<float>* paramPtr, const char* liveKey) {
            const bool isMod = isParamModulated(modID);
            float value = isMod ? getLiveParamValueFor(modID, liveKey, paramPtr->load()) : paramPtr->load();

            if (isMod) ImGui::BeginDisabled();
            // Note the min/max are inverted to match the canvas (1.0 at top)
            if (ImGui::VSliderFloat(label, sliderSize, &value, 0.0f, 1.0f, "")) {
                if (!isMod) *paramPtr = value;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && !isMod) onModificationEnded();
            if (isMod) ImGui::EndDisabled();
        };

        createV_Slider("##floor", paramIdFloorY, paramIdFloorYMod, floorYParam, "floorY_live");
        ImGui::SameLine();
        createV_Slider("##mid", paramIdMidY, paramIdMidYMod, midYParam, "midY_live");
        ImGui::SameLine();
        createV_Slider("##ceiling", paramIdCeilingY, paramIdCeilingYMod, ceilingYParam, "ceilingY_live");
    }
    ImGui::EndGroup();

    // --- Controls below canvas ---
    ImGui::Separator();
    if (ImGui::Button("Clear")) 
    { 
        userStrokes.clear(); 
        strokeDataDirty = true; // Signal the audio thread
    }
    ImGui::SameLine(); // Place the next item on the same line
    
    if (ImGui::Checkbox("Eraser", &isErasing))
    {
        // State is automatically updated by ImGui
    }
}
#endif

// --- State Management ---
juce::ValueTree StrokeSequencerModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree state("StrokeSequencerState");
    juce::ValueTree strokesNode("Strokes");

    for (const auto& stroke : userStrokes)
    {
        juce::ValueTree strokeNode("Stroke");
        juce::String pointsString;
        for (const auto& p : stroke)
            pointsString += juce::String(p.x) + "," + juce::String(p.y) + ";";
        strokeNode.setProperty("points", pointsString, nullptr);
        strokesNode.addChild(strokeNode, -1, nullptr);
    }
    state.addChild(strokesNode, -1, nullptr);
    
    // Save transport settings
    state.setProperty("sync", apvts.getRawParameterValue(paramIdSync)->load(), nullptr);
    state.setProperty("rate_division", apvts.getRawParameterValue(paramIdRateDivision)->load(), nullptr);
    
    return state;
}

void StrokeSequencerModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (state.hasType("StrokeSequencerState"))
    {
        userStrokes.clear();
        if (auto strokesNode = state.getChildWithName("Strokes"); strokesNode.isValid())
        {
            for (auto strokeNode : strokesNode)
            {
                userStrokes.emplace_back(); // Add a new stroke
                juce::StringArray pairs;
                pairs.addTokens(strokeNode.getProperty("points").toString(), ";", "");
                for (const auto& pair : pairs)
                {
                    juce::StringArray xy = juce::StringArray::fromTokens(pair, ",", "");
                    if (xy.size() == 2)
                        userStrokes.back().push_back({ xy[0].getFloatValue(), xy[1].getFloatValue() });
                }
            }
        }
        strokeDataDirty = true; // Tell the audio thread to update
        
        // Restore transport settings
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSync)))
            *p = (bool)state.getProperty("sync", false);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdRateDivision)))
            *p = (int)state.getProperty("rate_division", 3);
    }
}

