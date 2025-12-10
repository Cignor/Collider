#include "AutomationLaneModuleProcessor.h"
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
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
          BusesProperties().withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)),
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
    triggerThresholdParam = apvts.getRawParameterValue(paramIdTriggerThreshold);
    triggerEdgeParam = apvts.getRawParameterValue(paramIdTriggerEdge);

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

    // Zoom is purely visual, but we save it as a parameter for convenience
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdZoom, "Zoom", 10.0f, 200.0f, 50.0f)); // Pixels per beat

    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdRecordMode, "Record Mode", juce::StringArray{"Record", "Edit"}, 0));

    // Speed Division choices: 1/32 to 8x
    // This controls how fast the playhead moves relative to the global transport
    juce::StringArray divs = {"1/32", "1/16", "1/8", "1/4", "1/2", "1x", "2x", "4x", "8x"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDivision, "Speed", divs, 5)); // Default 1x

    // Duration mode: User Choice, 1 Bar, 4 Bars, 8 Bars, 16 Bars, 32 Bars
    juce::StringArray durationModes = {
        "User Choice", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars", "32 Bars"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDurationMode, "Duration", durationModes, 3)); // Default 4 Bars

    // Custom duration in beats (for User Choice mode)
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdCustomDuration,
            "Custom Duration (beats)",
            1.0f,
            256.0f,
            16.0f)); // Default 16 beats

    // Trigger threshold (0.0 to 1.0)
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdTriggerThreshold,
            "Trigger Threshold",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.5f));

    // Trigger edge selection (Rising, Falling, Both)
    juce::StringArray edgeChoices = {"Rising", "Falling", "Both"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdTriggerEdge, "Trigger Edge", edgeChoices, 0)); // Default Rising

    return {params.begin(), params.end()};
}

void AutomationLaneModuleProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    // Reset trigger state
    triggerPulseRemaining = 0;
    previousValue = -1.0f; // Use -1 as sentinel to indicate uninitialized
    lastValueAboveThreshold = false;
}

void AutomationLaneModuleProcessor::setTimingInfo(const TransportState& state)
{
    ModuleProcessor::setTimingInfo(state);

    const TransportCommand command = state.lastCommand.load();
    if (command != lastTransportCommand)
    {
        if (command == TransportCommand::Stop)
        {
            // Reset phase to 0 when transport stops
            currentPhase = 0.0;
            // Reset trigger state
            triggerPulseRemaining = 0;
            previousValue = -1.0f; // Mark as uninitialized
            lastValueAboveThreshold = false;
        }
        lastTransportCommand = command;
    }

    m_currentTransport = state;
}

void AutomationLaneModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Clear trigger output channel first
    if (numChannels > OUTPUT_TRIGGER)
        buffer.clear(OUTPUT_TRIGGER, 0, numSamples);

    // Get output pointers only if channels exist
    auto* outValue = (numChannels > OUTPUT_VALUE) ? buffer.getWritePointer(OUTPUT_VALUE) : nullptr;
    auto* outInverted =
        (numChannels > OUTPUT_INVERTED) ? buffer.getWritePointer(OUTPUT_INVERTED) : nullptr;
    auto* outBipolar =
        (numChannels > OUTPUT_BIPOLAR) ? buffer.getWritePointer(OUTPUT_BIPOLAR) : nullptr;
    auto* outPitch = (numChannels > OUTPUT_PITCH) ? buffer.getWritePointer(OUTPUT_PITCH) : nullptr;
    auto* outTrigger =
        (numChannels > OUTPUT_TRIGGER) ? buffer.getWritePointer(OUTPUT_TRIGGER) : nullptr;

    // Atomic load of the state
    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    // Null checks for parameter pointers
    if (!modeParam || !rateParam || !loopParam || !triggerThresholdParam || !triggerEdgeParam)
        return;

    const bool   isSync = *modeParam > 0.5f;
    const float  rateHz = *rateParam;
    const bool   isLooping = *loopParam > 0.5f;
    const double targetDuration = getTargetDuration();
    const float  triggerThreshold = triggerThresholdParam ? triggerThresholdParam->load() : 0.5f;
    const int    triggerEdgeMode = triggerEdgeParam ? (int)triggerEdgeParam->load() : 0;
    double       previousBeat = -1.0; // Track previous beat for loop detection

    for (int i = 0; i < numSamples; ++i)
    {
        // Check Global Reset (pulse from Timeline Master loop)
        if (m_currentTransport.forceGlobalReset.load())
        {
            currentPhase = 0.0;
        }

        double currentBeat = 0.0;

        // --- ROBUST SYNC LOGIC (Matching StepSequencer) ---
        if (isSync && m_currentTransport.isPlaying)
        {
            // SYNC MODE: Use the global beat position with division
            int divisionIndex = (int)*divisionParam;

            // Use global division if a Tempo Clock has override enabled
            // IMPORTANT: Read from parent's LIVE transport state, not cached copy
            if (getParent())
            {
                int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
                if (globalDiv >= 0)
                    divisionIndex = globalDiv;
            }

            // Map indices to speed multipliers
            // "1/32", "1/16", "1/8", "1/4", "1/2", "1x", "2x", "4x", "8x"
            static const double speedMultipliers[] = {
                1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};

            const double speed = speedMultipliers[juce::jlimit(0, 8, divisionIndex)];

            // Calculate beat position: (GlobalBeats * Speed)
            currentBeat = m_currentTransport.songPositionBeats * speed;
        }
        else
        {
            // FREE-RUNNING MODE or TRANSPORT STOPPED
            if (m_currentTransport.isPlaying)
            {
                // Calculate phase increment based on Hz
                // In free mode, rateHz is cycles per second.
                // We map 1 cycle to 'targetDuration' beats.
                const double phaseInc = (sampleRate > 0.0 ? (double)rateHz / sampleRate : 0.0);
                currentPhase += phaseInc;
                if (currentPhase >= 1.0)
                    currentPhase -= 1.0;
            }

            // Map 0..1 phase to 0..Duration beats
            currentBeat = currentPhase * targetDuration;
        }

        // Loop logic - check if we just wrapped
        bool justWrapped = false;
        if (isLooping && targetDuration > 0)
        {
            double wrappedBeat = std::fmod(currentBeat, targetDuration);
            if (previousBeat >= 0.0 && wrappedBeat < previousBeat)
                justWrapped = true; // Loop wrapped
            currentBeat = wrappedBeat;
        }
        else if (!isLooping && currentBeat > targetDuration)
        {
            // Clamp to end if not looping
            currentBeat = targetDuration;
        }

        // Sample lookup
        float value = 0.5f;
        auto  chunk = state->findChunkAt(currentBeat);
        if (chunk)
        {
            double beatInChunk = currentBeat - chunk->startBeat;
            int    sampleIndex = static_cast<int>(beatInChunk * chunk->samplesPerBeat);

            // Linear interpolation for smoother playback
            if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size() - 1)
            {
                float frac = (float)(beatInChunk * chunk->samplesPerBeat - sampleIndex);
                float s0 = chunk->samples[sampleIndex];
                float s1 = chunk->samples[sampleIndex + 1];
                value = s0 + frac * (s1 - s0);
            }
            else if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size())
            {
                value = chunk->samples[sampleIndex];
            }
        }

        // Trigger detection (only when transport is playing)
        if (outTrigger)
        {
            if (m_currentTransport.isPlaying && !justWrapped)
            {
                // Initialize previousValue if uninitialized (first sample or after reset)
                if (previousValue < 0.0f)
                {
                    previousValue = value;
                    lastValueAboveThreshold = (value > triggerThreshold);
                    outTrigger[i] = 0.0f; // No trigger on initialization
                }
                else
                {
                    // Check for threshold crossing
                    bool crossingDetected = lineSegmentCrossesThreshold(
                        previousValue, value, triggerThreshold, triggerEdgeMode);

                    if (crossingDetected)
                    {
                        triggerPulseRemaining = (int)(sampleRate * 0.001); // 1ms pulse
                    }

                    // Output trigger signal
                    outTrigger[i] = (triggerPulseRemaining > 0) ? 1.0f : 0.0f;

                    if (triggerPulseRemaining > 0)
                        --triggerPulseRemaining;

                    // Update state for next sample
                    previousValue = value;
                    lastValueAboveThreshold = (value > triggerThreshold);
                }
            }
            else
            {
                // Clear trigger output when not playing or on wrap
                outTrigger[i] = 0.0f;

                // Reset state on wrap or when transport stops
                if (justWrapped)
                {
                    previousValue = value; // Keep current value on wrap
                    lastValueAboveThreshold = (value > triggerThreshold);
                }
                else if (!m_currentTransport.isPlaying)
                {
                    previousValue = -1.0f; // Mark as uninitialized when stopped
                    lastValueAboveThreshold = false;
                }
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

        // Store current beat for next iteration
        previousBeat = currentBeat;
    }
}

std::optional<RhythmInfo> AutomationLaneModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;
    info.displayName = "Automation Lane #" + juce::String(getLogicalId());
    info.sourceType = "automation";

    const bool syncEnabled = modeParam != nullptr ? (modeParam->load() > 0.5f) : true;
    info.isSynced = syncEnabled;

    TransportState transport;
    bool           hasTransport = false;
    if (getParent())
    {
        transport = getParent()->getTransportState();
        hasTransport = true;
    }

    if (syncEnabled)
    {
        info.isActive = hasTransport ? transport.isPlaying : false;
        if (info.isActive)
        {
            int divisionIndex = divisionParam != nullptr ? (int)divisionParam->load() : 5;
            int globalDiv = transport.globalDivisionIndex.load();
            if (globalDiv >= 0)
                divisionIndex = globalDiv;

            static const double multipliers[] = {
                1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};
            const double speed = multipliers[juce::jlimit(0, 8, divisionIndex)];

            // BPM is relative to the speed multiplier
            info.bpm = static_cast<float>(transport.bpm * speed);
        }
    }
    else
    {
        info.isActive = true;
        const float rate = rateParam != nullptr ? rateParam->load() : 1.0f;
        double      duration = getTargetDuration();
        info.bpm = (rate / static_cast<float>(duration)) * 60.0f;
    }

    return info;
}

