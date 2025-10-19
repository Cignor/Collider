#include "MIDIPlayerModuleProcessor.h"

MIDIPlayerModuleProcessor::MIDIPlayerModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Speed Mod", juce::AudioChannelSet::mono(), true)
        .withInput("Pitch Mod", juce::AudioChannelSet::mono(), true)
        .withInput("Velocity Mod", juce::AudioChannelSet::mono(), true)
        .withInput("Reset Mod", juce::AudioChannelSet::mono(), true)
        .withInput("Loop Mod", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::discreteChannels(kTotalOutputs), true))
    , apvts(*this, nullptr, "MIDIPlayerParameters", createParameterLayout())
{
    // Initialize parameter pointers
    speedParam = apvts.getRawParameterValue(SPEED_PARAM);
    pitchParam = apvts.getRawParameterValue(PITCH_PARAM);
    tempoParam = apvts.getRawParameterValue(TEMPO_PARAM);
    trackParam = apvts.getRawParameterValue(TRACK_PARAM);
    loopParam = apvts.getRawParameterValue(LOOP_PARAM);
    speedModParam = apvts.getRawParameterValue(SPEED_MOD_PARAM);
    pitchModParam = apvts.getRawParameterValue(PITCH_MOD_PARAM);
    velocityModParam = apvts.getRawParameterValue(VELOCITY_MOD_PARAM);
    
    // Initialize output values
    lastOutputValues.resize(kTotalOutputs);
    for (auto& value : lastOutputValues)
        value = std::make_unique<std::atomic<float>>(0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout MIDIPlayerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    
    // Playback Controls
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        SPEED_PARAM, "Speed", 0.25f, 4.0f, 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        PITCH_PARAM, "Pitch", -24.0f, 24.0f, 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        TEMPO_PARAM, "Tempo", 60.0f, 200.0f, 120.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterInt>(
        TRACK_PARAM, "Track", 0, 31, 0));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        LOOP_PARAM, "Loop", true));
    
    // Modulation Inputs
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        SPEED_MOD_PARAM, "Speed Mod", 0.0f, 1.0f, 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        PITCH_MOD_PARAM, "Pitch Mod", 0.0f, 1.0f, 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        VELOCITY_MOD_PARAM, "Velocity Mod", 0.0f, 1.0f, 0.5f));
    
    return { parameters.begin(), parameters.end() };
}

void MIDIPlayerModuleProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::Logger::writeToLog("[MIDI Player] prepareToPlay sr=" + juce::String(sampleRate) + ", block=" + juce::String(maximumExpectedSamplesPerBlock));
}

void MIDIPlayerModuleProcessor::releaseResources()
{
    // Nothing to release for MIDI Player
}

void MIDIPlayerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const juce::ScopedLock lock (midiDataLock);
    // Get a dedicated view of the multi-channel output bus
    auto outBus = getBusBuffer(buffer, false, 0);
    outBus.clear(); // Start with a clean slate

    if (!hasMIDIFileLoaded()) {
        return; // Exit if no MIDI file is loaded
    }
    
    const int numSamples = outBus.getNumSamples();
    const double sampleRate = getSampleRate();
    const double deltaTime = numSamples / sampleRate;

    // --- 1. Update Playback Time ---
    float speed = speedParam->load();
    if (isParamInputConnected("speed"))
        speed *= juce::jmap(getBusBuffer(buffer, true, 0).getReadPointer(0)[0], 0.0f, 1.0f, 0.25f, 4.0f);
    
    // Store live speed value for UI
    setLiveParamValue("speed_live", speed);
    
    if (double seek = pendingSeekTime.load(); seek >= 0.0) {
        currentPlaybackTime = juce::jlimit(0.0, totalDuration, seek);
        pendingSeekTime.store(-1.0);
    }
    currentPlaybackTime += deltaTime * speed;

    // Handle Reset and Loop modulation
    bool shouldReset = false;
    bool shouldLoop = loopParam->load() > 0.5f;
    
    if (isParamInputConnected("reset")) {
        const auto& resetModBus = getBusBuffer(buffer, true, 3);
        if (resetModBus.getNumChannels() > 0) {
            float resetCV = resetModBus.getReadPointer(0)[0];
            if (resetCV > 0.5f && lastResetCV <= 0.5f) { // Rising edge
                shouldReset = true;
            }
            lastResetCV = resetCV;
        }
    }
    
    if (isParamInputConnected("loop")) {
        const auto& loopModBus = getBusBuffer(buffer, true, 4);
        if (loopModBus.getNumChannels() > 0) {
            float loopCV = loopModBus.getReadPointer(0)[0];
            shouldLoop = loopCV > 0.5f;
        }
    }
    
    // Apply reset
    if (shouldReset) {
        currentPlaybackTime = 0.0;
    }
    
    // Apply loop behavior
    if (currentPlaybackTime >= totalDuration && shouldLoop) {
        currentPlaybackTime = std::fmod(currentPlaybackTime, totalDuration);
    }

    // --- RESET SEARCH HINTS on loop or seek ---
    if (currentPlaybackTime < previousPlaybackTime)
    {
        std::fill(lastNoteIndexHint.begin(), lastNoteIndexHint.end(), 0);
    }
    previousPlaybackTime = currentPlaybackTime;

    // --- 2. Generate Outputs for Each Active MIDI Track ---
    const int tracksToProcess = std::min((int)activeTrackIndices.size(), kMaxTracks);
    if (tracksToProcess == 0)
    {
        // Debug in Collider: no active tracks, nothing to output
        static int ctr = 0; if ((ctr++ & 0x3F) == 0)
            juce::Logger::writeToLog("[MIDI Player] No active tracks; check preset load and activeTrackIndices");
    }
    for (int i = 0; i < tracksToProcess; ++i)
    {
        const int sourceTrackIndex = activeTrackIndices[i];
        
        // --- REPLACED with EFFICIENT SEARCH ---
        const NoteData* activeNote = nullptr;
        if (sourceTrackIndex < (int)notesByTrack.size())
        {
            auto& trackNotes = notesByTrack[sourceTrackIndex];
            int& searchIndex = lastNoteIndexHint[sourceTrackIndex];

            // Fast-forward past notes that have already ended
            while (searchIndex < (int)trackNotes.size() && trackNotes[searchIndex].endTime < currentPlaybackTime)
            {
                searchIndex++;
            }

            // Now, search from the hint to find the active note (last-note priority)
            double latestStart = -1.0;
            for (int j = searchIndex; j < (int)trackNotes.size(); ++j)
            {
                const auto& note = trackNotes[j];
                if (note.startTime > currentPlaybackTime) break; // Notes are sorted, so we can stop early

                if (currentPlaybackTime >= note.startTime && currentPlaybackTime <= note.endTime) {
                    if (note.startTime > latestStart) {
                        latestStart = note.startTime;
                        activeNote = &note;
                    }
                }
            }
        }
        // --- END OF EFFICIENT SEARCH ---

        // Calculate the four CV values for this track
        float pitchOut = 0.0f, gateOut = 0.0f, velOut = 0.0f, trigOut = 0.0f;
        if (activeNote) {
            pitchOut = (float)noteNumberToCV(activeNote->noteNumber);
            gateOut = 1.0f;
            velOut = activeNote->velocity / 127.0f;
            // Generate a 10ms trigger at the start of the note
            if (std::abs(currentPlaybackTime - activeNote->startTime) < 0.01) {
                trigOut = 1.0f;
            }
        }
        
        // Apply global pitch modulation
        float pitchOffset = pitchParam->load();
        if (isParamInputConnected("pitch"))
            pitchOffset += juce::jmap(getBusBuffer(buffer, true, 1).getReadPointer(0)[0], 0.0f, 1.0f, -24.0f, 24.0f);
        pitchOut = juce::jlimit(0.0f, 1.0f, pitchOut + (pitchOffset / 60.0f));
        
        // Store live pitch value for UI
        setLiveParamValue("pitch_live", pitchOffset);
    
    // Store live loop value for UI
    setLiveParamValue("loop_live", shouldLoop ? 1.0f : 0.0f);

    // --- 3. Ensure tooltip storage capacity ---
    const int requiredChannels = outBus.getNumChannels();
    if ((int) lastOutputValues.size() < requiredChannels)
    {
        const size_t oldSize = lastOutputValues.size();
        lastOutputValues.resize((size_t) requiredChannels);
        for (size_t i = oldSize; i < lastOutputValues.size(); ++i)
            lastOutputValues[i] = std::make_unique<std::atomic<float>>(0.0f);
    }

    // --- 4. Write to the Correct Output Channels ---
        const int pitchChan = i * kOutputsPerTrack + 0;
        const int gateChan  = i * kOutputsPerTrack + 1;
        const int velChan   = i * kOutputsPerTrack + 2;
        const int trigChan  = i * kOutputsPerTrack + 3;

        // This is the reliable way to write: check if channel exists, get pointer, then fill.
        if (pitchChan < outBus.getNumChannels())
    {
        juce::FloatVectorOperations::fill(outBus.getWritePointer(pitchChan), pitchOut, numSamples);
        if (pitchChan < (int) lastOutputValues.size() && lastOutputValues[(size_t) pitchChan])
            lastOutputValues[(size_t) pitchChan]->store(pitchOut);
    }
        if (gateChan < outBus.getNumChannels())
    {
        juce::FloatVectorOperations::fill(outBus.getWritePointer(gateChan), gateOut, numSamples);
        if (gateChan < (int) lastOutputValues.size() && lastOutputValues[(size_t) gateChan])
            lastOutputValues[(size_t) gateChan]->store(gateOut);
    }
        if (velChan < outBus.getNumChannels())
    {
        juce::FloatVectorOperations::fill(outBus.getWritePointer(velChan), velOut, numSamples);
        if (velChan < (int) lastOutputValues.size() && lastOutputValues[(size_t) velChan])
            lastOutputValues[(size_t) velChan]->store(velOut);
    }
        if (trigChan < outBus.getNumChannels())
    {
        juce::FloatVectorOperations::fill(outBus.getWritePointer(trigChan), trigOut, numSamples);
        if (trigChan < (int) lastOutputValues.size() && lastOutputValues[(size_t) trigChan])
            lastOutputValues[(size_t) trigChan]->store(trigOut);
    }
    }
    
    // --- 5. Write Global Outputs ---
    if (kClockChannelIndex < outBus.getNumChannels()) {
        float tempo = tempoParam->load();
        double beatTime = 60.0 / tempo;
        double clockPhase = std::fmod(currentPlaybackTime, beatTime) / beatTime;
        float clockValue = (clockPhase < 0.1f) ? 1.0f : 0.0f;
        juce::FloatVectorOperations::fill(outBus.getWritePointer(kClockChannelIndex), clockValue, numSamples);
        if (kClockChannelIndex < (int) lastOutputValues.size() && lastOutputValues[(size_t) kClockChannelIndex])
            lastOutputValues[(size_t) kClockChannelIndex]->store(clockValue);
    }

    // --- NEWLY ADDED BLOCK TO FIX THE "NUM TRACKS" OUTPUT ---
    if (kNumTracksChannelIndex < outBus.getNumChannels()) {
        // The value is the number of tracks with notes, normalized over the max possible tracks.
        const float numTracksValue = (float)activeTrackIndices.size() / (float)kMaxTracks;
        juce::FloatVectorOperations::fill(outBus.getWritePointer(kNumTracksChannelIndex), numTracksValue, numSamples);
        
        if (kNumTracksChannelIndex < (int)lastOutputValues.size() && lastOutputValues[(size_t)kNumTracksChannelIndex])
            lastOutputValues[(size_t)kNumTracksChannelIndex]->store(numTracksValue);
    }
    
    // --- RAW NUM TRACKS OUTPUT ---
    if (kRawNumTracksChannelIndex < outBus.getNumChannels()) {
        const float rawNumTracksValue = (float)activeTrackIndices.size();
        juce::FloatVectorOperations::fill(outBus.getWritePointer(kRawNumTracksChannelIndex), rawNumTracksValue, numSamples);
        
        if (kRawNumTracksChannelIndex < (int)lastOutputValues.size() && lastOutputValues[(size_t)kRawNumTracksChannelIndex])
            lastOutputValues[(size_t)kRawNumTracksChannelIndex]->store(rawNumTracksValue);
    }
    // --- END OF FIX ---
}

