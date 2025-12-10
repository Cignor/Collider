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

    // Activate the first track by default
    tracks[0]->active = true;
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiLoggerModuleProcessor::
    createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ADDITION FOR PHASE 3: Loop Length Control
    params.push_back(
        std::make_unique<juce::AudioParameterInt>("loopLength", "Loop Length", 1, 64, 4));

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

void MidiLoggerModuleProcessor::releaseResources() {}

// ==============================================================================
// PHASE 2: UPDATED PROCESS BLOCK WITH PLAYBACK
// ==============================================================================

void MidiLoggerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        if (ImGui::SliderInt("##loop", &loopLen, 1, 64, "Loop: %d bars"))
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

    const double displayBars = std::max((double)loopLengthBars, currentPlayheadBars + 0.25);
    const float  totalWidth = (float)(displayBars * 4.0 * pixelsPerBeat);

    // --- 3. MAIN TABLE (Tracks + Timeline) ---
    // We use a single table for everything.
    // Column 0: Track Headers (Frozen)
    // Column 1: Timeline (Scrollable)
    // Row 0: Timeline Ruler (Frozen)

    if (ImGui::BeginTable(
            "TrackTable",
            2,
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
            ImVec2(0, contentHeight)))
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
        // We allow seeking by clicking on the ruler
        ImGui::Dummy(ImVec2(totalWidth, rulerHeight)); // Reserve space
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImVec2 itemMin = ImGui::GetItemRectMin();
            float  mouseX = ImGui::GetMousePos().x;
            float  relativeX = mouseX - itemMin.x; // This is already in scrolled space!

            double       newTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;
            const double maxSamples = displayBars * 4 * samplesPerBeat;
            newTimeSamples = juce::jlimit(0.0, maxSamples, newTimeSamples);
            playheadPositionSamples = (int64_t)newTimeSamples;
        }

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

            // Grid Line
            drawList->AddLine(
                ImVec2(cellMin.x, cellMin.y + rowHeight),
                ImVec2(cellMin.x + totalWidth, cellMin.y + rowHeight),
                IM_COL32(50, 50, 50, 255));

            // Notes
            auto  events = tracks[i]->getEventsCopy();
            ImU32 noteColor = IM_COL32(
                tracks[i]->color.getRed(),
                tracks[i]->color.getGreen(),
                tracks[i]->color.getBlue(),
                204);
            juce::Colour brighterColor = tracks[i]->color.brighter(0.3f);
            ImU32        noteBorderColor = IM_COL32(
                brighterColor.getRed(), brighterColor.getGreen(), brighterColor.getBlue(), 255);

            for (const auto& ev : events)
            {
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

                drawList->AddRectFilled(
                    ImVec2(cellMin.x + noteStartX_px, noteY_top),
                    ImVec2(cellMin.x + noteEndX_px, noteY_bottom),
                    noteColor,
                    4.0f);

                drawList->AddRect(
                    ImVec2(cellMin.x + noteStartX_px, noteY_top),
                    ImVec2(cellMin.x + noteEndX_px, noteY_bottom),
                    noteBorderColor,
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

            // Allow seeking by clicking in the track lane too
            ImGui::Dummy(ImVec2(totalWidth, rowHeight));
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ImVec2 itemMin = ImGui::GetItemRectMin();
                float  mouseX = ImGui::GetMousePos().x;
                float  relativeX = mouseX - itemMin.x;

                double       newTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;
                const double maxSamples = displayBars * 4 * samplesPerBeat;
                newTimeSamples = juce::jlimit(0.0, maxSamples, newTimeSamples);
                playheadPositionSamples = (int64_t)newTimeSamples;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- DEBUG INFO ---
    ImGui::Text(
        "Playhead: %.2f beats | %d tracks",
        samplesPerBeat > 0 ? playheadPositionSamples / samplesPerBeat : 0.0,
        (int)tracks.size());

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
    ImGui::PopID();
}
