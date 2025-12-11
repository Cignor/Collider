# Signal Integration Architecture - Conceptual Design

## Overview

This document explores how to extend the current MIDI-centric signal architecture to support multiple signal protocols (OSC, and potentially others) in a modular, maintainable way.

## Current MIDI Implementation Analysis

### Architecture Pattern

The codebase uses a **centralized device manager + distributed module processors** pattern:

1. **MidiDeviceManager** (Central Hub)
   - Manages multiple MIDI input devices
   - Provides `MidiInputCallback` interface
   - Buffers messages with device source metadata
   - Thread-safe message queue (`CriticalSection` protected)
   - Activity tracking for UI visualization
   - Hot-plug detection via `Timer`

2. **ModularSynthProcessor** (Signal Router)
   - Receives MIDI messages via `processMidiWithDeviceInfo()`
   - Distributes to modules through `handleDeviceSpecificMidi()` override
   - Maintains MIDI activity state for UI feedback

3. **Module Processors** (Signal Consumers)
   - Inherit from `ModuleProcessor` base class
   - Implement `handleDeviceSpecificMidi()` for device/channel filtering
   - Convert MIDI to CV/Gate (e.g., `MIDICVModuleProcessor`)

### Key Design Patterns Observed

- **Device Source Tracking**: Messages include `deviceIdentifier`, `deviceName`, `deviceIndex`
- **Thread Safety**: MIDI callbacks on MIDI thread → buffering → audio thread processing
- **Multi-Device Support**: Each device can be enabled/disabled independently
- **Activity Monitoring**: Per-device, per-channel activity tracking
- **Message Metadata**: Messages carry source information, not just raw data

## JUCE OSC Support Analysis

### Native Support Status: ✅ **READY TO IMPLEMENT**

JUCE provides complete OSC support out-of-the-box:

#### Core Classes:
- **`juce::OSCReceiver`**: Receives OSC messages over UDP
- **`juce::OSCSender`**: Sends OSC messages to hosts
- **`juce::OSCMessage`**: Represents individual OSC messages (address + arguments)
- **`juce::OSCBundle`**: Groups multiple messages for atomic delivery

#### Key Features:
- Network-based (UDP) - can communicate across machines
- Address pattern matching (e.g., `/synth/note/on`, `/cv/pitch`)
- Type-safe argument extraction (int32, float32, string, blob, etc.)
- Built-in error handling
- Thread-safe (can be used from audio thread with proper care)

#### Availability:
- Part of `juce_osc` module (included in JUCE Pro/Personal licenses)
- No external dependencies required
- Cross-platform (Windows, macOS, Linux, iOS, Android)

### Comparison: MIDI vs OSC

| Aspect | MIDI | OSC |
|--------|------|-----|
| **Transport** | Hardware/USB/software ports | Network (UDP) |
| **Addressing** | Channel-based (1-16) | String-based patterns (`/path/to/value`) |
| **Message Types** | Fixed (Note On/Off, CC, etc.) | Arbitrary (address + typed args) |
| **Range** | Local device | Network-wide (local or remote) |
| **Latency** | Very low (<1ms) | Low (network dependent, ~1-10ms) |
| **Typing** | Weak (bytes interpreted) | Strong (explicit types) |
| **Multiplexing** | 16 channels per device | Unlimited address space |
| **Device Enumeration** | OS-level MIDI devices | Network discovery or manual IP/port |

## Conceptual Architecture: Multi-Protocol Signal System

### Design Goals

1. **Unified Interface**: Similar API patterns for MIDI, OSC, and future protocols
2. **Modularity**: Each protocol can be added/removed independently
3. **Consistency**: Device/source tracking works the same way across protocols
4. **Performance**: Minimal overhead, thread-safe
5. **Extensibility**: Easy to add new protocols (e.g., Art-Net, Serial, WebSocket)

### Proposed Architecture Pattern

```
┌─────────────────────────────────────────────────────────────┐
│                    Signal Protocol Layer                     │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ MidiDevice   │  │ OSCReceiver  │  │ Future:      │      │
│  │ Manager      │  │ Manager      │  │ Art-Net, etc │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │               │
│         └──────────────────┼──────────────────┘               │
│                            │                                   │
│                   ┌────────▼────────┐                         │
│                   │ UnifiedSignal   │                         │
│                   │ Manager         │                         │
│                   └────────┬────────┘                         │
└────────────────────────────┼──────────────────────────────────┘
                             │
                   ┌─────────▼─────────┐
                   │ ModularSynth      │
                   │ Processor         │
                   │ (Signal Router)   │
                   └─────────┬─────────┘
                             │
           ┌─────────────────┼─────────────────┐
           │                 │                 │
    ┌──────▼──────┐  ┌──────▼──────┐  ┌──────▼──────┐
    │ MIDI CV     │  │ OSC CV      │  │ Other       │
    │ Module      │  │ Module      │  │ Modules     │
    └─────────────┘  └─────────────┘  └─────────────┘
```

