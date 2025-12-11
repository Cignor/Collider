#include "MIDICVModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MIDICVModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    // Device selection (simplified - device enumeration not available in this context)
    juce::StringArray deviceOptions;
    deviceOptions.add("All Devices");
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "midiDevice", "MIDI Device", deviceOptions, 0));
    
    // Channel filter (0 = All Channels, 1-16 = specific channel)
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "midiChannel", "MIDI Channel", 0, 16, 0));
    
    return layout;
}

MIDICVModuleProcessor::MIDICVModuleProcessor()
    : ModuleProcessor(
        juce::AudioProcessor::BusesProperties()
            .withOutput("Main", juce::AudioChannelSet::discreteChannels(27), true)
            .withOutput("Mod", juce::AudioChannelSet::discreteChannels(64), true)
    ),
      apvts(*this, nullptr, "MIDICVParams", createParameterLayout())
{
    // Get parameter pointers
    deviceFilterParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("midiDevice"));
    midiChannelFilterParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("midiChannel"));
    
    // Initialize voices
    for (auto& voice : voices)
    {
        voice = Voice();
    }
    
    // Initialize last output values for telemetry (27 outputs: 24 voice + 3 global)
    lastOutputValues.resize(27);
    for (auto& val : lastOutputValues)
        val = std::make_unique<std::atomic<float>>(0.0f);
}

void MIDICVModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Reset all voices
    const juce::ScopedLock lock(voiceLock);
    for (auto& voice : voices)
    {
        voice = Voice();
    }
    currentSamplePosition = 0;
    
    globalModWheel = 0.0f;
    globalPitchBend = 0.0f;
    globalAftertouch = 0.0f;
    
    juce::Logger::writeToLog("[MIDI CV] Prepared to play at " + juce::String(sampleRate) + " Hz with " + juce::String(NUM_VOICES) + " voices");
}

void MIDICVModuleProcessor::releaseResources()
{
}

void MIDICVModuleProcessor::handleDeviceSpecificMidi(const std::vector<MidiMessageWithDevice>& midiMessages)
{
    // Get user's filter settings
    int deviceFilter = deviceFilterParam ? deviceFilterParam->getIndex() : 0;
    int channelFilter = midiChannelFilterParam ? midiChannelFilterParam->get() : 0;
    
    // Get current sample position for voice stealing priority
    int64_t samplePos = currentSamplePosition.load();
    
    for (const auto& msg : midiMessages)
    {
        // DEVICE FILTERING
        // Index 0 = "All Devices", Index 1+ = specific device
        if (deviceFilter != 0 && msg.deviceIndex != (deviceFilter - 1))
            continue;
        
        // CHANNEL FILTERING
        // 0 = "All Channels", 1-16 = specific channel
        int midiChannel = msg.message.getChannel();
        if (channelFilter != 0 && midiChannel != channelFilter)
            continue;
        
        // PROCESS FILTERED MESSAGE
        // This message passed both filters - process it
        if (msg.message.isNoteOn())
        {
            int midiNote = msg.message.getNoteNumber();
            float velocity = msg.message.getVelocity() / 127.0f;
            allocateVoice(midiNote, midiChannel, velocity, samplePos);
            samplePos++;  // Increment for next potential note-on
        }
        else if (msg.message.isNoteOff())
        {
            int midiNote = msg.message.getNoteNumber();
            int voiceIndex = findVoiceForNote(midiNote);
            if (voiceIndex >= 0)
            {
                releaseVoice(voiceIndex, midiNote);
            }
        }
        else if (msg.message.isController())
        {
            int ccNum = msg.message.getControllerNumber();
            int ccVal = msg.message.getControllerValue();
            
            if (ccNum == 1) // Mod Wheel
            {
                globalModWheel = ccVal / 127.0f;
            }
        }
        else if (msg.message.isPitchWheel())
        {
            globalPitchBend = (msg.message.getPitchWheelValue() - 8192) / 8192.0f;
        }
        else if (msg.message.isChannelPressure())
        {
            globalAftertouch = msg.message.getChannelPressureValue() / 127.0f;
        }
    }
}

void MIDICVModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages); // MIDI already processed in handleDeviceSpecificMidi
    
    if (buffer.getNumChannels() < 27)
    {
        buffer.clear();
        return;
    }
    
    // Update sample position counter
    currentSamplePosition += buffer.getNumSamples();
    
    const int numSamples = buffer.getNumSamples();
    
    // Get a snapshot of voice states (thread-safe copy)
    std::array<Voice, NUM_VOICES> voiceSnapshot;
    {
        const juce::ScopedLock lock(voiceLock);
        voiceSnapshot = voices;
    }
    
    // Get global controller values
    float modWheel = globalModWheel.load();
    float pitchBend = globalPitchBend.load();
    float aftertouch = globalAftertouch.load();
    
    // Generate per-voice CV outputs (24 channels: 8 voices × 3 outputs each)
    for (int voice = 0; voice < NUM_VOICES; ++voice)
    {
        const int baseChannel = voice * 3;
        
        const bool voiceActive = voiceSnapshot[voice].active;
        const float gate = voiceActive ? 1.0f : 0.0f;
        const float pitchCV = voiceActive ? midiNoteToCv(voiceSnapshot[voice].midiNote) : 0.0f;
        const float velocity = voiceActive ? voiceSnapshot[voice].velocity : 0.0f;
        
        // Gate (channel baseChannel)
        float* gateBuffer = buffer.getWritePointer(baseChannel);
        juce::FloatVectorOperations::fill(gateBuffer, gate, numSamples);
        
        // Pitch CV (channel baseChannel + 1)
        float* pitchBuffer = buffer.getWritePointer(baseChannel + 1);
        juce::FloatVectorOperations::fill(pitchBuffer, pitchCV, numSamples);
        
        // Velocity (channel baseChannel + 2)
        float* velBuffer = buffer.getWritePointer(baseChannel + 2);
        juce::FloatVectorOperations::fill(velBuffer, velocity, numSamples);
        
        // Update telemetry for this voice
        if (lastOutputValues.size() > (size_t)(baseChannel + 2))
        {
            lastOutputValues[baseChannel]->store(gate);
            lastOutputValues[baseChannel + 1]->store(pitchCV);
            lastOutputValues[baseChannel + 2]->store(velocity);
        }
    }
    
    // Output global controllers (channels 24, 25, 26)
    // Mod Wheel (channel 24)
    float* modWheelBuffer = buffer.getWritePointer(24);
    juce::FloatVectorOperations::fill(modWheelBuffer, modWheel, numSamples);
    
    // Pitch Bend (channel 25)
    float* pitchBendBuffer = buffer.getWritePointer(25);
    juce::FloatVectorOperations::fill(pitchBendBuffer, pitchBend, numSamples);
    
    // Aftertouch (channel 26)
    float* aftertouchBuffer = buffer.getWritePointer(26);
    juce::FloatVectorOperations::fill(aftertouchBuffer, aftertouch, numSamples);
    
    // Update telemetry for global controllers
    if (lastOutputValues.size() > 26)
    {
        lastOutputValues[24]->store(modWheel);
        lastOutputValues[25]->store(pitchBend);
        lastOutputValues[26]->store(aftertouch);
    }
}

float MIDICVModuleProcessor::midiNoteToCv(int noteNumber) const
{
    // 1V/octave standard: C4 (MIDI note 60) = 0V
    // Each semitone = 1/12 V
    return (noteNumber - 60) / 12.0f;
}