void AutomationLaneModuleProcessor::forceStop() { currentPhase = 0.0; }

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

            if (!chunk->samples.empty())
            {
                juce::MemoryBlock mb;
                mb.append(chunk->samples.data(), chunk->samples.size() * sizeof(float));
                chunkVt.setProperty("samples", mb, nullptr);
            }

            vt.addChild(chunkVt, -1, nullptr);
        }
    }

    vt.setProperty("mode", modeParam != nullptr ? modeParam->load() : 1.0f, nullptr);
    vt.setProperty(
        "rate_division", divisionParam != nullptr ? divisionParam->load() : 5.0f, nullptr);

    return vt;
}

void AutomationLaneModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("AutomationLaneState"))
    {
        auto newState = std::make_shared<AutomationState>();
        newState->totalDurationBeats = vt.getProperty("totalDurationBeats", 32.0);

        for (const auto& chunkVt : vt)
        {
            if (chunkVt.hasType("Chunk"))
            {
                double startBeat = chunkVt.getProperty("startBeat");
                double numBeats = chunkVt.getProperty("numBeats");
                double samplesPerBeat = chunkVt.getProperty("samplesPerBeat");

                auto chunk = std::make_shared<AutomationChunk>(
                    startBeat, static_cast<int>(numBeats), static_cast<int>(samplesPerBeat));

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

                if (chunk->samples.empty())
                {
                    chunk->samples.resize((size_t)(numBeats * samplesPerBeat), 0.5f);
                }

                newState->chunks.push_back(chunk);
            }
        }

        if (newState->chunks.empty())
        {
            auto firstChunk = std::make_shared<AutomationChunk>(0.0, 32, 256);
            newState->chunks.push_back(firstChunk);
        }

        updateState(newState);

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdMode)))
            *p = (float)vt.getProperty("mode", 1.0f);
        if (auto* p =
                dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdDivision)))
            *p = (int)vt.getProperty("rate_division", 5);
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
    case OUTPUT_TRIGGER:
        return "Trigger";
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
        return 32.0;

    // Get the index from AudioParameterChoice - use getIndex() for correct value
    int durationModeIndex = 0;
    if (auto* choiceParam =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdDurationMode)))
    {
        durationModeIndex = choiceParam->getIndex();
    }
    else
    {
        // Fallback to raw value cast if parameter type is wrong
        durationModeIndex = (int)*durationModeParam;
    }

    // Map duration mode index to beats
    // Index 0: User Choice, 1: 1 Bar, 2: 2 Bars, 3: 4 Bars, 4: 8 Bars, 5: 16 Bars, 6: 32 Bars
    switch (durationModeIndex)
    {
    case 0:
        return (double)*customDurationParam; // User Choice
    case 1:
        return 4.0; // 1 Bar = 4 beats
    case 2:
        return 8.0; // 2 Bars = 8 beats
    case 3:
        return 16.0; // 4 Bars = 16 beats
    case 4:
        return 32.0; // 8 Bars = 32 beats
    case 5:
        return 64.0; // 16 Bars = 64 beats
    case 6:
        return 128.0; // 32 Bars = 128 beats
    default:
        return 16.0;
    }
}

bool AutomationLaneModuleProcessor::lineSegmentCrossesThreshold(
    float prevValue,
    float currValue,
    float threshold,
    int   edgeMode) const
{
    // Check if line segment from (prevValue) to (currValue) crosses threshold
    // Edge mode: 0=Rising, 1=Falling, 2=Both

    const float d1 = prevValue - threshold;
    const float d2 = currValue - threshold;

    // Check if values are on opposite sides of threshold (crossing detected)
    if (d1 * d2 < 0.0f) // Opposite signs = crossing
    {
        if (edgeMode == 0) // Rising: crossing from below to above
            return (d1 < 0.0f && d2 > 0.0f);
        else if (edgeMode == 1) // Falling: crossing from above to below
            return (d1 > 0.0f && d2 < 0.0f);
        else // Both: crossing in either direction
            return true;
    }

    return false;
}