### Core Abstraction: Unified Signal Message

**Option A: Protocol-Agnostic Message Wrapper**

```cpp
enum class SignalProtocol { MIDI, OSC, ArtNet, Serial, WebSocket };

struct UnifiedSignalMessage {
    SignalProtocol protocol;
    juce::String sourceIdentifier;  // Device name or IP:port
    juce::String sourceName;        // Human-readable name
    int sourceIndex;                // Sequential index
    
    // Protocol-specific payload
    std::variant<
        juce::MidiMessage,          // For MIDI
        juce::OSCMessage,           // For OSC
        // Future: ArtNetPacket, SerialData, etc.
    > payload;
    
    double timestamp;               // Reception time
};
```

**Option B: Keep Protocols Separate, Share Infrastructure**

Keep `MidiMessageWithDevice` as-is, create parallel `OSCMessageWithSource`, etc.
Modules can handle multiple protocols via separate overrides:
- `handleDeviceSpecificMidi()`
- `handleOSCSignal()`
- `handleArtNetSignal()`

**RECOMMENDATION: Option B** - Less abstraction overhead, clearer type safety, easier to debug.

### Proposed Structure: OSC Integration

#### 1. **OSCDeviceManager** (Mirror MidiDeviceManager pattern)

```cpp
class OSCDeviceManager : public juce::OSCReceiver::Listener<>
{
public:
    struct DeviceInfo {
        juce::String identifier;     // "IP:port" (e.g., "192.168.1.100:57120")
        juce::String name;            // Human-readable name
        int port = 57120;             // UDP receive port
        bool enabled = false;
        int deviceIndex = -1;
    };
    
    struct OSCMessageWithSource {
        juce::OSCMessage message;
        juce::String sourceIdentifier;
        juce::String sourceName;
        int deviceIndex;
        double timestamp;
    };
    
    // Similar API to MidiDeviceManager:
    void scanDevices();              // Scan network (or load config)
    void enableDevice(const juce::String& identifier);
    void disableDevice(const juce::String& identifier);
    void swapMessageBuffer(std::vector<OSCMessageWithSource>& targetBuffer);
    
private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    // ... buffering, activity tracking similar to MIDI
};
```

#### 2. **OSCCVModuleProcessor** (Mirror MIDICVModuleProcessor)

```cpp
class OSCCVModuleProcessor : public ModuleProcessor
{
public:
    // Convert OSC messages to CV/Gate outputs
    // Example OSC addresses:
    //   /cv/pitch      → float (0-1)
    //   /cv/velocity   → float (0-1)
    //   /gate/trigger  → float (0 or 1)
    //   /cv/modwheel   → float (0-1)
    
    void handleOSCSignal(const std::vector<OSCMessageWithSource>& messages) override;
    
private:
    // OSC address pattern matching
    // Parameter: which OSC address to listen to
    // Similar filtering to MIDI's device/channel selection
};
```

#### 3. **ModularSynthProcessor Extension**

```cpp
void ModularSynthProcessor::processOSCWithSourceInfo(
    const std::vector<OSCMessageWithSource>& messages)
{
    // Distribute to modules that implement handleOSCSignal()
    for (auto* module : activeModules)
        if (auto* oscHandler = dynamic_cast<OSCSignalHandler*>(module))
            oscHandler->handleOSCSignal(messages);
}
```

## Design Decisions & Questions

### Q1: OSC Discovery vs Manual Configuration

**Option A: Network Discovery (mDNS/Bonjour)**
- Auto-detect OSC devices on network
- Pros: Zero-configuration, user-friendly
- Cons: Complex, requires network library, not always reliable

**Option B: Manual IP/Port Entry**
- User enters IP address and port manually
- Pros: Simple, reliable, works always
- Cons: Requires user knowledge

**Option C: Hybrid**
- Manual entry by default
- Optional mDNS discovery for known device types
- Pros: Best of both worlds
- Cons: More code

**RECOMMENDATION: Option B initially** - Manual entry, add discovery later if needed.

### Q2: OSC Message Address Patterns

**Standard Patterns to Support:**

```
/synth/note/on          → {note: int, velocity: float}
/synth/note/off         → {note: int}
/cv/pitch               → float (0-1, maps to 1V/octave)
/cv/modwheel            → float (0-1)
/cv/aftertouch          → float (0-1)
/gate/trigger           → float (0 or 1, trigger)
/gate/hold              → float (0 or 1, gate)
/sequencer/step         → int (step number)
/transport/play         → (no args, triggers play)
/transport/stop         → (no args, triggers stop)
/bpm                    → float (beats per minute)
```