void MIDIPlayerModuleProcessor::updatePlaybackTime(double deltaTime)
{
    currentPlaybackTime += deltaTime;
    
    // Handle looping
    if (currentPlaybackTime >= totalDuration && isLooping)
    {
        currentPlaybackTime = std::fmod(currentPlaybackTime, totalDuration);
    }
    else if (currentPlaybackTime >= totalDuration)
    {
        currentPlaybackTime = totalDuration;
    }
}

void MIDIPlayerModuleProcessor::generateCVOutputs()
{
    // Reset outputs
    // Keep last pitchCV to avoid dropping to zero between notes
    gateLevel = 0.0f;
    velocityLevel = 0.0f;
    triggerPulse = false;
    
    // Debug logging (only every 1000 samples to avoid spam)
    static int debugCounter = 0;
    if (++debugCounter % 1000 == 0)
    {
        juce::Logger::writeToLog("[MIDI Player] Debug - Time: " + juce::String(currentPlaybackTime, 3) + 
                                "s, Track: " + juce::String(currentTrackIndex) + 
                                ", Total notes: " + juce::String(getTotalNoteCount()));
    }
    
    // Find active note(s) at current time and apply mono priority: last note on
    int activeNotes = 0;
    const NoteData* chosenNote = nullptr;
    double latestStart = -1.0;
    
    // Use efficient search for the current track
    if (currentTrackIndex < (int)notesByTrack.size())
    {
        const auto& trackNotes = notesByTrack[currentTrackIndex];
        for (const auto& note : trackNotes)
        {
            if (currentPlaybackTime >= note.startTime && currentPlaybackTime <= note.endTime)
            {
                activeNotes++;
                // Prefer the most recent onset; tie-breaker by higher velocity
                if (note.startTime > latestStart || (std::abs(note.startTime - latestStart) < 1e-6 && chosenNote != nullptr && note.velocity > chosenNote->velocity))
                {
                    latestStart = note.startTime;
                    chosenNote = &note;
                }
            }
        }
    }
    
    if (chosenNote != nullptr)
    {
        pitchCV = (float) noteNumberToCV(chosenNote->noteNumber);
        gateLevel = 1.0f;
        velocityLevel = chosenNote->velocity / 127.0f;
        
        if (debugCounter % 1000 == 0)
        {
            juce::Logger::writeToLog("[MIDI Player] Active note - MIDI Note: " + juce::String(chosenNote->noteNumber) +
                                    ", Velocity: " + juce::String(chosenNote->velocity) +
                                    ", CV: " + juce::String(pitchCV, 3));
        }
        
        if (std::abs(currentPlaybackTime - chosenNote->startTime) < 0.01)
        {
            triggerPulse = true;
        }
    }
    
    // Fallback: if selected track has zero notes at all, try track 0
    if (activeNotes == 0 && currentTrackIndex != 0 && currentTrackIndex < (int) trackInfos.size() && trackInfos[(size_t) currentTrackIndex].noteCount == 0)
    {
        if (0 < (int)notesByTrack.size())
        {
            const auto& track0Notes = notesByTrack[0];
            for (const auto& note : track0Notes)
            {
                if (currentPlaybackTime >= note.startTime && currentPlaybackTime <= note.endTime)
                {
                    // Note is active
                    pitchCV = (float) noteNumberToCV(note.noteNumber);
                    gateLevel = 1.0f;
                    velocityLevel = note.velocity / 127.0f;
                    
                    if (debugCounter % 1000 == 0)
                    {
                        juce::Logger::writeToLog("[MIDI Player] FALLBACK to track 0 - MIDI Note: " + juce::String(note.noteNumber));
                    }
                    break;
                }
            }
        }
    }
    
    // Debug track information
    if (debugCounter % 1000 == 0)
    {
        juce::Logger::writeToLog("[MIDI Player] Current track: " + juce::String(currentTrackIndex) + 
                                ", Total tracks: " + juce::String(getNumTracks()));
        
        for (int t = 0; t < getNumTracks(); ++t)
        {
            int notesInTrack = 0;
            if (t < (int)notesByTrack.size())
            {
                notesInTrack = (int)notesByTrack[t].size();
            }
            if (notesInTrack > 0)
            {
                juce::String trackName = (t < trackInfos.size()) ? trackInfos[t].name : "Track " + juce::String(t);
                juce::Logger::writeToLog("[MIDI Player] " + trackName + ": " + juce::String(notesInTrack) + " notes");
            }
        }
        
        if (activeNotes == 0 && currentTrackIndex < trackInfos.size())
        {
            const auto& info = trackInfos[currentTrackIndex];
            juce::Logger::writeToLog("[MIDI Player] WARNING: No active notes in " + info.name + 
                                    " (has " + juce::String(info.noteCount) + " total notes)");
        }
    }
    
    // Generate clock output (quarter note pulses)
    float tempo = tempoParam->load();
    double beatTime = 60.0 / tempo;
    double clockPhase = std::fmod(currentPlaybackTime, beatTime) / beatTime;
    clockOutput = (clockPhase < 0.1f) ? 1.0f : 0.0f; // 10% duty cycle
}

