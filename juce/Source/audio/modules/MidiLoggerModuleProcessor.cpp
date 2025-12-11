#include "MidiLoggerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"  // Needed for connection queries
#include <juce_gui_basics/juce_gui_basics.h> // Needed for FileChooser
#include <algorithm>

MidiLoggerModuleProcessor::MidiLoggerModuleProcessor()
    : ModuleProcessor(
          BusesProperties()
              // We define a large potential number of buses, which will be dynamically
              // shown/hidden by getDynamicInputPins.
              .withInput("Inputs", juce::AudioChannelSet::discreteChannels(256), true)
              .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(256), true)),
      apvts(*this, nullptr, "MidiLoggerParams", createParameterLayout())
{
    loopLengthParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("loopLength"));
    
    // MIDI Output: Get pointers to MIDI output parameters
    enableMidiOutputParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("enable_midi_output"));
    midiOutputModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("midi_output_mode"));
    midiOutputDeviceIndexParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midi_output_device_index"));
    midiOutputChannelParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midi_output_channel"));

    // Pre-allocate tracks to ensure thread safety
    tracks.reserve(MaxTracks);
    for (int i = 0; i < MaxTracks; ++i)
    {
        tracks.push_back(std::make_unique<MidiTrack>());
        tracks.back()->name = "Track " + juce::String(i + 1);
        // Assign a default color cycle
        float hue = (i % 8) / 8.0f;
        tracks.back()->color = juce::Colour::fromHSV(hue, 0.7f, 0.9f, 1.0f);
    }

    // Activate the first 8 tracks by default to match MIDI CV's 8 voices
    for (int i = 0; i < 8 && i < MaxTracks; ++i)
    {
        tracks[i]->active = true;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiLoggerModuleProcessor::
    createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ADDITION FOR PHASE 3: Loop Length Control
    params.push_back(
        std::make_unique<juce::AudioParameterInt>("loopLength", "Loop Length", 1, 2048, 4));
    
    // MIDI Output Parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "enable_midi_output", "Enable MIDI Output", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "midi_output_mode", "MIDI Output Mode",
        juce::StringArray("Use Global Default", "Custom Device"), 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "midi_output_device_index", "MIDI Output Device Index", -1, 100, -1));  // -1 = none selected
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "midi_output_channel", "MIDI Output Channel", 0, 16, 0));

    return {params.begin(), params.end()};
}

void MidiLoggerModuleProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    playheadPositionSamples = 0;

    // Initialize states for the maximum potential number of tracks
    // Initialize states for the maximum potential number of tracks
    previousGateState.assign(MaxTracks, false);
    previousMidiNote.assign(MaxTracks, -1);
    playbackStates.assign(MaxTracks, {});
}

void MidiLoggerModuleProcessor::releaseResources()
{
    // Close MIDI output device
    {
        const juce::ScopedLock lock(midiOutputLock);
        midiOutputDevice.reset();
        currentMidiOutputDeviceId.clear();
    }
}

// ==============================================================================
// PHASE 2: UPDATED PROCESS BLOCK WITH PLAYBACK
// ==============================================================================

void MidiLoggerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // --- CRITICAL FIX: BPM SYNC ---
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (pos->getBpm().hasValue())
                currentBpm.store(*pos->getBpm());
    }

    const int numSamples = buffer.getNumSamples();
    auto      inputBus = getBusBuffer(buffer, true, 0);
    auto      outputBus = getBusBuffer(buffer, false, 0);

    // ALWAYS check for connected inputs to create tracks dynamically
    // We check up to MaxTracks
    for (int i = 0; i < MaxTracks; ++i)
    {
        const int gateChannel = i * 3 + 0;

        if (gateChannel < inputBus.getNumChannels())
        {
            const float* gateData = inputBus.getReadPointer(gateChannel);
            bool         hasSignal = false;

            // Optimization: Check strided samples to avoid checking every single one if CPU is
            // tight, but for now checking all is safer for short triggers.
            for (int s = 0; s < numSamples && !hasSignal; ++s)
            {
                if (std::abs(gateData[s]) > 0.001f)
                    hasSignal = true;
            }

            if (hasSignal && !tracks[i]->active)
            {
                activateTrack(i);
                // We don't log here to avoid spamming the audio thread log
            }
        }
    }

    // --- SECTION 1: RECORDING LOGIC ---
    if (transportState.load() == TransportState::Recording)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            for (int i = 0; i < MaxTracks; ++i)
            {
                // Skip inactive tracks for recording
                if (!tracks[i]->active)
                    continue;

                const int gateChannel = i * 3 + 0;
                const int pitchChannel = i * 3 + 1;
                const int veloChannel = i * 3 + 2;

                if (gateChannel >= inputBus.getNumChannels() ||
                    pitchChannel >= inputBus.getNumChannels() ||
                    veloChannel >= inputBus.getNumChannels())
                {
                    continue;
                }

                const float gateSignal = inputBus.getReadPointer(gateChannel)[sample];
                const bool  isGateHigh = gateSignal >= 0.5f;

                const float pitchCV = inputBus.getReadPointer(pitchChannel)[sample];
                const float veloCV = inputBus.getReadPointer(veloChannel)[sample];

                const int   midiNote = static_cast<int>(std::round(pitchCV * 12.0f) + 60.0f);
                const float velocity = juce::jlimit(0.0f, 1.0f, veloCV);

                // Case 1: New Note On (Gate Rising Edge)
                if (isGateHigh && !previousGateState[i])
                {
                    tracks[i]->addNoteOn(midiNote, velocity, playheadPositionSamples);
                    previousMidiNote[i] = midiNote;
                }
                // Case 2: Note Off (Gate Falling Edge)
                else if (!isGateHigh && previousGateState[i])
                {
                    tracks[i]->addNoteOff(-1, playheadPositionSamples);
                    previousMidiNote[i] = -1;
                }
                // Case 3: Legato Pitch Change (Gate Held, Pitch Changed)
                else if (isGateHigh && previousGateState[i])
                {
                    if (midiNote != previousMidiNote[i] && previousMidiNote[i] != -1)
                    {
                        // Retrigger: Off old note, On new note
                        tracks[i]->addNoteOff(previousMidiNote[i], playheadPositionSamples);
                        tracks[i]->addNoteOn(midiNote, velocity, playheadPositionSamples);
                        previousMidiNote[i] = midiNote;
                    }
                }

                previousGateState[i] = isGateHigh;
            }

            playheadPositionSamples++;
        }
    }
    // --- SECTION 2: PLAYBACK LOGIC ---
    else if (transportState.load() == TransportState::Playing)
    {
        // Clear all output channels
        for (int i = 0; i < outputBus.getNumChannels(); ++i)
            juce::FloatVectorOperations::clear(outputBus.getWritePointer(i), numSamples);

        // === MIDI OUTPUT GENERATION ===
        // Check if MIDI output is enabled and send MIDI messages during playback
        if (enableMidiOutputParam != nullptr)
        {
            const float enableValue = enableMidiOutputParam->get();
            if (enableValue >= 0.5f)
            {
                const int64_t currentSample = playheadPositionSamples.load();
                const int channelOverride = midiOutputChannelParam ? 
                    midiOutputChannelParam->get() : 0;  // 0 = preserve original channels
                
                // Process each track to detect note onsets/offsets
                for (int trackIdx = 0; trackIdx < MaxTracks; ++trackIdx)
                {
                    if (!tracks[trackIdx]->active)
                        continue;
                    
                    auto events = tracks[trackIdx]->getEventsCopy();
                    
                    // Determine MIDI channel for this track
                    int midiChannel = (channelOverride > 0) ? channelOverride : ((trackIdx % 16) + 1);
                    
                    // Check for note onsets and offsets in this buffer
                    for (const auto& ev : events)
                    {
                        const int64_t noteStart = ev.startTimeInSamples;
                        const int64_t noteEnd = noteStart + ev.durationInSamples;
                        
                        // Note On: check if note starts within this buffer
                        if (currentSample <= noteStart && 
                            noteStart < currentSample + numSamples)
                        {
                            juce::MidiMessage noteOn = juce::MidiMessage::noteOn(
                                midiChannel, ev.pitch, (juce::uint8)(ev.velocity * 127.0f));
                            
                            // Add to graph MIDI buffer for routing to VSTi and other nodes
                            const int sampleOffset = (int)(noteStart - currentSample);
                            midiMessages.addEvent(noteOn, sampleOffset);
                            
                            // Also send to external MIDI output device
                            sendMidiToOutput(noteOn);
                        }
                        
                        // Note Off: check if note ends within this buffer
                        if (currentSample <= noteEnd && 
                            noteEnd < currentSample + numSamples)
                        {
                            juce::MidiMessage noteOff = juce::MidiMessage::noteOff(midiChannel, ev.pitch);
                            
                            // Add to graph MIDI buffer for routing to VSTi and other nodes
                            const int sampleOffset = (int)(noteEnd - currentSample);
                            midiMessages.addEvent(noteOff, sampleOffset);
                            
                            // Also send to external MIDI output device
                            sendMidiToOutput(noteOff);
                        }
                    }
                }
            }
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // For each track, check if any events are active at this playhead position
            for (int trackIdx = 0; trackIdx < MaxTracks; ++trackIdx)
            {
                if (!tracks[trackIdx]->active)
                    continue;

                bool isNoteActive = false;
                auto events = tracks[trackIdx]->getEventsCopy();

                for (const auto& ev : events)
                {
                    const int64_t noteStart = ev.startTimeInSamples;
                    const int64_t noteEnd = noteStart + ev.durationInSamples;

                    if (playheadPositionSamples >= noteStart && playheadPositionSamples < noteEnd)
                    {
                        // This note is active right now
                        isNoteActive = true;
                        playbackStates[trackIdx].gate = 1.0f;
                        playbackStates[trackIdx].pitch =
                            (ev.pitch - 60.0f) / 12.0f; // MIDI to V/Oct
                        playbackStates[trackIdx].velocity = ev.velocity;
                        break;
                    }
                }

                if (!isNoteActive)
                {
                    playbackStates[trackIdx].gate = 0.0f;
                }
            }

            // Write playback states to output buffer
            for (int trackIdx = 0; trackIdx < MaxTracks; ++trackIdx)
            {
                if (!tracks[trackIdx]->active)
                    continue;

                const int gateChan = trackIdx * 3 + 0;
                const int pitchChan = trackIdx * 3 + 1;
                const int veloChan = trackIdx * 3 + 2;

                if (gateChan < outputBus.getNumChannels())
                    outputBus.getWritePointer(gateChan)[sample] = playbackStates[trackIdx].gate;
                if (pitchChan < outputBus.getNumChannels())
                    outputBus.getWritePointer(pitchChan)[sample] = playbackStates[trackIdx].pitch;
                if (veloChan < outputBus.getNumChannels())
                    outputBus.getWritePointer(veloChan)[sample] = playbackStates[trackIdx].velocity;
            }

            // Advance playhead with looping
            playheadPositionSamples++;

            // PHASE 3: LOOPING LOGIC
            const double  samplesPerBeat = (60.0 / currentBpm.load()) * currentSampleRate;
            const int     loopLengthBars = loopLengthParam ? loopLengthParam->get() : 4;
            const int64_t loopEndSamples =
                static_cast<int64_t>(loopLengthBars * 4.0 * samplesPerBeat);

            // CRITICAL FIX: Only loop if we are PLAYING.
            // If we are RECORDING, we want linear recording (infinite).
            if (transportState.load() == TransportState::Playing)
            {
                if (playheadPositionSamples >= loopEndSamples)
                {
                    playheadPositionSamples = 0;
                }
            }
            // If Recording, we just keep incrementing playheadPositionSamples forever (until int64
            // overflow)
        }
        
        // Update MIDI output device if settings changed (check periodically)
        static int updateCounter = 0;
        if ((++updateCounter % 1000) == 0) // Check every 1000 blocks (~20 seconds at 48kHz)
            updateMidiOutputDevice();
    }
    else // Stopped
    {
        playheadPositionSamples = 0;
        // Turn off all gates
        for (auto& state : playbackStates)
            state.gate = 0.0f;
    }
}

// ==============================================================================
// PHASE 1: DYNAMIC PIN MANAGEMENT
// ==============================================================================
std::vector<DynamicPinInfo> MidiLoggerModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    // Show pins for all active tracks + 1 potential new one (up to MaxTracks)
    // Actually, for simplicity and stability, let's just show pins for all MaxTracks
    // or a subset. The user requested "dynamic" behavior.
    // Let's count active tracks and add one.

    int activeCount = 0;
    for (const auto& t : tracks)
        if (t->active)
            activeCount++;

    const int numTracksToShow = std::min((int)MaxTracks, activeCount + 1);

    for (int i = 0; i < numTracksToShow; ++i)
    {
        const juce::String trackNumStr = juce::String(i + 1);
        pins.push_back({"Gate " + trackNumStr, i * 3 + 0, PinDataType::Gate});
        pins.push_back({"Pitch " + trackNumStr, i * 3 + 1, PinDataType::CV});
        pins.push_back({"Velo " + trackNumStr, i * 3 + 2, PinDataType::CV});
    }
    return pins;
}

