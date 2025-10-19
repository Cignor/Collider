#pragma once

#include <juce_osc/juce_osc.h>
#include "CommandBus.h"

// OSC-based server. Binds to a UDP port and translates messages into Commands.
class IpcServer : public juce::OSCReceiver,
                  public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    explicit IpcServer (CommandBus& b) : bus (b)
    {
        addListener (this);
    }
    ~IpcServer() override
    {
        disconnect();
        removeListener (this);
    }

    bool bind (int port)
    {
        return connect (port);
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto addr = message.getAddressPattern().toString();
        Command cmd;
        if (addr == "/voice/create" && message.size() == 3 && message[0].isInt32() && message[1].isString() && message[2].isString())
        {
            cmd.type = Command::Type::Create;
            cmd.voiceId = (juce::uint64) message[0].getInt32();
            cmd.voiceType = message[1].getString();
            cmd.resourceName = message[2].getString();
            juce::Logger::writeToLog ("[OSC] create id=" + juce::String ((juce::int64) cmd.voiceId) + " type=" + cmd.voiceType + " res=" + cmd.resourceName);
            bus.enqueue (cmd);
        }
        else if (addr == "/voice/destroy" && message.size() == 1 && message[0].isInt32())
        {
            cmd.type = Command::Type::Destroy;
            cmd.voiceId = (juce::uint64) message[0].getInt32();
            juce::Logger::writeToLog ("[OSC] destroy id=" + juce::String ((juce::int64) cmd.voiceId));
            bus.enqueue (cmd);
        }
        // Extended create with initial position and amplitude to avoid later backlog
        else if (addr == "/voice/create_ex" || addr == "/voice/createEx")
        {
            if (message.size() >= 6 && message[0].isInt32() && message[1].isString() && message[2].isString())
            {
                cmd.type = Command::Type::Create;
                cmd.voiceId = (juce::uint64) message[0].getInt32();
                cmd.voiceType = message[1].getString();
                cmd.resourceName = message[2].getString();
                cmd.initialPosX = message[3].isFloat32() ? message[3].getFloat32() : (message[3].isInt32() ? (float) message[3].getInt32() : 0.0f);
                cmd.initialPosY = message[4].isFloat32() ? message[4].getFloat32() : (message[4].isInt32() ? (float) message[4].getInt32() : 0.0f);
                cmd.initialAmplitude = message[5].isFloat32() ? message[5].getFloat32() : (message[5].isInt32() ? (float) message[5].getInt32() : 0.0f);
                // Optional flags: pitch_on_grid, looping, volume
                if (message.size() > 6)
                {
                    if (message[6].isInt32()) cmd.initialPitchOnGrid = message[6].getInt32();
                    if (message.size() > 7 && message[7].isInt32()) cmd.initialLooping = message[7].getInt32();
                    if (message.size() > 8 && (message[8].isFloat32() || message[8].isInt32()))
                        cmd.initialVolume = message[8].isFloat32() ? message[8].getFloat32() : (float) message[8].getInt32();
                }
                juce::Logger::writeToLog ("[OSC] create_ex id=" + juce::String ((juce::int64) cmd.voiceId) + " type=" + cmd.voiceType + " res=" + cmd.resourceName);
                bus.enqueue (cmd);
            }
        }
        else if (addr.startsWith ("/voice/update/") && message.size() == 2 && message[0].isInt32())
        {
            cmd.type = Command::Type::Update;
            cmd.voiceId = (juce::uint64) message[0].getInt32();
            cmd.paramName = addr.fromLastOccurrenceOf ("/", false, false);
            if (message[1].isFloat32())       cmd.paramValue = message[1].getFloat32();
            else if (message[1].isInt32())    cmd.paramValue = (float) message[1].getInt32();
            else                              cmd.paramValue = 0.0f;
            // Coalesce frequent param updates
            if (cmd.paramName == "positionX" || cmd.paramName == "positionY")
                bus.enqueueLatest (cmd);
            else
                bus.enqueue (cmd);
        }
        else if (addr == "/settings/setMasterGain" && message.size() == 1)
        {
            Command c; c.type = Command::Type::Update; c.voiceId = 0; c.paramName = "master.gain";
            if (message[0].isFloat32()) c.paramValue = message[0].getFloat32();
            else if (message[0].isInt32()) c.paramValue = (float) message[0].getInt32();
            else c.paramValue = 0.0f;
            bus.enqueue (c);
        }
        else if (addr == "/settings/setDevice" && message.size() == 2)
        {
            // type ("input"/"output"), deviceName
            Command c; c.type = Command::Type::Update; c.voiceId = 0; c.paramName = "device.set";
            juce::String t = message[0].getString();
            c.paramValue = 0.0f; // unused numeric channel
            // Pack the type and name into a temp string we retrieve on the other side via bus (quick hack)
            // Format: "type\nname"
            juce::String name = message[1].getString();
            c.voiceType = t + "\n" + name;
            bus.enqueue (c);
        }
        else if (addr == "/settings/setBufferSize" && message.size() == 1)
        {
            // buffer size in frames (int)
            Command c; c.type = Command::Type::Update; c.voiceId = 0; c.paramName = "device.bufferSize";
            if (message[0].isInt32()) c.paramValue = (float) message[0].getInt32();
            else if (message[0].isFloat32()) c.paramValue = message[0].getFloat32();
            else c.paramValue = 0.0f;
            bus.enqueue (c);
        }
        else if (addr == "/settings/requestInfo")
        {
            // No args; request JUCE to resend all info snapshots
            Command c; c.type = Command::Type::Update; c.voiceId = 0; c.paramName = "engine.requestInfo"; c.paramValue = 0.0f;
            bus.enqueue (c);
        }
        else if (addr == "/debug/dump_state")
        {
            Command cd; cd.type = Command::Type::DebugDump; cd.voiceId = 0; bus.enqueue (cd);
        }
        else if (addr == "/listener/pos" && message.size() == 2)
        {
            // Coalesce into two updates so CommandProcessor can apply latest per tick
            Command cx; cx.type = Command::Type::Update; cx.voiceId = 0; cx.paramName = "listener.posX";
            if (message[0].isFloat32()) cx.paramValue = message[0].getFloat32(); else if (message[0].isInt32()) cx.paramValue = (float) message[0].getInt32(); else cx.paramValue = 0.0f;
            bus.enqueueLatest (cx);
            Command cy; cy.type = Command::Type::Update; cy.voiceId = 0; cy.paramName = "listener.posY";
            if (message[1].isFloat32()) cy.paramValue = message[1].getFloat32(); else if (message[1].isInt32()) cy.paramValue = (float) message[1].getInt32(); else cy.paramValue = 0.0f;
            bus.enqueueLatest (cy);
        }
        else if (addr == "/listener/set" && message.size() == 2)
        {
            Command cr; cr.type = Command::Type::Update; cr.voiceId = 0; cr.paramName = "listener.radius";
            if (message[0].isFloat32()) cr.paramValue = message[0].getFloat32(); else if (message[0].isInt32()) cr.paramValue = (float) message[0].getInt32(); else cr.paramValue = 0.0f;
            bus.enqueue (cr);
            Command cn; cn.type = Command::Type::Update; cn.voiceId = 0; cn.paramName = "listener.near";
            if (message[1].isFloat32()) cn.paramValue = message[1].getFloat32(); else if (message[1].isInt32()) cn.paramValue = (float) message[1].getInt32(); else cn.paramValue = 0.0f;
            bus.enqueue (cn);
        }
        else if (addr == "/engine/stopAll")
        {
            Command ce; ce.type = Command::Type::Update; ce.voiceId = 0; ce.paramName = "engine.stopAll"; ce.paramValue = 0.0f; bus.enqueue (ce);
        }
        else if (addr == "/voices/update_positions" && message.size() >= 3)
        {
            // Expect triplets: id(int32), x(float), y(float) ...
            const int n = (int) message.size();
            for (int i = 0; i + 2 < n; i += 3)
            {
                if (! message[i].isInt32()) continue;
                const juce::uint64 vid = (juce::uint64) message[i].getInt32();
                float x = 0.0f, y = 0.0f;
                if (message[i+1].isFloat32()) x = message[i+1].getFloat32(); else if (message[i+1].isInt32()) x = (float) message[i+1].getInt32();
                if (message[i+2].isFloat32()) y = message[i+2].getFloat32(); else if (message[i+2].isInt32()) y = (float) message[i+2].getInt32();
                Command cx; cx.type = Command::Type::Update; cx.voiceId = vid; cx.paramName = "positionX"; cx.paramValue = x; bus.enqueueLatest (cx);
                Command cy; cy.type = Command::Type::Update; cy.voiceId = vid; cy.paramName = "positionY"; cy.paramValue = y; bus.enqueueLatest (cy);
            }
        }
    }

private:
    CommandBus& bus;
};