double MIDIPlayerModuleProcessor::noteNumberToCV(int noteNumber) const
{
    // Map MIDI note range C2..C7 (36..96) to 0..1 linearly
    // Notes below C2 clamp to 0, above C7 clamp to 1
    const double minNote = 36.0; // C2
    const double maxNote = 96.0; // C7
    if (noteNumber <= minNote) return 0.0;
    if (noteNumber >= maxNote) return 1.0;
    return (noteNumber - minNote) / (maxNote - minNote);
}

int MIDIPlayerModuleProcessor::getTotalNoteCount() const
{
    int totalNotes = 0;
    for (const auto& trackNotes : notesByTrack) {
        totalNotes += (int)trackNotes.size();
    }
    return totalNotes;
}

void MIDIPlayerModuleProcessor::parseMIDIFile()
{
    if (!midiFile)
        return;
    
    // Build new state off-thread, then swap under lock
    std::vector<std::vector<NoteData>> newNotesByTrack;
    std::vector<TrackInfo> newTrackInfos;
    std::vector<int> newActiveTrackIndices;
    double newTotalDuration = 0.0;

    newNotesByTrack.clear();
    newNotesByTrack.resize (midiFile->getNumTracks());
    
    for (int track = 0; track < midiFile->getNumTracks(); ++track)
    {
        const auto* sequence = midiFile->getTrack (track);
        if (! sequence) continue;
        
        for (int event = 0; event < sequence->getNumEvents(); ++event)
        {
            const auto& message = sequence->getEventPointer(event)->message;
            if (message.isNoteOn() && message.getVelocity() > 0)
            {
                double startTime = message.getTimeStamp();
                double endTime = startTime + 1.0;
                for (int searchEvent = event + 1; searchEvent < sequence->getNumEvents(); ++searchEvent)
                {
                    const auto& searchMessage = sequence->getEventPointer(searchEvent)->message;
                    if ((searchMessage.isNoteOff() || (searchMessage.isNoteOn() && searchMessage.getVelocity() == 0)) &&
                        searchMessage.getNoteNumber() == message.getNoteNumber())
                    {
                        endTime = searchMessage.getTimeStamp();
                        break;
                    }
                }
                if (endTime - startTime < 0.05)
                    endTime = startTime + 0.05;

                NoteData note;
                const double ticksPerQuarter = midiFile->getTimeFormat();
                const double tempo = tempoParam ? (double) tempoParam->load() : 120.0;
                const double secondsPerTick = (60.0 / tempo) / ticksPerQuarter;
                note.startTime = startTime * secondsPerTick;
                note.endTime   = endTime   * secondsPerTick;
                note.noteNumber = message.getNoteNumber();
                note.velocity   = message.getVelocity();
                note.trackIndex = track;
                newNotesByTrack[track].push_back (note);
                newTotalDuration = std::max (newTotalDuration, note.endTime);
            }
        }
    }

    newTrackInfos.clear();
    newTrackInfos.resize (midiFile->getNumTracks());
    newActiveTrackIndices.clear();
    int newNumActiveTracks = 0;
    
    for (int track = 0; track < midiFile->getNumTracks(); ++track)
    {
        TrackInfo info;
        info.name = "Track " + juce::String (track + 1);
        info.noteCount = (int) newNotesByTrack[track].size();
        info.hasNotes = (info.noteCount > 0);
        if (const auto* sequence = midiFile->getTrack (track))
        {
            for (int i = 0; i < sequence->getNumEvents(); ++i)
            {
                const auto& msg = sequence->getEventPointer(i)->message;
                if (msg.isTrackNameEvent()) { info.name = msg.getTextFromTextMetaEvent(); break; }
            }
        }
        newTrackInfos[track] = info;
        if (info.hasNotes)
        {
            ++newNumActiveTracks;
            if ((int) newActiveTrackIndices.size() < kMaxTracks)
                newActiveTrackIndices.push_back (track);
        }
    }

    {
        const juce::ScopedLock lock (midiDataLock);
        notesByTrack.swap (newNotesByTrack);
        trackInfos.swap (newTrackInfos);
        activeTrackIndices.swap (newActiveTrackIndices);
        totalDuration = newTotalDuration;
        lastNoteIndexHint.assign (midiFile->getNumTracks(), 0);
        previousPlaybackTime = -1.0;
        numActiveTracks = newNumActiveTracks;
    }

    int totalNotes = 0;
    for (const auto& tn : notesByTrack) totalNotes += (int) tn.size();
    juce::Logger::writeToLog ("[MIDI Player] Parsed " + juce::String (totalNotes) +
                              " notes from " + juce::String (midiFile->getNumTracks()) + " tracks");
    juce::Logger::writeToLog ("[MIDI Player] Time format: " + juce::String (midiFile->getTimeFormat()) +
                              " ticks/quarter, Total duration: " + juce::String (totalDuration, 3) + "s");
}