// ==============================================================================
// PHASE 2: DYNAMIC OUTPUT PIN MANAGEMENT
// ==============================================================================
std::vector<DynamicPinInfo> MidiLoggerModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;

    int activeCount = 0;
    for (const auto& t : tracks)
        if (t->active)
            activeCount++;

    for (int i = 0; i < activeCount; ++i)
    {
        const juce::String trackNumStr = juce::String(i + 1);
        pins.push_back({"Gate " + trackNumStr, i * 3 + 0, PinDataType::Gate});
        pins.push_back({"Pitch " + trackNumStr, i * 3 + 1, PinDataType::CV});
        pins.push_back({"Velo " + trackNumStr, i * 3 + 2, PinDataType::CV});
    }
    return pins;
}

// ==============================================================================
// PHASE 4: PIANO ROLL UI IMPLEMENTATION
// ==============================================================================
#if defined(PRESET_CREATOR_UI)

void MidiLoggerModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String&)>&,
    const std::function<void()>& onModificationEnded)
{
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth); // FIXED: Added missing PushItemWidth

    // --- Invisible Scaffolding ---
    ImGui::Dummy(ImVec2(nodeWidth, 0.0f));

    // --- 1. TOOLBAR ---
    const char* statusText = "■ Stopped";
    ImVec4      statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    if (transportState.load() == TransportState::Recording)
    {
        statusText = "● REC";
        statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    else if (transportState.load() == TransportState::Playing)
    {
        statusText = "▶ PLAY";
        statusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    ImGui::Text("%s", statusText);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button("Record"))
    {
        transportState = TransportState::Recording;
        playheadPositionSamples = 0;
        for (auto& track : tracks)
        {
            if (track)
                track->setEvents({});
        }
        juce::Logger::writeToLog("[MIDI Logger] Recording started");
    }
    ImGui::SameLine();
    if (ImGui::Button("Play"))
    {
        transportState = TransportState::Playing;
        playheadPositionSamples = 0;
        juce::Logger::writeToLog("[MIDI Logger] Playback started");
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        transportState = TransportState::Stopped;
        juce::Logger::writeToLog("[MIDI Logger] Stopped");
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // PHASE 5: Save .mid Button
    if (ImGui::Button("Save .mid"))
    {
        exportToMidiFile();
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Loop Length Slider
    if (loopLengthParam)
    {
        ImGui::PushItemWidth(100);
        int loopLen = loopLengthParam->get();
        if (ImGui::SliderInt("##loop", &loopLen, 1, 2048, "Loop: %d bars"))
        {
            *loopLengthParam = loopLen;
            onModificationEnded();
        }
        ImGui::PopItemWidth();
    }

    // Zoom Slider (controls horizontal density of timeline)
    ImGui::SameLine();
    ImGui::PushItemWidth(120);
    if (ImGui::SliderFloat("##zoom", &zoomX, 20.0f, 400.0f, "Zoom: %.0fpx/beat"))
    {
        // Clamp to reasonable values
        zoomX = juce::jlimit(20.0f, 400.0f, zoomX);
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();

    // --- 2. CALCULATE DIMENSIONS ---
    const float  contentHeight = 250.0f;
    const double samplesPerBeat = (60.0 / currentBpm) * currentSampleRate;
    const float  pixelsPerBeat = zoomX;
    const int    loopLengthBars = loopLengthParam ? loopLengthParam->get() : 4;

    double currentPlayheadBars = 0.0;
    if (samplesPerBeat > 0)
        currentPlayheadBars = (double)playheadPositionSamples.load() / samplesPerBeat / 4.0;

    // Allow infinite scrolling: ensure displayBars is at least loopLengthBars, but allow scrolling
    // far beyond that (up to 10000 bars) for very long recordings
    const double displayBars = std::max((double)loopLengthBars, std::max(currentPlayheadBars + 0.25, 10000.0));
    const float  totalWidth = (float)(displayBars * 4.0 * pixelsPerBeat);

    // --- 3. MAIN TABLE (Tracks + Timeline) ---
    // We use a single table for everything.
    // Column 0: Track Headers (Frozen)
    // Column 1: Timeline (Scrollable)
    // Row 0: Timeline Ruler (Frozen)

    // Enlarge scrollbar for better precision when scrubbing long timelines
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 20.0f);

    if (ImGui::BeginTable(
            "TrackTable",
            2,
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
            ImVec2(nodeWidth, contentHeight)))
    {
        // Setup columns
        ImGui::TableSetupScrollFreeze(1, 1); // Freeze 1st column (Headers) and 1st row (Ruler)
        ImGui::TableSetupColumn("Header", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthFixed, totalWidth);

        // --- CUSTOM HEADER ROW (TIMELINE RULER) ---
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

        // Col 0: "Tracks" Label
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Tracks");

        // Col 1: Timeline Ruler
        ImGui::TableSetColumnIndex(1);

        // Draw Ruler
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2      rulerMin = ImGui::GetCursorScreenPos();
        float       rulerHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0f;

        // Background for ruler (optional, TableHeaderBg handles this usually)

        // Culling for Ruler
        float scrollX = ImGui::GetScrollX();
        float visibleWidth = ImGui::GetWindowWidth(); // Visible width of the table
        // Note: In a scrolling table column, we draw in "virtual" space.
        // But we want to optimize drawing.
        // The 'scrollX' here is the table's horizontal scroll.

        const int totalBeats = (int)(displayBars * 4.0) + 1;
        const int firstBeat = juce::jmax(0, static_cast<int>(scrollX / pixelsPerBeat));
        const int lastBeat =
            juce::jmin(totalBeats, static_cast<int>((scrollX + visibleWidth) / pixelsPerBeat) + 1);

        for (int beatIndex = firstBeat; beatIndex <= lastBeat; ++beatIndex)
        {
            const bool  isBarLine = (beatIndex % 4 == 0);
            const int   barNumber = beatIndex / 4;
            const float x = rulerMin.x + (beatIndex * pixelsPerBeat);

            drawList->AddLine(
                ImVec2(x, rulerMin.y),
                ImVec2(x, rulerMin.y + rulerHeight),
                isBarLine ? IM_COL32(140, 140, 140, 255) : IM_COL32(70, 70, 70, 255),
                isBarLine ? 2.0f : 1.0f);

            if (isBarLine)
            {
                char label[8];
                snprintf(label, sizeof(label), "%d", barNumber + 1);
                drawList->AddText(ImVec2(x + 4, rulerMin.y), IM_COL32(220, 220, 220, 255), label);
            }
        }

        // CLICK-TO-SEEK (In Ruler)
        // Create invisible button for the ruler to capture clicks and prevent node dragging
        // Use the existing rulerMin variable from line 541
        ImVec2 savedRulerCursorPos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(rulerMin);
        ImGui::InvisibleButton("ruler", ImVec2(totalWidth, rulerHeight), 
            ImGuiButtonFlags_MouseButtonLeft);
        const bool rulerHovered = ImGui::IsItemHovered();
        const bool rulerActive = ImGui::IsItemActive();
        ImGui::SetCursorScreenPos(savedRulerCursorPos);
        
        // Reserve space for ruler drawing
        ImGui::Dummy(ImVec2(totalWidth, rulerHeight));
        
        if (rulerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !draggingNote.isDragging)
        {
            ImVec2 itemMin = rulerMin;
            float  mouseX = ImGui::GetMousePos().x;
            float  relativeX = mouseX - itemMin.x; // This is already in scrolled space!

            double       newTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;
            const double maxSamples = displayBars * 4 * samplesPerBeat;
            newTimeSamples = juce::jlimit(0.0, maxSamples, newTimeSamples);
            playheadPositionSamples = (int64_t)newTimeSamples;
        }
        
        // Note: Invisible buttons automatically prevent node dragging by capturing mouse events

        // Store track bounds for right-click erase functionality
        struct TrackBounds
        {
            size_t trackIndex;
            float cellMinX;
            float cellMinY;
            float rowHeight;
        };
        std::vector<TrackBounds> trackBounds;

        // --- TRACK ROWS ---
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            if (!tracks[i] || !tracks[i]->active)
                continue;

            ImGui::PushID((int)i);
            ImGui::TableNextRow();

            // Col 0: Controls
            ImGui::TableSetColumnIndex(0);
            ImU32 trackColorU32 = IM_COL32(
                tracks[i]->color.getRed(),
                tracks[i]->color.getGreen(),
                tracks[i]->color.getBlue(),
                255);
            ImGui::PushStyleColor(ImGuiCol_Text, trackColorU32);
            ImGui::Text("%s", tracks[i]->name.toRawUTF8());
            ImGui::PopStyleColor();

            if (ImGui::SmallButton("M"))
                tracks[i]->isMuted = !tracks[i]->isMuted;
            ImGui::SameLine();
            if (ImGui::SmallButton("S"))
                tracks[i]->isSoloed = !tracks[i]->isSoloed;

            // Col 1: Piano Roll
            ImGui::TableSetColumnIndex(1);
            ImVec2 cellMin = ImGui::GetCursorScreenPos();
            float  rowHeight = ImGui::GetTextLineHeightWithSpacing() + 10.0f;
            
            // Store track bounds for right-click erase
            trackBounds.push_back({i, cellMin.x, cellMin.y, rowHeight});

            // Grid Line
            drawList->AddLine(
                ImVec2(cellMin.x, cellMin.y + rowHeight),
                ImVec2(cellMin.x + totalWidth, cellMin.y + rowHeight),
                IM_COL32(50, 50, 50, 255));

            // Notes
            auto events = tracks[i]->getEventsCopy();
            ImU32 noteColor = IM_COL32(
                tracks[i]->color.getRed(),
                tracks[i]->color.getGreen(),
                tracks[i]->color.getBlue(),
                204);
            juce::Colour brighterColor = tracks[i]->color.brighter(0.3f);
            ImU32        noteBorderColor = IM_COL32(
                brighterColor.getRed(), brighterColor.getGreen(), brighterColor.getBlue(), 255);

            bool noteHovered = false;
            bool noteActive = false;
            int hoveredEventIndex = -1;
            
            // First pass: Draw notes
            for (size_t evIdx = 0; evIdx < events.size(); ++evIdx)
            {
                const auto& ev = events[evIdx];
                const float noteStartX_px =
                    ((float)ev.startTimeInSamples / samplesPerBeat) * pixelsPerBeat;
                const float noteEndX_px =
                    ((float)(ev.startTimeInSamples + ev.durationInSamples) / samplesPerBeat) *
                    pixelsPerBeat;

                // Culling
                if (noteEndX_px < scrollX || noteStartX_px > scrollX + visibleWidth)
                    continue;

                const float noteY_top = cellMin.y + 2.0f;
                const float noteY_bottom = cellMin.y + rowHeight - 4.0f;
                
                // Highlight if dragging this note
                ImU32 currentNoteColor = noteColor;
                ImU32 currentBorderColor = noteBorderColor;
                if (draggingNote.isDragging && 
                    draggingNote.trackIndex == (int)i && 
                    draggingNote.eventIndex == (int)evIdx)
                {
                    currentNoteColor = IM_COL32(255, 255, 100, 255); // Bright yellow when dragging
                    currentBorderColor = IM_COL32(255, 255, 200, 255);
                }

                drawList->AddRectFilled(
                    ImVec2(cellMin.x + noteStartX_px, noteY_top),
                    ImVec2(cellMin.x + noteEndX_px, noteY_bottom),
                    currentNoteColor,
                    4.0f);

                drawList->AddRect(
                    ImVec2(cellMin.x + noteStartX_px, noteY_top),
                    ImVec2(cellMin.x + noteEndX_px, noteY_bottom),
                    currentBorderColor,
                    4.0f,
                    0,
                    1.5f);

                if (noteEndX_px - noteStartX_px > 15.0f)
                {
                    juce::String noteName =
                        juce::MidiMessage::getMidiNoteName(ev.pitch, true, true, 3);
                    drawList->AddText(
                        ImVec2(cellMin.x + noteStartX_px + 2, noteY_top + 2),
                        IM_COL32(255, 255, 255, 200),
                        noteName.toRawUTF8());
                }
            }
            
            // Second pass: Create invisible buttons over notes for interaction
            ImVec2 savedCursorPos = ImGui::GetCursorScreenPos();
            for (size_t evIdx = 0; evIdx < events.size(); ++evIdx)
            {
                const auto& ev = events[evIdx];
                const float noteStartX_px =
                    ((float)ev.startTimeInSamples / samplesPerBeat) * pixelsPerBeat;
                const float noteEndX_px =
                    ((float)(ev.startTimeInSamples + ev.durationInSamples) / samplesPerBeat) *
                    pixelsPerBeat;

                // Culling
                if (noteEndX_px < scrollX || noteStartX_px > scrollX + visibleWidth)
                    continue;

                const float noteY_top = cellMin.y + 2.0f;
                const float noteY_bottom = cellMin.y + rowHeight - 4.0f;
                const float noteWidth = noteEndX_px - noteStartX_px;
                const float noteHeight = noteY_bottom - noteY_top;
                
                // Create invisible button over note to capture mouse events and prevent node dragging
                ImGui::PushID((int)(i * 1000 + evIdx));
                ImGui::SetCursorScreenPos(ImVec2(cellMin.x + noteStartX_px, noteY_top));
                ImGui::InvisibleButton("note", ImVec2(noteWidth, noteHeight), 
                    ImGuiButtonFlags_MouseButtonLeft);
                const bool isHovered = ImGui::IsItemHovered();
                const bool isActive = ImGui::IsItemActive();
                ImGui::PopID();
                
                if (isHovered || isActive)
                {
                    noteHovered = true;
                    noteActive = noteActive || isActive;
                    hoveredEventIndex = (int)evIdx;
                }
            }
            ImGui::SetCursorScreenPos(savedCursorPos);
            
            // Handle note dragging
            if (noteHovered && hoveredEventIndex >= 0 && hoveredEventIndex < (int)events.size())
            {
                // Start dragging on click
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    draggingNote.trackIndex = (int)i;
                    draggingNote.eventIndex = hoveredEventIndex;
                    draggingNote.initialMouseX = ImGui::GetMousePos().x;
                    draggingNote.initialStartTimeSamples = events[hoveredEventIndex].startTimeInSamples;
                    draggingNote.isDragging = true;
                }
            }
            
            // Continue dragging if already dragging this note
            if (draggingNote.isDragging && 
                draggingNote.trackIndex == (int)i && 
                draggingNote.eventIndex >= 0 && 
                draggingNote.eventIndex < (int)events.size())
            {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    float currentMouseX = ImGui::GetMousePos().x;
                    float deltaX = currentMouseX - draggingNote.initialMouseX;
                    
                    // Convert pixel delta to sample delta
                    double deltaSamples = (deltaX / pixelsPerBeat) * samplesPerBeat;
                    int64_t newStartTime = draggingNote.initialStartTimeSamples + (int64_t)deltaSamples;
                    
                    // Clamp to valid range
                    const double maxSamples = displayBars * 4 * samplesPerBeat;
                    newStartTime = juce::jlimit((int64_t)0, (int64_t)maxSamples, newStartTime);
                    
                    // Update the note's start time
                    auto updatedEvents = tracks[i]->getEventsCopy();
                    if (draggingNote.eventIndex < (int)updatedEvents.size())
                    {
                        updatedEvents[draggingNote.eventIndex].startTimeInSamples = newStartTime;
                        tracks[i]->setEvents(updatedEvents);
                    }
                }
                else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    // Finalize the drag
                    draggingNote.isDragging = false;
                    draggingNote.trackIndex = -1;
                    draggingNote.eventIndex = -1;
                    onModificationEnded(); // Notify that the preset has been modified
                }
            }

            // Playhead
            if (samplesPerBeat > 0)
            {
                const float playheadPx = (playheadPositionSamples / samplesPerBeat) * pixelsPerBeat;
                if (playheadPx >= scrollX && playheadPx <= scrollX + visibleWidth)
                {
                    drawList->AddLine(
                        ImVec2(cellMin.x + playheadPx, cellMin.y),
                        ImVec2(cellMin.x + playheadPx, cellMin.y + rowHeight),
                        IM_COL32(255, 255, 0, 200),
                        2.0f);
                }
            }

            // Allow seeking by clicking in the track lane (but not on notes)
            // Create invisible button for the entire track lane to capture clicks
            // This must be AFTER the note buttons so notes take priority
            ImGui::PushID((int)(i + 10000));
            ImVec2 savedCursorPos2 = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(cellMin);
            ImGui::InvisibleButton("track_lane", ImVec2(totalWidth, rowHeight), 
                ImGuiButtonFlags_MouseButtonLeft);
            const bool laneHovered = ImGui::IsItemHovered();
            const bool laneActive = ImGui::IsItemActive();
            ImGui::PopID();
            ImGui::SetCursorScreenPos(savedCursorPos2);
            
            if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !noteHovered && !noteActive)
            {
                ImVec2 itemMin = cellMin;
                float  mouseX = ImGui::GetMousePos().x;
                float  relativeX = mouseX - itemMin.x;

                double       newTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;
                const double maxSamples = displayBars * 4 * samplesPerBeat;
                newTimeSamples = juce::jlimit(0.0, maxSamples, newTimeSamples);
                playheadPositionSamples = (int64_t)newTimeSamples;
            }
            
            // Note: Invisible buttons automatically prevent node dragging by capturing mouse events
            // Reserve space for the track lane
            ImGui::Dummy(ImVec2(totalWidth, rowHeight));

            ImGui::PopID();
        }
        
        // --- RIGHT-CLICK DRAG ERASE ---
        // Similar to VideoDrawImpactModuleProcessor's erase functionality
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            bool   erasedAny = false;

            // Find which track the mouse is over and erase notes that intersect
            for (const auto& bounds : trackBounds)
            {
                if (mousePos.y >= bounds.cellMinY && mousePos.y <= bounds.cellMinY + bounds.rowHeight)
                {
                    // Mouse is over this track
                    // Convert mouse X to sample time (accounting for table scroll)
                    const float scrollXCurrent = ImGui::GetScrollX();
                    float       relativeX = mousePos.x - bounds.cellMinX + scrollXCurrent;

                    // Clamp relativeX to valid range
                    relativeX = juce::jmax(0.0f, relativeX);

                    // Convert pixel position to sample time
                    double mouseTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;

                    // Get tolerance for erasing (similar to VideoDrawImpactModuleProcessor)
                    // Use a tolerance based on zoom level - about 10 pixels worth of time
                    const double timeTolerance = (10.0 / pixelsPerBeat) * samplesPerBeat;

                    // Get events for this track
                    auto events = tracks[bounds.trackIndex]->getEventsCopy();
                    std::vector<MidiEvent> filteredEvents;

                    // Filter out notes that intersect with mouse position
                    for (const auto& ev : events)
                    {
                        double noteStart = (double)ev.startTimeInSamples;
                        double noteEnd = noteStart + (double)ev.durationInSamples;

                        // Check if mouse time intersects with note (with tolerance)
                        bool intersects = (mouseTimeSamples >= noteStart - timeTolerance) &&
                                         (mouseTimeSamples <= noteEnd + timeTolerance);

                        if (!intersects)
                        {
                            filteredEvents.push_back(ev);
                        }
                    }

                    // Update track if any notes were removed
                    if (filteredEvents.size() != events.size())
                    {
                        tracks[bounds.trackIndex]->setEvents(filteredEvents);
                        erasedAny = true;
                    }

                    // Only process one track (the first one mouse is over)
                    break;
                }
            }

            // Visual cue: red circle at mouse position while erasing
            if (erasedAny)
                ImGui::GetWindowDrawList()->AddCircleFilled(mousePos, 6.0f, IM_COL32(255, 60, 60, 200));
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            // Notify modification ended when right mouse button is released
            onModificationEnded();
        }
        
        ImGui::EndTable();
    }

    ImGui::PopStyleVar(); // scrollbar size

    // --- DEBUG INFO ---
    ImGui::Text(
        "Playhead: %.2f beats | %d tracks",
        samplesPerBeat > 0 ? playheadPositionSamples / samplesPerBeat : 0.0,
        (int)tracks.size());

    // === MIDI OUTPUT SECTION ===
    ImGui::Separator();
    ImGui::Text("MIDI Output:");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Send MIDI messages to external devices or VSTi plugins during playback");
    
    bool enableMIDI = false;
    if (enableMidiOutputParam != nullptr)
    {
        const float enableValue = enableMidiOutputParam->get();
        enableMIDI = (enableValue >= 0.5f);
    }
    if (ImGui::Checkbox("Enable##midi_out", &enableMIDI))
    {
        if (enableMidiOutputParam)
        {
            enableMidiOutputParam->setValueNotifyingHost(enableMIDI ? 1.0f : 0.0f);
            updateMidiOutputDevice();
            onModificationEnded();
        }
    }
    
    if (enableMIDI)
    {
        ImGui::Indent(20.0f);
        
        // Output mode dropdown - compact layout
        ImGui::Text("Mode:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);  // Fixed width to prevent expansion
        int outputMode = midiOutputModeParam ? midiOutputModeParam->getIndex() : 0;
        const char* modeItems[] = { "Global Default", "Custom" };  // Shorter labels
        
        if (ImGui::Combo("##midi_out_mode", &outputMode, modeItems, 2))
        {
            if (midiOutputModeParam)
            {
                midiOutputModeParam->setValueNotifyingHost(
                    apvts.getParameterRange("midi_output_mode").convertTo0to1((float)outputMode));
                updateMidiOutputDevice();
                onModificationEnded();
            }
        }
        
        // Scroll-edit for Mode combo
        if (ImGui::IsItemHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const int maxIndex = 1; // 2 modes: 0-1
                const int newIndex = juce::jlimit(0, maxIndex, outputMode + (wheel > 0.0f ? -1 : 1));
                if (newIndex != outputMode && midiOutputModeParam)
                {
                    midiOutputModeParam->setValueNotifyingHost(
                        apvts.getParameterRange("midi_output_mode").convertTo0to1((float)newIndex));
                    updateMidiOutputDevice();
                    onModificationEnded();
                }
            }
        }
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Global Default: Uses device from Audio Settings\n"
                            "Custom: Select specific device for this module");
        }
        
        // Custom device selector (only when mode is "Custom Device")
        if (outputMode == 1)
        {
            auto devices = getAvailableMidiOutputDevices();
            int currentIndex = midiOutputDeviceIndexParam ? midiOutputDeviceIndexParam->get() : -1;
            
            // Clamp index to valid range
            if (currentIndex >= (int)devices.size())
                currentIndex = -1;
            
            // Build combo list (keep strings alive)
            std::vector<const char*> deviceNames;
            std::vector<juce::String> deviceNamesStr;
            deviceNamesStr.reserve(devices.size() + 1);
            deviceNamesStr.push_back("<None>");
            deviceNames.push_back(deviceNamesStr.back().toRawUTF8());
            for (const auto& dev : devices)
            {
                deviceNamesStr.push_back(dev.first);
                deviceNames.push_back(deviceNamesStr.back().toRawUTF8());
            }
            
            ImGui::Text("Device:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);  // Fixed width
            int selectedIndex = currentIndex + 1;  // +1 because first item is "<None>"
            if (ImGui::Combo("##midi_out_device", &selectedIndex, 
                           deviceNames.data(), (int)deviceNames.size()))
            {
                int deviceIndex = selectedIndex - 1;  // -1 because first item is "<None>"
                if (deviceIndex >= 0 && deviceIndex < (int)devices.size())
                {
                    storedMidiOutputDeviceId = devices[deviceIndex].second;
                    if (midiOutputDeviceIndexParam)
                    {
                        midiOutputDeviceIndexParam->setValueNotifyingHost(
                            apvts.getParameterRange("midi_output_device_index").convertTo0to1((float)deviceIndex));
                        updateMidiOutputDevice();
                        onModificationEnded();
                    }
                }
                else if (selectedIndex == 0)  // "<None>"
                {
                    storedMidiOutputDeviceId.clear();
                    if (midiOutputDeviceIndexParam)
                    {
                        midiOutputDeviceIndexParam->setValueNotifyingHost(
                            apvts.getParameterRange("midi_output_device_index").convertTo0to1(-1.0f));
                        updateMidiOutputDevice();
                        onModificationEnded();
                    }
                }
            }
            
            // Scroll-edit for Device combo
            if (ImGui::IsItemHovered())
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    const int maxIndex = (int)deviceNames.size() - 1; // Max index in combo (including "<None>")
                    int newSelectedIndex = juce::jlimit(0, maxIndex, selectedIndex + (wheel > 0.0f ? -1 : 1));
                    if (newSelectedIndex != selectedIndex)
                    {
                        int deviceIndex = newSelectedIndex - 1;  // -1 because first item is "<None>"
                        if (deviceIndex >= 0 && deviceIndex < (int)devices.size())
                        {
                            storedMidiOutputDeviceId = devices[deviceIndex].second;
                            if (midiOutputDeviceIndexParam)
                            {
                                midiOutputDeviceIndexParam->setValueNotifyingHost(
                                    apvts.getParameterRange("midi_output_device_index").convertTo0to1((float)deviceIndex));
                                updateMidiOutputDevice();
                                onModificationEnded();
                            }
                        }
                        else if (newSelectedIndex == 0)  // "<None>"
                        {
                            storedMidiOutputDeviceId.clear();
                            if (midiOutputDeviceIndexParam)
                            {
                                midiOutputDeviceIndexParam->setValueNotifyingHost(
                                    apvts.getParameterRange("midi_output_device_index").convertTo0to1(-1.0f));
                                updateMidiOutputDevice();
                                onModificationEnded();
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // Show global device name - compact
            juce::String globalDeviceName = "<None>";
            if (audioDeviceManager)
            {
                juce::String deviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
                if (deviceId.isNotEmpty())
                {
                    auto devices = juce::MidiOutput::getAvailableDevices();
                    for (const auto& dev : devices)
                    {
                        if (dev.identifier == deviceId)
                        {
                            globalDeviceName = dev.name;
                            break;
                        }
                    }
                }
            }
            ImGui::Text("Device:");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", globalDeviceName.toRawUTF8());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Using device from Audio Settings\n"
                                "Change in Settings → Audio Settings → MIDI Output");
            }
        }
        
        // Channel selector - compact
        ImGui::Text("Channel:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);  // Narrower for channel
        int channel = midiOutputChannelParam ? midiOutputChannelParam->get() : 0;
        if (ImGui::SliderInt("##midi_out_ch", &channel, 0, 16))
        {
            if (midiOutputChannelParam)
            {
                midiOutputChannelParam->setValueNotifyingHost(
                    apvts.getParameterRange("midi_output_channel").convertTo0to1((float)channel));
                onModificationEnded();
            }
        }
        
        // Scroll-edit for Channel slider
        if (ImGui::IsItemHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                int newChannel = juce::jlimit(0, 16, channel + (wheel > 0.0f ? -1 : 1));
                if (newChannel != channel && midiOutputChannelParam)
                {
                    midiOutputChannelParam->setValueNotifyingHost(
                        apvts.getParameterRange("midi_output_channel").convertTo0to1((float)newChannel));
                    onModificationEnded();
                }
            }
        }
        
        if (ImGui::IsItemHovered())
        {
            juce::String tooltip = (channel == 0) ?
                "Channel 0: Each track outputs on its own channel (Track 1→Ch1, Track 2→Ch2, etc.)" :
                juce::String("All tracks output on channel ") + juce::String(channel);
            ImGui::SetTooltip("%s", tooltip.toRawUTF8());
        }
        
        ImGui::Unindent(20.0f);
    }

    ImGui::PopItemWidth(); // FIXED: Matches the PushItemWidth at start
    ImGui::PopID();
}

void MidiLoggerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Show pins for all active tracks + 1
    int activeCount = 0;
    for (const auto& t : tracks)
        if (t->active)
            activeCount++;

    const int numInputTracks = std::min((int)MaxTracks, activeCount + 1);

    for (int trackIndex = 0; trackIndex < numInputTracks; ++trackIndex)
    {
        const bool hasOutputs = tracks[trackIndex]->active;
        const int  trackNumber = trackIndex + 1;

        auto drawRow = [&](const char* labelPrefix, int channelOffset) {
            const int    channel = trackIndex * 3 + channelOffset;
            juce::String inLabel = juce::String(labelPrefix) + " " + juce::String(trackNumber);
            juce::String outLabel = inLabel;

            helpers.drawParallelPins(
                inLabel.toRawUTF8(),
                channel,
                hasOutputs ? outLabel.toRawUTF8() : nullptr,
                hasOutputs ? channel : -1);
        };

        drawRow("Gate", 0);
        drawRow("Pitch", 1);
        drawRow("Velo", 2);
    }
}
#endif

bool MidiLoggerModuleProcessor::usesCustomPinLayout() const { return true; }

// --- MidiTrack Method Implementations ---

void MidiLoggerModuleProcessor::MidiTrack::addNoteOn(int pitch, float velocity, int64_t startTime)
{
    const juce::ScopedWriteLock lock(eventLock);
    // Only add a new note if it's not already playing (prevents duplicate ons)
    if (activeNotes.find(pitch) == activeNotes.end())
    {
        activeNotes[pitch] = {velocity, startTime};
    }
}

