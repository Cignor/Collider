#include "MIDICVModuleProcessor.h"

MIDICVModuleProcessor::MIDICVModuleProcessor()
    : ModuleProcessor(
        juce::AudioProcessor::BusesProperties()
            .withOutput("Main", juce::AudioChannelSet::discreteChannels(6), true)
            .withOutput("Mod", juce::AudioChannelSet::discreteChannels(64), true)
    ),
      dummyApvts(*this, nullptr, "DummyParams", createParameterLayout())
{
    // No parameters needed - this module is purely MIDI->CV conversion
    
    // Initialize last output values for telemetry
    lastOutputValues.resize(6);
    for (auto& val : lastOutputValues)
        val = std::make_unique<std::atomic<float>>(0.0f);
}

void MIDICVModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Reset MIDI state
    midiState = MIDIState();
    
    juce::Logger::writeToLog("[MIDI CV] Prepared to play at " + juce::String(sampleRate) + " Hz");
}

void MIDICVModuleProcessor::releaseResources()
{
}

void MIDICVModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    if (buffer.getNumChannels() < 6)
    {
        buffer.clear();
        return;
    }
    
    // --- NEW, MORE DETAILED LOGGING ---
    if (!midiMessages.isEmpty())
    {
        juce::Logger::writeToLog("[MIDI CV] Processing " + juce::String(midiMessages.getNumEvents()) + " incoming MIDI events.");
    }
    // --- END OF NEW LOGGING ---

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        
        // Use getDescription() for a detailed, human-readable log
        juce::Logger::writeToLog("[MIDI CV] Received: " + msg.getDescription());

        if (msg.isNoteOn())
        {
            midiState.currentNote = msg.getNoteNumber();
            midiState.currentVelocity = msg.getVelocity() / 127.0f;
            midiState.gateHigh = true;
        }
        else if (msg.isNoteOff())
        {
            if (msg.getNoteNumber() == midiState.currentNote)
            {
                midiState.gateHigh = false;
            }
        }
        else if (msg.isController())
        {
            if (msg.getControllerNumber() == 1) // Mod Wheel
            {
                midiState.modWheel = msg.getControllerValue() / 127.0f;
            }
        }
        else if (msg.isPitchWheel())
        {
            midiState.pitchBend = (msg.getPitchWheelValue() - 8192) / 8192.0f;
        }
        else if (msg.isAftertouch())
        {
            midiState.aftertouch = msg.getAfterTouchValue() / 127.0f;
        }
    }
    
    // Generate CV outputs for the entire block
    // Channel 0: Pitch CV (1V/octave)
    // Channel 1: Gate (0 or 1)
    // Channel 2: Velocity (0-1)
    // Channel 3: Mod Wheel (0-1)
    // Channel 4: Pitch Bend (-1 to +1)
    // Channel 5: Aftertouch (0-1)
    
    const int numSamples = buffer.getNumSamples();
    
    // Pitch CV
    const float pitchCv = midiState.currentNote >= 0 ? midiNoteToCv(midiState.currentNote) : 0.0f;
    buffer.getWritePointer(0)[0] = pitchCv;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(0)[i] = pitchCv;
    
    // Gate
    const float gateValue = midiState.gateHigh ? 1.0f : 0.0f;
    buffer.getWritePointer(1)[0] = gateValue;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(1)[i] = gateValue;
    
    // Velocity
    buffer.getWritePointer(2)[0] = midiState.currentVelocity;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(2)[i] = midiState.currentVelocity;
    
    // Mod Wheel
    buffer.getWritePointer(3)[0] = midiState.modWheel;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(3)[i] = midiState.modWheel;
    
    // Pitch Bend
    buffer.getWritePointer(4)[0] = midiState.pitchBend;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(4)[i] = midiState.pitchBend;
    
    // Aftertouch
    buffer.getWritePointer(5)[0] = midiState.aftertouch;
    for (int i = 1; i < numSamples; ++i)
        buffer.getWritePointer(5)[i] = midiState.aftertouch;
    
    // Update telemetry
    if (lastOutputValues.size() >= 6)
    {
        lastOutputValues[0]->store(pitchCv);
        lastOutputValues[1]->store(gateValue);
        lastOutputValues[2]->store(midiState.currentVelocity);
        lastOutputValues[3]->store(midiState.modWheel);
        lastOutputValues[4]->store(midiState.pitchBend);
        lastOutputValues[5]->store(midiState.aftertouch);
    }
}

