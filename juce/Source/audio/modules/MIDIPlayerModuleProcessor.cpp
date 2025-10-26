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
    
    // TEMPO HANDLING: Get pointers to new tempo control parameters
    syncToHostParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("syncToHost"));
    tempoMultiplierParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("tempoMultiplier"));
    
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
        TRACK_PARAM, "Track", -1, 31, 0)); // -1 = "Show All Tracks"
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        LOOP_PARAM, "Loop", true));
    
    // TEMPO HANDLING: Smart tempo control parameters
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        "syncToHost", "Sync to Host", false)); // Default: use file tempo
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tempoMultiplier", "Tempo Multiplier", 0.25f, 4.0f, 1.0f)); // 0.25x to 4x speed
    
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

    // HIERARCHICAL TEMPO CALCULATION (Option A + Option B)
    // Priority: 1. Sync to Host (if enabled) → 2. File Tempo → 3. User Multiplier
    double activeBpm = fileBpm; // Start with file's tempo (Option A)
    
    if (syncToHostParam && syncToHostParam->get())
    {
        // Sync to Host enabled: use transport BPM from tempo clock (highest priority)
        activeBpm = m_currentTransport.bpm;
    }
    
    // Apply user's tempo multiplier (Option B: 0.25x to 4x)
    const float tempoMult = tempoMultiplierParam ? tempoMultiplierParam->get() : 1.0f;
    const double finalBpm = activeBpm * tempoMult;
    
    // Store for UI display and other calculations
    if (tempoParam)
        tempoParam->store((float)finalBpm);

    // --- 1. Update Playback Time (THE CRITICAL FIX: HOST SYNC) ---
    bool isSynced = syncToHostParam && syncToHostParam->get();
    
    // Calculate speed based on sync mode
    const double tempoSpeedRatio = (fileBpm > 0.0) ? (finalBpm / fileBpm) : 1.0;
    const double effectiveSpeed = tempoSpeedRatio;
    
    // Store live speed value for UI
    setLiveParamValue("speed_live", (float)effectiveSpeed);
    
    // Handle user seeking (clicking on the timeline)
    if (double seek = pendingSeekTime.load(); seek >= 0.0) {
        currentPlaybackTime = juce::jlimit(0.0, totalDuration, seek);
        pendingSeekTime.store(-1.0);
    }
    
    // Advance playback time (using transport BPM if synced, file BPM otherwise)
    // Note: activeBpm already contains the correct BPM based on sync mode (line 95-99)
    currentPlaybackTime += deltaTime * effectiveSpeed;

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

        // Calculate the four CV values for this track (Gate, Pitch, Velocity, Trigger)
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
        // POLYPHONIC OUTPUTS: Use 4 channels per track (Gate, Pitch, Velo, Trigger)
        const int baseChannel = i * 4;
        const int gateChan  = baseChannel + 0;
        const int pitchChan = baseChannel + 1;
        const int velChan   = baseChannel + 2;
        const int trigChan  = baseChannel + 3;

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
        // CRITICAL FIX: Output TOTAL tracks (notesByTrack.size()), not just tracks with notes
        // This matches the number of dynamic output pins created and ensures correct allocation
        const float numTracksValue = (float)notesByTrack.size() / (float)kMaxTracks;
        juce::FloatVectorOperations::fill(outBus.getWritePointer(kNumTracksChannelIndex), numTracksValue, numSamples);
        
        if (kNumTracksChannelIndex < (int)lastOutputValues.size() && lastOutputValues[(size_t)kNumTracksChannelIndex])
            lastOutputValues[(size_t)kNumTracksChannelIndex]->store(numTracksValue);
    }
    
    // --- RAW NUM TRACKS OUTPUT ---
    if (kRawNumTracksChannelIndex < outBus.getNumChannels()) {
        // CRITICAL FIX: Output TOTAL tracks from MIDI file
        // This ensures PolyVCO/TrackMixer allocate the correct number of voices/channels
        const float rawNumTracksValue = (float)notesByTrack.size();
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
            
            // TEMPO PARSING: Extract BPM from MIDI file (Option A)
            // Most MIDI files store tempo in the first track as a meta event
            fileBpm = 120.0; // Default fallback
            if (midiFile && midiFile->getNumTracks() > 0)
            {
                const auto* firstTrack = midiFile->getTrack(0);
                for (int i = 0; i < firstTrack->getNumEvents(); ++i)
                {
                    const auto& event = firstTrack->getEventPointer(i)->message;
                    if (event.isTempoMetaEvent())
                    {
                        fileBpm = event.getTempoSecondsPerQuarterNote() > 0.0 
                            ? 60.0 / event.getTempoSecondsPerQuarterNote() 
                            : 120.0;
                        juce::Logger::writeToLog("[MIDI Player] Detected file tempo: " + 
                                                juce::String(fileBpm, 1) + " BPM");
                        break; // Use the first tempo event found
                    }
                }
            }
        }
        parseMIDIFile();
        
        juce::Logger::writeToLog("[MIDI Player] Loaded MIDI file: " + currentMIDIFileName);
        
        // --- ADD THIS LINE ---
        // If a new file is loaded, signal to the UI that connections may need to be updated.
        connectionUpdateRequested = true;
    }
    else
    {
        juce::Logger::writeToLog("[MIDI Player] Failed to load MIDI file: " + file.getFullPathName());
    }
}

