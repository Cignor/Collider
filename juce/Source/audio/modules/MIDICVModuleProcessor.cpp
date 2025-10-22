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
    // Ensure we have enough output channels
    if (buffer.getNumChannels() < 6)
    {
        buffer.clear();
        return;
    }
    
    // Process all MIDI messages in this block
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        
        if (msg.isNoteOn())
        {
            midiState.currentNote = msg.getNoteNumber();
            midiState.currentVelocity = msg.getVelocity() / 127.0f;
            midiState.gateHigh = true;
            
            juce::Logger::writeToLog("[MIDI CV] Note On: " + juce::String(midiState.currentNote) + 
                                   " Velocity: " + juce::String(midiState.currentVelocity));
        }
        else if (msg.isNoteOff())
        {
            if (msg.getNoteNumber() == midiState.currentNote)
            {
                midiState.gateHigh = false;
                juce::Logger::writeToLog("[MIDI CV] Note Off: " + juce::String(midiState.currentNote));
            }
        }
        else if (msg.isController())
        {
            const int ccNumber = msg.getControllerNumber();
            const float ccValue = msg.getControllerValue() / 127.0f;
            
            if (ccNumber == 1) // Mod Wheel
            {
                midiState.modWheel = ccValue;
            }
        }
        else if (msg.isPitchWheel())
        {
            // Convert 14-bit pitch wheel (0-16383) to -1 to +1 range
            const int pitchValue = msg.getPitchWheelValue();
            midiState.pitchBend = (pitchValue - 8192) / 8192.0f;
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