void AutomationLaneModuleProcessor::ensureChunkExistsAt(double beat)
{
    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    if (state->findChunkAt(beat))
        return;

    const double chunkDuration = 32.0;
    const int    samplesPerBeat = 256;
    double       chunkStart = std::floor(beat / chunkDuration) * chunkDuration;

    for (const auto& chunk : state->chunks)
    {
        if (std::abs(chunk->startBeat - chunkStart) < 0.001)
            return;
    }

    auto newState = std::make_shared<AutomationState>();
    newState->chunks = state->chunks;

    auto newChunk =
        std::make_shared<AutomationChunk>(chunkStart, (int)chunkDuration, samplesPerBeat);
    newState->chunks.push_back(newChunk);

    std::sort(newState->chunks.begin(), newState->chunks.end(), compareChunks);

    double maxDuration = 0.0;
    if (!newState->chunks.empty())
    {
        auto lastChunk = newState->chunks.back();
        maxDuration = lastChunk->startBeat + lastChunk->numBeats;
    }
    newState->totalDurationBeats = maxDuration;

    updateState(newState);
}

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

    AutomationState::Ptr state = activeState.load();
    if (!state)
        return;

    auto newState = std::make_shared<AutomationState>();
    newState->totalDurationBeats = state->totalDurationBeats;

    double targetStartBeat = chunk->startBeat;

    for (const auto& oldChunk : state->chunks)
    {
        if (std::abs(oldChunk->startBeat - targetStartBeat) < 0.001)
        {
            auto newChunk = std::make_shared<AutomationChunk>(
                chunk->startBeat, chunk->numBeats, chunk->samplesPerBeat);
            newChunk->samples = chunk->samples;

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
            newState->chunks.push_back(oldChunk);
        }
    }

    updateState(newState);
}

// --- UI Implementation ---

#if defined(PRESET_CREATOR_UI)

// Helper for tooltips
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