void MidiLoggerModuleProcessor::MidiTrack::addNoteOff(int pitch, int64_t endTime)
{
    const juce::ScopedWriteLock lock(eventLock);

    if (activeNotes.empty())
        return;

    int pitchToTurnOff = pitch;

    // If the pitch is -1, it's our signal to find the oldest active note.
    // This is a robust heuristic for monophonic lines: turn off the note
    // that has been held the longest.
    if (pitchToTurnOff == -1)
    {
        // Find the note with the smallest start time (the oldest one).
        auto oldestNote = std::min_element(
            activeNotes.begin(), activeNotes.end(), [](const auto& a, const auto& b) {
                return a.second.second < b.second.second; // Compare start times
            });

        if (oldestNote != activeNotes.end())
        {
            pitchToTurnOff = oldestNote->first;
        }
        else
        {
            return; // No active notes to turn off
        }
    }

    // Turn off the specific pitch
    auto it = activeNotes.find(pitchToTurnOff);
    if (it != activeNotes.end())
    {
        const auto    noteData = it->second;
        const float   velocity = noteData.first;
        const int64_t startTime = noteData.second;
        const int64_t duration = endTime - startTime;

        // Only record notes that have a valid duration
        if (duration > 0)
        {
            events.push_back({pitchToTurnOff, velocity, startTime, duration});
        }
        activeNotes.erase(it);
    }
}