#if defined(PRESET_CREATOR_UI)

// Helper function for tooltip with help marker
static void HelpMarkerPlayer(const char* desc)
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

// ==============================================================================
// PHASE 2: PIANO ROLL UI (Ported from MidiLogger)
// ==============================================================================
void MIDIPlayerModuleProcessor::drawParametersInNode(float /*itemWidth*/, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    // --- Invisible Scaffolding ---
    ImGui::Dummy(ImVec2(nodeWidth, 0.0f));
    
    // --- 1. TOOLBAR ---
    // Status indicator based on playback
    const char* statusText = speedParam->load() > 0.01f ? "▶ PLAY" : "■ Stopped";
    ImVec4 statusColor = speedParam->load() > 0.01f ? 
        ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    ImGui::Text("%s", statusText);
        ImGui::PopStyleColor();
    ImGui::SameLine();
    
    // Load .mid Button
    if (ImGui::Button("Load .mid"))
    {
        juce::File startDir;
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            auto dir = appFile.getParentDirectory();
            for (int i = 0; i < 10 && dir.exists(); ++i)
            {
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
        if (!startDir.exists()) startDir = juce::File();
        
        fileChooser = std::make_unique<juce::FileChooser>("Select MIDI File", startDir, "*.mid;*.midi");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this, onModificationEnded](const juce::FileChooser& fc)
        {
                auto file = fc.getResult();
                if (file != juce::File{})
                {
                juce::Logger::writeToLog("[MIDI Player] Loading file: " + file.getFullPathName());
                    loadMIDIFile(file);
                onModificationEnded();
            }
        });
    }
    ImGui::SameLine();
    
    // File name display
    ImGui::Text("File: %s", currentMIDIFileName.isEmpty() ? "No file loaded" : currentMIDIFileName.toRawUTF8());
    
    // === FILE INFORMATION DISPLAY ===
    if (hasMIDIFileLoaded())
    {
    ImGui::Spacing();
    
        // Count tracks with notes
        int tracksWithNotes = 0;
        for (const auto& track : notesByTrack)
        {
            if (!track.empty()) tracksWithNotes++;
        }
        
        // Get MIDI file format info
        int ppq = midiFile ? midiFile->getTimeFormat() : 0;
        int totalTracks = getNumTracks();
        
        // Calculate effective playback BPM
        const double currentBpm = tempoParam ? tempoParam->load() : fileBpm;
        const float tempoMult = tempoMultiplierParam ? tempoMultiplierParam->get() : 1.0f;
        const bool isSynced = syncToHostParam ? syncToHostParam->get() : false;
        
        // Display file info in a compact, organized way
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f)); // Light blue
        
        // Line 1: Original tempo and format info
        ImGui::Text("Original: %.1f BPM • PPQ: %d • Tracks: %d (%d with notes) • Duration: %.1fs",
                   fileBpm, ppq, totalTracks, tracksWithNotes, totalDuration);
        
        // Line 2: Current playback info
        const char* tempoSource = isSynced ? "Host" : "File";
        ImVec4 playbackColor = isSynced ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, playbackColor);
        
        ImGui::Text("Playback: %.1f BPM (%.2fx from %s) • Time: %.2fs / %.2fs",
                   currentBpm, tempoMult, tempoSource, currentPlaybackTime, totalDuration);
        
        ImGui::PopStyleColor();
        ImGui::Spacing();
        
        // === HOTSWAP DROP ZONE (Compact, always visible when file is loaded) ===
        ImVec2 hotswapSize = ImVec2(nodeWidth, 30.0f);
        bool isDragging = ImGui::GetDragDropPayload() != nullptr;
        
        if (isDragging)
        {
            // Highlight during drag
            float time = (float)ImGui::GetTime();
            float pulse = (std::sin(time * 8.0f) * 0.5f + 0.5f);
            ImU32 fillColor = IM_COL32(180, 100, 255, (int)(80 + pulse * 100));
            ImU32 borderColor = IM_COL32((int)(220 * pulse), 120, 255, 255);
            
            ImGui::PushStyleColor(ImGuiCol_Button, fillColor);
            ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::Button("##hotswap_zone", hotswapSize);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        else
        {
            // Subtle zone when idle
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 40, 45, 150));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(80, 80, 90, 180));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::Button("##hotswap_zone", hotswapSize);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        
        // Draw text overlay
        const char* hotswapText = isDragging ? "⟳ Drop to Hotswap MIDI" : "⟳ Drop MIDI to Hotswap";
        ImVec2 textSize = ImGui::CalcTextSize(hotswapText);
        ImVec2 textPos = ImGui::GetItemRectMin();
        textPos.x += (hotswapSize.x - textSize.x) * 0.5f;
        textPos.y += (hotswapSize.y - textSize.y) * 0.5f;
        ImU32 textColor = isDragging ? IM_COL32(220, 180, 255, 255) : IM_COL32(150, 150, 160, 200);
        ImGui::GetWindowDrawList()->AddText(textPos, textColor, hotswapText);
        
        // Handle drop
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MIDI_PATH"))
            {
                const char* path = (const char*)payload->Data;
                loadMIDIFile(juce::File(path));
            onModificationEnded();
        }
            ImGui::EndDragDropTarget();
    }
    ImGui::Spacing();
        // === END HOTSWAP ZONE ===
    }
    // === END FILE INFO ===
    
    // Track Selector
    if (hasMIDIFileLoaded() && getNumTracks() > 0)
    {
        ImGui::SameLine();
        ImGui::PushItemWidth(150);
    int track = (int)trackParam->load();
    int maxTrack = std::max(0, getNumTracks() - 1);
    
        // Clamp track to valid range
        if (track > maxTrack)
        {
        track = 0;
        auto normZero = apvts.getParameterRange(TRACK_PARAM).convertTo0to1(0.0f);
        apvts.getParameter(TRACK_PARAM)->setValueNotifyingHost(normZero);
        currentTrackIndex = 0;
    }
    
        juce::String previewLabel;
        if (track == -1)
        {
            previewLabel = "Show All Tracks";
        }
        else if (track >= 0 && track < (int)trackInfos.size())
        {
            const auto& info = trackInfos[(size_t)track];
            previewLabel = info.name + " (" + juce::String(info.noteCount) + " notes)";
        }
        const char* previewText = previewLabel.isNotEmpty() ? previewLabel.toRawUTF8() : "No Track";
        
        if (ImGui::BeginCombo("##track", previewText))
        {
            // "Show All" option (track index -1)
            bool showAllSelected = (track == -1);
            if (ImGui::Selectable("Show All Tracks", showAllSelected))
            {
                float norm = apvts.getParameterRange(TRACK_PARAM).convertTo0to1(-1.0f);
                apvts.getParameter(TRACK_PARAM)->setValueNotifyingHost(norm);
                currentTrackIndex = -1;
                onModificationEnded();
            }
            if (showAllSelected)
                ImGui::SetItemDefaultFocus();
            
            ImGui::Separator();
            
            // Individual tracks
            for (int i = 0; i < getNumTracks(); ++i)
            {
                if (i < (int)trackInfos.size())
                {
                    const auto& info = trackInfos[(size_t)i];
                    juce::String label = info.name + " (" + juce::String(info.noteCount) + " notes)";
                    bool isSelected = (track == i);
                    
                    if (ImGui::Selectable(label.toRawUTF8(), isSelected))
                    {
                        float norm = apvts.getParameterRange(TRACK_PARAM).convertTo0to1((float)i);
                        apvts.getParameter(TRACK_PARAM)->setValueNotifyingHost(norm);
                        currentTrackIndex = i;
                        onModificationEnded();
                    }
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
    }
    
    // === TEMPO CONTROL SECTION ===
    ImGui::Text("Tempo Control:");
    ImGui::SameLine();
    
    bool syncToHost = syncToHostParam ? syncToHostParam->get() : false;
    if (ImGui::Checkbox("Sync to Host", &syncToHost))
    {
        if (syncToHostParam)
        {
            float norm = syncToHost ? 1.0f : 0.0f;
            apvts.getParameter("syncToHost")->setValueNotifyingHost(norm);
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lock tempo to application BPM");
    
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    float tempoMult = tempoMultiplierParam ? tempoMultiplierParam->get() : 1.0f;
    if (ImGui::SliderFloat("##tempo", &tempoMult, 0.25f, 4.0f, "%.2fx"))
    {
        juce::Logger::writeToLog("[TEMPO SLIDER] Changed to: " + juce::String(tempoMult) + " | zoomX is: " + juce::String(zoomX));
        if (tempoMultiplierParam)
        {
            float norm = apvts.getParameterRange("tempoMultiplier").convertTo0to1(tempoMult);
            apvts.getParameter("tempoMultiplier")->setValueNotifyingHost(norm);
            juce::Logger::writeToLog("[TEMPO SLIDER] Wrote " + juce::String(tempoMult) + " to tempoMultiplier param (norm=" + juce::String(norm) + ")");
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered())
    {
        const double currentBpm = tempoParam ? tempoParam->load() : fileBpm;
        ImGui::SetTooltip("Tempo: %.1f BPM (%.2fx multiplier)\nBase: %.1f BPM from %s",
                         currentBpm, tempoMult, fileBpm,
                         syncToHost ? "Host" : "File");
    }
    ImGui::PopItemWidth();
    
    ImGui::Spacing(); // Visual separation between tempo and pitch
    
    // === PITCH TRANSPOSE SECTION ===
    ImGui::Text("Pitch Transpose:");
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    float pitchOffset = pitchParam ? pitchParam->load() : 0.0f;
    if (ImGui::SliderFloat("##pitchTranspose", &pitchOffset, -24.0f, 24.0f, "%+.0f semi"))
    {
        if (pitchParam)
        {
            float norm = apvts.getParameterRange(PITCH_PARAM).convertTo0to1(pitchOffset);
            apvts.getParameter(PITCH_PARAM)->setValueNotifyingHost(norm);
            onModificationEnded();
        }
    }
    if (ImGui::IsItemHovered())
    {
        int octaves = (int)(pitchOffset / 12.0f);
        int semis = (int)pitchOffset % 12;
        juce::String tooltip = "Transpose all notes by " + juce::String((int)pitchOffset) + " semitones";
        if (octaves != 0)
            tooltip += juce::String::formatted(" (%+d octave%s", octaves, std::abs(octaves) > 1 ? "s" : "");
        if (semis != 0)
        {
            if (octaves != 0) tooltip += ",";
            tooltip += juce::String::formatted(" %+d semi%s", semis, std::abs(semis) > 1 ? "s" : "");
        }
        if (octaves != 0) tooltip += ")";
        ImGui::SetTooltip("%s", tooltip.toRawUTF8());
    }
    ImGui::PopItemWidth();
    
    ImGui::Spacing(); // Visual separation between pitch and zoom
    
    // === TIMELINE ZOOM SECTION ===
    ImGui::Text("Timeline Zoom:");
    ImGui::SameLine();
    ImGui::PushItemWidth(120);
    if (ImGui::SliderFloat("##zoom", &zoomX, 20.0f, 400.0f, "%.0fpx/beat"))
    {
        juce::Logger::writeToLog("[ZOOM SLIDER] Changed to: " + juce::String(zoomX) + "px/beat");
    }
    ImGui::PopItemWidth();
    
    ImGui::Spacing();
    
    // --- QUICK CONNECT BUTTONS ---
    if (hasMIDIFileLoaded() && getNumTracks() > 0)
    {
        ImGui::Separator();
        ImGui::Text("Quick Connect:");
        ImGui::SameLine();
        
        if (ImGui::Button("→ PolyVCO"))
        {
            // Request connection: Pitch→Freq, Gate→Gate, Velo→Wave for all tracks
            connectionRequestType = 1; // PolyVCO
    }
    if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create PolyVCO and connect all tracks:\nPitch → Freq Mod\nGate → Gate Mod\nVelocity → Wave Mod");
    
    ImGui::SameLine();
        if (ImGui::Button("→ Samplers"))
    {
            // Request connection: Create one SampleLoader per track
            connectionRequestType = 2; // Samplers
    }
    if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create %d Sample Loaders (one per track):\nPitch → Pitch Mod\nGate → Gate Mod\nTrigger → Trigger Mod", getNumTracks());
    
        ImGui::SameLine();
        if (ImGui::Button("→ Both"))
    {
            // Request connection: Do both
            connectionRequestType = 3; // Both
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Connect to both PolyVCO and Sample Loaders");
    
        ImGui::Separator();
    }
    
        ImGui::Spacing();
        
    // --- 2. MAIN CONTENT AREA (PIANO ROLL) ---
    if (!hasMIDIFileLoaded())
    {
        // No file loaded - show drop zone with visual feedback
        ImVec2 dropZoneSize = ImVec2(nodeWidth, 100.0f);
        
        // Check if a drag-drop operation is in progress
        bool isDragging = ImGui::GetDragDropPayload() != nullptr;
        
        if (isDragging)
        {
            // Beautiful blinking animation during drag-drop
            float time = (float)ImGui::GetTime();
            float pulse = (std::sin(time * 8.0f) * 0.5f + 0.5f); // Fast blink
            float glow = (std::sin(time * 3.0f) * 0.3f + 0.7f);  // Slower glow
            
            // Vibrant purple/magenta with pulsing alpha
            ImU32 fillColor = IM_COL32((int)(180 * glow), (int)(100 * glow), (int)(255 * glow), (int)(100 + pulse * 155));
            ImU32 borderColor = IM_COL32((int)(220 * pulse), (int)(120 * glow), (int)(255 * pulse), 255);
            
            ImGui::PushStyleColor(ImGuiCol_Button, fillColor);
            ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
            ImGui::Button("##dropzone_midi", dropZoneSize);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        else
        {
            // Discrete outline only when idle
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0)); // Transparent fill
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(100, 100, 100, 120)); // Gray outline
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::Button("##dropzone_midi", dropZoneSize);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        
        const char* text = isDragging ? "Drop MIDI Here!" : "Drop MIDI File Here or Click Load .mid";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos = ImGui::GetItemRectMin();
        textPos.x += (dropZoneSize.x - textSize.x) * 0.5f;
        textPos.y += (dropZoneSize.y - textSize.y) * 0.5f;
        ImU32 textColor = isDragging ? IM_COL32(220, 150, 255, 255) : IM_COL32(150, 150, 150, 200);
        ImGui::GetWindowDrawList()->AddText(textPos, textColor, text);
        
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MIDI_PATH"))
            {
                const char* path = (const char*)payload->Data;
                loadMIDIFile(juce::File(path));
                onModificationEnded();
            }
            ImGui::EndDragDropTarget();
        }
        
        return; // Exit early if no file loaded
    }
    
    const float contentHeight = 250.0f;
    const float timelineHeight = 30.0f;
    
    ImGui::BeginChild("MainContent", ImVec2(nodeWidth, contentHeight), true, 
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float scrollX = ImGui::GetScrollX();
    
    // --- 3. TIMELINE RULER ---
    // CRITICAL FIX: Use ORIGINAL file tempo for visual layout, not current playback tempo!
    // This prevents the timeline from "zooming" when tempo multiplier changes
    const double visualTempo = fileBpm; // Use original file tempo for consistent visual layout
    const double samplesPerBeat = (60.0 / visualTempo) * getSampleRate();
    const float pixelsPerBeat = zoomX;
    const int numBars = (int)std::ceil(totalDuration / (60.0 / visualTempo * 4.0)); // Estimate bars from duration
    const float totalWidth = numBars * 4.0f * pixelsPerBeat;
    
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
    const int firstBeat = juce::jmax(0, static_cast<int>(scrollX / pixelsPerBeat));
    const int lastBeat = juce::jmin(numBars * 4, static_cast<int>((scrollX + nodeWidth) / pixelsPerBeat) + 1);
    
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
    
    // --- 4. PIANO ROLL GRID & NOTE RENDERING ---
    ImGui::Spacing();
    
    // CRITICAL: Calculate piano roll content height based on track count
    int currentTrack = (int)trackParam->load();
    const float trackHeight = 40.0f;
    const float pianoRollHeight = (currentTrack == -1) 
        ? (notesByTrack.size() * 30.0f + 10.0f)  // Multi-track view
        : trackHeight;                            // Single track view
    
    // CRITICAL: Reserve space for ENTIRE piano roll content (width × height)
    ImGui::Dummy(ImVec2(totalWidth, pianoRollHeight));
    
    // Get the piano roll area bounds (AFTER Dummy)
    const ImVec2 pianoRollStartPos = ImGui::GetItemRectMin();
    const ImVec2 pianoRollEndPos = ImGui::GetItemRectMax();
    
    if (currentTrack == -1)
    {
        // SHOW ALL TRACKS: Multi-track visualization like MidiLogger
        
        // Color palette for different tracks (cycling through hues)
        const ImU32 trackColors[] = {
            IM_COL32(100, 180, 255, 204),  // Blue
            IM_COL32(255, 120, 100, 204),  // Red
            IM_COL32(120, 255, 100, 204),  // Green
            IM_COL32(255, 200, 100, 204),  // Orange
            IM_COL32(200, 100, 255, 204),  // Purple
            IM_COL32(100, 255, 200, 204),  // Cyan
            IM_COL32(255, 100, 180, 204),  // Pink
            IM_COL32(220, 220, 100, 204),  // Yellow
        };
        const int numColors = sizeof(trackColors) / sizeof(trackColors[0]);
        
        const float trackHeightMulti = 30.0f;
        for (size_t trackIdx = 0; trackIdx < notesByTrack.size(); ++trackIdx)
        {
            const auto& notes = notesByTrack[trackIdx];
            if (notes.empty()) continue;
            
            // Assign color based on track index
            ImU32 noteColor = trackColors[trackIdx % numColors];
            ImU32 noteBorderColor = noteColor | IM_COL32(0, 0, 0, 51); // Slightly darker border
            
            const float trackY_top = pianoRollStartPos.y + (trackIdx * trackHeightMulti);
            const float trackY_bottom = trackY_top + trackHeightMulti - 5.0f;
            
            for (const auto& note : notes)
            {
                const float noteStartX_px = pianoRollStartPos.x + (float)(note.startTime / (60.0 / visualTempo)) * pixelsPerBeat;
                const float noteEndX_px = pianoRollStartPos.x + (float)(note.endTime / (60.0 / visualTempo)) * pixelsPerBeat;
                
                // ImGui handles clipping automatically - no manual culling needed
                
                // Draw note rectangle
                drawList->AddRectFilled(
                    ImVec2(noteStartX_px, trackY_top),
                    ImVec2(noteEndX_px, trackY_bottom),
                    noteColor,
                    3.0f // corner rounding
                );
                // Draw border
                drawList->AddRect(
                    ImVec2(noteStartX_px, trackY_top),
                    ImVec2(noteEndX_px, trackY_bottom),
                    noteBorderColor,
                    3.0f,
                    0,
                    1.2f
                );
            }
        }
    }
    else if (currentTrack >= 0 && currentTrack < (int)notesByTrack.size())
    {
        // SINGLE TRACK VIEW
        const auto& notes = notesByTrack[(size_t)currentTrack];
        
        // Draw notes
        ImU32 noteColor = IM_COL32(100, 180, 255, 204); // Blue with alpha
        ImU32 noteBorderColor = IM_COL32(150, 200, 255, 255); // Lighter blue border
        
        const float noteY_top = pianoRollStartPos.y + 5.0f;
        const float noteY_bottom = noteY_top + trackHeight - 10.0f;
        
        for (const auto& note : notes)
        {
            // Convert time (seconds) to pixels
            const float noteStartX_px = pianoRollStartPos.x + (float)(note.startTime / (60.0 / visualTempo)) * pixelsPerBeat;
            const float noteEndX_px = pianoRollStartPos.x + (float)(note.endTime / (60.0 / visualTempo)) * pixelsPerBeat;
            
            // ImGui handles clipping automatically - no manual culling needed
            
            // Draw note rectangle
            drawList->AddRectFilled(
                ImVec2(noteStartX_px, noteY_top),
                ImVec2(noteEndX_px, noteY_bottom),
                noteColor,
                4.0f // corner rounding
            );
            // Draw border
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
    
    // --- 6. DRAW PLAYHEAD LINE (CRITICAL: Must be INSIDE BeginChild/EndChild for clipping!) ---
    // Draw playhead at its absolute position in the content (ImGui clips to child window)
    const float playheadX = timelineStartPos.x + (float)(currentPlaybackTime / (60.0 / visualTempo)) * pixelsPerBeat;
    
    drawList->AddLine(
        ImVec2(playheadX, timelineStartPos.y),
        ImVec2(playheadX, pianoRollEndPos.y),
        IM_COL32(255, 255, 0, 200), // Yellow playhead
        2.0f
    );
    
    // Draw a triangle handle at the top for visual reference
    drawList->AddTriangleFilled(
        ImVec2(playheadX, timelineStartPos.y),
        ImVec2(playheadX - 6.0f, timelineStartPos.y + 10.0f),
        ImVec2(playheadX + 6.0f, timelineStartPos.y + 10.0f),
        IM_COL32(255, 255, 0, 255)
    );
    
    ImGui::EndChild();
    
    // --- 5. PLAYHEAD INTERACTION (Click anywhere in timeline to seek) ---
    // Check if user clicked in the MainContent child window
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // CRITICAL FIX: Get the child window's screen bounds (the item we just ended)
        ImVec2 childWindowMin = ImGui::GetItemRectMin(); // Top-left of visible child window
        float mouseX = ImGui::GetMousePos().x;
        
        // Calculate timeline position: mouse relative to visible window + scroll offset
        float relativeX = (mouseX - childWindowMin.x) + scrollX;
        double newTime = (relativeX / pixelsPerBeat) * (60.0 / visualTempo); // Use visualTempo
        
        // Clamp to valid range
        newTime = juce::jlimit(0.0, totalDuration, newTime);
        
        // Set the new playhead via the atomic seek mechanism
        pendingSeekTime.store(newTime);
    }
    
    // --- TRACK INFO ---
    int trackNum = (int)trackParam->load();
    if (trackNum == -1)
    {
        ImGui::Text("Viewing: All Tracks (Stacked)");
    }
    else if (trackNum >= 0 && trackNum < (int)trackInfos.size())
    {
        const auto& info = trackInfos[(size_t)trackNum];
        ImGui::Text("Track %d: %s • %d notes", 
                    trackNum + 1, 
                    info.name.isNotEmpty() ? info.name.toRawUTF8() : "Untitled",
                    info.noteCount);
    }
}
#endif

// ==============================================================================
// LEGACY UI CODE REMOVED
// Old controls (Speed, Pitch, Tempo, Loop sliders) are now accessed via parameter modulation
// The piano roll provides superior visual feedback for MIDI playback
// Speed/Pitch/Tempo/Loop parameters can still be modulated via CV inputs
// ==============================================================================

// ==============================================================================
// POLYPHONIC OUTPUTS: Dynamic pins for multi-track playback
// ==============================================================================
std::vector<DynamicPinInfo> MIDIPlayerModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const juce::ScopedLock lock(midiDataLock);
    
    // CRITICAL FIX: Create output pins for ALL tracks (not just those with notes)
    // This ensures pin indices match track indices for Quick Connect
    // Format: Gate 1, Pitch 1, Velo 1, Trig 1, Gate 2, Pitch 2, Velo 2, Trig 2, ...
    for (size_t i = 0; i < notesByTrack.size(); ++i)
    {
        const juce::String trackNumStr = juce::String(i + 1);
        const juce::String trackName = (i < trackInfos.size() && trackInfos[i].name.isNotEmpty()) 
            ? trackInfos[i].name 
            : ("Track " + trackNumStr);
        
        // Each track gets 4 pins: Gate, Pitch, Velocity, Trigger
        // Pin indices: track 0 = 0,1,2,3  track 1 = 4,5,6,7  etc.
        const int baseChannel = (int)i * 4;
        pins.push_back({ trackName + " Gate", baseChannel + 0, PinDataType::Gate });
        pins.push_back({ trackName + " Pitch", baseChannel + 1, PinDataType::CV });
        pins.push_back({ trackName + " Velo", baseChannel + 2, PinDataType::CV });
        pins.push_back({ trackName + " Trig", baseChannel + 3, PinDataType::Gate });
    }
    
    // Add "Num Tracks" output (Raw type for Track Mixer connection)
    pins.push_back({ "Num Tracks", kRawNumTracksChannelIndex, PinDataType::Raw });
    
    return pins;
}

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
