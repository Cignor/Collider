#include "AutomationLaneModuleProcessor.h"
#include <cmath>
#include <algorithm>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/theme/ThemeManager.h"
#endif

// Helper function for sorting chunks
static bool compareChunks(const AutomationChunk::Ptr& a, const AutomationChunk::Ptr& b)
{
    return a->startBeat < b->startBeat;
}

AutomationLaneModuleProcessor::AutomationLaneModuleProcessor()
    : ModuleProcessor(
          BusesProperties().withOutput("Output", juce::AudioChannelSet::discreteChannels(4), true)),
      apvts(*this, nullptr, "AutomationLaneParams", createParameterLayout())
{
    // Initialize default state with one empty chunk
    auto initialState = std::make_shared<AutomationState>();
    auto firstChunk = std::make_shared<AutomationChunk>(0.0, 32, 256); // 32 beats, 256 samples/beat
    initialState->chunks.push_back(firstChunk);
    initialState->totalDurationBeats = 32.0;

    activeState.store(initialState);

    // Cache parameter pointers
    rateParam = apvts.getRawParameterValue(paramIdRate);
    modeParam = apvts.getRawParameterValue(paramIdMode);
    loopParam = apvts.getRawParameterValue(paramIdLoop);
    divisionParam = apvts.getRawParameterValue(paramIdDivision);
    durationModeParam = apvts.getRawParameterValue(paramIdDurationMode);
    customDurationParam = apvts.getRawParameterValue(paramIdCustomDuration);

#if defined(PRESET_CREATOR_UI)
    // Initialize UI state
    lastMousePosInCanvas = ImVec2(-1.0f, -1.0f);
#endif
}

juce::AudioProcessorValueTreeState::ParameterLayout AutomationLaneModuleProcessor::
    createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate (Hz)", 0.01f, 20.0f, 1.0f));
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdMode, "Mode", juce::StringArray{"Free (Hz)", "Sync"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdLoop, "Loop", true));
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdZoom, "Zoom", 10.0f, 200.0f, 50.0f)); // Pixels per beat
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdRecordMode, "Record Mode", juce::StringArray{"Record", "Edit"}, 0));

    // Division choices: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8
    juce::StringArray divs = {
        "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDivision, "Division", divs, 3)); // Default 1/4

    // Duration mode: User Choice, 1 Bar, 4 Bars, 8 Bars, 16 Bars, 32 Bars
    juce::StringArray durationModes = {
        "User Choice", "1 Bar", "4 Bars", "8 Bars", "16 Bars", "32 Bars"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDurationMode, "Duration", durationModes, 0)); // Default User Choice

    // Custom duration in beats (for User Choice mode)
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdCustomDuration,
            "Custom Duration (beats)",
            4.0f,
            256.0f,
            32.0f)); // Default 32 beats

    return {params.begin(), params.end()};
}

void AutomationLaneModuleProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
}

void AutomationLaneModuleProcessor::setTimingInfo(const TransportState& state)
{
    m_currentTransport = state;
}

void AutomationLaneModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Get output pointers only if channels exist
    auto* outValue = (numChannels > OUTPUT_VALUE) ? buffer.getWritePointer(OUTPUT_VALUE) : nullptr;
    auto* outInverted =
        (numChannels > OUTPUT_INVERTED) ? buffer.getWritePointer(OUTPUT_INVERTED) : nullptr;
    auto* outBipolar =
        (numChannels > OUTPUT_BIPOLAR) ? buffer.getWritePointer(OUTPUT_BIPOLAR) : nullptr;
    auto* outPitch = (numChannels > OUTPUT_PITCH) ? buffer.getWritePointer(OUTPUT_PITCH) : nullptr;

    // Atomic load of the state
    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    // Null checks for parameter pointers
    if (!modeParam || !rateParam || !loopParam)
        return;

    bool  isSync = *modeParam > 0.5f;
    float rateHz = *rateParam;

    // Calculate phase increment
    double phaseInc = 0.0;
    if (!isSync)
    {
        phaseInc = (rateHz / sampleRate);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        double currentBeat = 0.0;

        if (isSync)
        {
            if (m_currentTransport.isPlaying)
            {
                // Sync Mode: Lock to host transport
                currentBeat = m_currentTransport.songPositionBeats;
            }
            else
            {
                // Transport stopped: stay at current position
                currentBeat = m_currentTransport.songPositionBeats;
            }
        }
        else
        {
            // Free Mode: LFO behavior - use target duration
            double duration = getTargetDuration();

            // Increment phase (0..1)
            currentPhase += (rateHz / sampleRate);
            if (currentPhase >= 1.0)
                currentPhase -= 1.0;

            currentBeat = currentPhase * duration;
        }

        // Loop logic - use target duration
        double loopDuration = getTargetDuration();
        if (isSync && *loopParam > 0.5f && loopDuration > 0)
        {
            currentBeat = std::fmod(currentBeat, loopDuration);
        }

        // Sample lookup
        float value = 0.5f;
        auto  chunk = state->findChunkAt(currentBeat);
        if (chunk)
        {
            double beatInChunk = currentBeat - chunk->startBeat;
            int    sampleIndex = static_cast<int>(beatInChunk * chunk->samplesPerBeat);
            if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size())
            {
                value = chunk->samples[sampleIndex];
            }
        }

        // Output
        if (outValue)
            outValue[i] = value;
        if (outInverted)
            outInverted[i] = 1.0f - value;
        if (outBipolar)
            outBipolar[i] = (value * 2.0f) - 1.0f;
        if (outPitch)
            outPitch[i] = (value * 10.0f); // 0-10V range
    }
}

std::optional<RhythmInfo> AutomationLaneModuleProcessor::getRhythmInfo() const
{
    return std::nullopt;
}

// --- State Management ---

juce::ValueTree AutomationLaneModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree      vt("AutomationLaneState");
    AutomationState::Ptr state = activeState.load();

    if (state)
    {
        vt.setProperty("totalDurationBeats", state->totalDurationBeats, nullptr);

        for (const auto& chunk : state->chunks)
        {
            juce::ValueTree chunkVt("Chunk");
            chunkVt.setProperty("startBeat", chunk->startBeat, nullptr);
            chunkVt.setProperty("numBeats", chunk->numBeats, nullptr);
            chunkVt.setProperty("samplesPerBeat", chunk->samplesPerBeat, nullptr);

            // Serialize samples to MemoryBlock
            if (!chunk->samples.empty())
            {
                juce::MemoryBlock mb;
                mb.append(chunk->samples.data(), chunk->samples.size() * sizeof(float));
                chunkVt.setProperty("samples", mb, nullptr);
            }

            vt.addChild(chunkVt, -1, nullptr);
        }
    }

    return vt;
}

void AutomationLaneModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("AutomationLaneState"))
    {
        auto newState = std::make_shared<AutomationState>();
        newState->totalDurationBeats = vt.getProperty("totalDurationBeats", 32.0);

        // Load chunks
        for (const auto& chunkVt : vt)
        {
            if (chunkVt.hasType("Chunk"))
            {
                double startBeat = chunkVt.getProperty("startBeat");
                double numBeats = chunkVt.getProperty("numBeats");
                double samplesPerBeat = chunkVt.getProperty("samplesPerBeat");

                auto chunk = std::make_shared<AutomationChunk>(
                    startBeat, static_cast<int>(numBeats), static_cast<int>(samplesPerBeat));

                // Load samples
                if (chunkVt.hasProperty("samples"))
                {
                    juce::MemoryBlock* mb = chunkVt.getProperty("samples").getBinaryData();
                    if (mb && mb->getSize() > 0)
                    {
                        size_t numFloats = mb->getSize() / sizeof(float);
                        chunk->samples.resize(numFloats);
                        memcpy(chunk->samples.data(), mb->getData(), mb->getSize());
                    }
                }

                // Fallback if samples failed to load or were empty but size is defined
                if (chunk->samples.empty())
                {
                    chunk->samples.resize((size_t)(numBeats * samplesPerBeat), 0.5f);
                }

                newState->chunks.push_back(chunk);
            }
        }

        // Ensure at least one chunk exists
        if (newState->chunks.empty())
        {
            auto firstChunk = std::make_shared<AutomationChunk>(0.0, 32, 256);
            newState->chunks.push_back(firstChunk);
        }

        updateState(newState);
    }
}