int MIDICVModuleProcessor::allocateVoice(int midiNote, int midiChannel, float velocity, int64_t currentSample)
{
    const juce::ScopedLock lock(voiceLock);
    
    // First, try to find an inactive voice
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        if (!voices[i].active)
        {
            voices[i].active = true;
            voices[i].midiNote = midiNote;
            voices[i].velocity = velocity;
            voices[i].midiChannel = midiChannel;
            voices[i].noteStartSample = currentSample;
            return i;
        }
    }
    
    // All voices are active - steal the voice with the lowest MIDI note
    int lowestNote = 128;
    int voiceToSteal = -1;
    
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        if (voices[i].active && voices[i].midiNote < lowestNote)
        {
            lowestNote = voices[i].midiNote;
            voiceToSteal = i;
        }
    }
    
    if (voiceToSteal >= 0)
    {
        // Steal this voice for the new note
        voices[voiceToSteal].active = true;
        voices[voiceToSteal].midiNote = midiNote;
        voices[voiceToSteal].velocity = velocity;
        voices[voiceToSteal].midiChannel = midiChannel;
        voices[voiceToSteal].noteStartSample = currentSample;
        return voiceToSteal;
    }
    
    // Fallback (shouldn't happen, but handle gracefully)
    return 0;
}

void MIDICVModuleProcessor::releaseVoice(int voiceIndex, int midiNote)
{
    if (voiceIndex < 0 || voiceIndex >= NUM_VOICES)
        return;
    
    const juce::ScopedLock lock(voiceLock);
    
    // Only release if this voice is playing the specified note
    if (voices[voiceIndex].active && voices[voiceIndex].midiNote == midiNote)
    {
        voices[voiceIndex].active = false;
        voices[voiceIndex].midiNote = -1;
        voices[voiceIndex].velocity = 0.0f;
    }
}

int MIDICVModuleProcessor::findVoiceForNote(int midiNote)
{
    const juce::ScopedLock lock(voiceLock);
    
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        if (voices[i].active && voices[i].midiNote == midiNote)
        {
            return i;
        }
    }
    
    return -1;  // Note not found
}

#if defined(PRESET_CREATOR_UI)

