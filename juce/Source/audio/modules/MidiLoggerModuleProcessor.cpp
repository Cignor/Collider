#include "MidiLoggerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h" // Needed for connection queries
#include <juce_gui_basics/juce_gui_basics.h> // Needed for FileChooser
#include <algorithm>

MidiLoggerModuleProcessor::MidiLoggerModuleProcessor()
    : ModuleProcessor(BusesProperties()
          // We define a large potential number of buses, which will be dynamically
          // shown/hidden by getDynamicInputPins.
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(256), true)
          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(256), true)),
      apvts(*this, nullptr, "MidiLoggerParams", createParameterLayout())
{
    loopLengthParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("loopLength"));
    
    ensureTrackExists(0); // Start with one default track
    tracks[0]->name = "Track 1";
    // Assign a default color for the first track
    tracks[0]->color = juce::Colour(0xff8080ff); // Light blue
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiLoggerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // ADDITION FOR PHASE 3: Loop Length Control
    params.push_back(std::make_unique<juce::AudioParameterInt>("loopLength", "Loop Length", 1, 64, 4));
    
    return { params.begin(), params.end() };
}

void MidiLoggerModuleProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    playheadPositionSamples = 0;
    
    // Initialize states for the maximum potential number of tracks
    const int maxTracks = 256 / 3; // Based on our max channel count
    previousGateState.assign(maxTracks, false);
    playbackStates.assign(maxTracks, {});
}

void MidiLoggerModuleProcessor::releaseResources() {}

// ==============================================================================
// PHASE 2: UPDATED PROCESS BLOCK WITH PLAYBACK
// ==============================================================================
void MidiLoggerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    auto inputBus = getBusBuffer(buffer, true, 0);
    auto outputBus = getBusBuffer(buffer, false, 0);

    // ALWAYS check for connected inputs to create tracks dynamically
    for (int i = 0; i < (int)tracks.size() + 1; ++i)
    {
        const int gateChannel = i * 3 + 0;
        
        if (gateChannel < inputBus.getNumChannels())
        {
            const float* gateData = inputBus.getReadPointer(gateChannel);
            bool hasSignal = false;
            
            for (int s = 0; s < numSamples && !hasSignal; ++s)
            {
                if (std::abs(gateData[s]) > 0.001f)
                    hasSignal = true;
            }
            
            if (hasSignal && i >= (int)tracks.size())
            {
                ensureTrackExists(i);
                juce::Logger::writeToLog("[MIDI Logger] Track " + juce::String(i + 1) + " auto-created (input detected)");
            }
        }
    }

    // --- SECTION 1: RECORDING LOGIC ---
    if (transportState.load() == TransportState::Recording)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            for (int i = 0; i < (int)tracks.size() + 1; ++i)
            {
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
                const bool isGateHigh = gateSignal >= 0.5f;

                if (isGateHigh && !previousGateState[i])
                {
                    ensureTrackExists(i);

                    const float pitchCV = inputBus.getReadPointer(pitchChannel)[sample];
                    const float veloCV = inputBus.getReadPointer(veloChannel)[sample];

                    const int midiNote = static_cast<int>(std::round(pitchCV * 12.0f) + 60.0f);
                    const float velocity = juce::jlimit(0.0f, 1.0f, veloCV);

                    tracks[i]->addNoteOn(midiNote, velocity, playheadPositionSamples);
                    
                    juce::Logger::writeToLog("RECORD [Track " + juce::String(i + 1) + 
                                             "]: Note ON, Pitch: " + juce::String(midiNote) + 
                                             ", Velo: " + juce::String(velocity));
                }
                else if (!isGateHigh && previousGateState[i])
                {
                    if (i < (int)tracks.size() && tracks[i])
                    {
                        tracks[i]->addNoteOff(-1, playheadPositionSamples);
                        juce::Logger::writeToLog("RECORD [Track " + juce::String(i + 1) + "]: Note OFF");
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
            for (int trackIdx = 0; trackIdx < (int)tracks.size(); ++trackIdx)
            {
                if (!tracks[trackIdx]) continue;

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
                        playbackStates[trackIdx].pitch = (ev.pitch - 60.0f) / 12.0f; // MIDI to V/Oct
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
            for (int trackIdx = 0; trackIdx < (int)tracks.size(); ++trackIdx)
            {
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
            const double samplesPerBeat = (60.0 / currentBpm) * currentSampleRate;
            const int loopLengthBars = loopLengthParam ? loopLengthParam->get() : 4;
            const int64_t loopEndSamples = static_cast<int64_t>(loopLengthBars * 4.0 * samplesPerBeat);
            
            if (playheadPositionSamples >= loopEndSamples)
            {
                playheadPositionSamples = 0;
            }
        }
    }
    else // Stopped
    {
        playheadPositionSamples = 0;
        // Turn off all gates
        for(auto& state : playbackStates) 
            state.gate = 0.0f;
    }
}

// ==============================================================================
// PHASE 1: DYNAMIC PIN MANAGEMENT
// ==============================================================================
std::vector<DynamicPinInfo> MidiLoggerModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    // Always expose inputs for N existing tracks + 1 empty one.
    const int numTracksToShow = (int)tracks.size() + 1;

    for (int i = 0; i < numTracksToShow; ++i)
    {
        const juce::String trackNumStr = juce::String(i + 1);
        pins.push_back({ "Gate " + trackNumStr, i * 3 + 0, PinDataType::Gate });
        pins.push_back({ "Pitch " + trackNumStr, i * 3 + 1, PinDataType::CV });
        pins.push_back({ "Velo " + trackNumStr, i * 3 + 2, PinDataType::CV });
    }
    return pins;
}

// ==============================================================================
// PHASE 2: DYNAMIC OUTPUT PIN MANAGEMENT
// ==============================================================================
std::vector<DynamicPinInfo> MidiLoggerModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    // Show output pins for every track that has been created.
    const int numTracksToShow = (int)tracks.size();

    for (int i = 0; i < numTracksToShow; ++i)
    {
        const juce::String trackNumStr = juce::String(i + 1);
        pins.push_back({ "Gate " + trackNumStr, i * 3 + 0, PinDataType::Gate });
        pins.push_back({ "Pitch " + trackNumStr, i * 3 + 1, PinDataType::CV });
        pins.push_back({ "Velo " + trackNumStr, i * 3 + 2, PinDataType::CV });
    }
    return pins;
}

