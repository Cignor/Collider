#include "StrokeSequencerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h" // For ImGui calls
#include "../../preset_creator/ControllerPresetManager.h"
#endif

// ==============================================================================
// StrokeSequencerModuleProcessor Implementation
// ==============================================================================

StrokeSequencerModuleProcessor::StrokeSequencerModuleProcessor()
    : ModuleProcessor(BusesProperties()
                          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // Reset, Rate, 3x Thresholds
                          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(7), true)), // 3x Triggers, 1x Continuous Pitch, 3x Held Pitch
      apvts(*this, nullptr, "StrokeSeqParams", createParameterLayout())
{
    // Initialize cached parameter pointers
    rateParam = apvts.getRawParameterValue(paramIdRate);
    floorYParam = apvts.getRawParameterValue(paramIdFloorY);
    midYParam = apvts.getRawParameterValue(paramIdMidY);
    ceilingYParam = apvts.getRawParameterValue(paramIdCeilingY);
    playheadParam = apvts.getRawParameterValue(paramIdPlayhead);
    
    startTimerHz(30); // For UI updates
    m_isPrimed = false;
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdPlayhead, "Playhead", 
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

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
        
        // Initialize previous position to avoid spurious triggers on start
        previousPlayheadPos = 0.0;
        previousStrokeY = 0.5f; // Center
        
        juce::Logger::writeToLog("[StrokeSeq] Transport started - reset positions");
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
        { "Continuous Pitch", 3, PinDataType::CV },
        { "Floor Pitch",    4, PinDataType::CV },
        { "Mid Pitch",      5, PinDataType::CV },
        { "Ceiling Pitch",  6, PinDataType::CV }
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
        // Precompute min/max Y per stroke for audio-thread usage
        audioStrokeInfo.clear();
        audioStrokeInfo.resize(userStrokes.size());
        for (size_t si = 0; si < userStrokes.size(); ++si)
        {
            const auto& stroke = userStrokes[si];
            if (stroke.empty()) continue;
            float minY = stroke[0].y;
            float maxY = stroke[0].y;
            for (const auto& p : stroke)
            {
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
            audioStrokeInfo[si].minY = minY;
            audioStrokeInfo[si].maxY = maxY;
        }

        audioStrokePoints.clear();
        audioPointToStrokeIndex.clear();
        // Build pairs so we can sort points and keep stroke indices aligned
        struct PointWithStroke { StrokePoint pt; int strokeIdx; };
        std::vector<PointWithStroke> pw;
        pw.reserve(1024);
        for (size_t si = 0; si < userStrokes.size(); ++si)
        {
            const auto& stroke = userStrokes[si];
            for (const auto& p : stroke)
                pw.push_back({ p, (int)si });
        }
        std::sort(pw.begin(), pw.end(), [](const PointWithStroke& a, const PointWithStroke& b){ return a.pt.x < b.pt.x; });
        audioStrokePoints.reserve(pw.size());
        audioPointToStrokeIndex.reserve(pw.size());
        for (const auto& e : pw)
        {
            audioStrokePoints.push_back(e.pt);
            audioPointToStrokeIndex.push_back(e.strokeIdx);
        }
        strokeDataDirty = false;
    }
    // --- End Block ---

    const int numSamples = buffer.getNumSamples();
    const auto inBus = getBusBuffer(buffer, true, 0);

    // --- Get Modulation CVs (check if connected) ---
    const bool isRateMod = isParamInputConnected(paramIdRateMod);
    const bool isFloorMod = isParamInputConnected(paramIdFloorYMod);
    const bool isMidMod = isParamInputConnected(paramIdMidYMod);
    const bool isCeilingMod = isParamInputConnected(paramIdCeilingYMod);

    // CRITICAL: Read and SAVE all CV values BEFORE clearing buffer (in-place processing protection)
    const float savedRateCV = (isRateMod && inBus.getNumChannels() > 1) ? inBus.getReadPointer(1)[0] : 0.0f;
    const float savedFloorCV = (isFloorMod && inBus.getNumChannels() > 2) ? inBus.getReadPointer(2)[0] : 0.0f;
    const float savedMidCV = (isMidMod && inBus.getNumChannels() > 3) ? inBus.getReadPointer(3)[0] : 0.0f;
    const float savedCeilingCV = (isCeilingMod && inBus.getNumChannels() > 4) ? inBus.getReadPointer(4)[0] : 0.0f;
    
    // Check for reset trigger BEFORE clearing
    const bool resetTriggered = (inBus.getNumChannels() > 0 && inBus.getReadPointer(0)[0] > 0.5f);
    
    // NOW it's safe to clear the output buffer
    buffer.clear();

    // --- Get Base Parameter Values ---
    const bool sync = apvts.getRawParameterValue(paramIdSync)->load() > 0.5f;
    const float baseRate = rateParam->load();
    float finalThresholds[3] = { floorYParam->load(), midYParam->load(), ceilingYParam->load() };
    
    // --- Calculate Final Values (using saved CV values) ---
    float finalRate = baseRate;
    if (isRateMod)
    {
        // CV (0..1) scales the rate from its minimum to its maximum
        auto range = apvts.getParameterRange(paramIdRate);
        finalRate = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, savedRateCV));
    }

    if (isFloorMod)   finalThresholds[0] = juce::jlimit(0.0f, 1.0f, savedFloorCV);
    if (isMidMod)     finalThresholds[1] = juce::jlimit(0.0f, 1.0f, savedMidCV);
    if (isCeilingMod) finalThresholds[2] = juce::jlimit(0.0f, 1.0f, savedCeilingCV);

    // --- Update UI Telemetry ---
    setLiveParamValue("rate_live", finalRate);
    setLiveParamValue("floorY_live", finalThresholds[0]);
    setLiveParamValue("midY_live", finalThresholds[1]);
    setLiveParamValue("ceilingY_live", finalThresholds[2]);

    // Apply reset trigger if it was detected
    if (resetTriggered)
    {
        playheadPosition = 0.0;
        phase = 0.0;
        previousPlayheadPos = 0.0;
        previousStrokeY = 0.5f;
        juce::Logger::writeToLog("[StrokeSeq] RESET triggered");
    }

    // --- Calculate Playhead Increment (DJ Platter - Always Spinning Underneath) ---
    double increment = 0.0;
    
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
        // FREE-RUNNING MODE: Calculate increment per sample
        increment = (sampleRate > 0.0 ? (double)finalRate / sampleRate : 0.0);
    }

    // --- Main processing loop ---
    // Get direct write pointers to output channels
    auto* floorTrigOut = buffer.getWritePointer(0);
    auto* midTrigOut = buffer.getWritePointer(1);
    auto* ceilTrigOut = buffer.getWritePointer(2);
    auto* continuousPitchOut = buffer.getWritePointer(3);
    auto* floorPitchOut = buffer.getWritePointer(4);
    auto* midPitchOut = buffer.getWritePointer(5);
    auto* ceilPitchOut = buffer.getWritePointer(6);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Playhead control: Manual drag has priority, otherwise auto-advance
        if (isUnderManualControl.load())
        {
            playheadPosition = juce::jlimit(0.0, 1.0, (double)playheadParam->load());
        }
        else if (increment > 0.0)
        {
            playheadPosition += increment;
            if (playheadPosition >= 1.0)
                playheadPosition -= 1.0;
            phase = playheadPosition;
        }
        
        // Find Stroke Value at Playhead
        float currentStrokeY = 0.0f; // 0 when no active segment
        float floorPitchThisSample = 0.0f;
        float midPitchThisSample = 0.0f;
        float ceilPitchThisSample = 0.0f;
        const bool justWrapped = (playheadPosition < previousPlayheadPos);
        if (justWrapped)
        {
            m_hasTriggeredThisSegment = { false, false, false };
            m_activeStrokeIndex = -1;
            m_isPrimed = false;
        }
        if (audioStrokePoints.size() > 1)
        {
            auto it = std::lower_bound(audioStrokePoints.begin(), audioStrokePoints.end(), (float)playheadPosition, 
                                       [](const StrokePoint& p, float val) { return p.x < val; });
            
            if (it == audioStrokePoints.begin())
            {
                // Off-stroke at the very beginning; do not interpolate
                currentStrokeY = 0.0f;
                m_activeStrokeIndex = -1;
                m_isPrimed = false;
                m_activeStrokeCrosses = { false, false, false };
            }
            else if (it == audioStrokePoints.end())
            {
                // Off-stroke at the very end; do not interpolate
                currentStrokeY = 0.0f;
                m_activeStrokeIndex = -1;
                m_isPrimed = false;
                m_activeStrokeCrosses = { false, false, false };
            }
            else
            {
                // Between two points p1 and p2
                const auto p1 = *(it - 1);
                const auto p2 = *it;

                // Determine stroke indices for both endpoints
                const int p1Index = (int)std::distance(audioStrokePoints.begin(), it) - 1;
                const int p2Index = p1Index + 1;
                const int strokeIdx1 = (p1Index >= 0 && p1Index < (int)audioPointToStrokeIndex.size()) ? audioPointToStrokeIndex[p1Index] : -1;
                const int strokeIdx2 = (p2Index >= 0 && p2Index < (int)audioPointToStrokeIndex.size()) ? audioPointToStrokeIndex[p2Index] : -1;

                // Only interpolate inside a continuous stroke; treat gaps as off-stroke
                if (strokeIdx1 == strokeIdx2 && strokeIdx1 >= 0)
                {
                    const float t = (playheadPosition - p1.x) / (p2.x - p1.x);
                    currentStrokeY = juce::jmap(t, p1.y, p2.y);

                    if (strokeIdx1 != m_activeStrokeIndex)
                    {
                        m_activeStrokeIndex = strokeIdx1;
                        m_hasTriggeredThisSegment = { false, false, false };
                        m_isPrimed = false; // new stroke: require one stable sample

                        // Determine held pitch crossing for this stroke using precomputed min/max
                        if (m_activeStrokeIndex >= 0 && m_activeStrokeIndex < (int)audioStrokeInfo.size())
                        {
                            const auto& info = audioStrokeInfo[m_activeStrokeIndex];
                            const float threshFloor = finalThresholds[0];
                            const float threshMid   = finalThresholds[1];
                            const float threshCeil  = finalThresholds[2];
                            m_activeStrokeCrosses[0] = (info.minY - threshFloor) * (info.maxY - threshFloor) < 0.0f;
                            m_activeStrokeCrosses[1] = (info.minY - threshMid)   * (info.maxY - threshMid)   < 0.0f;
                            m_activeStrokeCrosses[2] = (info.minY - threshCeil)  * (info.maxY - threshCeil)  < 0.0f;
                        }
                        else
                        {
                            m_activeStrokeCrosses = { false, false, false };
                        }
                    }
                }
                else
                {
                    // Gap between strokes
                    currentStrokeY = 0.0f;
                    m_activeStrokeIndex = -1;
                    m_isPrimed = false;
                    m_activeStrokeCrosses = { false, false, false };
                }
            }
        }
        else if (!audioStrokePoints.empty())
        {
            currentStrokeY = audioStrokePoints[0].y;
        }

        currentStrokeYValue.store(currentStrokeY);
        // Continuous pitch (live under playhead)
        m_continuousPitchCV.store(currentStrokeY);
        continuousPitchOut[i] = currentStrokeY;
        // Held per-line pitch: output for duration of stroke if stroke crosses that line
        floorPitchOut[i] = m_activeStrokeCrosses[0] ? currentStrokeY : 0.0f;
        midPitchOut[i]   = m_activeStrokeCrosses[1] ? currentStrokeY : 0.0f;
        ceilPitchOut[i]  = m_activeStrokeCrosses[2] ? currentStrokeY : 0.0f;

        // === Line Segment Intersection Detection ===
        // Check if playhead movement from previous position to current position crosses any threshold
        
        // Test Floor threshold
        if (!justWrapped && m_isPrimed && !m_hasTriggeredThisSegment[0] && lineSegmentCrossesHorizontalLine(
                static_cast<float>(previousPlayheadPos), previousStrokeY,
                static_cast<float>(playheadPosition), currentStrokeY,
                finalThresholds[0]))
        {
            floorTrigOut[i] = 1.0f; // Trigger!
            juce::Logger::writeToLog("[StrokeSeq] *** FLOOR TRIGGER at sample " + juce::String(i));
            m_currentPitchCV[0].store(currentStrokeY);
            m_hasTriggeredThisSegment[0] = true;
        }
        
        // Test Mid threshold
        if (!justWrapped && m_isPrimed && !m_hasTriggeredThisSegment[1] && lineSegmentCrossesHorizontalLine(
                static_cast<float>(previousPlayheadPos), previousStrokeY,
                static_cast<float>(playheadPosition), currentStrokeY,
                finalThresholds[1]))
        {
            midTrigOut[i] = 1.0f; // Trigger!
            juce::Logger::writeToLog("[StrokeSeq] *** MID TRIGGER at sample " + juce::String(i));
            m_currentPitchCV[1].store(currentStrokeY);
            m_hasTriggeredThisSegment[1] = true;
        }
        
        // Test Ceiling threshold
        if (!justWrapped && m_isPrimed && !m_hasTriggeredThisSegment[2] && lineSegmentCrossesHorizontalLine(
                static_cast<float>(previousPlayheadPos), previousStrokeY,
                static_cast<float>(playheadPosition), currentStrokeY,
                finalThresholds[2]))
        {
            ceilTrigOut[i] = 1.0f; // Trigger!
            juce::Logger::writeToLog("[StrokeSeq] *** CEILING TRIGGER at sample " + juce::String(i));
            m_currentPitchCV[2].store(currentStrokeY);
            m_hasTriggeredThisSegment[2] = true;
        }

        // Prime state: if we are on a stroke this sample, allow triggers next sample
        // Determine if on stroke by comparing active stroke index validity
        // When no interpolation occurred (no segment), currentStrokeIndex remains undefined; infer from m_activeStrokeIndex
        if (m_activeStrokeIndex >= 0)
            m_isPrimed = true;
        else
            m_isPrimed = false;
        
        // Store current position for next sample
        previousStrokeY = currentStrokeY;
        previousPlayheadPos = playheadPosition;
    }
    
    // Report live playhead position for UI
    livePlayheadPosition.store(playheadPosition);
}