std::vector<MidiLoggerModuleProcessor::MidiEvent> MidiLoggerModuleProcessor::MidiTrack::
    getEventsCopy() const
{
    const juce::ScopedReadLock lock(eventLock);
    return events;
}

void MidiLoggerModuleProcessor::MidiTrack::setEvents(const std::vector<MidiEvent>& newEvents)
{
    const juce::ScopedWriteLock lock(eventLock);
    events = newEvents;
}

// Helper to safely create a new track when it receives its first event.
void MidiLoggerModuleProcessor::activateTrack(int trackIndex)
{
    if (trackIndex >= 0 && trackIndex < tracks.size())
    {
        if (!tracks[trackIndex]->active)
        {
            tracks[trackIndex]->active = true;
            // Signal the message thread to update the name and UI
            trackNeedsNaming[trackIndex] = true;
        }
    }
}

void MidiLoggerModuleProcessor::timerCallback()
{
    bool anyUpdate = false;

    for (int i = 0; i < MaxTracks; ++i)
    {
        if (trackNeedsNaming[i].exchange(false))
        {
            // SMART NAMING: Get the name of the connected source node
            // This is safe to do on the message thread
            juce::String trackName = "Track " + juce::String(i + 1); // Default name

            if (auto* parent = getParent())
            {
                const juce::uint32 myLogicalId = getLogicalId();
                auto               connections = parent->getConnectionsInfo();
                const int          gateChannel = i * 3 + 0;

                for (const auto& conn : connections)
                {
                    if (conn.dstLogicalId == myLogicalId && conn.dstChan == gateChannel &&
                        !conn.dstIsOutput)
                    {
                        if (auto* sourceModule = parent->getModuleForLogical(conn.srcLogicalId))
                        {
                            trackName = sourceModule->getName();
                            juce::Logger::writeToLog(
                                "[MIDI Logger] Track " + juce::String(i + 1) + " auto-named: \"" +
                                trackName + "\" (from connected node)");
                            break;
                        }
                    }
                }
            }

            tracks[i]->name = trackName;
            anyUpdate = true;
        }
    }

    if (anyUpdate)
    {
        updateHostDisplay();
    }
}