**Module Configuration:**
- Each OSC CV module can listen to specific address patterns
- Pattern matching with wildcards: `/cv/*` matches all `/cv/...` addresses
- Type checking: module validates argument types match expected

### Q3: Thread Safety Model

**OSC Reception Thread:**
- `OSCReceiver::Listener` callbacks run on a network thread (not audio thread)
- Similar to MIDI: buffer messages, swap buffer on audio thread
- Use `CriticalSection` for thread-safe buffer swapping

**Pattern Matching:**
- Perform pattern matching on network thread (non-blocking)
- Pre-filter messages before buffering (reduce audio thread work)
- Cache compiled patterns (if using regex-like matching)

### Q4: Integration with Existing MIDI System

**Coexistence Strategy:**
- OSC and MIDI run in parallel (independent)
- Modules can handle both protocols simultaneously
- Example: `MIDICVModuleProcessor` + `OSCCVModuleProcessor` can both feed same oscillator
- No conflict - both contribute to CV signals additively (with mixer/priority logic)

**UI Considerations:**
- Separate device lists for MIDI vs OSC
- Activity indicators for both protocols
- Unified signal activity visualization (merge both sources)

### Q5: Future Protocol Extensions

**Potential Protocols to Consider:**

1. **Art-Net (DMX over Ethernet)**
   - Lighting control protocol
   - Could map to CV for visual modules
   - Similar UDP-based architecture

2. **Serial/USB Communication**
   - Arduino, Teensy, custom hardware
   - Raw byte stream or custom protocol
   - Lower-level than MIDI/OSC

3. **WebSocket/HTTP**
   - Web-based controllers (mobile apps, web UIs)
   - JSON or custom binary format
   - Real-time bidirectional communication

4. **Custom UDP/TCP Protocols**
   - Proprietary hardware communication
   - Gamepad input, motion sensors, etc.

**Architecture Should:**
- Define clear interface for protocol managers
- Use consistent message buffering pattern
- Support device enumeration/discovery per protocol
- Allow modules to subscribe to multiple protocols

## Benefits of This Architecture

1. **Reuses Existing Patterns**: OSC implementation mirrors MIDI, familiar codebase
2. **No Breaking Changes**: Existing MIDI code remains unchanged
3. **Incremental Adoption**: Can add OSC support without touching MIDI
4. **Type Safety**: OSC's typed messages prevent MIDI-style byte interpretation errors
5. **Network Capability**: Control synth from remote devices (phones, tablets, other computers)
6. **Flexibility**: OSC address patterns more flexible than MIDI's 16 channels
7. **Future-Proof**: Pattern supports adding more protocols later

## Potential Challenges

1. **Network Latency**: UDP can have variable latency (1-10ms typical, up to 100ms in bad conditions)
2. **Packet Loss**: UDP is unreliable - need to handle dropped messages gracefully
3. **Network Configuration**: Firewalls, port forwarding, multicast routing
4. **Security**: OSC has no authentication - anyone on network can send messages
5. **Address Pattern Complexity**: Wildcards, multiple subscriptions, pattern conflicts
6. **Threading Complexity**: More threads (network thread, audio thread, UI thread)

## Alternative Approaches

### Option: Third-Party OSC Libraries

**liblo** (Lightweight OSC):
- C library, well-established
- Pros: Battle-tested, stable
- Cons: External dependency, C API (less C++-friendly)

**oscpack**:
- C++ header-only OSC library
- Pros: Simple, header-only
- Cons: Less feature-complete than JUCE's OSC

**RECOMMENDATION: Use JUCE OSC** - Native integration, consistent with codebase, no external deps.

## References & Research

1. **JUCE OSC Tutorial**: https://juce.com/tutorials/tutorial_osc_sender_receiver
2. **OSC Specification**: http://opensoundcontrol.org/
3. **Current MIDI Implementation**: `MidiDeviceManager.h`, `MIDICVModuleProcessor.h`
4. **Modular Synth Integration**: `ModularSynthProcessor::processMidiWithDeviceInfo()`

## Next Steps

1. ✅ Conceptual design (this document)
2. ⏭️ Detailed implementation plan with risk assessment
3. ⏭️ Prototype OSCDeviceManager (mirror MidiDeviceManager)
4. ⏭️ Create OSCCVModuleProcessor (mirror MIDICVModuleProcessor)
5. ⏭️ Integrate into ModularSynthProcessor
6. ⏭️ Add UI for OSC device management
7. ⏭️ Testing with external OSC controllers (TouchOSC, Lemur, etc.)