void AutomationLaneModuleProcessor::drawParametersInNode(
    float                                           itemWidth,
    const std::function<bool(const juce::String&)>& checkHover,
    const std::function<void()>&                    markEdited)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto&       ap = getAPVTS();

    if (!modeParam || !rateParam || !divisionParam || !loopParam || !durationModeParam ||
        !customDurationParam || !triggerThresholdParam || !triggerEdgeParam)
    {
        ImGui::Text("Initializing...");
        return;
    }

    // --- 1. TOOLBAR AREA ---
    ImGui::PushID("Toolbar");

    // Row 1: Sync | Speed | Duration
    bool syncEnabled = *modeParam > 0.5f;
    if (ImGui::Checkbox("Sync", &syncEnabled))
    {
        float newVal = syncEnabled ? 1.0f : 0.0f;
        *modeParam = newVal;
        ap.getParameter(paramIdMode)->setValueNotifyingHost(newVal);
        markEdited();
    }
    ImGui::SameLine();

    if (syncEnabled)
    {
        // Speed Combo
        ImGui::SetNextItemWidth(80);
        int globalDiv =
            getParent() ? getParent()->getTransportState().globalDivisionIndex.load() : -1;
        bool        isGlobal = globalDiv >= 0;
        int         divIndex = isGlobal ? globalDiv : (int)*divisionParam;
        const char* divs[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1x", "2x", "4x", "8x"};

        if (isGlobal)
            ImGui::BeginDisabled();
        if (ImGui::Combo("##speed", &divIndex, divs, 9))
        {
            if (!isGlobal)
            {
                *divisionParam = (float)divIndex;
                ap.getParameter(paramIdDivision)
                    ->setValueNotifyingHost(
                        ap.getParameterRange(paramIdDivision).convertTo0to1((float)divIndex));
                markEdited();
            }
        }
        // Scroll-edit for Speed Combo
        if (!isGlobal && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                // wheel > 0.0f (scroll down) gives -1, wheel < 0.0f (scroll up) gives +1
                const int delta = wheel > 0.0f ? -1 : 1;
                int       newIndex = juce::jlimit(0, 8, divIndex + delta);
                if (newIndex != divIndex)
                {
                    *divisionParam = (float)newIndex;
                    ap.getParameter(paramIdDivision)
                        ->setValueNotifyingHost(
                            ap.getParameterRange(paramIdDivision).convertTo0to1((float)newIndex));
                    markEdited();
                }
            }
        }
        if (isGlobal)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Controlled by Tempo Clock");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Speed Multiplier");
    }
    else
    {
        ImGui::SetNextItemWidth(80);
        float rate = *rateParam;
        if (ImGui::DragFloat("##rate", &rate, 0.01f, 0.01f, 20.0f, "%.2f Hz"))
        {
            *rateParam = rate;
            ap.getParameter(paramIdRate)
                ->setValueNotifyingHost(ap.getParameterRange(paramIdRate).convertTo0to1(rate));
            markEdited();
        }
        // Scroll-edit for Rate DragFloat
        adjustParamOnWheel(ap.getParameter(paramIdRate), paramIdRate, rate);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Rate in Hz");
    }

    ImGui::SameLine();

    // Duration Combo
    ImGui::SetNextItemWidth(100);
    int         durIndex = (int)*durationModeParam;
    const char* durModes[] = {"User", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars", "32 Bars"};
    if (ImGui::Combo("##dur", &durIndex, durModes, 7))
    {
        *durationModeParam = (float)durIndex;
        ap.getParameter(paramIdDurationMode)
            ->setValueNotifyingHost(
                ap.getParameterRange(paramIdDurationMode).convertTo0to1((float)durIndex));
        markEdited();
    }
    // Scroll-edit for Duration Combo
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            // wheel > 0.0f (scroll down) gives -1, wheel < 0.0f (scroll up) gives +1
            const int delta = wheel > 0.0f ? -1 : 1;
            int       newIndex = juce::jlimit(0, 6, durIndex + delta);
            if (newIndex != durIndex)
            {
                *durationModeParam = (float)newIndex;
                ap.getParameter(paramIdDurationMode)
                    ->setValueNotifyingHost(
                        ap.getParameterRange(paramIdDurationMode).convertTo0to1((float)newIndex));
                markEdited();
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Total Loop Duration");

    // Row 2: Zoom | Rec/Edit | Custom Duration (if applicable)

    // Zoom Slider
    float currentZoom = *apvts.getRawParameterValue(paramIdZoom);
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderFloat("##zoom", &currentZoom, 10.0f, 200.0f, "Zoom: %.0f"))
    {
        *apvts.getRawParameterValue(paramIdZoom) = currentZoom;
        ap.getParameter(paramIdZoom)
            ->setValueNotifyingHost(ap.getParameterRange(paramIdZoom).convertTo0to1(currentZoom));
        markEdited();
    }
    // Scroll-edit for Zoom Slider
    adjustParamOnWheel(ap.getParameter(paramIdZoom), paramIdZoom, currentZoom);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Horizontal Zoom (Pixels per Beat)");

    ImGui::SameLine();

    // Record/Edit Mode
    bool isRec = *apvts.getRawParameterValue(paramIdRecordMode) < 0.5f;
    if (ImGui::Button(isRec ? "REC" : "EDIT", ImVec2(50, 0)))
    {
        float newVal = isRec ? 1.0f : 0.0f;
        apvts.getParameter(paramIdRecordMode)->setValueNotifyingHost(newVal);
        markEdited();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle Record/Edit Mode");

    // Custom Duration Slider (only if User Choice is selected)
    if (durIndex == 0)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        float customDur = *customDurationParam;
        if (ImGui::DragFloat("##customDur", &customDur, 1.0f, 1.0f, 256.0f, "%.0f Beats"))
        {
            *customDurationParam = customDur;
            ap.getParameter(paramIdCustomDuration)
                ->setValueNotifyingHost(
                    ap.getParameterRange(paramIdCustomDuration).convertTo0to1(customDur));
            markEdited();
        }
        // Scroll-edit for Custom Duration DragFloat
        adjustParamOnWheel(
            ap.getParameter(paramIdCustomDuration), paramIdCustomDuration, customDur);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Custom Duration in Beats");
    }

    ImGui::PopID(); // End Toolbar

    ImGui::Spacing();

    // --- TRIGGER CONTROLS ---
    if (!triggerThresholdParam || !triggerEdgeParam)
    {
        ImGui::Text("Initializing trigger parameters...");
    }
    else
    {
        ImGui::PushItemWidth(itemWidth - 100);

        // Trigger Threshold Slider
        float trigThresh = *triggerThresholdParam;
        if (ImGui::SliderFloat("Trigger Threshold", &trigThresh, 0.0f, 1.0f, "%.2f"))
        {
            *triggerThresholdParam = trigThresh;
            ap.getParameter(paramIdTriggerThreshold)
                ->setValueNotifyingHost(
                    ap.getParameterRange(paramIdTriggerThreshold).convertTo0to1(trigThresh));
            markEdited();
        }
        // Scroll-edit for Trigger Threshold Slider
        adjustParamOnWheel(
            ap.getParameter(paramIdTriggerThreshold), paramIdTriggerThreshold, trigThresh);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Threshold level for trigger output");

        ImGui::SameLine();

        // Edge Selection Combo
        ImGui::SetNextItemWidth(80);
        int         edgeIndex = (int)*triggerEdgeParam;
        const char* edges[] = {"Rising", "Falling", "Both"};
        if (ImGui::Combo("##edge", &edgeIndex, edges, 3))
        {
            *triggerEdgeParam = (float)edgeIndex;
            ap.getParameter(paramIdTriggerEdge)
                ->setValueNotifyingHost(
                    ap.getParameterRange(paramIdTriggerEdge).convertTo0to1((float)edgeIndex));
            markEdited();
        }
        // Scroll-edit for Trigger Edge Combo
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                // wheel > 0.0f (scroll down) gives -1, wheel < 0.0f (scroll up) gives +1
                const int delta = wheel > 0.0f ? -1 : 1;
                int       newIndex = juce::jlimit(0, 2, edgeIndex + delta);
                if (newIndex != edgeIndex)
                {
                    *triggerEdgeParam = (float)newIndex;
                    ap.getParameter(paramIdTriggerEdge)
                        ->setValueNotifyingHost(ap.getParameterRange(paramIdTriggerEdge)
                                                    .convertTo0to1((float)newIndex));
                    markEdited();
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Trigger on rising edge, falling edge, or both");

        ImGui::PopItemWidth();
    }

    ImGui::Spacing();

    // --- 2. TIMELINE & EDITOR AREA ---

    const float  timelineHeight = 30.0f;
    const float  editorHeight = 200.0f;
    const float  pixelsPerBeat = currentZoom;
    const double totalDuration = getTargetDuration();
    const float  totalWidth = (float)(totalDuration * pixelsPerBeat);

    // Scrollable Child Window
    // IMPORTANT: Using ImGuiWindowFlags_NoMove to prevent the child window itself from being
    // draggable but allowing content scrolling.
    ImGui::BeginChild(
        "TimelineEditor",
        ImVec2(itemWidth, editorHeight + timelineHeight),
        true,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

    ImDrawList*  drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetCursorScreenPos();
    const float  scrollX = ImGui::GetScrollX();

    // Reserve space for content
    ImGui::Dummy(ImVec2(totalWidth, editorHeight + timelineHeight));

    // --- A. Ruler ---
    const ImVec2 rulerStart = windowPos;
    const float  visibleLeft = scrollX;
    const float  visibleRight = scrollX + itemWidth;

    // Draw Ruler Background
    drawList->AddRectFilled(
        rulerStart,
        ImVec2(rulerStart.x + std::max(itemWidth, totalWidth), rulerStart.y + timelineHeight),
        IM_COL32(40, 40, 40, 255));

    // Draw Ruler Ticks (Optimized culling)
    const int startBeat = (int)(visibleLeft / pixelsPerBeat);
    const int endBeat = (int)(visibleRight / pixelsPerBeat) + 1;

    for (int b = startBeat; b <= endBeat; ++b)
    {
        float x = rulerStart.x + b * pixelsPerBeat;
        bool  isBar = (b % 4 == 0);
        float h = isBar ? timelineHeight : timelineHeight * 0.5f;

        drawList->AddLine(
            ImVec2(x, rulerStart.y + timelineHeight - h),
            ImVec2(x, rulerStart.y + timelineHeight),
            isBar ? IM_COL32(150, 150, 150, 255) : IM_COL32(80, 80, 80, 255));

        if (isBar)
        {
            char buf[16];
            snprintf(buf, 16, "%d", b / 4); // Start at 0 instead of 1
            drawList->AddText(ImVec2(x + 3, rulerStart.y), IM_COL32(180, 180, 180, 255), buf);
        }
    }

    // --- B. Automation Curve Editor ---
    const ImVec2 editorStart = ImVec2(windowPos.x, windowPos.y + timelineHeight);

    // Draw Background Grid
    drawList->AddRectFilled(
        editorStart,
        ImVec2(editorStart.x + std::max(itemWidth, totalWidth), editorStart.y + editorHeight),
        IM_COL32(20, 20, 20, 255));

    // Draw Horizontal Grid Lines (0, 0.5, 1.0)
    float y05 = editorStart.y + editorHeight * 0.5f;
    drawList->AddLine(
        ImVec2(editorStart.x, y05),
        ImVec2(editorStart.x + std::max(itemWidth, totalWidth), y05),
        IM_COL32(50, 50, 50, 255));

    // Draw Trigger Threshold Line
    if (triggerThresholdParam)
    {
        float       threshold = *triggerThresholdParam;
        float       thresholdY = editorStart.y + editorHeight * (1.0f - threshold);
        const ImU32 thresholdColor = IM_COL32(255, 150, 0, 200); // Orange/amber
        drawList->AddLine(
            ImVec2(editorStart.x, thresholdY),
            ImVec2(editorStart.x + std::max(itemWidth, totalWidth), thresholdY),
            thresholdColor,
            2.0f);
    }

    // Draw Vertical Grid Lines
    for (int b = startBeat; b <= endBeat; ++b)
    {
        float x = editorStart.x + b * pixelsPerBeat;
        drawList->AddLine(
            ImVec2(x, editorStart.y),
            ImVec2(x, editorStart.y + editorHeight),
            IM_COL32(30, 30, 30, 255));
    }

    // Draw Curve
    AutomationState::Ptr state = activeState.load();
    if (state)
    {
        for (const auto& chunk : state->chunks)
        {
            // Culling: check if chunk is visible
            float chunkStartX = editorStart.x + (float)(chunk->startBeat * pixelsPerBeat);
            float chunkWidth = (float)(chunk->numBeats * pixelsPerBeat);

            if (chunkStartX + chunkWidth < windowPos.x + visibleLeft ||
                chunkStartX > windowPos.x + visibleRight)
                continue;

            // Draw samples
            const auto& samples = chunk->samples;
            if (samples.empty())
                continue;

            // Optimization: Don't draw every sample if zoomed out
            int step = 1;
            if (pixelsPerBeat < 20.0f)
                step = 4;

            for (size_t i = 0; i < samples.size() - step; i += step)
            {
                float b1 = (float)i / chunk->samplesPerBeat;
                float b2 = (float)(i + step) / chunk->samplesPerBeat;

                float x1 = chunkStartX + b1 * pixelsPerBeat;
                float x2 = chunkStartX + b2 * pixelsPerBeat;

                // Vertical culling
                if (x2 < windowPos.x + visibleLeft || x1 > windowPos.x + visibleRight)
                    continue;

                float val1 = samples[i];
                float val2 = samples[i + step];

                float py1 = editorStart.y + editorHeight * (1.0f - val1);
                float py2 = editorStart.y + editorHeight * (1.0f - val2);

                drawList->AddLine(
                    ImVec2(x1, py1), ImVec2(x2, py2), IM_COL32(100, 200, 255, 255), 2.0f);
            }
        }
    }

    // --- C. Playhead ---
    double currentBeat = 0.0;
    if (syncEnabled && m_currentTransport.isPlaying)
    {
        int divisionIndex = (int)*divisionParam;
        if (getParent())
        {
            int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
            if (globalDiv >= 0)
                divisionIndex = globalDiv;
        }
        static const double multipliers[] = {
            1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};
        const double speed = multipliers[juce::jlimit(0, 8, divisionIndex)];
        currentBeat = m_currentTransport.songPositionBeats * speed;
    }
    else
    {
        currentBeat = currentPhase * totalDuration;
    }

    // Wrap for looping
    const bool isLooping = loopParam != nullptr ? (*loopParam > 0.5f) : true;
    if (isLooping && totalDuration > 0)
        currentBeat = std::fmod(currentBeat, totalDuration);

    float playheadX = editorStart.x + (float)(currentBeat * pixelsPerBeat);
    if (playheadX >= windowPos.x + visibleLeft && playheadX <= windowPos.x + visibleRight)
    {
        drawList->AddLine(
            ImVec2(playheadX, rulerStart.y),
            ImVec2(playheadX, editorStart.y + editorHeight),
            IM_COL32(255, 255, 0, 200),
            2.0f);
    }

    // --- D. Interaction (Draw on Canvas) ---
    if (!isRec)
    {
        // Use InvisibleButton to capture mouse events without visual interference
        // This prevents the click from falling through to the node editor background
        ImGui::SetCursorPos(ImVec2(0, timelineHeight)); // Position relative to child window
        ImGui::InvisibleButton(
            "##CanvasInteraction", ImVec2(std::max(itemWidth, totalWidth), editorHeight));

        // --- START NEW TOOLTIP CODE ---
        if (ImGui::IsItemHovered())
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float  relX = mousePos.x - editorStart.x;
            float  relY = mousePos.y - editorStart.y;

            // Calculate Data
            double beat = relX / pixelsPerBeat;
            float  val = 1.0f - (relY / editorHeight);
            val = juce::jlimit(0.0f, 1.0f, val);

            // 1. Draw Crosshair (Visual Feedback)
            // Vertical Line (Time)
            drawList->AddLine(
                ImVec2(mousePos.x, editorStart.y),
                ImVec2(mousePos.x, editorStart.y + editorHeight),
                IM_COL32(255, 255, 255, 50));

            // Horizontal Line (Value)
            drawList->AddLine(
                ImVec2(editorStart.x, mousePos.y),
                ImVec2(editorStart.x + std::max(itemWidth, totalWidth), mousePos.y),
                IM_COL32(255, 255, 255, 50));

            // 2. Rich Tooltip (Data Display)
            ImGui::BeginTooltip();
            // Header: Time Info
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Time: %.2f Beats", beat);

            // Calculate Bar.Beat format (assuming 4/4 for display)
            int    bar = (int)(beat / 4.0) + 1;
            double beatInBar = std::fmod(beat, 4.0) + 1.0;
            ImGui::TextDisabled("Position: %d.%02d", bar, (int)(beatInBar * 100)); // e.g. 1.50

            ImGui::Separator();

            // Value Info
            ImGui::Text("Value (0-1):   %.3f", val);
            ImGui::Text("Bipolar (-1/1): %.3f", (val * 2.0f) - 1.0f);
            ImGui::TextColored(
                ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CV Output:      %.2f V", val * 10.0f);

            ImGui::EndTooltip();
        }
        // --- END NEW TOOLTIP CODE ---

        // Capture interaction with interpolation for smooth drawing
        // Check mouse state every frame to ensure smooth drawing even with fast mouse movement
        bool isMouseDown = ImGui::IsItemActive() && ImGui::IsMouseDown(0);

        if (isMouseDown)
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float  relX = mousePos.x - editorStart.x;
            float  relY = mousePos.y - editorStart.y;

            double currentBeat = relX / pixelsPerBeat;
            float  currentVal = 1.0f - (relY / editorHeight);
            currentVal = juce::jlimit(0.0f, 1.0f, currentVal);

            // Check if this is the start of a new drag (lastMousePosInCanvas is invalid)
            bool isNewDrag = (lastMousePosInCanvas.x < 0.0f || lastMousePosInCanvas.y < 0.0f);

            // Also check if mouse has moved significantly (to avoid redundant updates)
            bool mouseMoved = isNewDrag;
            if (!isNewDrag)
            {
                float dx = mousePos.x - lastMousePosInCanvas.x;
                float dy = mousePos.y - lastMousePosInCanvas.y;
                mouseMoved =
                    (std::abs(dx) > 0.5f ||
                     std::abs(dy) > 0.5f); // Threshold to avoid micro-movements
            }

            if (isNewDrag)
            {
                // First point: just draw at current position with larger radius
                lastMousePosInCanvas = mousePos;

                ensureChunkExistsAt(currentBeat);
                state = activeState.load();
                auto chunk = state->findChunkAt(currentBeat);
                if (chunk)
                {
                    double beatInChunk = currentBeat - chunk->startBeat;
                    int    sampleIdx = (int)(beatInChunk * chunk->samplesPerBeat);
                    int    radius = 8; // Larger radius for initial point
                    modifyChunkSamplesThreadSafe(
                        chunk, sampleIdx - radius, sampleIdx + radius, currentVal, currentVal);
                    markEdited();
                }
            }
            else if (mouseMoved)
            {
                // Interpolate between last position and current position
                float lastRelX = lastMousePosInCanvas.x - editorStart.x;
                float lastRelY = lastMousePosInCanvas.y - editorStart.y;

                double lastBeat = lastRelX / pixelsPerBeat;
                float  lastVal = 1.0f - (lastRelY / editorHeight);
                lastVal = juce::jlimit(0.0f, 1.0f, lastVal);

                // Ensure we have chunks for both start and end points
                ensureChunkExistsAt(lastBeat);
                ensureChunkExistsAt(currentBeat);
                state = activeState.load();

                // Find chunks and sample indices for both positions
                auto lastChunk = state->findChunkAt(lastBeat);
                auto currentChunk = state->findChunkAt(currentBeat);

                if (lastChunk && currentChunk)
                {
                    // Calculate sample indices
                    double lastBeatInChunk = lastBeat - lastChunk->startBeat;
                    double currentBeatInChunk = currentBeat - currentChunk->startBeat;

                    int lastSampleIdx = (int)(lastBeatInChunk * lastChunk->samplesPerBeat);
                    int currentSampleIdx = (int)(currentBeatInChunk * currentChunk->samplesPerBeat);

                    // Handle same chunk vs different chunks
                    int radius = 5; // Radius for smooth drawing

                    if (lastChunk == currentChunk)
                    {
                        // Same chunk: fill all samples between the two indices with radius
                        int startIdx = juce::jmin(lastSampleIdx, currentSampleIdx) - radius;
                        int endIdx = juce::jmax(lastSampleIdx, currentSampleIdx) + radius;
                        startIdx = juce::jmax(0, startIdx);
                        endIdx = juce::jmin((int)lastChunk->samples.size() - 1, endIdx);

                        float startVal = (lastSampleIdx < currentSampleIdx) ? lastVal : currentVal;
                        float endVal = (lastSampleIdx < currentSampleIdx) ? currentVal : lastVal;

                        // Fill every sample in the range with interpolation
                        // modifyChunkSamplesThreadSafe already handles interpolation between
                        // startVal and endVal
                        modifyChunkSamplesThreadSafe(lastChunk, startIdx, endIdx, startVal, endVal);
                    }
                    else
                    {
                        // Different chunks: fill from last position to end of last chunk,
                        // then from start of current chunk to current position

                        // Fill to end of last chunk (with radius at start)
                        int lastChunkEndIdx = (int)lastChunk->samples.size() - 1;
                        int lastStartIdx = juce::jmax(0, lastSampleIdx - radius);
                        int lastEndIdx = juce::jmin(lastChunkEndIdx, lastSampleIdx + radius);
                        if (lastStartIdx <= lastEndIdx)
                        {
                            modifyChunkSamplesThreadSafe(
                                lastChunk, lastStartIdx, lastEndIdx, lastVal, lastVal);
                        }

                        // Fill from start of current chunk to current position (with radius at end)
                        int currentStartIdx = juce::jmax(0, currentSampleIdx - radius);
                        int currentEndIdx = juce::jmin(
                            (int)currentChunk->samples.size() - 1, currentSampleIdx + radius);
                        if (currentStartIdx <= currentEndIdx)
                        {
                            modifyChunkSamplesThreadSafe(
                                currentChunk, currentStartIdx, currentEndIdx, lastVal, currentVal);
                        }
                    }
                }
                else if (currentChunk)
                {
                    // Fallback: just draw at current position
                    double beatInChunk = currentBeat - currentChunk->startBeat;
                    int    sampleIdx = (int)(beatInChunk * currentChunk->samplesPerBeat);
                    int    radius = 5;
                    modifyChunkSamplesThreadSafe(
                        currentChunk,
                        sampleIdx - radius,
                        sampleIdx + radius,
                        currentVal,
                        currentVal);
                }

                markEdited();
                // Update last position immediately after processing
                lastMousePosInCanvas = mousePos;
            }
        }
        else
        {
            // Mouse released: reset last position to invalidate next drag
            lastMousePosInCanvas = ImVec2(-1.0f, -1.0f);
        }
    }

    ImGui::EndChild();
}

#endif