void StrokeSequencerModuleProcessor::timerCallback()
{
    // This timer is just for the UI, to force an update
    // It's a simple way to get the playhead to animate smoothly
    if (auto* editor = getActiveEditor())
        editor->repaint();
}

void StrokeSequencerModuleProcessor::clearStrokes()
{
    userStrokes.clear();
    audioStrokePoints.clear();
    strokeDataDirty = true;
}

// ==============================================================================
// Line Segment Intersection Math
// ==============================================================================

bool StrokeSequencerModuleProcessor::lineSegmentCrossesHorizontalLine(float x1, float y1, float x2, float y2, float lineY) const
{
    // Check if line segment from (x1, y1) to (x2, y2) crosses horizontal line at y = lineY
    // Trigger on crossings in EITHER direction (above->below or below->above)
    const float d1 = y1 - lineY;
    const float d2 = y2 - lineY;
    if (d1 * d2 < 0.0f)
    {
        juce::Logger::writeToLog("[StrokeSeq] CROSS: y1=" + juce::String(y1, 4) +
                                 " -> y2=" + juce::String(y2, 4) + " crosses lineY=" + juce::String(lineY, 4));
        return true;
    }

    return false;
}

#if defined(PRESET_CREATOR_UI)
void StrokeSequencerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // Helper for tooltips
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

    // --- 80s DIGITAL WATCH AESTHETIC ---
    // Bold retro title with LCD-style yellow-green color
    ImGui::TextColored(ImVec4(0.9f, 0.95f, 0.2f, 1.0f), "STROKE SEQUENCER");
    ImGui::Spacing();
    
    // --- TIMING SECTION ---
    ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.3f, 1.0f), "TIMING");
    ImGui::Spacing();
    
    bool sync = apvts.getRawParameterValue(paramIdSync)->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Transport", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSync))) *p = sync;
        onModificationEnded();
    }
    HelpMarker("Sync playhead to DAW transport. When off, runs in free-running mode.");
    
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
    ImGui::Spacing();
    ImGui::Spacing();
    
    // --- DRAWING CANVAS SECTION ---
    ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.3f, 1.0f), "DISPLAY");
    ImGui::Spacing();
    
    ImGui::BeginGroup(); // Group canvas and sliders together

    // --- Canvas ---
    ImVec2 canvas_size(840.0f, 360.0f);
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);
    auto* draw_list = ImGui::GetWindowDrawList();

    // 80s LCD Yellow background with dark border
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(180, 196, 91, 255)); // Retro yellow LCD
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(60, 55, 20, 255), 0.0f, 0, 3.0f); // Dark brownish border
    
    ImGui::InvisibleButton("##canvas", canvas_size);
    const bool is_hovered = ImGui::IsItemHovered();
    ImVec2 mouse_pos_in_canvas = ImVec2(ImGui::GetIO().MousePos.x - canvas_p0.x, ImGui::GetIO().MousePos.y - canvas_p0.y);

    // --- Mouse Input ---

    // ERASER LOGIC (Right Mouse Drag)
    if (is_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        isDrawing = false; // Stop any drawing action
        const float eraseRadius = 15.0f;
        juce::Point<float> mousePos = { mouse_pos_in_canvas.x, mouse_pos_in_canvas.y };
        bool needsUpdate = false;

        std::vector<std::vector<StrokePoint>> newStrokes;
        for (const auto& stroke : userStrokes)
        {
            std::vector<StrokePoint> currentSegment;
            bool segmentModified = false;
            for (const auto& point : stroke)
            {
                juce::Point<float> pCanvas = { point.x * canvas_size.x, (1.0f - point.y) * canvas_size.y };
                if (mousePos.getDistanceFrom(pCanvas) < eraseRadius)
                {
                    if (currentSegment.size() > 1) newStrokes.push_back(currentSegment);
                    currentSegment.clear();
                    segmentModified = true;
                }
                else
                {
                    currentSegment.push_back(point);
                }
            }
            if (currentSegment.size() > 1) newStrokes.push_back(currentSegment);
            if (segmentModified) needsUpdate = true;
        }

        if (needsUpdate)
        {
            userStrokes = newStrokes;
            strokeDataDirty = true;
        }
    }
    
    // DRAWING LOGIC (Left Mouse Button)
    else if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        isDrawing = true;
        userStrokes.emplace_back(); // Start a new stroke
    }

    if (isDrawing && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (!userStrokes.empty())
        {
            float x = juce::jlimit(0.0f, 1.0f, mouse_pos_in_canvas.x / canvas_size.x);
            float y = 1.0f - juce::jlimit(0.0f, 1.0f, mouse_pos_in_canvas.y / canvas_size.y);
            userStrokes.back().push_back({x, y});
        }
    }

    if (isDrawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDrawing = false;
        strokeDataDirty = true;
    }

    // --- Rendering ---
    draw_list->PushClipRect(canvas_p0, canvas_p1, true);

    // --- Active Stroke Highlighting Logic (Multi-Stroke Support) ---
    // 1. Get the current playhead position from the audio thread
    float phPos = (float)livePlayheadPosition.load();

    // 2. Create a set to hold the indices of all active strokes
    std::set<size_t> activeStrokeIndices;

    // 3. Iterate through all strokes to find which ones are under the playhead
    for (size_t i = 0; i < userStrokes.size(); ++i)
    {
        const auto& stroke = userStrokes[i];
        if (stroke.size() > 1)
        {
            // A stroke is active if the playhead is between its min and max X value
            auto minMax = std::minmax_element(stroke.begin(), stroke.end(), 
                                              [](const StrokePoint& a, const StrokePoint& b) { return a.x < b.x; });
            
            if (phPos >= minMax.first->x && phPos <= minMax.second->x)
            {
                activeStrokeIndices.insert(i);
            }
        }
    }

    // --- Draw all Strokes ---
    // 4. Use the set to determine the color for each stroke
    for (size_t i = 0; i < userStrokes.size(); ++i)
    {
        const auto& stroke = userStrokes[i];
        if (stroke.size() > 1)
        {
            // Check if the current stroke's index is in our set of active strokes
            ImU32 color = activeStrokeIndices.count(i)
                            ? IM_COL32(255, 80, 0, 255)    // Bright orange-red (active LCD segment)
                            : IM_COL32(80, 75, 25, 220);   // Dark olive-brown (inactive LCD)
            
            float thickness = activeStrokeIndices.count(i) ? 4.0f : 2.8f;  // Thicker for active
            
            std::vector<ImVec2> pointsForImGui;
            for (const auto& p : stroke)
                pointsForImGui.emplace_back(canvas_p0.x + p.x * canvas_size.x, canvas_p0.y + (1.0f - p.y) * canvas_size.y);
            draw_list->AddPolyline(pointsForImGui.data(), pointsForImGui.size(), color, 0, thickness);
        }
    }

    // Draw Threshold Lines with 80s digital watch colors and fading gradients
    float liveThresholds[3] = {
        isParamModulated(paramIdFloorYMod) ? getLiveParamValueFor(paramIdFloorYMod, "floorY_live", floorYParam->load()) : floorYParam->load(),
        isParamModulated(paramIdMidYMod)   ? getLiveParamValueFor(paramIdMidYMod, "midY_live", midYParam->load())     : midYParam->load(),
        isParamModulated(paramIdCeilingYMod)? getLiveParamValueFor(paramIdCeilingYMod, "ceilingY_live", ceilingYParam->load()) : ceilingYParam->load()
    };
    // Retro digital watch colors - darker tones on yellow LCD (50% more transparent)
    ImU32 colors[3] = { 
        IM_COL32(160, 50, 30, 100),    // Dark red-brown
        IM_COL32(50, 120, 40, 100),    // Dark olive-green
        IM_COL32(40, 70, 140, 100)     // Dark blue
    };
    
    // Draw fading gradient zones under each threshold line
    for (int t = 0; t < 3; ++t) {
        float y = canvas_p0.y + (1.0f - liveThresholds[t]) * canvas_size.y;
        
        // Gradient below the line (fades downward)
        float gradientHeight = 40.0f;
        ImU32 colorTop = colors[t];
        ImU32 colorBottom = IM_COL32(
            (colors[t] >> IM_COL32_R_SHIFT) & 0xFF,
            (colors[t] >> IM_COL32_G_SHIFT) & 0xFF,
            (colors[t] >> IM_COL32_B_SHIFT) & 0xFF,
            0  // Fully transparent
        );
        
        draw_list->AddRectFilledMultiColor(
            ImVec2(canvas_p0.x, y),
            ImVec2(canvas_p1.x, juce::jmin(y + gradientHeight, canvas_p1.y)),
            colorTop, colorTop, colorBottom, colorBottom
        );
        
        // Draw the threshold line on top of gradient
        draw_list->AddLine({canvas_p0.x, y}, {canvas_p1.x, y}, colors[t], 2.0f);
    }

    // Draw Playhead with bright 80s style
    float playheadX = canvas_p0.x + (float)playheadPosition * canvas_size.x;
    draw_list->AddLine({playheadX, canvas_p0.y}, {playheadX, canvas_p1.y}, IM_COL32(255, 0, 100, 255), 3.0f); // Hot pink!

    // Eraser Visual Feedback (Right Mouse Button) - 80s neon style
    if (is_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const float eraseRadius = 15.0f;
        ImVec2 center = ImVec2(canvas_p0.x + mouse_pos_in_canvas.x, canvas_p0.y + mouse_pos_in_canvas.y);
        draw_list->AddCircleFilled(center, eraseRadius, IM_COL32(255, 0, 100, 80)); // Hot pink fill
        draw_list->AddCircle(center, eraseRadius, IM_COL32(255, 0, 150, 220), 0, 2.5f); // Magenta outline
    }

    draw_list->PopClipRect();
    
    // --- PLAYHEAD SLIDER (Manual Control Only - No CV Input) ---
    ImGui::PushItemWidth(canvas_size.x);

    // The slider's visual position is ALWAYS driven by the live playhead from the audio thread
    float displayValue = (float)livePlayheadPosition.load();

    // Apply 80s retro styling to the playhead slider
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.28f, 0.1f, 0.7f)); // Dark yellow-brown
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.4f, 0.38f, 0.15f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.5f, 0.48f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 0.0f, 0.4f, 1.0f)); // Hot pink grab
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.3f, 0.6f, 1.0f));

    if (ImGui::SliderFloat("##Playhead", &displayValue, 0.0f, 1.0f, "%.3f"))
    {
        // When the user drags, we update the parameter
        *playheadParam = displayValue;
    }

    // Detect when the user grabs and releases the slider
    if (ImGui::IsItemActivated())
    {
        isUnderManualControl = true; // Tell audio thread to stop auto-incrementing
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        isUnderManualControl = false; // Tell audio thread to resume auto-incrementing
        onModificationEnded();
    }

    ImGui::PopStyleColor(5);
    ImGui::PopItemWidth();
    
    ImGui::EndGroup(); // End the canvas group

    // --- Vertical Sliders to the Right ---
    ImGui::SameLine(); // Place sliders to the right of the canvas group

    ImGui::BeginGroup();
    {
        const ImVec2 sliderSize(18.0f, canvas_size.y);
        
        // Helper lambda for a color-coded vertical modulated slider
        auto createV_Slider = [&](const char* label, const juce::String& paramID, const juce::String& modID, 
                                   std::atomic<float>* paramPtr, const char* liveKey, ImVec4 sliderColor) {
            const bool isMod = isParamModulated(modID);
            float value = isMod ? getLiveParamValueFor(modID, liveKey, paramPtr->load()) : paramPtr->load();

            // Apply 80s retro color scheme to slider
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(sliderColor.x * 0.3f, sliderColor.y * 0.3f, sliderColor.z * 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(sliderColor.x * 0.5f, sliderColor.y * 0.5f, sliderColor.z * 0.5f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, sliderColor);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, sliderColor);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(sliderColor.x * 1.2f, sliderColor.y * 1.2f, sliderColor.z * 1.2f, 1.0f));
            
            if (isMod) ImGui::BeginDisabled();
            // Note the min/max are inverted to match the canvas (1.0 at top)
            if (ImGui::VSliderFloat(label, sliderSize, &value, 0.0f, 1.0f, "")) {
                if (!isMod) *paramPtr = value;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && !isMod) onModificationEnded();
            if (isMod) ImGui::EndDisabled();
            
            ImGui::PopStyleColor(5);
        };

        // Color-coded sliders matching threshold line colors
        createV_Slider("##floor", paramIdFloorY, paramIdFloorYMod, floorYParam, "floorY_live", 
                      ImVec4(0.8f, 0.25f, 0.15f, 1.0f)); // Red-brown
        ImGui::SameLine();
        createV_Slider("##mid", paramIdMidY, paramIdMidYMod, midYParam, "midY_live",
                      ImVec4(0.25f, 0.6f, 0.2f, 1.0f)); // Olive-green
        ImGui::SameLine();
        createV_Slider("##ceiling", paramIdCeilingY, paramIdCeilingYMod, ceilingYParam, "ceilingY_live",
                      ImVec4(0.2f, 0.35f, 0.7f, 1.0f)); // Blue
    }
    ImGui::EndGroup();

    // --- CANVAS CONTROLS ---
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.3f, 1.0f), "CONTROLS");
    ImGui::Spacing();
    
    // 80s style Clear button - bright orange/red
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.4f, 0.1f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.5f, 0.15f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.3f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow text
    if (ImGui::Button("CLEAR", ImVec2(100, 0))) 
    { 
        clearStrokes();
    }
    ImGui::PopStyleColor(4);
    HelpMarker("Clear all strokes from the display.");
    
    ImGui::SameLine();
    
    // Build Drum Kit Quick-Connect Button (80s blue style)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.85f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.95f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.8f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("BUILD DRUM KIT", ImVec2(150, 0)))
    {
        autoBuildDrumKitTriggered = true;
    }
    ImGui::PopStyleColor(4);
    HelpMarker("Auto-create 3 samplers + mixer, wire triggers to pads.");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === STROKE PRESET MANAGEMENT ===
    ImGui::TextColored(ImVec4(0.9f, 0.95f, 0.2f, 1.0f), "STROKE PRESETS");
    
    auto& presetManager = ControllerPresetManager::get();
    const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::StrokeSequencer);
    
    // UI SYNCHRONIZATION: On first draw after loading, find the index for the saved preset name
    if (activeStrokePresetName.isNotEmpty())
    {
        selectedStrokePresetIndex = presetNames.indexOf(activeStrokePresetName);
        activeStrokePresetName = ""; // Clear so we only do this once
    }
    
    // Create a C-style array of char pointers for the ImGui combo box
    std::vector<const char*> names;
    for (const auto& name : presetNames)
        names.push_back(name.toRawUTF8());
    
    // Add a placeholder if no presets exist
    if (names.empty())
        names.push_back("<no presets>");
    
    // Dropdown menu
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##StrokePreset", &selectedStrokePresetIndex, names.data(), (int)names.size()))
    {
        // When a preset is selected, load it
        if (selectedStrokePresetIndex >= 0 && selectedStrokePresetIndex < (int)presetNames.size())
        {
            activeStrokePresetName = presetNames[selectedStrokePresetIndex];
            juce::ValueTree presetData = presetManager.loadPreset(ControllerPresetManager::ModuleType::StrokeSequencer, activeStrokePresetName);
            setExtraStateTree(presetData);
            onModificationEnded(); // Create an undo state
        }
    }
    
    // "Save" button
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Save##strokepreset"))
    {
        ImGui::OpenPopup("Save Stroke Preset");
    }
    ImGui::PopStyleColor(3);
    
    // "Delete" button
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Delete##strokepreset"))
    {
        if (selectedStrokePresetIndex >= 0 && selectedStrokePresetIndex < (int)presetNames.size())
        {
            presetManager.deletePreset(ControllerPresetManager::ModuleType::StrokeSequencer, presetNames[selectedStrokePresetIndex]);
            selectedStrokePresetIndex = -1; // Deselect
            activeStrokePresetName = ""; // Clear active name
        }
    }
    ImGui::PopStyleColor(3);
    
    // Save popup
    if (ImGui::BeginPopup("Save Stroke Preset"))
    {
        ImGui::InputText("Preset Name", strokePresetNameBuffer, sizeof(strokePresetNameBuffer));
        if (ImGui::Button("Save New##confirm"))
        {
            juce::String name(strokePresetNameBuffer);
            if (name.isNotEmpty())
            {
                presetManager.savePreset(ControllerPresetManager::ModuleType::StrokeSequencer, name, getExtraStateTree());
                activeStrokePresetName = name; // Mark this new preset as active
                selectedStrokePresetIndex = presetNames.indexOf(activeStrokePresetName); // Resync UI
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##strokepreset"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.3f, 1.0f), "L-CLICK: DRAW  |  R-CLICK: ERASE");
}
#endif

// --- State Management ---
juce::ValueTree StrokeSequencerModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree state("StrokeSequencerState");
    
    // Save the active preset name
    #if defined(PRESET_CREATOR_UI)
    if (selectedStrokePresetIndex >= 0)
    {
        auto& presetManager = ControllerPresetManager::get();
        const auto& presetNames = presetManager.getPresetNamesFor(ControllerPresetManager::ModuleType::StrokeSequencer);
        if (selectedStrokePresetIndex < presetNames.size())
            state.setProperty("strokePreset", presetNames[selectedStrokePresetIndex], nullptr);
    }
    #endif
    
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
        // Load the preset name for UI synchronization
        activeStrokePresetName = state.getProperty("strokePreset", "").toString();
        
        clearStrokes(); // Use the helper to properly reset all state
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