// ==============================================================================
// PHASE 5: MIDI EXPORT IMPLEMENTATION
// ==============================================================================

// This function converts our internal sample-based timing to standard MIDI ticks.
double MidiLoggerModuleProcessor::samplesToMidiTicks(int64_t samples) const
{
    // A standard MIDI file resolution is 960 pulses (ticks) per quarter note.
    const double ticksPerQuarterNote = 960.0;
    const double seconds = static_cast<double>(samples) / currentSampleRate;
    const double beats = seconds * (currentBpm.load() / 60.0);
    return beats * ticksPerQuarterNote;
}

// This is the main export function.
void MidiLoggerModuleProcessor::exportToMidiFile()
{
    // Default to exe/midi/ directory, create if it doesn't exist
    juce::File startDir;
    auto       exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    auto midiDir = exeDir.getChildFile("midi");
    if (midiDir.exists() && midiDir.isDirectory())
        startDir = midiDir;
    else if (midiDir.createDirectory())
        startDir = midiDir;
    else
        startDir = juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory); // Fallback to user documents

    // Use a native file chooser to ask the user where to save the file.
    fileChooser = std::make_unique<juce::FileChooser>("Save MIDI File", startDir, "*.mid");

    auto chooserFlags =
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File{})
            return; // User cancelled

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(960);

        // === CRITICAL FIX: Create Tempo Track (Track 0) with Meta Events ===
        juce::MidiMessageSequence tempoTrack;

        // 1. Add track name
        tempoTrack.addEvent(juce::MidiMessage::textMetaEvent(3, "Tempo Track"), 0.0);

        // 2. Add time signature (4/4 - standard)
        tempoTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);

        // 3. **CRITICAL**: Add tempo meta event
        // Convert BPM to microseconds per quarter note
        const double microsecondsPerQuarterNote = 60'000'000.0 / currentBpm.load();
        tempoTrack.addEvent(
            juce::MidiMessage::tempoMetaEvent(static_cast<int>(microsecondsPerQuarterNote)), 0.0);

        // 4. Add end-of-track marker
        tempoTrack.addEvent(juce::MidiMessage::endOfTrack(), 0.0);

        // Add tempo track FIRST (Track 0 in Type 1 MIDI files)
        midiFile.addTrack(tempoTrack);
        // === END TEMPO TRACK ===

        // Iterate through each of our internal note tracks (become Track 1, 2, 3...).
        for (size_t trackIdx = 0; trackIdx < tracks.size(); ++trackIdx)
        {
            if (!tracks[trackIdx])
                continue;

            auto events = tracks[trackIdx]->getEventsCopy();
            if (events.empty())
                continue;

            juce::MidiMessageSequence sequence;

            // Add track name meta event for better organization
            sequence.addEvent(
                juce::MidiMessage::textMetaEvent(3, tracks[trackIdx]->name.toStdString()), 0.0);

            // Convert each MidiEvent into a pair of juce::MidiMessages.
            for (const auto& ev : events)
            {
                sequence.addEvent(
                    juce::MidiMessage::noteOn(1, ev.pitch, (juce::uint8)(ev.velocity * 127.0f)),
                    samplesToMidiTicks(ev.startTimeInSamples));

                sequence.addEvent(
                    juce::MidiMessage::noteOff(1, ev.pitch),
                    samplesToMidiTicks(ev.startTimeInSamples + ev.durationInSamples));
            }

            // Add end-of-track marker for this track
            double lastTick =
                events.empty()
                    ? 0.0
                    : samplesToMidiTicks(
                          events.back().startTimeInSamples + events.back().durationInSamples);
            sequence.addEvent(juce::MidiMessage::endOfTrack(), lastTick + 100.0);

            sequence.updateMatchedPairs(); // Clean up the sequence
            midiFile.addTrack(sequence);
        }

        // Write the completed MIDI file to disk.
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
        {
            midiFile.writeTo(stream);
            juce::Logger::writeToLog(
                "[MIDI Logger] Exported " + juce::String(midiFile.getNumTracks()) + " tracks at " +
                juce::String(currentBpm, 1) + " BPM to: " + file.getFullPathName());
        }
        else
        {
            juce::Logger::writeToLog("[MIDI Logger] ERROR: Failed to open file for writing");
        }
    });
}