bool AutomationLaneModuleProcessor::getParamRouting(
    const juce::String& paramId,
    int&                outBusIndex,
    int&                outChannelIndexInBus) const
{
    return false;
}

juce::String AutomationLaneModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
    case OUTPUT_VALUE:
        return "Value";
    case OUTPUT_INVERTED:
        return "Inverted";
    case OUTPUT_BIPOLAR:
        return "Bipolar";
    case OUTPUT_PITCH:
        return "Pitch";
    default:
        return {};
    }
}

void AutomationLaneModuleProcessor::updateState(AutomationState::Ptr newState)
{
    activeState.store(newState);
}

AutomationState::Ptr AutomationLaneModuleProcessor::getState() const { return activeState.load(); }

double AutomationLaneModuleProcessor::getTargetDuration() const
{
    if (!durationModeParam || !customDurationParam)
        return 32.0; // Default fallback

    int durationModeIndex = (int)*durationModeParam;

    switch (durationModeIndex)
    {
    case 0: // User Choice
        return (double)*customDurationParam;
    case 1: // 1 Bar
        return 4.0;
    case 2: // 4 Bars
        return 16.0;
    case 3: // 8 Bars
        return 32.0;
    case 4: // 16 Bars
        return 64.0;
    case 5: // 32 Bars
        return 128.0;
    default:
        return 32.0; // Default fallback
    }
}

void AutomationLaneModuleProcessor::ensureChunkExistsAt(double beat)
{
    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    // Check if chunk already exists
    if (state->findChunkAt(beat))
        return;

    // Determine chunk parameters
    // We use fixed 32-beat chunks for now
    const double chunkDuration = 32.0;
    const int    samplesPerBeat = 256;

    double chunkStart = std::floor(beat / chunkDuration) * chunkDuration;

    // Double check if we have a chunk starting here
    for (const auto& chunk : state->chunks)
    {
        if (std::abs(chunk->startBeat - chunkStart) < 0.001)
            return;
    }

    // Create new state
    auto newState = std::make_shared<AutomationState>();
    newState->chunks = state->chunks;

    // Create new chunk
    auto newChunk =
        std::make_shared<AutomationChunk>(chunkStart, (int)chunkDuration, samplesPerBeat);
    newState->chunks.push_back(newChunk);

    // Sort chunks
    std::sort(newState->chunks.begin(), newState->chunks.end(), compareChunks);

    // Update total duration
    double maxDuration = 0.0;
    if (!newState->chunks.empty())
    {
        auto lastChunk = newState->chunks.back();
        maxDuration = lastChunk->startBeat + lastChunk->numBeats;
    }
    newState->totalDurationBeats = maxDuration;

    updateState(newState);
}

// Thread-safe helper to modify chunk samples
void AutomationLaneModuleProcessor::modifyChunkSamplesThreadSafe(
    AutomationChunk::Ptr chunk,
    int                  startSampleIndex,
    int                  endSampleIndex,
    float                startValue,
    float                endValue)
{
    if (!chunk || startSampleIndex < 0 || endSampleIndex < 0 ||
        startSampleIndex >= (int)chunk->samples.size() ||
        endSampleIndex >= (int)chunk->samples.size())
        return;

    // Create new state with cloned chunk
    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    auto newState = std::make_shared<AutomationState>();
    newState->totalDurationBeats = state->totalDurationBeats;

    // Clone all chunks, replacing the one we're modifying
    // Use startBeat as unique identifier for chunk matching
    double targetStartBeat = chunk->startBeat;

    for (const auto& oldChunk : state->chunks)
    {
        if (std::abs(oldChunk->startBeat - targetStartBeat) < 0.001)
        {
            // Clone this chunk with modifications
            auto newChunk = std::make_shared<AutomationChunk>(
                chunk->startBeat, chunk->numBeats, chunk->samplesPerBeat);
            newChunk->samples = chunk->samples; // Copy samples

            // Apply interpolation between start and end
            if (startSampleIndex <= endSampleIndex)
            {
                for (int i = startSampleIndex;
                     i <= endSampleIndex && i < (int)newChunk->samples.size();
                     ++i)
                {
                    float t = (endSampleIndex == startSampleIndex)
                                  ? 1.0f
                                  : (float)(i - startSampleIndex) /
                                        (float)(endSampleIndex - startSampleIndex);
                    newChunk->samples[i] = juce::jmap(t, startValue, endValue);
                }
            }

            newState->chunks.push_back(newChunk);
        }
        else
        {
            // Copy other chunks as-is (share pointers is fine, chunks are immutable)
            newState->chunks.push_back(oldChunk);
        }
    }

    updateState(newState);
}