void MIDIPlayerModuleProcessor::loadMIDIFile(const juce::File& file)
{
    auto newMidiFile = std::make_unique<juce::MidiFile>();
    
    juce::FileInputStream inputStream(file);
    if (inputStream.openedOk() && newMidiFile->readFrom(inputStream))
    {
        {
            const juce::ScopedLock lock (midiDataLock);
            midiFile = std::move(newMidiFile);
            currentMIDIFileName = file.getFileName();
            currentMIDIFilePath = file.getFullPathName();
            currentPlaybackTime = 0.0;
            previousPlaybackTime = -1.0;
            lastNoteIndexHint.clear();
        }
        parseMIDIFile();
        
        juce::Logger::writeToLog("[MIDI Player] Loaded MIDI file: " + currentMIDIFileName);
    }
    else
    {
        juce::Logger::writeToLog("[MIDI Player] Failed to load MIDI file: " + file.getFullPathName());
    }
}

#if defined(PRESET_CREATOR_UI)
void MIDIPlayerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // File Info
    if (hasMIDIFileLoaded())
    {
        ImGui::Text("MIDI: %s", currentMIDIFileName.toRawUTF8());
        ImGui::Text("Tracks: %d, Notes: %d", getNumTracks(), getTotalNoteCount());
    }
    else
    {
        ImGui::Text("No MIDI file loaded");
    }
    
    // Load MIDI File Button
    if (ImGui::Button("Load MIDI File", ImVec2(itemWidth, 0)))
    {
        juce::File startDir;
        {
            // Look for the project root (where juce folder is)
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            auto dir = appFile.getParentDirectory();
            for (int i = 0; i < 10 && dir.exists(); ++i)
            {
                // Look for the juce folder, then go up one level to find audio/MIDI
                if (dir.getChildFile("juce").isDirectory())
                {
                    auto candidate = dir.getChildFile("audio").getChildFile("MIDI");
                    if (candidate.exists() && candidate.isDirectory())
                    {
                        startDir = candidate;
                        break;
                    }
                }
                dir = dir.getParentDirectory();
            }
        }
        if (!startDir.exists()) startDir = juce::File();
        
        fileChooser = std::make_unique<juce::FileChooser>("Select MIDI File", startDir, "*.mid;*.midi");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            try {
                auto file = fc.getResult();
                if (file != juce::File{})
                {
                    juce::Logger::writeToLog("[MIDI Player] User selected file: " + file.getFullPathName());
                    loadMIDIFile(file);
                }
            } catch (...) {
                juce::Logger::writeToLog("[MIDI Player][FATAL] Exception during file chooser callback");
            }
        });
    }
    
    // Connect All to Samplers Button
    if (ImGui::Button("Connect All to Samplers", ImVec2(itemWidth, 0)))
    {
        autoConnectTriggered = true; // Set the flag for the UI controller to see
    }
    
    // Connect All to PolyVCO Button
    if (ImGui::Button("Connect All to PolyVCO", ImVec2(itemWidth, 0)))
    {
        autoConnectVCOTriggered = true; // Set the flag for the UI controller to see
    }
    
    // Connect to PolyVCO and Samples Button
    if (ImGui::Button("Connect to PolyVCO and Samples", ImVec2(itemWidth, 0)))
    {
        autoConnectHybridTriggered = true;
    }
    
    ImGui::Spacing();
    
    // Playback Controls
    bool speedModulated = isParamModulated("speed");
    float speed = speedParam->load();
    if (speedModulated) {
        speed = getLiveParamValueFor("speed", "speed_live", speed);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Speed", &speed, 0.25f, 4.0f, "%.2fx"))
    {
        if (!speedModulated) {
            apvts.getParameter(SPEED_PARAM)->setValueNotifyingHost(apvts.getParameterRange(SPEED_PARAM).convertTo0to1(speed));
            onModificationEnded();
        }
    }
    if (speedModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    bool pitchModulated = isParamModulated("pitch");
    float pitch = pitchParam->load();
    if (pitchModulated) {
        pitch = getLiveParamValueFor("pitch", "pitch_live", pitch);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Pitch", &pitch, -24.0f, 24.0f, "%.1f st"))
    {
        if (!pitchModulated) {
            apvts.getParameter(PITCH_PARAM)->setValueNotifyingHost(apvts.getParameterRange(PITCH_PARAM).convertTo0to1(pitch));
            onModificationEnded();
        }
    }
    if (pitchModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    float tempo = tempoParam->load();
    if (ImGui::SliderFloat("Tempo", &tempo, 60.0f, 200.0f, "%.0f BPM"))
    {
        apvts.getParameter(TEMPO_PARAM)->setValueNotifyingHost(apvts.getParameterRange(TEMPO_PARAM).convertTo0to1(tempo));
        onModificationEnded();
    }
    
    // Track Selection Dropdown
    int track = (int)trackParam->load();
    int maxTrack = std::max(0, getNumTracks() - 1);
    
    // Clamp track to valid range and normalize parameter if out of range
    if (track > maxTrack) {
        track = 0;
        auto normZero = apvts.getParameterRange(TRACK_PARAM).convertTo0to1(0.0f);
        apvts.getParameter(TRACK_PARAM)->setValueNotifyingHost(normZero);
        currentTrackIndex = 0;
    }
    
    if (getNumTracks() > 0)
    {
        // Keep preview label alive during BeginCombo call
        juce::String previewLabel;
        if (track >= 0 && track < (int) trackInfos.size())
        {
            const auto& info = trackInfos[(size_t) track];
            previewLabel = info.name + " (" + juce::String(info.noteCount) + " notes)";
        }
        const char* previewText = previewLabel.isNotEmpty() ? previewLabel.toRawUTF8() : "No Track";
        
        if (ImGui::BeginCombo("Track", previewText))
        {
            for (int i = 0; i < getNumTracks(); ++i)
            {
                if (i < (int) trackInfos.size())
                {
                    const auto& info = trackInfos[(size_t) i];
                    juce::String label = info.name + " (" + juce::String(info.noteCount) + " notes)";
                    bool isSelected = (track == i);
                    
                    if (ImGui::Selectable(label.toRawUTF8(), isSelected))
                    {
                        track = i;
                        float norm = apvts.getParameterRange(TRACK_PARAM).convertTo0to1((float) track);
                        apvts.getParameter(TRACK_PARAM)->setValueNotifyingHost(norm);
                        currentTrackIndex = track;
                        onModificationEnded();
                        juce::Logger::writeToLog("[MIDI Player] Selected track " + juce::String(track) + ": " + info.name + " (" + juce::String(info.noteCount) + " notes)");
                    }
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    
    bool loopModulated = isParamModulated("loop");
    bool loop = loopParam->load() > 0.5f;
    if (loopModulated) {
        loop = getLiveParamValueFor("loop", "loop_live", loop ? 1.0f : 0.0f) > 0.5f;
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Loop", &loop))
    {
        if (!loopModulated) {
            apvts.getParameter(LOOP_PARAM)->setValueNotifyingHost(loop ? 1.0f : 0.0f);
            isLooping = loop;
            onModificationEnded();
        }
    }
    if (loopModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    ImGui::Spacing();
    // Timeline / Playhead
    if (totalDuration > 0.0)
    {
        float t = (float) currentPlaybackTime;
        if (ImGui::SliderFloat("Time", &t, 0.0f, (float) totalDuration, "%.2fs"))
        {
            pendingSeekTime.store((double) t);
            juce::Logger::writeToLog("[MIDI Player] Seek requested: " + juce::String(t, 2) + "s");
            onModificationEnded();
        }
    }
    if (ImGui::Button("Reset Playback"))
    {
        pendingSeekTime.store(0.0);
        juce::Logger::writeToLog("[MIDI Player] Playback reset to start");
    }
    
    ImGui::PopItemWidth();
}
#endif

void MIDIPlayerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // --- Global Inputs & Outputs (In Parallel) ---
    helpers.drawParallelPins("Speed Mod", 0, "Clock", kClockChannelIndex);
    helpers.drawParallelPins("Pitch Mod", 1, "Num Tracks", kNumTracksChannelIndex);
    helpers.drawParallelPins("Velocity Mod", 2, "Raw Num Tracks", kRawNumTracksChannelIndex);
    helpers.drawParallelPins("Reset Mod", 3, nullptr, 0);
    helpers.drawParallelPins("Loop Mod", 4, nullptr, 0);
    
    // --- Per-Track Outputs (Inputs side will be blank) ---
    int outIndex = 0;
    const int tracksToShow = std::min((int) activeTrackIndices.size(), kMaxTracks);

    for (int t = 0; t < tracksToShow; ++t)
    {
        const int srcTrack = activeTrackIndices[(size_t) t];
        juce::String base = (srcTrack < (int) trackInfos.size() && trackInfos[(size_t) srcTrack].name.isNotEmpty())
            ? trackInfos[(size_t) srcTrack].name : (juce::String("Track ") + juce::String(srcTrack+1));
        
        // Draw each track output on its own line, but on the right side of the node
        helpers.drawParallelPins(nullptr, 0, (base + " Pitch").toRawUTF8(),    outIndex++);
        helpers.drawParallelPins(nullptr, 0, (base + " Gate").toRawUTF8(),     outIndex++);
        helpers.drawParallelPins(nullptr, 0, (base + " Velocity").toRawUTF8(), outIndex++);
        helpers.drawParallelPins(nullptr, 0, (base + " Trigger").toRawUTF8(),  outIndex++);
    }
}

juce::String MIDIPlayerModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Speed Mod";
        case 1: return "Pitch Mod";
        case 2: return "Velocity Mod";
        case 3: return "Reset Mod";
        case 4: return "Loop Mod";
        default: return juce::String("In ") + juce::String(channel + 1);
    }
}

juce::String MIDIPlayerModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == kClockChannelIndex) return "Clock";
    if (channel == kNumTracksChannelIndex) return "Num Tracks";
    if (channel == kRawNumTracksChannelIndex) return "Raw Num Tracks";
    
    // Per-track outputs (Pitch/Gate/Velocity/Trigger)
    const int trackIndex = channel / kOutputsPerTrack;
    const int outputType = channel % kOutputsPerTrack;
    
    if (trackIndex < (int)activeTrackIndices.size())
    {
        const int srcTrack = activeTrackIndices[(size_t)trackIndex];
        juce::String base = (srcTrack < (int)trackInfos.size() && trackInfos[(size_t)srcTrack].name.isNotEmpty())
            ? trackInfos[(size_t)srcTrack].name : (juce::String("Track ") + juce::String(srcTrack+1));
        
        switch (outputType)
        {
            case 0: return base + " Pitch";
            case 1: return base + " Gate";
            case 2: return base + " Velocity";
            case 3: return base + " Trigger";
        }
    }
    
    return "Out " + juce::String(channel + 1);
}

// Parameter bus contract implementation
bool MIDIPlayerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "speed") { outBusIndex = 0; outChannelIndexInBus = 0; return true; }
    if (paramId == "pitch") { outBusIndex = 1; outChannelIndexInBus = 0; return true; }
    if (paramId == "velocity") { outBusIndex = 2; outChannelIndexInBus = 0; return true; }
    if (paramId == "reset") { outBusIndex = 3; outChannelIndexInBus = 0; return true; }
    if (paramId == "loop") { outBusIndex = 4; outChannelIndexInBus = 0; return true; }
    return false;
}