// Helper function for tooltip with help marker
static void HelpMarkerCV(const char* desc)
{
    ImGui::SameLine();  // CRITICAL: Position inline with previous element
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void MIDICVModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&)
{
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
    
    // === MULTI-MIDI DEVICE FILTERING ===
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
    
    // === VOICE STATUS TABLE ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Voice Status");
    
    // Get voice snapshot for display
    std::array<Voice, NUM_VOICES> voiceSnapshot;
    {
        const juce::ScopedLock lock(voiceLock);
        voiceSnapshot = voices;
    }
    
    // Count active voices
    int activeVoiceCount = 0;
    for (const auto& voice : voiceSnapshot)
    {
        if (voice.active)
            activeVoiceCount++;
    }
    
    ImGui::Text("Active: %d / %d", activeVoiceCount, NUM_VOICES);
    ImGui::Spacing();
    
    // Voice status table
    ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit |
                                 ImGuiTableFlags_Borders |
                                 ImGuiTableFlags_ScrollY;
    
    float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4;
    float tableHeight = rowHeight * (NUM_VOICES + 1.5f);
    
    if (ImGui::BeginTable("##voices_table", 4, tableFlags, ImVec2(itemWidth, tableHeight)))
    {
        ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Vel", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();
        
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        
        for (int i = 0; i < NUM_VOICES; ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            
            // Voice number
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i + 1);
            
            // Note
            ImGui::TableSetColumnIndex(1);
            if (voiceSnapshot[i].active && voiceSnapshot[i].midiNote >= 0)
            {
                int midiNote = voiceSnapshot[i].midiNote;
                int octave = (midiNote / 12) - 1;
                const char* noteName = noteNames[midiNote % 12];
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.5f, 1.0f)); // Bright green
                ImGui::Text("%s%d", noteName, octave);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextDisabled("---");
            }
            
            // Velocity
            ImGui::TableSetColumnIndex(2);
            if (voiceSnapshot[i].active)
            {
                ImGui::Text("%.2f", voiceSnapshot[i].velocity);
            }
            else
            {
                ImGui::TextDisabled("---");
            }
            
            // MIDI Channel
            ImGui::TableSetColumnIndex(3);
            if (voiceSnapshot[i].active)
            {
                ImGui::Text("%d", voiceSnapshot[i].midiChannel);
            }
            else
            {
                ImGui::TextDisabled("---");
            }
            
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    
    ImGui::Spacing();
    
    // === QUICK CONNECT BUTTON ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Quick Connect");
    if (ImGui::Button("→ MidiLogger"))
    {
        connectionRequestType = 1; // Request connection to MidiLogger
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create MidiLogger and connect all 8 voices:\nV1-8 Gate → Gate 1-8\nV1-8 Pitch → Pitch 1-8\nV1-8 Vel → Velo 1-8");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // === GLOBAL CONTROLLERS ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Global Controllers");
    ImGui::Spacing();
    
    // Get global controller values from telemetry
    float modWheel = lastOutputValues.size() > 24 ? lastOutputValues[24]->load() : 0.0f;
    float pitchBend = lastOutputValues.size() > 25 ? lastOutputValues[25]->load() : 0.0f;
    float aftertouch = lastOutputValues.size() > 26 ? lastOutputValues[26]->load() : 0.0f;
    
    const float progressBarWidth = itemWidth * 0.6f;
    
    // Mod Wheel with progress bar
    ImGui::Text("Mod");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.15f, 0.7f, modWheel).Value);
    ImGui::ProgressBar(modWheel, ImVec2(progressBarWidth, 0), juce::String(modWheel, 2).toRawUTF8());
    ImGui::PopStyleColor();
    HelpMarker("Mod Wheel (CC#1, 0-1)");
    
    // Pitch Bend with centered bar
    ImGui::Text("Bend");
    ImGui::SameLine();
    float normalizedBend = (pitchBend + 1.0f) / 2.0f; // -1..1 -> 0..1
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.0f, 0.7f, std::abs(pitchBend)).Value);
    ImGui::ProgressBar(normalizedBend, ImVec2(progressBarWidth, 0), juce::String(pitchBend, 2).toRawUTF8());
    ImGui::PopStyleColor();
    HelpMarker("Pitch Bend (-1 to +1)");
    
    // Aftertouch with progress bar
    ImGui::Text("AT");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.85f, 0.7f, aftertouch).Value);
    ImGui::ProgressBar(aftertouch, ImVec2(progressBarWidth, 0), juce::String(aftertouch, 2).toRawUTF8());
    ImGui::PopStyleColor();
    HelpMarker("Channel Aftertouch (0-1)");
    
    ImGui::PopItemWidth();
}

void MIDICVModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Per-voice outputs (8 voices × 3 outputs each = 24 outputs)
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        const int baseChannel = i * 3;
        
        juce::String gateLabel = "V" + juce::String(i + 1) + " Gate";
        juce::String pitchLabel = "V" + juce::String(i + 1) + " Pitch";
        juce::String velLabel = "V" + juce::String(i + 1) + " Vel";
        
        helpers.drawAudioOutputPin(gateLabel.toRawUTF8(), baseChannel);
        helpers.drawAudioOutputPin(pitchLabel.toRawUTF8(), baseChannel + 1);
        helpers.drawAudioOutputPin(velLabel.toRawUTF8(), baseChannel + 2);
        
        // Add spacing between voice groups
        if (i < NUM_VOICES - 1)
        {
            ImGui::Spacing();
        }
    }
    
    ImGui::Spacing();
    
    // Global controller outputs (3 outputs)
    helpers.drawAudioOutputPin("Mod Wheel", 24);
    helpers.drawAudioOutputPin("Pitch Bend", 25);
    helpers.drawAudioOutputPin("Aftertouch", 26);
}
#endif