// ==============================================================================
// PHASE 4: PIANO ROLL UI IMPLEMENTATION
// ==============================================================================
#if defined(PRESET_CREATOR_UI)
void MidiLoggerModuleProcessor::drawParametersInNode(float /*itemWidth*/, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    // --- Invisible Scaffolding ---
    ImGui::Dummy(ImVec2(nodeWidth, 0.0f));
    
    // --- 1. TOOLBAR ---
    const char* statusText = "■ Stopped";
    ImVec4 statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    
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
        for(auto& track : tracks) 
        {
            if(track) track->setEvents({});
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

    // --- 2. MAIN CONTENT AREA (TRACKS + TIMELINE) ---
    const float trackHeaderWidth = 100.0f;
    const float timelineHeight = 30.0f;
    const float contentHeight = 250.0f;

    ImGui::BeginChild("MainContent", ImVec2(nodeWidth, contentHeight), true, 
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetCursorScreenPos();
    const float scrollX = ImGui::GetScrollX();
    const float scrollY = ImGui::GetScrollY();

    // --- 3. TIMELINE RULER (UPGRADED: Matches MIDI Player) ---
    const double samplesPerBeat = (60.0 / currentBpm) * currentSampleRate;
    const float pixelsPerBeat = zoomX;
    const int loopLengthBars = loopLengthParam ? loopLengthParam->get() : 4;
    const float totalWidth = loopLengthBars * 4.0f * pixelsPerBeat;

    // CRITICAL: Reserve space for the ENTIRE timeline content so scrolling works properly
    ImGui::Dummy(ImVec2(totalWidth, timelineHeight));
    
    // Get the screen position for drawing (AFTER Dummy)
    const ImVec2 timelineStartPos = ImGui::GetItemRectMin();
    
    // Draw timeline background (only visible portion for performance)
    const float visibleLeft = timelineStartPos.x;
    const float visibleRight = visibleLeft + nodeWidth;
    drawList->AddRectFilled(
        ImVec2(visibleLeft, timelineStartPos.y), 
        ImVec2(visibleRight, timelineStartPos.y + timelineHeight), 
        IM_COL32(30, 30, 30, 255)
    );
    
    // --- SCROLL-AWARE CULLING FOR PERFORMANCE ---
    // Only draw beats that are actually visible in the current scroll position
    const int totalBeats = loopLengthBars * 4;
    const int firstBeat = juce::jmax(0, static_cast<int>(scrollX / pixelsPerBeat));
    const int lastBeat = juce::jmin(totalBeats, static_cast<int>((scrollX + nodeWidth) / pixelsPerBeat) + 1);
    
    // Draw only visible bar and beat lines
    for (int beatIndex = firstBeat; beatIndex <= lastBeat; ++beatIndex)
    {
        const bool isBarLine = (beatIndex % 4 == 0);
        const int barNumber = beatIndex / 4;
        
        // Calculate absolute position in content space
        const float x = timelineStartPos.x + (beatIndex * pixelsPerBeat);
        
        // Draw the vertical line
        drawList->AddLine(
            ImVec2(x, timelineStartPos.y),
            ImVec2(x, timelineStartPos.y + timelineHeight),
            isBarLine ? IM_COL32(140, 140, 140, 255) : IM_COL32(70, 70, 70, 255),
            isBarLine ? 2.0f : 1.0f
        );
        
        // Draw bar number label for bar lines
        if (isBarLine)
        {
            char label[8];
            snprintf(label, sizeof(label), "%d", barNumber + 1);
            drawList->AddText(ImVec2(x + 4, timelineStartPos.y + 4), IM_COL32(220, 220, 220, 255), label);
        }
    }

    // --- 4. TRACK HEADERS ---
    // Draw track names and controls on the left side
    const float trackHeight = 40.0f;
    ImGui::SetCursorScreenPos(ImVec2(windowPos.x, timelineStartPos.y + timelineHeight));

    ImGui::BeginChild("TrackHeaders", ImVec2(trackHeaderWidth, contentHeight - timelineHeight), false);
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (!tracks[i]) continue;
        ImGui::BeginGroup();
        ImGui::PushID((int)i);
        
        // Use the track's color for its name
        ImU32 trackColorU32 = IM_COL32(tracks[i]->color.getRed(), 
                                       tracks[i]->color.getGreen(), 
                                       tracks[i]->color.getBlue(), 255);
        ImGui::PushStyleColor(ImGuiCol_Text, trackColorU32);
        ImGui::Text("%s", tracks[i]->name.toRawUTF8());
        ImGui::PopStyleColor();

        // Mute/Solo buttons (functionality to be added later)
        if (ImGui::SmallButton("M")) { tracks[i]->isMuted = !tracks[i]->isMuted; }
        ImGui::SameLine();
        if (ImGui::SmallButton("S")) { tracks[i]->isSoloed = !tracks[i]->isSoloed; }
        
        ImGui::PopID();
        ImGui::EndGroup();
        
        // FIX: Use Dummy() to properly reserve space for the track height
        // This tells ImGui we used the space, preventing the layout error
        const float groupHeight = ImGui::GetItemRectSize().y;
        const float remainingHeight = trackHeight - groupHeight;
        if (remainingHeight > 0)
            ImGui::Dummy(ImVec2(0, remainingHeight));
    }
    ImGui::EndChild();

    // --- 5. PIANO ROLL GRID & NOTE RENDERING ---
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("PianoRoll", ImVec2(0, contentHeight - timelineHeight), false, 
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    
    // CRITICAL: Reserve space for ENTIRE piano roll content (width × height)
    const float pianoRollHeight = std::max(100.0f, (float)tracks.size() * trackHeight);
    ImGui::Dummy(ImVec2(totalWidth, pianoRollHeight));
    
    // Get the piano roll area bounds (AFTER Dummy)
    const ImVec2 gridStartPos = ImGui::GetItemRectMin();

    // A. Draw Grid Background (horizontal lines for tracks)
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const float y = gridStartPos.y + (i * trackHeight) - scrollY;
        drawList->AddLine(
            ImVec2(windowPos.x, y), 
            ImVec2(windowPos.x + totalWidth, y),
            IM_COL32(50, 50, 50, 255)
        );
    }

    // B. Draw MIDI Notes for each track
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (!tracks[i] || !tracks[i]->isVisible) continue;

        auto events = tracks[i]->getEventsCopy();
        
        // Convert juce::Colour to ImU32
        ImU32 noteColor = IM_COL32(tracks[i]->color.getRed(), 
                                   tracks[i]->color.getGreen(), 
                                   tracks[i]->color.getBlue(), 
                                   204); // 0.8f * 255
        juce::Colour brighterColor = tracks[i]->color.brighter(0.3f);
        ImU32 noteBorderColor = IM_COL32(brighterColor.getRed(), 
                                         brighterColor.getGreen(), 
                                         brighterColor.getBlue(), 255);

        for (const auto& ev : events)
        {
            const float noteStartX_samples = (float)ev.startTimeInSamples;
            const float noteEndX_samples = (float)(ev.startTimeInSamples + ev.durationInSamples);

            // Convert sample time to pixel position (ABSOLUTE positioning - no scroll subtraction)
            const float noteStartX_px = gridStartPos.x + (noteStartX_samples / samplesPerBeat) * pixelsPerBeat;
            const float noteEndX_px = gridStartPos.x + (noteEndX_samples / samplesPerBeat) * pixelsPerBeat;

            // ImGui handles clipping automatically - no manual culling needed

            // Stack notes on a single line per track for now
            // (A full piano roll would use `ev.pitch` to determine the Y position)
            const float noteY_top = gridStartPos.y + (i * trackHeight) + 5.0f - scrollY;
            const float noteY_bottom = noteY_top + trackHeight - 10.0f;

            // Draw the note rectangle
            drawList->AddRectFilled(
                ImVec2(noteStartX_px, noteY_top),
                ImVec2(noteEndX_px, noteY_bottom),
                noteColor,
                4.0f // corner rounding
            );
            // Draw a border for clarity
            drawList->AddRect(
                ImVec2(noteStartX_px, noteY_top),
                ImVec2(noteEndX_px, noteY_bottom),
                noteBorderColor,
                4.0f,
                0,
                1.5f
            );
        }
    }

    // --- 6. PLAYHEAD (CRITICAL: Must be INSIDE BeginChild/EndChild for clipping!) ---
    // Draw playhead at its absolute position in the content (ImGui clips to child window)
    if (samplesPerBeat > 0)
    {
        const float playheadX = gridStartPos.x + (playheadPositionSamples / samplesPerBeat) * pixelsPerBeat;
        
        drawList->AddLine(
            ImVec2(playheadX, gridStartPos.y),
            ImVec2(playheadX, gridStartPos.y + pianoRollHeight),
            IM_COL32(255, 255, 0, 200), // Yellow playhead
            2.0f
        );
        
        // Draw a triangle handle at the top for visual reference
        drawList->AddTriangleFilled(
            ImVec2(playheadX, gridStartPos.y),
            ImVec2(playheadX - 6.0f, gridStartPos.y + 10.0f),
            ImVec2(playheadX + 6.0f, gridStartPos.y + 10.0f),
            IM_COL32(255, 255, 0, 255)
        );
    }
    
    ImGui::EndChild();

    ImGui::EndChild();
    
    // --- 7. CLICK-TO-SEEK INTERACTION (New feature from MIDI Player) ---
    // Check if user clicked in the MainContent child window
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // CRITICAL FIX: Get the child window's screen bounds (the item we just ended)
        ImVec2 childWindowMin = ImGui::GetItemRectMin(); // Top-left of visible child window
        float mouseX = ImGui::GetMousePos().x;
        
        // Calculate timeline position: mouse relative to visible window + scroll offset
        float relativeX = (mouseX - childWindowMin.x) + scrollX;
        double newTimeSamples = (relativeX / pixelsPerBeat) * samplesPerBeat;
        
        // Clamp to valid range
        const double loopEndSamples = loopLengthBars * 4 * samplesPerBeat;
        newTimeSamples = juce::jlimit(0.0, loopEndSamples, newTimeSamples);
        
        // Set the new playhead position
        playheadPositionSamples = (int64_t)newTimeSamples;
    }
    
    // --- DEBUG INFO ---
    ImGui::Text("Playhead: %.2f beats | %d tracks", 
                samplesPerBeat > 0 ? playheadPositionSamples / samplesPerBeat : 0.0, 
                (int)tracks.size());
}
#endif