float MIDICVModuleProcessor::midiNoteToCv(int noteNumber) const
{
    // 1V/octave standard: C4 (MIDI note 60) = 0V
    // Each semitone = 1/12 V
    return (noteNumber - 60) / 12.0f;
}

#if defined(PRESET_CREATOR_UI)

// Helper function for tooltip with help marker
static void HelpMarkerCV(const char* desc)
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

void MIDICVModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>&)
{
    ImGui::PushItemWidth(itemWidth);
    
    // === MIDI INPUT STATUS ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "MIDI Input Status");
    ImGui::Spacing();
    
    // Get current values from telemetry
    float pitchCV = lastOutputValues.size() > 0 ? lastOutputValues[0]->load() : 0.0f;
    float gateValue = lastOutputValues.size() > 1 ? lastOutputValues[1]->load() : 0.0f;
    float velocity = lastOutputValues.size() > 2 ? lastOutputValues[2]->load() : 0.0f;
    float modWheel = lastOutputValues.size() > 3 ? lastOutputValues[3]->load() : 0.0f;
    float pitchBend = lastOutputValues.size() > 4 ? lastOutputValues[4]->load() : 0.0f;
    float aftertouch = lastOutputValues.size() > 5 ? lastOutputValues[5]->load() : 0.0f;
    
    // Convert pitch CV back to MIDI note for display
    int midiNote = midiState.currentNote;
    bool hasNote = (midiNote >= 0);
    
    // === NOTE DISPLAY ===
    ImGui::Text("Note:");
    ImGui::SameLine();
    if (hasNote)
    {
        // Note name conversion (C4 = 60)
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int octave = (midiNote / 12) - 1;
        const char* noteName = noteNames[midiNote % 12];
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.5f, 1.0f)); // Bright green
        ImGui::Text("%s%d (#%d)", noteName, octave, midiNote);
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("---");
    }
    
    // === GATE INDICATOR ===
    ImGui::Text("Gate:");
    ImGui::SameLine();
    if (gateValue > 0.5f)
    {
        // Animated gate indicator
        float phase = (float)std::fmod(ImGui::GetTime() * 2.0, 1.0);
        float brightness = 0.6f + 0.4f * std::sin(phase * 6.28318f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f * brightness, 0.3f * brightness, 0.3f * brightness, 1.0f));
        ImGui::Text("ON");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("OFF");
    }
    
    ImGui::Spacing();
    
    // === LIVE VALUE DISPLAYS ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live Values");
    ImGui::Spacing();
    
    // Velocity with progress bar
    ImGui::Text("Vel");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.55f, 0.7f, velocity).Value);
    ImGui::ProgressBar(velocity, ImVec2(0, 0), juce::String(velocity, 2).toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    HelpMarkerCV("MIDI Note Velocity (0-1)");
    
    // Mod Wheel with progress bar
    ImGui::Text("Mod");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.15f, 0.7f, modWheel).Value);
    ImGui::ProgressBar(modWheel, ImVec2(0, 0), juce::String(modWheel, 2).toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    HelpMarkerCV("Mod Wheel (CC#1, 0-1)");
    
    // Pitch Bend with centered bar
    ImGui::Text("Bend");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    float normalizedBend = (pitchBend + 1.0f) / 2.0f; // -1..1 -> 0..1
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.0f, 0.7f, std::abs(pitchBend)).Value);
    ImGui::ProgressBar(normalizedBend, ImVec2(0, 0), juce::String(pitchBend, 2).toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    HelpMarkerCV("Pitch Bend (-1 to +1)");
    
    // Aftertouch with progress bar
    ImGui::Text("AT");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor::HSV(0.85f, 0.7f, aftertouch).Value);
    ImGui::ProgressBar(aftertouch, ImVec2(0, 0), juce::String(aftertouch, 2).toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    HelpMarkerCV("Channel Aftertouch (0-1)");
    
    ImGui::Spacing();
    
    // === CV OUTPUT INFO ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "CV Output");
    ImGui::Spacing();
    
    ImGui::Text("Pitch CV: %.3f V", pitchCV);
    ImGui::SameLine();
    HelpMarkerCV("1V/octave standard\nC4 (MIDI note 60) = 0V");
    
    ImGui::PopItemWidth();
}

void MIDICVModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioOutputPin("Pitch", 0);
    helpers.drawAudioOutputPin("Gate", 1);
    helpers.drawAudioOutputPin("Velocity", 2);
    helpers.drawAudioOutputPin("Mod Wheel", 3);
    helpers.drawAudioOutputPin("Pitch Bend", 4);
    helpers.drawAudioOutputPin("Aftertouch", 5);
}
#endif