// --- UI Implementation ---

#if defined(PRESET_CREATOR_UI)
void AutomationLaneModuleProcessor::drawParametersInNode(
    float                                           itemWidth,
    const std::function<bool(const juce::String&)>& checkHover,
    const std::function<void()>&                    markEdited)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    ImGui::Text("Automation Lane");

    // --- Top Controls ---
    ImGui::PushItemWidth(80);

    // Rate / Sync Control - add null checks
    if (!modeParam || !rateParam || !divisionParam || !loopParam || !durationModeParam ||
        !customDurationParam)
    {
        ImGui::PopItemWidth();
        ImGui::Text("Initializing...");
        return;
    }

    bool  isSync = *modeParam > 0.5f;
    float currentDivision = 1.0f; // Default 1 beat

    if (isSync)
    {
        // Sync Mode: Show Division
        int         divIndex = (int)*divisionParam;
        const char* divs[] = {
            "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars"};

        // Map index to beats
        switch (divIndex)
        {
        case 0:
            currentDivision = 0.125f;
            break; // 1/32
        case 1:
            currentDivision = 0.25f;
            break; // 1/16
        case 2:
            currentDivision = 0.5f;
            break; // 1/8
        case 3:
            currentDivision = 1.0f;
            break; // 1/4 (1 Beat)
        case 4:
            currentDivision = 2.0f;
            break; // 1/2
        case 5:
            currentDivision = 4.0f;
            break; // 1 Bar
        case 6:
            currentDivision = 8.0f;
            break; // 2 Bars
        case 7:
            currentDivision = 16.0f;
            break; // 4 Bars
        case 8:
            currentDivision = 32.0f;
            break; // 8 Bars
        }

        if (ImGui::Combo("##div", &divIndex, divs, 9))
        {
            *divisionParam = (float)divIndex;
            apvts.getParameter(paramIdDivision)
                ->setValueNotifyingHost(
                    apvts.getParameterRange(paramIdDivision).convertTo0to1((float)divIndex));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Grid Division");
    }
    else
    {
        // Free Mode: Show Hz
        float rate = *rateParam;
        if (ImGui::DragFloat("##rate", &rate, 0.01f, 0.01f, 20.0f, "%.2f Hz"))
        {
            *rateParam = rate;
            apvts.getParameter(paramIdRate)
                ->setValueNotifyingHost(apvts.getParameterRange(paramIdRate).convertTo0to1(rate));
        }
    }

    ImGui::SameLine();

    // Record Mode Toggle
    bool isRec = *apvts.getRawParameterValue(paramIdRecordMode) < 0.5f; // 0=Rec, 1=Edit
    if (ImGui::Button(isRec ? "REC" : "EDIT"))
    {
        float newVal = isRec ? 1.0f : 0.0f;
        apvts.getParameter(paramIdRecordMode)->setValueNotifyingHost(newVal);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(isRec ? "Recording Mode (Auto-scroll)" : "Edit Mode (Manual scroll)");

    ImGui::PopItemWidth();

    // --- Manual Value Slider (Monitor/Record) ---
    // For now, just a visual monitor of the current output value
    static float manualValue = 0.5f;
    ImGui::PushItemWidth(itemWidth - 10);
    ImGui::SliderFloat("##manual", &manualValue, 0.0f, 1.0f, "Value: %.2f");
    ImGui::PopItemWidth();

    // --- Duration Control ---
    ImGui::PushItemWidth(120);
    int         durationModeIndex = (int)*durationModeParam;
    const char* durationModes[] = {
        "User Choice", "1 Bar", "4 Bars", "8 Bars", "16 Bars", "32 Bars"};

    if (ImGui::Combo("##duration", &durationModeIndex, durationModes, 6))
    {
        *durationModeParam = (float)durationModeIndex;
        apvts.getParameter(paramIdDurationMode)
            ->setValueNotifyingHost(apvts.getParameterRange(paramIdDurationMode)
                                        .convertTo0to1((float)durationModeIndex));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Timeline Duration");

    ImGui::SameLine();

    // Show custom duration input if User Choice is selected
    if (durationModeIndex == 0) // User Choice
    {
        float customDur = *customDurationParam;
        ImGui::PushItemWidth(80);
        if (ImGui::DragFloat("##customDur", &customDur, 1.0f, 4.0f, 256.0f, "%.0f beats"))
        {
            *customDurationParam = customDur;
            apvts.getParameter(paramIdCustomDuration)
                ->setValueNotifyingHost(
                    apvts.getParameterRange(paramIdCustomDuration).convertTo0to1(customDur));
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Custom Duration in Beats");
    }

    ImGui::PopItemWidth();

    // --- Timeline Canvas ---
    // Read data BEFORE BeginChild (following VCO pattern)
    AutomationState::Ptr state = activeState.load();
    auto*                zoomParam = apvts.getRawParameterValue(paramIdZoom);

    if (!state || !zoomParam)
    {
        ImGui::Text("Initializing...");
        return;
    }

    // Get target duration and prepare chunks
    double targetDuration = getTargetDuration();
    ensureChunkExistsAt(0.0);
    ensureChunkExistsAt(targetDuration - 0.1);
    state = activeState.load(); // Reload after chunk creation

    float zoom = *zoomParam;
    float totalBeats = (float)targetDuration;
    float totalWidth = totalBeats * zoom;

    // Calculate playhead position
    double currentBeat = 0.0;
    if (isSync && m_currentTransport.isPlaying)
    {
        currentBeat = m_currentTransport.songPositionBeats;
    }
    else
    {
        currentBeat = currentPhase;
    }

    // Loop wrapping for display
    double loopDuration = getTargetDuration();
    if (isSync && *loopParam > 0.5f && loopDuration > 0)
    {
        currentBeat = std::fmod(currentBeat, loopDuration);
    }

    // Use PushID for unique scoping (following VCO pattern)
    ImGui::PushID(this);

    const float            timelineHeight = 150.0f;
    const ImVec2           graphSize(itemWidth, timelineHeight);
    const ImGuiWindowFlags childFlags =
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginChild("AutomationLaneTimeline", graphSize, false, childFlags))
    {
        // Calculate available height for content (excluding scrollbar)
        float canvasHeight = ImGui::GetContentRegionAvail().y;

        // Create dummy for horizontal scrolling (must be inside child)
        ImGui::Dummy(ImVec2(totalWidth, canvasHeight));

        ImDrawList*  drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);

        // Get scroll position
        float scrollX = ImGui::GetScrollX();
        float visibleW = graphSize.x;

        // Background - use theme colors
        const auto resolveColor = [](ImU32 value, ImU32 fallback) {
            return value != 0 ? value : fallback;
        };
        const ImU32 bgColor =
            resolveColor(theme.canvas.canvas_background, IM_COL32(30, 30, 30, 255));
        const ImU32 frameColor =
            resolveColor(theme.canvas.node_frame, IM_COL32(150, 150, 150, 255));

        drawList->AddRectFilled(p0, p1, bgColor);
        drawList->AddRect(p0, p1, frameColor);

        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);

        // Grid Lines (Based on Division)
        double gridStep = currentDivision;
        // If grid is too dense, double it until it's reasonable
        while (gridStep * zoom < 10.0f)
            gridStep *= 2.0;

        int firstGridIdx = (int)((scrollX / zoom) / gridStep);
        int lastGridIdx = (int)(((scrollX + visibleW) / zoom) / gridStep) + 1;

        for (int i = firstGridIdx; i <= lastGridIdx; ++i)
        {
            double beat = i * gridStep;
            float  x = p0.x + (float)(beat * zoom) - scrollX;

            // Determine line style
            bool isBar = (std::fmod(beat, 4.0) < 0.001);
            bool isBeat = (std::fmod(beat, 1.0) < 0.001);

            ImU32 color = IM_COL32(50, 50, 50, 255);
            if (isBar)
                color = IM_COL32(100, 100, 100, 255);
            else if (isBeat)
                color = IM_COL32(70, 70, 70, 255);

            drawList->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), color);

            if (isBar)
            {
                char buf[16];
                sprintf(buf, "%d", (int)(beat / 4) + 1);
                drawList->AddText(ImVec2(x + 2, p0.y), IM_COL32(150, 150, 150, 255), buf);
            }
        }

        // Draw Chunks
        for (const auto& chunk : state->chunks)
        {
            // Culling
            float chunkStartX = p0.x + (float)(chunk->startBeat * zoom) - scrollX;
            float chunkEndX = chunkStartX + (float)(chunk->numBeats * zoom);

            if (chunkEndX < p0.x || chunkStartX > p1.x)
                continue;

            // Draw Curve
            if (chunk->samples.size() > 1)
            {
                for (size_t i = 0; i < chunk->samples.size() - 1; ++i)
                {
                    float b1 = (float)i / chunk->samplesPerBeat;
                    float b2 = (float)(i + 1) / chunk->samplesPerBeat;

                    float x1 = chunkStartX + (b1 * zoom);
                    float x2 = chunkStartX + (b2 * zoom);

                    // Optimization: Skip if pixel x1 == x2
                    if ((int)x1 == (int)x2)
                        continue;

                    float y1 = p0.y + canvasHeight * (1.0f - chunk->samples[i]);
                    float y2 = p0.y + canvasHeight * (1.0f - chunk->samples[i + 1]);

                    // Clamp Y values
                    y1 = juce::jlimit(p0.y, p0.y + canvasHeight, y1);
                    y2 = juce::jlimit(p0.y, p0.y + canvasHeight, y2);

                    // Use bright blue color - bold
                    drawList->AddLine(
                        ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(100, 150, 255, 255), 4.0f);
                }
            }
        }

        // Playhead
        float playheadX = p0.x + (float)(currentBeat * zoom) - scrollX;
        playheadX = juce::jlimit(p0.x, p1.x, playheadX);
        drawList->AddLine(
            ImVec2(playheadX, p0.y),
            ImVec2(playheadX, p0.y + canvasHeight),
            IM_COL32(255, 255, 0, 200),
            2.0f);

        drawList->PopClipRect();

        // Auto-scroll
        if (m_currentTransport.isPlaying && isRec)
        {
            float targetScroll = (float)(currentBeat * zoom) - (visibleW * 0.5f);
            if (targetScroll < 0)
                targetScroll = 0;
            ImGui::SetScrollX(targetScroll);
        }

        // Mouse interaction for drawing - use InvisibleButton to prevent node dragging
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton(
            "##automationLaneCanvasDrag", graphSize, ImGuiButtonFlags_MouseButtonLeft);
        const bool is_hovered = ImGui::IsItemHovered();
        const bool is_active = ImGui::IsItemActive();

        if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDragging = true;
            ImVec2 mousePos = ImGui::GetMousePos();
            lastMousePosInCanvas = ImVec2(mousePos.x - p0.x + scrollX, mousePos.y - p0.y);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (isDragging)
                markEdited(); // Notify undo system
            isDragging = false;
            lastMousePosInCanvas = ImVec2(-1, -1);
        }

        if (isDragging && is_active)
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 current_pos = ImVec2(mousePos.x - p0.x + scrollX, mousePos.y - p0.y);

            // Calculate beat positions for last and current mouse positions
            double beat0 = lastMousePosInCanvas.x / zoom;
            double beat1 = current_pos.x / zoom;

            // Ensure chunks exist at both positions
            ensureChunkExistsAt(beat0);
            ensureChunkExistsAt(beat1);
            state = activeState.load(); // Reload after potential chunk creation

            if (state)
            {
                // Find chunks at both positions
                auto chunk0 = state->findChunkAt(beat0);
                auto chunk1 = state->findChunkAt(beat1);

                if (chunk0 && chunk1)
                {
                    // Normalize Y values (0-1, with 0 at top, 1 at bottom)
                    float y0 =
                        1.0f - juce::jlimit(0.0f, 1.0f, lastMousePosInCanvas.y / canvasHeight);
                    float y1 = 1.0f - juce::jlimit(0.0f, 1.0f, current_pos.y / canvasHeight);

                    // Handle drawing within a single chunk
                    if (chunk0.get() == chunk1.get())
                    {
                        double beatInChunk0 = beat0 - chunk0->startBeat;
                        double beatInChunk1 = beat1 - chunk0->startBeat;

                        int sampleIdx0 = static_cast<int>(beatInChunk0 * chunk0->samplesPerBeat);
                        int sampleIdx1 = static_cast<int>(beatInChunk1 * chunk0->samplesPerBeat);

                        sampleIdx0 = juce::jlimit(0, (int)chunk0->samples.size() - 1, sampleIdx0);
                        sampleIdx1 = juce::jlimit(0, (int)chunk0->samples.size() - 1, sampleIdx1);

                        if (sampleIdx0 > sampleIdx1)
                            std::swap(sampleIdx0, sampleIdx1);

                        // Modify samples with interpolation
                        modifyChunkSamplesThreadSafe(chunk0, sampleIdx0, sampleIdx1, y0, y1);
                        state = activeState.load(); // Reload after modification
                    }
                    else
                    {
                        // Cross-chunk drawing: handle each chunk separately
                        // First chunk: from beat0 to chunk0 end
                        {
                            double beatInChunk0 = beat0 - chunk0->startBeat;
                            int    sampleIdx0 =
                                static_cast<int>(beatInChunk0 * chunk0->samplesPerBeat);
                            int sampleIdx1 = (int)chunk0->samples.size() - 1;

                            sampleIdx0 =
                                juce::jlimit(0, (int)chunk0->samples.size() - 1, sampleIdx0);
                            sampleIdx1 =
                                juce::jlimit(0, (int)chunk0->samples.size() - 1, sampleIdx1);

                            // Interpolate to chunk boundary
                            double chunkEndBeat = chunk0->startBeat + chunk0->numBeats;
                            float  t = (chunkEndBeat - beat0) / (beat1 - beat0);
                            float  yBoundary = juce::jmap((float)t, y0, y1);

                            modifyChunkSamplesThreadSafe(
                                chunk0, sampleIdx0, sampleIdx1, y0, yBoundary);
                        }

                        // Second chunk: from chunk1 start to beat1
                        {
                            double beatInChunk1 = beat1 - chunk1->startBeat;
                            int    sampleIdx0 = 0;
                            int    sampleIdx1 =
                                static_cast<int>(beatInChunk1 * chunk1->samplesPerBeat);

                            sampleIdx0 =
                                juce::jlimit(0, (int)chunk1->samples.size() - 1, sampleIdx0);
                            sampleIdx1 =
                                juce::jlimit(0, (int)chunk1->samples.size() - 1, sampleIdx1);

                            // Interpolate from chunk boundary
                            double chunkStartBeat = chunk1->startBeat;
                            float  t = (chunkStartBeat - beat0) / (beat1 - beat0);
                            float  yBoundary = juce::jmap((float)t, y0, y1);

                            modifyChunkSamplesThreadSafe(
                                chunk1, sampleIdx0, sampleIdx1, yBoundary, y1);
                        }

                        state = activeState.load(); // Reload after modifications
                    }
                }
            }

            lastMousePosInCanvas = current_pos;
        }
    }
    ImGui::EndChild(); // CRITICAL: Must be OUTSIDE the if block!

    ImGui::PopID();
}
#endif
