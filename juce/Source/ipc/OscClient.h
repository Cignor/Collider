#pragma once

#include <juce_osc/juce_osc.h>

class OscClient
{
public:
    OscClient() = default;
    ~OscClient() { disconnect(); }

    bool connect (const juce::String& host, int port)
    {
        return sender.connect (host, port);
    }

    void disconnect()
    {
        sender.disconnect();
    }

    void sendDeviceList (const juce::String& type, const juce::StringArray& names)
    {
        DBG ("JUCE CLIENT: Sending /info/audioDeviceList (" + type + ") with " + juce::String (names.size()) + " entries");
        juce::OSCMessage msg ("/info/audioDeviceList");
        msg.addString (type);
        for (auto& n : names) msg.addString (n);
        sender.send (msg);
    }

    void sendMidiDeviceList (const juce::StringArray& names)
    {
        DBG ("JUCE CLIENT: Sending /info/midiDeviceList with " + juce::String (names.size()) + " entries");
        juce::OSCMessage msg ("/info/midiDeviceList");
        for (auto& n : names) msg.addString (n);
        sender.send (msg);
    }

    void sendCurrentSettings (const juce::String& inputName, const juce::String& outputName, float sampleRate, int bufferSize)
    {
        DBG ("JUCE CLIENT: Sending /info/currentSettings in='" + inputName + "' out='" + outputName + "' sr=" + juce::String (sampleRate) + " bs=" + juce::String (bufferSize));
        juce::OSCMessage msg ("/info/currentSettings");
        msg.addString (inputName);
        msg.addString (outputName);
        msg.addFloat32 (sampleRate);
        msg.addInt32 (bufferSize);
        sender.send (msg);
    }

    void sendMasterGain (float gain)
    {
        DBG ("JUCE CLIENT: Sending /info/masterGain=" + juce::String (gain));
        juce::OSCMessage msg ("/info/masterGain");
        msg.addFloat32 (gain);
        sender.send (msg);
    }

    void sendCpuLoad (float cpuLoad01)
    {
        DBG ("JUCE CLIENT: Sending /info/cpuLoad=" + juce::String (cpuLoad01));
        juce::OSCMessage msg ("/info/cpuLoad");
        msg.addFloat32 (cpuLoad01);
        sender.send (msg);
    }

private:
    juce::OSCSender sender;
};


