# Complete Guide to Handling Multiple MIDI Controllers and Events in JUCE

## Table of Contents
1. [Overview](#overview)
2. [Core JUCE MIDI Classes](#core-juce-midi-classes)
3. [Single MIDI Device Tutorial Analysis](#single-midi-device-tutorial-analysis)
4. [Handling Multiple MIDI Controllers](#handling-multiple-midi-controllers)
5. [MIDI Message Types and Parsing](#midi-message-types-and-parsing)
6. [Advanced Multi-Device Architecture](#advanced-multi-device-architecture)
7. [Best Practices and Optimization](#best-practices-and-optimization)
8. [Practical Implementation Examples](#practical-implementation-examples)
9. [Troubleshooting Common Issues](#troubleshooting-common-issues)

---

## Overview

This guide provides a comprehensive approach to handling multiple MIDI controllers and events in JUCE applications, based on the official JUCE tutorial and extended with advanced multi-device patterns.

### Key Capabilities
- **Multiple simultaneous MIDI inputs**: Handle multiple controllers at once
- **Device identification**: Track which device sent which message
- **Message routing**: Route messages from different devices to different handlers
- **State management**: Maintain separate state for each device
- **Thread safety**: Handle MIDI callbacks safely in audio and GUI threads

---

## Core JUCE MIDI Classes

### 1. `juce::AudioDeviceManager`
The central hub for audio and MIDI device management.

**Key Methods:**
```cpp
// Get available MIDI devices
auto devices = juce::MidiInput::getAvailableDevices();

// Enable a MIDI device
deviceManager.setMidiInputDeviceEnabled(deviceIdentifier, true);

// Add callback for MIDI input
deviceManager.addMidiInputDeviceCallback(deviceIdentifier, callback);

// Remove callback
deviceManager.removeMidiInputDeviceCallback(deviceIdentifier, callback);

// Check if device is enabled
bool isEnabled = deviceManager.isMidiInputDeviceEnabled(deviceIdentifier);
```

### 2. `juce::MidiInput`
Represents a MIDI input device.

**Device Information:**
```cpp
struct MidiDeviceInfo {
    juce::String identifier;  // Unique device ID
    juce::String name;        // Human-readable name
};

// Get all available devices
auto devices = juce::MidiInput::getAvailableDevices();
```

### 3. `juce::MidiInputCallback`
Interface for receiving MIDI messages.

```cpp
class MyMidiHandler : private juce::MidiInputCallback {
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override {
        // Process MIDI message
        // Note: This is called on the MIDI thread!
    }
};
```

### 4. `juce::MidiMessage`
Represents a MIDI message with timestamp.

**Key Methods:**
```cpp
message.isNoteOn()           // Note on event
message.isNoteOff()          // Note off event
message.isController()       // Control change (CC)
message.isProgramChange()    // Program change
message.isPitchWheel()       // Pitch bend
message.isChannelPressure()  // Channel pressure (aftertouch)
message.isAftertouch()       // Polyphonic aftertouch
message.isAllNotesOff()      // All notes off
message.isAllSoundOff()      // All sound off
message.isMetaEvent()        // Meta event

// Get message data
int channel = message.getChannel();
int noteNumber = message.getNoteNumber();
float velocity = message.getVelocity();
int ccNumber = message.getControllerNumber();
int ccValue = message.getControllerValue();
```

### 5. `juce::MidiKeyboardState`
Tracks the state of MIDI notes (which are currently pressed).

```cpp
class MyKeyboardListener : private juce::MidiKeyboardStateListener {
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, 
                     int midiNoteNumber, float velocity) override { }
    
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, 
                      int midiNoteNumber, float velocity) override { }
};

// Process MIDI messages through the state
keyboardState.processNextMidiEvent(message);
```

---

## Single MIDI Device Tutorial Analysis

### Architecture Overview

The tutorial demonstrates a basic MIDI handling pattern:

```cpp
class MainContentComponent : public juce::Component,
                             private juce::MidiInputCallback,
                             private juce::MidiKeyboardStateListener
{
private:
    juce::AudioDeviceManager deviceManager;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::ComboBox midiInputList;
    int lastInputIndex = 0;
    bool isAddingFromMidiInput = false;
};
```

### Key Patterns from Tutorial

#### 1. **Device Enumeration**
```cpp
auto midiInputs = juce::MidiInput::getAvailableDevices();
juce::StringArray midiInputNames;

for (auto input : midiInputs)
    midiInputNames.add(input.name);

midiInputList.addItemList(midiInputNames, 1);
```

#### 2. **Device Selection and Switching**
```cpp
void setMidiInput(int index)
{
    auto list = juce::MidiInput::getAvailableDevices();
    
    // Remove old callback
    deviceManager.removeMidiInputDeviceCallback(
        list[lastInputIndex].identifier, this);
    
    auto newInput = list[index];
    
    // Enable device if not already enabled
    if (!deviceManager.isMidiInputDeviceEnabled(newInput.identifier))
        deviceManager.setMidiInputDeviceEnabled(newInput.identifier, true);
    
    // Add callback for new device
    deviceManager.addMidiInputDeviceCallback(newInput.identifier, this);
    
    lastInputIndex = index;
}
```

#### 3. **MIDI Message Reception**
```cpp
void handleIncomingMidiMessage(juce::MidiInput* source, 
                               const juce::MidiMessage& message) override
{
    const juce::ScopedValueSetter<bool> scopedInputFlag(isAddingFromMidiInput, true);
    keyboardState.processNextMidiEvent(message);
    postMessageToList(message, source->getName());
}
```

#### 4. **Thread-Safe GUI Updates**
```cpp
class IncomingMessageCallback : public juce::CallbackMessage
{
public:
    IncomingMessageCallback(MainContentComponent* o, 
                           const juce::MidiMessage& m, 
                           const juce::String& s)
        : owner(o), message(m), source(s) {}
    
    void messageCallback() override
    {
        if (owner != nullptr)
            owner->addMessageToList(message, source);
    }
    
    Component::SafePointer<MainContentComponent> owner;
    juce::MidiMessage message;
    juce::String source;
};

void postMessageToList(const juce::MidiMessage& message, const juce::String& source)
{
    (new IncomingMessageCallback(this, message, source))->post();
}
```

**Why This Pattern?**
- MIDI callbacks happen on the MIDI thread (NOT the message thread)
- GUI updates MUST happen on the message thread
- `CallbackMessage` safely dispatches to the message thread
- `SafePointer` prevents crashes if component is deleted

---

## Handling Multiple MIDI Controllers

### Strategy 1: Single Callback with Source Identification

This is the simplest approach - one callback receives messages from all devices.

```cpp
class MultiDeviceMidiHandler : public juce::Component,
                               private juce::MidiInputCallback
{
public:
    MultiDeviceMidiHandler()
    {
        // Enable all available MIDI devices
        auto devices = juce::MidiInput::getAvailableDevices();
        
        for (const auto& device : devices)
        {
            if (!deviceManager.isMidiInputDeviceEnabled(device.identifier))
                deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            
            deviceManager.addMidiInputDeviceCallback(device.identifier, this);
            enabledDevices.add(device);
        }
    }
    
    ~MultiDeviceMidiHandler() override
    {
        // Clean up all callbacks
        for (const auto& device : enabledDevices)
            deviceManager.removeMidiInputDeviceCallback(device.identifier, this);
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // Identify which device sent the message
        juce::String deviceName = source->getName();
        juce::String deviceIdentifier = source->getIdentifier();
        
        // Route based on device
        if (deviceName.contains("Keyboard"))
            handleKeyboardMessage(message, deviceIdentifier);
        else if (deviceName.contains("Pad"))
            handlePadMessage(message, deviceIdentifier);
        else if (deviceName.contains("Fader"))
            handleFaderMessage(message, deviceIdentifier);
        else
            handleGenericMessage(message, deviceIdentifier);
    }
    
    void handleKeyboardMessage(const juce::MidiMessage& msg, const juce::String& deviceId)
    {
        if (msg.isNoteOn())
        {
            // Process keyboard note on
            int note = msg.getNoteNumber();
            float velocity = msg.getVelocity();
            // ... handle note
        }
    }
    
    void handlePadMessage(const juce::MidiMessage& msg, const juce::String& deviceId)
    {
        // Handle pad controller (e.g., drum pads)
    }
    
    void handleFaderMessage(const juce::MidiMessage& msg, const juce::String& deviceId)
    {
        if (msg.isController())
        {
            int ccNumber = msg.getControllerNumber();
            int ccValue = msg.getControllerValue();
            // ... handle fader movement
        }
    }
    
    void handleGenericMessage(const juce::MidiMessage& msg, const juce::String& deviceId)
    {
        // Handle unknown device
    }
    
    juce::AudioDeviceManager deviceManager;
    juce::Array<juce::MidiDeviceInfo> enabledDevices;
};
```

### Strategy 2: Multiple Callbacks (One Per Device)

Use separate callback objects for different devices for better organization.

```cpp
// Base class for device-specific handlers
class DeviceSpecificMidiHandler : public juce::MidiInputCallback
{
public:
    DeviceSpecificMidiHandler(const juce::String& deviceId) 
        : deviceIdentifier(deviceId) {}
    
    virtual ~DeviceSpecificMidiHandler() = default;
    
protected:
    juce::String deviceIdentifier;
};

// Keyboard-specific handler
class KeyboardMidiHandler : public DeviceSpecificMidiHandler
{
public:
    using DeviceSpecificMidiHandler::DeviceSpecificMidiHandler;
    
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        if (message.isNoteOn())
            onNoteOn.callAsync(message.getNoteNumber(), message.getVelocity());
        else if (message.isNoteOff())
            onNoteOff.callAsync(message.getNoteNumber());
    }
    
    // Thread-safe callbacks
    juce::ListenerList<NoteListener> noteListeners;
    
    struct NoteListener
    {
        virtual ~NoteListener() = default;
        virtual void noteOn(int noteNumber, float velocity) = 0;
        virtual void noteOff(int noteNumber) = 0;
    };
    
private:
    juce::AsyncUpdater onNoteOn;
    juce::AsyncUpdater onNoteOff;
};

// Fader controller handler
class FaderMidiHandler : public DeviceSpecificMidiHandler
{
public:
    using DeviceSpecificMidiHandler::DeviceSpecificMidiHandler;
    
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        if (message.isController())
        {
            int ccNumber = message.getControllerNumber();
            int ccValue = message.getControllerValue();
            
            // Store the latest CC values
            ccValues[ccNumber] = ccValue;
            
            // Notify listeners
            for (auto* listener : ccListeners)
                listener->controllerChanged(ccNumber, ccValue);
        }
    }
    
    int getCCValue(int ccNumber) const 
    { 
        return ccValues[ccNumber]; 
    }
    
    struct CCListener
    {
        virtual ~CCListener() = default;
        virtual void controllerChanged(int ccNumber, int value) = 0;
    };
    
    juce::ListenerList<CCListener> ccListeners;
    
private:
    std::map<int, int> ccValues;
};

// Main application manages all handlers
class MultiDeviceApplication
{
public:
    MultiDeviceApplication()
    {
        setupMidiDevices();
    }
    
    ~MultiDeviceApplication()
    {
        // Clean up all device callbacks
        for (auto& [deviceId, handler] : deviceHandlers)
            deviceManager.removeMidiInputDeviceCallback(deviceId, handler.get());
    }
    
private:
    void setupMidiDevices()
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        
        for (const auto& device : devices)
        {
            // Enable device
            if (!deviceManager.isMidiInputDeviceEnabled(device.identifier))
                deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            
            // Create appropriate handler based on device type
            std::unique_ptr<DeviceSpecificMidiHandler> handler;
            
            if (device.name.contains("Keyboard"))
                handler = std::make_unique<KeyboardMidiHandler>(device.identifier);
            else if (device.name.contains("Fader") || device.name.contains("Control"))
                handler = std::make_unique<FaderMidiHandler>(device.identifier);
            else
                handler = std::make_unique<DeviceSpecificMidiHandler>(device.identifier);
            
            // Register callback
            deviceManager.addMidiInputDeviceCallback(device.identifier, handler.get());
            
            // Store handler
            deviceHandlers[device.identifier] = std::move(handler);
        }
    }
    
    juce::AudioDeviceManager deviceManager;
    std::map<juce::String, std::unique_ptr<DeviceSpecificMidiHandler>> deviceHandlers;
};
```

### Strategy 3: Channel-Based Routing

Use MIDI channels to distinguish between different controllers.

```cpp
class ChannelBasedMidiRouter : private juce::MidiInputCallback
{
public:
    ChannelBasedMidiRouter()
    {
        // Initialize 16 channel handlers
        for (int i = 0; i < 16; ++i)
            channelHandlers[i] = std::make_unique<ChannelHandler>(i + 1);
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        int channel = message.getChannel();
        
        if (channel >= 1 && channel <= 16)
        {
            channelHandlers[channel - 1]->processMessage(message);
        }
    }
    
    class ChannelHandler
    {
    public:
        ChannelHandler(int channelNum) : channel(channelNum) {}
        
        void processMessage(const juce::MidiMessage& message)
        {
            if (message.isNoteOn())
                activeNotes.insert(message.getNoteNumber());
            else if (message.isNoteOff())
                activeNotes.erase(message.getNoteNumber());
            else if (message.isController())
                ccValues[message.getControllerNumber()] = message.getControllerValue();
        }
        
        bool isNoteActive(int noteNumber) const 
        { 
            return activeNotes.count(noteNumber) > 0; 
        }
        
        int getCCValue(int ccNumber) const
        {
            auto it = ccValues.find(ccNumber);
            return it != ccValues.end() ? it->second : 0;
        }
        
    private:
        int channel;
        std::set<int> activeNotes;
        std::map<int, int> ccValues;
    };
    
    std::array<std::unique_ptr<ChannelHandler>, 16> channelHandlers;
};
```

---

## MIDI Message Types and Parsing

### Complete Message Type Reference

```cpp
static juce::String getMidiMessageDescription(const juce::MidiMessage& m)
{
    // Channel Voice Messages
    if (m.isNoteOn())
        return juce::String::formatted("Note On: %d, Velocity: %d, Channel: %d",
                                      m.getNoteNumber(), 
                                      (int)(m.getVelocity() * 127), 
                                      m.getChannel());
    
    if (m.isNoteOff())
        return juce::String::formatted("Note Off: %d, Channel: %d",
                                      m.getNoteNumber(), 
                                      m.getChannel());
    
    if (m.isController())
        return juce::String::formatted("CC %d (%s): %d, Channel: %d",
                                      m.getControllerNumber(),
                                      juce::MidiMessage::getControllerName(m.getControllerNumber()),
                                      m.getControllerValue(),
                                      m.getChannel());
    
    if (m.isProgramChange())
        return juce::String::formatted("Program Change: %d, Channel: %d",
                                      m.getProgramChangeNumber(),
                                      m.getChannel());
    
    if (m.isPitchWheel())
        return juce::String::formatted("Pitch Wheel: %d (14-bit), Channel: %d",
                                      m.getPitchWheelValue(),
                                      m.getChannel());
    
    if (m.isAftertouch())
        return juce::String::formatted("Polyphonic Aftertouch: Note %d, Pressure: %d, Channel: %d",
                                      m.getNoteNumber(),
                                      m.getAfterTouchValue(),
                                      m.getChannel());
    
    if (m.isChannelPressure())
        return juce::String::formatted("Channel Pressure: %d, Channel: %d",
                                      m.getChannelPressureValue(),
                                      m.getChannel());
    
    // Channel Mode Messages
    if (m.isAllNotesOff())
        return juce::String::formatted("All Notes Off, Channel: %d", m.getChannel());
    
    if (m.isAllSoundOff())
        return juce::String::formatted("All Sound Off, Channel: %d", m.getChannel());
    
    // System Messages
    if (m.isMidiClock())
        return "MIDI Clock";
    
    if (m.isMidiStart())
        return "MIDI Start";
    
    if (m.isMidiStop())
        return "MIDI Stop";
    
    if (m.isMidiContinue())
        return "MIDI Continue";
    
    if (m.isActiveSense())
        return "Active Sense";
    
    // SysEx
    if (m.isSysEx())
        return juce::String::formatted("SysEx: %d bytes", m.getSysExDataSize());
    
    // Meta Events (from MIDI files)
    if (m.isMetaEvent())
        return juce::String::formatted("Meta Event: Type %d", m.getMetaEventType());
    
    // Unknown
    return "Unknown MIDI Message: " + juce::String::toHexString(m.getRawData(), m.getRawDataSize());
}
```

### Common Control Change Numbers

```cpp
namespace MidiCC
{
    enum
    {
        BankSelect = 0,
        ModWheel = 1,
        BreathController = 2,
        FootController = 4,
        PortamentoTime = 5,
        DataEntry = 6,
        Volume = 7,
        Balance = 8,
        Pan = 10,
        Expression = 11,
        Sustain = 64,
        Portamento = 65,
        Sostenuto = 66,
        SoftPedal = 67,
        LegatoFootswitch = 68,
        Hold2 = 69,
        FilterResonance = 71,
        ReleaseTime = 72,
        AttackTime = 73,
        Brightness = 74,
        DecayTime = 75,
        VibratoRate = 76,
        VibratoDepth = 77,
        VibratoDelay = 78,
        ReverbSend = 91,
        ChorusSend = 93,
        DelaySend = 94,
        AllSoundOff = 120,
        ResetAllControllers = 121,
        AllNotesOff = 123
    };
}

void handleControlChange(const juce::MidiMessage& m)
{
    int ccNumber = m.getControllerNumber();
    int value = m.getControllerValue();
    
    switch (ccNumber)
    {
        case MidiCC::ModWheel:
            setModulation(value / 127.0f);
            break;
            
        case MidiCC::Volume:
            setVolume(value / 127.0f);
            break;
            
        case MidiCC::Pan:
            setPan((value - 64) / 64.0f);  // -1 to +1
            break;
            
        case MidiCC::Sustain:
            setSustainPedal(value >= 64);  // On/Off at threshold
            break;
            
        // ... handle other CCs
    }
}
```

---

## Advanced Multi-Device Architecture

### Complete Production-Ready System

```cpp
// ========================================================================
// MidiDeviceManager.h
// ========================================================================

class MidiDeviceManager : public juce::DeletedAtShutdown
{
public:
    MidiDeviceManager();
    ~MidiDeviceManager() override;
    
    // Singleton access
    static MidiDeviceManager& getInstance();
    
    // Device management
    void scanDevices();
    void enableDevice(const juce::String& identifier);
    void disableDevice(const juce::String& identifier);
    void enableAllDevices();
    void disableAllDevices();
    
    // Get device information
    juce::Array<juce::MidiDeviceInfo> getAvailableDevices() const;
    juce::Array<juce::String> getEnabledDeviceIdentifiers() const;
    bool isDeviceEnabled(const juce::String& identifier) const;
    
    // Routing
    void routeDeviceToHandler(const juce::String& deviceId, 
                             juce::MidiInputCallback* callback);
    void removeDeviceRoute(const juce::String& deviceId);
    
    // Listeners
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void midiDeviceListChanged() = 0;
        virtual void midiDeviceEnabled(const juce::String& deviceId) = 0;
        virtual void midiDeviceDisabled(const juce::String& deviceId) = 0;
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    
private:
    struct DeviceConnection
    {
        juce::String identifier;
        juce::String name;
        bool enabled = false;
        juce::MidiInputCallback* callback = nullptr;
    };
    
    juce::AudioDeviceManager audioDeviceManager;
    std::map<juce::String, DeviceConnection> devices;
    juce::ListenerList<Listener> listeners;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDeviceManager)
};

// ========================================================================
// MidiMessageBuffer.h - Thread-safe message buffering
// ========================================================================

class MidiMessageBuffer
{
public:
    void addMessage(const juce::MidiMessage& message, const juce::String& source)
    {
        const juce::ScopedLock lock(bufferLock);
        buffer.addEvent(message, messageCount++);
        sources[messageCount - 1] = source;
    }
    
    void swapWith(juce::MidiBuffer& other, std::map<int, juce::String>& sourceMap)
    {
        const juce::ScopedLock lock(bufferLock);
        buffer.swapWith(other);
        sourceMap.swap(sources);
        messageCount = 0;
    }
    
    void clear()
    {
        const juce::ScopedLock lock(bufferLock);
        buffer.clear();
        sources.clear();
        messageCount = 0;
    }
    
private:
    juce::CriticalSection bufferLock;
    juce::MidiBuffer buffer;
    std::map<int, juce::String> sources;
    int messageCount = 0;
};

// ========================================================================
// MidiRouter.h - Message routing system
// ========================================================================

class MidiRouter : public juce::MidiInputCallback
{
public:
    // Route configuration
    struct Route
    {
        juce::String name;
        std::function<bool(const juce::MidiMessage&, const juce::String&)> filter;
        juce::MidiInputCallback* destination;
        bool enabled = true;
    };
    
    void addRoute(const juce::String& name, 
                 std::function<bool(const juce::MidiMessage&, const juce::String&)> filter,
                 juce::MidiInputCallback* destination)
    {
        Route route;
        route.name = name;
        route.filter = std::move(filter);
        route.destination = destination;
        routes.add(route);
    }
    
    void removeRoute(const juce::String& name)
    {
        routes.removeIf([&name](const Route& r) { return r.name == name; });
    }
    
    void setRouteEnabled(const juce::String& name, bool enabled)
    {
        for (auto& route : routes)
            if (route.name == name)
                route.enabled = enabled;
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        juce::String deviceName = source->getName();
        
        for (auto& route : routes)
        {
            if (route.enabled && 
                route.destination && 
                route.filter(message, deviceName))
            {
                route.destination->handleIncomingMidiMessage(source, message);
            }
        }
    }
    
    juce::Array<Route> routes;
};

// ========================================================================
// Example Usage
// ========================================================================

class MyApplication : public MidiDeviceManager::Listener
{
public:
    MyApplication()
    {
        auto& manager = MidiDeviceManager::getInstance();
        manager.addListener(this);
        manager.enableAllDevices();
        
        setupRouting();
    }
    
    ~MyApplication()
    {
        MidiDeviceManager::getInstance().removeListener(this);
    }
    
private:
    void setupRouting()
    {
        // Route all note messages from keyboards to synthesizer
        router.addRoute("keyboard_to_synth",
            [](const juce::MidiMessage& msg, const juce::String& device) {
                return (msg.isNoteOn() || msg.isNoteOff()) && 
                       device.contains("Keyboard");
            },
            &synthHandler);
        
        // Route CC messages from control surfaces to mixer
        router.addRoute("cc_to_mixer",
            [](const juce::MidiMessage& msg, const juce::String& device) {
                return msg.isController() && 
                       (device.contains("Control") || device.contains("Fader"));
            },
            &mixerHandler);
        
        // Route drum pad messages to drum machine
        router.addRoute("pads_to_drums",
            [](const juce::MidiMessage& msg, const juce::String& device) {
                return msg.isNoteOn() && device.contains("Pad");
            },
            &drumHandler);
    }
    
    // MidiDeviceManager::Listener
    void midiDeviceListChanged() override
    {
        DBG("MIDI device list changed");
    }
    
    void midiDeviceEnabled(const juce::String& deviceId) override
    {
        DBG("Device enabled: " + deviceId);
    }
    
    void midiDeviceDisabled(const juce::String& deviceId) override
    {
        DBG("Device disabled: " + deviceId);
    }
    
    MidiRouter router;
    SynthMidiHandler synthHandler;
    MixerMidiHandler mixerHandler;
    DrumMidiHandler drumHandler;
};
```

---

## Best Practices and Optimization

### 1. Thread Safety

**Critical Rule:** MIDI callbacks run on the MIDI thread, NOT the message thread!

```cpp
class ThreadSafeMidiHandler : private juce::MidiInputCallback
{
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // ❌ BAD: Direct GUI update (crashes!)
        // someLabel.setText("Got MIDI!");
        
        // ❌ BAD: Non-atomic data access
        // myString = "new value";
        
        // ✅ GOOD: Lock-free atomic operations
        noteCount.fetch_add(1, std::memory_order_relaxed);
        
        // ✅ GOOD: Lock-free queue
        messageQueue.push(message);
        
        // ✅ GOOD: Post to message thread
        juce::MessageManager::callAsync([this, message]() {
            // Now safe to update GUI
            updateDisplay(message);
        });
        
        // ✅ GOOD: Use AsyncUpdater
        triggerAsyncUpdate();
    }
    
    void handleAsyncUpdate() override
    {
        // Called on message thread - safe for GUI updates
        processQueuedMessages();
    }
    
    std::atomic<int> noteCount{0};
    juce::AbstractFifo messageQueue;
    juce::AsyncUpdater asyncUpdater;
};
```

### 2. Performance Optimization

```cpp
class OptimizedMidiHandler
{
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // ✅ Early rejection for unneeded messages
        if (message.isActiveSense() || message.isMidiClock())
            return;
        
        // ✅ Use switch on message type (faster than if-else chain)
        const auto* data = message.getRawData();
        const int statusByte = data[0] & 0xF0;
        
        switch (statusByte)
        {
            case 0x90:  // Note On
                if (data[2] > 0)  // Velocity > 0
                    handleNoteOn(data[1], data[2]);
                else
                    handleNoteOff(data[1]);
                break;
                
            case 0x80:  // Note Off
                handleNoteOff(data[1]);
                break;
                
            case 0xB0:  // Control Change
                handleControlChange(data[1], data[2]);
                break;
                
            // ... other cases
        }
    }
    
    // ✅ Inline hot path functions
    inline void handleNoteOn(int note, int velocity)
    {
        // Fast note-on processing
    }
};
```

### 3. Memory Management

```cpp
class MidiHandlerWithSmartMemory
{
public:
    ~MidiHandlerWithSmartMemory()
    {
        // ✅ Clean up callbacks in destructor
        auto& manager = MidiDeviceManager::getInstance();
        for (const auto& deviceId : registeredDevices)
            manager.removeDeviceRoute(deviceId);
    }
    
private:
    // ✅ Use smart pointers for owned resources
    std::unique_ptr<juce::MidiKeyboardState> keyboardState;
    std::vector<std::unique_ptr<DeviceHandler>> handlers;
    
    // ✅ Track registered devices for cleanup
    std::vector<juce::String> registeredDevices;
};
```

### 4. Error Handling

```cpp
class RobustMidiHandler
{
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        try
        {
            // ✅ Validate source
            if (source == nullptr)
            {
                jassertfalse;
                return;
            }
            
            // ✅ Validate message
            if (message.getRawDataSize() == 0)
            {
                DBG("Received empty MIDI message");
                return;
            }
            
            // ✅ Bounds checking
            if (message.isNoteOn() || message.isNoteOff())
            {
                int note = message.getNoteNumber();
                if (note < 0 || note > 127)
                {
                    DBG("Invalid note number: " + juce::String(note));
                    return;
                }
            }
            
            // Process message
            processMessage(message);
        }
        catch (const std::exception& e)
        {
            // ❌ Never throw from callback!
            DBG("Exception in MIDI callback: " + juce::String(e.what()));
        }
    }
};
```

### 5. Device Hot-Plugging

```cpp
class HotPlugMidiManager : public juce::Timer
{
public:
    HotPlugMidiManager()
    {
        updateDeviceList();
        startTimer(1000);  // Check every second
    }
    
private:
    void timerCallback() override
    {
        auto currentDevices = juce::MidiInput::getAvailableDevices();
        
        if (currentDevices.size() != lastDeviceCount)
        {
            updateDeviceList();
            lastDeviceCount = currentDevices.size();
            
            // Notify listeners
            listeners.call(&Listener::midiDevicesChanged);
        }
    }
    
    void updateDeviceList()
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        
        // Remove disconnected devices
        for (auto it = activeDevices.begin(); it != activeDevices.end();)
        {
            bool stillConnected = false;
            for (const auto& device : devices)
            {
                if (device.identifier == it->first)
                {
                    stillConnected = true;
                    break;
                }
            }
            
            if (!stillConnected)
            {
                deviceManager.removeMidiInputDeviceCallback(it->first, this);
                it = activeDevices.erase(it);
            }
            else
                ++it;
        }
        
        // Add new devices
        for (const auto& device : devices)
        {
            if (activeDevices.find(device.identifier) == activeDevices.end())
            {
                deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
                deviceManager.addMidiInputDeviceCallback(device.identifier, this);
                activeDevices[device.identifier] = device.name;
            }
        }
    }
    
    juce::AudioDeviceManager deviceManager;
    std::map<juce::String, juce::String> activeDevices;
    int lastDeviceCount = 0;
    juce::ListenerList<Listener> listeners;
};
```

---

## Practical Implementation Examples

### Example 1: Multi-Keyboard Layer System

```cpp
class MultiKeyboardLayerSystem
{
public:
    struct KeyboardLayer
    {
        juce::String name;
        juce::String deviceIdentifier;
        int transpose = 0;
        float volume = 1.0f;
        bool enabled = true;
        int midiChannelOut = 1;
    };
    
    void addLayer(const KeyboardLayer& layer)
    {
        layers.add(layer);
        
        // Register MIDI callback for this device
        auto& manager = MidiDeviceManager::getInstance();
        manager.routeDeviceToHandler(layer.deviceIdentifier, this);
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        juce::String deviceId = source->getIdentifier();
        
        // Find which layer this device belongs to
        for (const auto& layer : layers)
        {
            if (layer.deviceIdentifier == deviceId && layer.enabled)
            {
                processLayerMessage(message, layer);
            }
        }
    }
    
    void processLayerMessage(const juce::MidiMessage& message, const KeyboardLayer& layer)
    {
        if (message.isNoteOn())
        {
            int transposedNote = juce::jlimit(0, 127, 
                message.getNoteNumber() + layer.transpose);
            
            float scaledVelocity = message.getVelocity() * layer.volume;
            
            auto newMessage = juce::MidiMessage::noteOn(
                layer.midiChannelOut,
                transposedNote,
                scaledVelocity
            );
            
            // Send to synthesizer
            synthProcessor->processMessage(newMessage);
        }
        else if (message.isNoteOff())
        {
            int transposedNote = juce::jlimit(0, 127, 
                message.getNoteNumber() + layer.transpose);
            
            auto newMessage = juce::MidiMessage::noteOff(
                layer.midiChannelOut,
                transposedNote
            );
            
            synthProcessor->processMessage(newMessage);
        }
    }
    
    juce::Array<KeyboardLayer> layers;
    SynthProcessor* synthProcessor = nullptr;
};
```

### Example 2: MIDI Learn System

```cpp
class MidiLearnSystem : public juce::MidiInputCallback
{
public:
    void startLearning(const juce::String& parameterName)
    {
        learningParameter = parameterName;
        isLearning = true;
    }
    
    void stopLearning()
    {
        isLearning = false;
        learningParameter.clear();
    }
    
    void bindParameter(const juce::String& parameterName, 
                      const juce::String& deviceId,
                      int ccNumber,
                      float minValue = 0.0f,
                      float maxValue = 1.0f)
    {
        Binding binding;
        binding.parameterName = parameterName;
        binding.deviceIdentifier = deviceId;
        binding.ccNumber = ccNumber;
        binding.minValue = minValue;
        binding.maxValue = maxValue;
        
        bindings[parameterName] = binding;
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        if (message.isController())
        {
            if (isLearning)
            {
                // Learn this CC
                bindParameter(learningParameter,
                            source->getIdentifier(),
                            message.getControllerNumber());
                stopLearning();
                
                DBG("Learned: " + learningParameter + " -> CC" + 
                    juce::String(message.getControllerNumber()));
            }
            else
            {
                // Process existing bindings
                processControlChange(source->getIdentifier(), 
                                   message.getControllerNumber(),
                                   message.getControllerValue());
            }
        }
    }
    
    void processControlChange(const juce::String& deviceId, int ccNumber, int value)
    {
        for (const auto& [paramName, binding] : bindings)
        {
            if (binding.deviceIdentifier == deviceId && 
                binding.ccNumber == ccNumber)
            {
                // Map CC value (0-127) to parameter range
                float normalizedValue = value / 127.0f;
                float paramValue = binding.minValue + 
                    normalizedValue * (binding.maxValue - binding.minValue);
                
                // Update parameter
                if (auto* param = getParameter(paramName))
                    param->setValue(paramValue);
            }
        }
    }
    
    struct Binding
    {
        juce::String parameterName;
        juce::String deviceIdentifier;
        int ccNumber;
        float minValue;
        float maxValue;
    };
    
    std::map<juce::String, Binding> bindings;
    bool isLearning = false;
    juce::String learningParameter;
};
```

### Example 3: MIDI Monitor with Filtering

```cpp
class MidiMonitor : public juce::Component,
                    private juce::MidiInputCallback,
                    private juce::Timer
{
public:
    MidiMonitor()
    {
        addAndMakeVisible(messageList);
        addAndMakeVisible(filterCombo);
        
        filterCombo.addItem("All Messages", 1);
        filterCombo.addItem("Notes Only", 2);
        filterCombo.addItem("CC Only", 3);
        filterCombo.addItem("Exclude Clock/Active Sense", 4);
        filterCombo.setSelectedId(4);
        
        startTimer(50);  // Update GUI at 20 Hz
    }
    
    void connectToAllDevices()
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        
        for (const auto& device : devices)
        {
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        }
    }
    
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // Apply filter
        int filterType = filterCombo.getSelectedId();
        
        bool shouldDisplay = true;
        
        switch (filterType)
        {
            case 2:  // Notes only
                shouldDisplay = message.isNoteOn() || message.isNoteOff();
                break;
                
            case 3:  // CC only
                shouldDisplay = message.isController();
                break;
                
            case 4:  // Exclude system messages
                shouldDisplay = !message.isMidiClock() && !message.isActiveSense();
                break;
                
            default:  // All messages
                break;
        }
        
        if (shouldDisplay)
        {
            MessageInfo info;
            info.timestamp = juce::Time::getMillisecondCounterHiRes();
            info.device = source->getName();
            info.message = message;
            
            const juce::ScopedLock lock(messageLock);
            pendingMessages.add(info);
            
            // Limit buffer size
            if (pendingMessages.size() > 1000)
                pendingMessages.remove(0);
        }
    }
    
    void timerCallback() override
    {
        // Move pending messages to display
        juce::Array<MessageInfo> messagesToDisplay;
        
        {
            const juce::ScopedLock lock(messageLock);
            messagesToDisplay.swapWith(pendingMessages);
        }
        
        // Update GUI
        for (const auto& info : messagesToDisplay)
        {
            juce::String text = juce::String(info.timestamp, 3) + " | " +
                               info.device + " | " +
                               getMidiMessageDescription(info.message);
            
            messageList.addItemToList(text);
        }
    }
    
    struct MessageInfo
    {
        double timestamp;
        juce::String device;
        juce::MidiMessage message;
    };
    
    juce::AudioDeviceManager deviceManager;
    juce::ListBox messageList;
    juce::ComboBox filterCombo;
    juce::Array<MessageInfo> pendingMessages;
    juce::CriticalSection messageLock;
};
```

---

## Troubleshooting Common Issues

### Issue 1: No MIDI Messages Received

**Symptoms:** MIDI device connected but no messages appear

**Checklist:**
```cpp
void debugMidiSetup()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    
    DBG("=== MIDI Debug Info ===");
    DBG("Available devices: " + juce::String(devices.size()));
    
    for (const auto& device : devices)
    {
        DBG("Device: " + device.name);
        DBG("  Identifier: " + device.identifier);
        
        bool enabled = deviceManager.isMidiInputDeviceEnabled(device.identifier);
        DBG("  Enabled: " + juce::String(enabled ? "YES" : "NO"));
        
        // Try to enable
        if (!enabled)
        {
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            DBG("  -> Enabled device");
        }
    }
    
    // Verify callback is registered
    // (Add debug output in your handleIncomingMidiMessage)
}
```

### Issue 2: Crashes on Device Disconnect

**Problem:** Application crashes when MIDI device is unplugged

**Solution:**
```cpp
class SafeMidiHandler : public juce::MidiInputCallback
{
public:
    ~SafeMidiHandler() override
    {
        // ✅ ALWAYS remove callbacks in destructor
        removeAllCallbacks();
    }
    
private:
    void removeAllCallbacks()
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        
        for (const auto& device : devices)
        {
            try
            {
                deviceManager.removeMidiInputDeviceCallback(
                    device.identifier, this);
            }
            catch (...)
            {
                // Ignore errors - device may already be gone
            }
        }
    }
    
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // ✅ Check for null pointer
        if (source == nullptr)
            return;
        
        // ✅ Don't cache the source pointer
        juce::String deviceName = source->getName();  // Copy name immediately
        
        // Process message using copied data
        processMessage(message, deviceName);
    }
};
```

### Issue 3: GUI Freezing

**Problem:** GUI becomes unresponsive when processing MIDI

**Solution:**
```cpp
class NonBlockingMidiHandler : public juce::MidiInputCallback
{
private:
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // ❌ DON'T DO THIS (blocks MIDI thread!)
        // juce::MessageManager::getInstance()->callAsync([this, message]() {
        //     processLongOperation(message);  // Takes 50ms
        // });
        
        // ✅ DO THIS (queue and process on separate thread)
        messageQueue.push(message);
        asyncProcessor.triggerAsyncUpdate();
    }
    
    class AsyncProcessor : public juce::Thread
    {
    public:
        AsyncProcessor() : juce::Thread("MIDI Processor") 
        {
            startThread();
        }
        
        ~AsyncProcessor() override
        {
            signalThreadShouldExit();
            waitForThreadToExit(1000);
        }
        
        void run() override
        {
            while (!threadShouldExit())
            {
                juce::MidiMessage message;
                
                if (messageQueue.pop(message))
                {
                    processMessage(message);
                }
                else
                {
                    wait(1);  // Sleep if no messages
                }
            }
        }
    };
    
    LockFreeQueue<juce::MidiMessage> messageQueue;
    AsyncProcessor asyncProcessor;
};
```

### Issue 4: Missing Messages

**Problem:** Some MIDI messages not received

**Causes and Solutions:**

```cpp
// Cause 1: Active Sense and Clock messages flooding
void handleIncomingMidiMessage(juce::MidiInput* source, 
                               const juce::MidiMessage& message) override
{
    // ✅ Filter out system realtime messages if not needed
    if (message.isActiveSense() || message.isMidiClock())
        return;
    
    processMessage(message);
}

// Cause 2: Buffer overflow
class BufferedMidiHandler
{
private:
    // ✅ Use appropriately sized buffer
    static constexpr int MaxBufferSize = 1024;
    
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        if (buffer.size() < MaxBufferSize)
        {
            buffer.push(message);
        }
        else
        {
            DBG("WARNING: MIDI buffer overflow!");
            ++droppedMessageCount;
        }
    }
    
    std::atomic<int> droppedMessageCount{0};
};
```

---

## Reference: Complete Working Example

Here's a complete, production-ready multi-device MIDI handler:

```cpp
// MultiDeviceMidiComponent.h
#pragma once

#include <JuceHeader.h>

class MultiDeviceMidiComponent : public juce::Component,
                                 private juce::MidiInputCallback,
                                 private juce::Timer
{
public:
    MultiDeviceMidiComponent()
    {
        setupUI();
        scanAndEnableDevices();
        startTimer(100);  // Update stats 10 times per second
    }
    
    ~MultiDeviceMidiComponent() override
    {
        stopTimer();
        disableAllDevices();
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
    }
    
    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        
        deviceListLabel.setBounds(area.removeFromTop(30));
        deviceList.setBounds(area.removeFromTop(100));
        
        statsLabel.setBounds(area.removeFromTop(30));
        messageDisplay.setBounds(area);
    }
    
private:
    void setupUI()
    {
        addAndMakeVisible(deviceListLabel);
        deviceListLabel.setText("Connected MIDI Devices:", juce::dontSendNotification);
        
        addAndMakeVisible(deviceList);
        deviceList.setMultiLine(true);
        deviceList.setReadOnly(true);
        
        addAndMakeVisible(statsLabel);
        statsLabel.setText("MIDI Statistics:", juce::dontSendNotification);
        
        addAndMakeVisible(messageDisplay);
        messageDisplay.setMultiLine(true);
        messageDisplay.setReadOnly(true);
        messageDisplay.setScrollbarsShown(true);
    }
    
    void scanAndEnableDevices()
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        
        juce::String deviceText;
        
        for (const auto& device : devices)
        {
            if (!deviceManager.isMidiInputDeviceEnabled(device.identifier))
                deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            
            deviceManager.addMidiInputDeviceCallback(device.identifier, this);
            
            deviceText += device.name + "\n";
            enabledDevices.add(device.identifier);
        }
        
        deviceList.setText(deviceText.isEmpty() ? "No MIDI devices found" : deviceText);
    }
    
    void disableAllDevices()
    {
        for (const auto& deviceId : enabledDevices)
        {
            deviceManager.removeMidiInputDeviceCallback(deviceId, this);
        }
        enabledDevices.clear();
    }
    
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override
    {
        // Update statistics
        totalMessageCount++;
        
        if (message.isNoteOn())
            noteOnCount++;
        else if (message.isNoteOff())
            noteOffCount++;
        else if (message.isController())
            ccCount++;
        
        // Queue message for display
        MessageInfo info;
        info.deviceName = source->getName();
        info.message = message;
        info.timestamp = juce::Time::getMillisecondCounterHiRes();
        
        const juce::ScopedLock lock(messageLock);
        pendingMessages.add(info);
        
        // Limit queue size
        if (pendingMessages.size() > 100)
            pendingMessages.remove(0);
    }
    
    void timerCallback() override
    {
        // Update statistics display
        juce::String stats;
        stats << "Total Messages: " << totalMessageCount << "\n";
        stats << "Note On: " << noteOnCount << "\n";
        stats << "Note Off: " << noteOffCount << "\n";
        stats << "Control Change: " << ccCount;
        
        statsLabel.setText(stats, juce::dontSendNotification);
        
        // Update message display
        juce::Array<MessageInfo> messagesToShow;
        {
            const juce::ScopedLock lock(messageLock);
            messagesToShow = pendingMessages;
            pendingMessages.clear();
        }
        
        for (const auto& info : messagesToShow)
        {
            juce::String messageText;
            messageText << "[" << info.deviceName << "] ";
            messageText << getMidiMessageDescription(info.message);
            messageText << "\n";
            
            messageDisplay.moveCaretToEnd();
            messageDisplay.insertTextAtCaret(messageText);
        }
    }
    
    static juce::String getMidiMessageDescription(const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            return "Note On: " + juce::String(m.getNoteNumber()) + 
                   " Vel: " + juce::String((int)(m.getVelocity() * 127));
        
        if (m.isNoteOff())
            return "Note Off: " + juce::String(m.getNoteNumber());
        
        if (m.isController())
            return "CC " + juce::String(m.getControllerNumber()) + 
                   ": " + juce::String(m.getControllerValue());
        
        if (m.isPitchWheel())
            return "Pitch Wheel: " + juce::String(m.getPitchWheelValue());
        
        return "Other MIDI Message";
    }
    
    struct MessageInfo
    {
        juce::String deviceName;
        juce::MidiMessage message;
        double timestamp;
    };
    
    juce::AudioDeviceManager deviceManager;
    juce::StringArray enabledDevices;
    
    juce::Label deviceListLabel;
    juce::TextEditor deviceList;
    juce::Label statsLabel;
    juce::TextEditor messageDisplay;
    
    std::atomic<int> totalMessageCount{0};
    std::atomic<int> noteOnCount{0};
    std::atomic<int> noteOffCount{0};
    std::atomic<int> ccCount{0};
    
    juce::Array<MessageInfo> pendingMessages;
    juce::CriticalSection messageLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiDeviceMidiComponent)
};
```

---

## Summary

### Key Takeaways

1. **Use `AudioDeviceManager`** for centralized device management
2. **Implement `MidiInputCallback`** to receive MIDI messages
3. **Track device sources** using the `MidiInput* source` parameter
4. **Thread safety is critical** - MIDI callbacks run on MIDI thread
5. **Use `CallbackMessage` or `AsyncUpdater`** for GUI updates
6. **Enable multiple devices simultaneously** by calling `addMidiInputDeviceCallback` for each
7. **Filter and route messages** based on device identifier or name
8. **Handle device hot-plugging** by monitoring device list changes
9. **Always clean up callbacks** in destructors

### Additional Resources

- [JUCE API Documentation](https://docs.juce.com/master/index.html) [[memory:8727116]]
- JUCE Tutorial: Handling MIDI Events (included in this directory)
- JUCE Forum: https://forum.juce.com/

### Next Steps

1. Study the provided tutorial code
2. Implement a simple multi-device MIDI monitor
3. Experiment with routing different devices to different handlers
4. Build a MIDI learn system for parameter mapping
5. Explore advanced features like MIDI clock sync and SysEx messages

---

*This guide was created based on the official JUCE MIDI handling tutorial and extended with production-ready patterns for handling multiple MIDI controllers simultaneously.*