// ==============================================================================
// MIDI OUTPUT IMPLEMENTATION
// ==============================================================================

void MidiLoggerModuleProcessor::updateMidiOutputDevice()
{
    const juce::ScopedLock lock(midiOutputLock);
    
    if (!enableMidiOutputParam || !enableMidiOutputParam->get())
    {
        // MIDI output disabled - close device
        midiOutputDevice.reset();
        currentMidiOutputDeviceId.clear();
        return;
    }
    
    juce::String targetDeviceId;
    
    // Determine which device to use
    if (midiOutputModeParam && midiOutputModeParam->getCurrentChoiceName() == "Use Global Default")
    {
        // Use global default from AudioDeviceManager
        if (audioDeviceManager)
            targetDeviceId = audioDeviceManager->getDefaultMidiOutputIdentifier();
    }
    else
    {
        // Use custom device from stored ID (set by UI) or index
        if (!storedMidiOutputDeviceId.isEmpty())
        {
            // First try stored ID (most reliable)
            targetDeviceId = storedMidiOutputDeviceId;
        }
        else if (midiOutputDeviceIndexParam && midiOutputDeviceIndexParam->get() >= 0)
        {
            // Fall back to index if ID not stored
            auto devices = getAvailableMidiOutputDevices();
            int deviceIndex = midiOutputDeviceIndexParam->get();
            if (deviceIndex < (int)devices.size())
            {
                targetDeviceId = devices[deviceIndex].second;
                storedMidiOutputDeviceId = targetDeviceId;  // Cache the ID for next time
            }
        }
    }
    
    // Only change if device changed
    if (targetDeviceId == currentMidiOutputDeviceId && midiOutputDevice)
        return;
    
    // Close current device
    midiOutputDevice.reset();
    currentMidiOutputDeviceId.clear();
    
    // Open new device
    if (targetDeviceId.isNotEmpty())
    {
        midiOutputDevice = juce::MidiOutput::openDevice(targetDeviceId);
        if (midiOutputDevice)
        {
            currentMidiOutputDeviceId = targetDeviceId;
            juce::Logger::writeToLog("[MIDI Logger] Opened MIDI output: " + targetDeviceId);
        }
        else
        {
            juce::Logger::writeToLog("[MIDI Logger] Failed to open MIDI output: " + targetDeviceId);
        }
    }
}

void MidiLoggerModuleProcessor::sendMidiToOutput(const juce::MidiMessage& message)
{
    const juce::ScopedLock lock(midiOutputLock);
    if (midiOutputDevice)
        midiOutputDevice->sendMessageNow(message);
}

std::vector<std::pair<juce::String, juce::String>> 
MidiLoggerModuleProcessor::getAvailableMidiOutputDevices() const
{
    std::vector<std::pair<juce::String, juce::String>> devices;
    auto available = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : available)
        devices.push_back({device.name, device.identifier});
    return devices;
}