// --- MidiTrack Method Implementations ---

void MidiLoggerModuleProcessor::MidiTrack::addNoteOn(int pitch, float velocity, int64_t startTime)
{
    const juce::ScopedWriteLock lock(eventLock);
    // Only add a new note if it's not already playing (prevents duplicate ons)
    if (activeNotes.find(pitch) == activeNotes.end())
    {
        activeNotes[pitch] = { velocity, startTime };
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
        auto oldestNote = std::min_element(activeNotes.begin(), activeNotes.end(),
            [](const auto& a, const auto& b) {
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
        const auto noteData = it->second;
        const float velocity = noteData.first;
        const int64_t startTime = noteData.second;
        const int64_t duration = endTime - startTime;

        // Only record notes that have a valid duration
        if (duration > 0)
        {
            events.push_back({ pitchToTurnOff, velocity, startTime, duration });
        }
        activeNotes.erase(it);
    }
}

std::vector<MidiLoggerModuleProcessor::MidiEvent> MidiLoggerModuleProcessor::MidiTrack::getEventsCopy() const
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
void MidiLoggerModuleProcessor::ensureTrackExists(int trackIndex)
{
    if (trackIndex >= (int)tracks.size())
    {
        tracks.resize(trackIndex + 1);
        previousGateState.resize(trackIndex + 2, false);
        if (tracks[trackIndex] == nullptr)
        {
            tracks[trackIndex] = std::make_unique<MidiTrack>();
            
            // SMART NAMING: Get the name of the connected source node
            juce::String trackName = "Track " + juce::String(trackIndex + 1); // Default name
            
            if (auto* parent = getParent())
            {
                // Get my own logical ID and all connections
                const juce::uint32 myLogicalId = getLogicalId();
                auto connections = parent->getConnectionsInfo();
                
                // Calculate the gate input channel for this track (track i uses channels i*3, i*3+1, i*3+2)
                const int gateChannel = trackIndex * 3 + 0;
                
                // Find what's connected to this track's gate input
                for (const auto& conn : connections)
                {
                    // Check if this connection is to our gate input
                    if (conn.dstLogicalId == myLogicalId && 
                        conn.dstChan == gateChannel && 
                        !conn.dstIsOutput)
                    {
                        // Get the source module
                        if (auto* sourceModule = parent->getModuleForLogical(conn.srcLogicalId))
                        {
                            trackName = sourceModule->getName();
                            juce::Logger::writeToLog("[MIDI Logger] Track " + juce::String(trackIndex + 1) + 
                                                    " auto-named: \"" + trackName + "\" (from connected node)");
                            break;
                        }
                    }
                }
            }
            
            tracks[trackIndex]->name = trackName;
            // We need to inform the UI that the pins have changed.
            updateHostDisplay();
        }
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
    const double beats = seconds * (currentBpm / 60.0);
    return beats * ticksPerQuarterNote;
}

// This is the main export function.
void MidiLoggerModuleProcessor::exportToMidiFile()
{
    // Use a native file chooser to ask the user where to save the file.
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save MIDI File",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid"
    );

    auto chooserFlags = juce::FileBrowserComponent::saveMode
                      | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        juce::File file = fc.getResult();
        if (file == juce::File{}) return; // User cancelled

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
        const double microsecondsPerQuarterNote = 60'000'000.0 / currentBpm;
        tempoTrack.addEvent(
            juce::MidiMessage::tempoMetaEvent(static_cast<int>(microsecondsPerQuarterNote)), 
            0.0
        );
        
        // 4. Add end-of-track marker
        tempoTrack.addEvent(juce::MidiMessage::endOfTrack(), 0.0);
        
        // Add tempo track FIRST (Track 0 in Type 1 MIDI files)
        midiFile.addTrack(tempoTrack);
        // === END TEMPO TRACK ===

        // Iterate through each of our internal note tracks (become Track 1, 2, 3...).
        for (size_t trackIdx = 0; trackIdx < tracks.size(); ++trackIdx)
        {
            if (!tracks[trackIdx]) continue;

            auto events = tracks[trackIdx]->getEventsCopy();
            if (events.empty()) continue;

            juce::MidiMessageSequence sequence;
            
            // Add track name meta event for better organization
            sequence.addEvent(
                juce::MidiMessage::textMetaEvent(3, tracks[trackIdx]->name.toStdString()), 
                0.0
            );

            // Convert each MidiEvent into a pair of juce::MidiMessages.
            for (const auto& ev : events)
            {
                sequence.addEvent(juce::MidiMessage::noteOn(1, ev.pitch, (juce::uint8)(ev.velocity * 127.0f)),
                                  samplesToMidiTicks(ev.startTimeInSamples));
                
                sequence.addEvent(juce::MidiMessage::noteOff(1, ev.pitch),
                                  samplesToMidiTicks(ev.startTimeInSamples + ev.durationInSamples));
            }
            
            // Add end-of-track marker for this track
            double lastTick = events.empty() ? 0.0 : 
                samplesToMidiTicks(events.back().startTimeInSamples + events.back().durationInSamples);
            sequence.addEvent(juce::MidiMessage::endOfTrack(), lastTick + 100.0);
            
            sequence.updateMatchedPairs(); // Clean up the sequence
            midiFile.addTrack(sequence);
        }
        
        // Write the completed MIDI file to disk.
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
        {
            midiFile.writeTo(stream);
            juce::Logger::writeToLog("[MIDI Logger] Exported " + juce::String(midiFile.getNumTracks()) + 
                                    " tracks at " + juce::String(currentBpm, 1) + " BPM to: " + 
                                    file.getFullPathName());
        }
        else
        {
            juce::Logger::writeToLog("[MIDI Logger] ERROR: Failed to open file for writing");
        }
    });
}

