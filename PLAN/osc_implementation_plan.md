ok# OSC Implementation Plan

## Overview

This plan implements OSC (Open Sound Control) support to complement the existing MIDI infrastructure. OSC brings network-based control, flexible addressing, and type safety to the modular synth.

## Architecture Alignment

Following the existing **Option B** pattern from the conceptual design: keep protocols separate but share infrastructure patterns. This maintains type safety and minimizes abstraction overhead.

## Key Design Principles

1. **Mirror MIDI Patterns**: OSC implementation follows the same architectural patterns as `MidiDeviceManager` for consistency
2. **Address-Based Filtering**: OSC uses string pattern matching instead of MIDI's device/channel filtering
3. **Network-First**: Focus on network-based control (TouchOSC, mobile apps, web interfaces)
4. **Type Safety**: Leverage OSC's typed arguments (float32, int32, string, etc.)
5. **Coexistence**: OSC and MIDI work in parallel without conflicts

## Implementation Phases

### Phase 1: Core OSC Infrastructure

#### 1.1 OSCDeviceManager (`juce/Source/audio/OscDeviceManager.h/.cpp`)

Mirrors `MidiDeviceManager` architecture:

```cpp
class OscDeviceManager : public juce::OSCReceiver::Listener<>
{
public:
    struct DeviceInfo {
        juce::String identifier;     // "IP:port" (e.g., "192.168.1.100:57120")
        juce::String name;            // Human-readable name (user-defined)
        int port = 57120;             // UDP receive port
        bool enabled = false;
        int deviceIndex = -1;
    };
    
    struct OscMessageWithSource {
        juce::OSCMessage message;
        juce::String sourceIdentifier;  // "IP:port"
        juce::String sourceName;        // User-friendly name
        int deviceIndex;
        double timestamp;
    };
    
    // Core API (mirrors MidiDeviceManager)
    void scanDevices();  // Load saved devices from config
    void addDevice(const juce::String& name, int port);
    void removeDevice(const juce::String& identifier);
    void enableDevice(const juce::String& identifier);
    void disableDevice(const juce::String& identifier);
    void swapMessageBuffer(std::vector<OscMessageWithSource>& targetBuffer);
    
    // Activity tracking (similar to MIDI)
    struct ActivityInfo {
        juce::String sourceName;
        int deviceIndex;
        juce::String lastAddress;  // Last OSC address received
        juce::uint32 lastActivityFrame = 0;
        juce::uint64 lastMessageTime = 0;
    };
    
    std::vector<ActivityInfo> getActivityInfo() const;
    
private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;
    
    // Per-port OSCReceiver instances
    std::map<int, std::unique_ptr<juce::OSCReceiver>> receivers;
    std::vector<DeviceInfo> devices;
    
    // Thread-safe message buffering
    juce::CriticalSection bufferLock;
    std::vector<OscMessageWithSource> messageBuffer;
};
```

**Key Differences from MIDI:**
- Multiple `OSCReceiver` instances (one per port) vs single MIDI input callback
- Device discovery is manual (user adds IP:port) vs automatic OS enumeration
- Messages include full address path instead of channel number

#### 1.2 Integration with ModularSynthProcessor

Add to `ModularSynthProcessor.h`:

```cpp
// OSC support (mirrors MIDI pattern)
struct OscMessageWithSource;  // Forward declare (from OscDeviceManager)
void processOscWithSourceInfo(const std::vector<OscMessageWithSource>& messages);
```

Add to `ModularSynthProcessor::processBlock()`:

```cpp
// === OSC DISTRIBUTION: Mirror MIDI pattern ===
{
    const juce::ScopedLock lock(oscActivityLock);  // Similar to midiActivityLock
    
    auto currentProcessors = activeAudioProcessors.load();
    if (currentProcessors && !currentBlockOscMessages.empty())
    {
        for (const auto& modulePtr : *currentProcessors)
        {
            if (modulePtr != nullptr)
            {
                modulePtr->handleOscSignal(currentBlockOscMessages);
            }
        }
        currentBlockOscMessages.clear();
    }
}
```

#### 1.3 Module Base Class Extension

Add to `ModuleProcessor.h`:

```cpp
/**
    OSC signal processing (network-based control)
    
    Modules that can process OSC messages should override this method.
    Similar to handleDeviceSpecificMidi() but uses address pattern matching.
    
    @param oscMessages Vector of OSC messages with source information
*/
virtual void handleOscSignal(const std::vector<OscMessageWithSource>& oscMessages)
{
    juce::ignoreUnused(oscMessages);
    // Default: do nothing. OSC-aware modules will override.
}
```

#### 1.4 PresetCreatorComponent Integration

Mirror MIDI timer callback pattern:

```cpp
// In timerCallback():
// OSC SUPPORT: Transfer OSC messages from OscDeviceManager to ModularSynthProcessor
if (oscDeviceManager && synth)
{
    std::vector<OscDeviceManager::OscMessageWithSource> oscMessages;
    oscDeviceManager->swapMessageBuffer(oscMessages);
    
    if (!oscMessages.empty())
    {
        synth->processOscWithSourceInfo(oscMessages);
    }
}
```

### Phase 2: OSC CV Module

#### 2.1 OSCCVModuleProcessor (`juce/Source/audio/modules/OSCCVModuleProcessor.h/.cpp`)

Mirrors `MIDICVModuleProcessor` but uses OSC address patterns:

```cpp
class OSCCVModuleProcessor : public ModuleProcessor
{
public:
    // OSC address pattern matching parameters
    juce::AudioParameterChoice* addressPatternParam;  // "/cv/pitch", "/cv/gate", etc.
    juce::AudioParameterChoice* sourceFilterParam;     // "All Sources", or specific IP:port
    
    // Outputs (same as MIDI CV)
    // Channel 0: Gate (0-1)
    // Channel 1: Pitch CV (0-1, maps to 1V/octave)
    // Channel 2: Velocity (0-1)
    
    void handleOscSignal(const std::vector<OscMessageWithSource>& oscMessages) override;
    
private:
    // Pattern matching helper
    bool matchesPattern(const juce::String& address, const juce::String& pattern);
    
    // Current CV state (similar to MIDI CV's voice state)
    float currentGate = 0.0f;
    float currentPitch = 0.5f;  // C4 by default
    float currentVelocity = 0.8f;
};
```

**Standard OSC Address Patterns:**

```
/synth/note/on          → {note: int32, velocity: float32}
                          Sets gate=1, pitch=noteToCV(note), velocity=velocity/127.0
                          
/synth/note/off         → {note: int32}
                          Sets gate=0 if current pitch matches note
                          
/cv/pitch               → float32 (0-1)
                          Direct pitch CV mapping
                          
/cv/velocity            → float32 (0-1)
                          Direct velocity CV
                          
/gate                   → float32 (0 or 1)
                          Direct gate control
                          
/trigger                → (no args, or float32)
                          Triggers gate=1 briefly, then gate=0
                          
/cv/modwheel            → float32 (0-1)
                          Mod wheel CV output
                          
/pitchbend              → float32 (0-1, centered at 0.5)
                          Pitch bend CV
```

**Pattern Matching:**
- Exact match: `/cv/pitch` matches only `/cv/pitch`
- Wildcard: `/cv/*` matches `/cv/pitch`, `/cv/velocity`, `/cv/modwheel`, etc.
- Single-level wildcard: `/synth/note/*` matches `/synth/note/on`, `/synth/note/off`

### Phase 3: OSC Controller Modules (Optional, Phase 2+)

Create OSC equivalents of MIDI controller modules:

- **OSCKnobsModuleProcessor**: Maps OSC addresses to knob CV outputs
- **OSCButtonsModuleProcessor**: Maps OSC addresses to button triggers
- **OSCFadersModuleProcessor**: Maps OSC addresses to fader CV outputs

These would be useful for complex OSC controllers like TouchOSC templates.

### Phase 4: UI Integration

#### 4.1 OSC Device Manager UI

Add to `ImGuiNodeEditorComponent` (similar to MIDI Device Manager UI):

```cpp
// OSC Device Manager Panel
if (ImGui::CollapsingHeader("OSC Devices"))
{
    if (oscDeviceManager)
    {
        // List existing devices
        auto devices = oscDeviceManager->getDevices();
        for (const auto& device : devices)
        {
            ImGui::PushID(device.identifier.toRawUTF8());
            
            bool enabled = device.enabled;
            if (ImGui::Checkbox(device.name.toRawUTF8(), &enabled))
            {
                if (enabled)
                    oscDeviceManager->enableDevice(device.identifier);
                else
                    oscDeviceManager->disableDevice(device.identifier);
            }
            
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", device.identifier.toRawUTF8());
            
            ImGui::PopID();
        }
        
        // Add new device button
        if (ImGui::Button("Add OSC Device..."))
        {
            // Show dialog: Name, IP (optional), Port
            // Default: localhost, port 57120
        }
    }
}
```

#### 4.2 OSC CV Module UI

In `OSCCVModuleProcessor::drawParametersInNode()`:

```cpp
// Source filter dropdown (mirrors MIDI device filter)
ImGui::Text("OSC Source:");
ImGui::SameLine();
// Dropdown: "All Sources", then list of enabled OSC devices

// Address pattern input/selector
ImGui::Text("Address Pattern:");
ImGui::SameLine();
// Combo box with common patterns:
//   - /synth/note/on
//   - /cv/pitch
//   - /cv/velocity
//   - /gate
//   - Custom (text input)

// Pattern preview
ImGui::TextDisabled("Matches: /cv/*, /synth/note/on, etc.");
```

### Phase 5: OSC Output (Send Messages)

For completeness, add OSC sending capability:

#### 5.1 OSCSender Module

Create `OSCSenderModuleProcessor`:

```cpp
class OSCSenderModuleProcessor : public ModuleProcessor
{
public:
    // Parameters
    juce::AudioParameterChoice* targetDeviceParam;  // Which OSC device to send to
    juce::AudioParameterString* addressPatternParam; // OSC address to send
    
    // Inputs
    // Channel 0: Value to send (float 0-1)
    // Channel 1: Trigger (sends message when > 0.5)
    
    void processBlock(...) override
    {
        // Read CV input
        // Send OSC message when triggered
        oscDeviceManager->sendMessage(targetDevice, address, value);
    }
};
```

This enables bidirectional communication and feedback loops.

## Configuration & Persistence

### Device Storage

Store OSC devices in the same configuration system as MIDI:

```cpp
// In application properties or preset
<oscDevices>
    <device name="TouchOSC" identifier="192.168.1.100:8000" port="8000" enabled="true"/>
    <device name="Web Interface" identifier="localhost:57120" port="57120" enabled="false"/>
</oscDevices>
```

### Module State

OSC modules save their address patterns and source filters in `getExtraStateTree()` / `setExtraStateTree()`, similar to MIDI modules.

## Testing Strategy

1. **Unit Tests**: Pattern matching logic, message buffering
2. **Integration Tests**: End-to-end flow from OSC sender → module → CV output
3. **External Tools**: Test with TouchOSC, OSCulator, or custom OSC sender apps
4. **Network Scenarios**: Localhost, same subnet, across network (with firewall considerations)

## Performance Considerations

1. **Pattern Matching**: Cache compiled patterns for efficiency
2. **Message Buffering**: Limit buffer size to prevent memory growth
3. **Network Thread**: OSC callbacks run on network thread, buffer for audio thread
4. **Multiple Receivers**: Each port requires a separate `OSCReceiver` instance

## Security Considerations

1. **No Authentication**: OSC has no built-in security - anyone on network can send messages
2. **Firewall**: Users must configure firewall to allow UDP ports
3. **Documentation**: Warn users about network exposure
4. **Future Enhancement**: Optional authentication/encryption layer

## Meaningful Use Cases

### 1. TouchOSC Integration
- User creates TouchOSC template with faders/buttons
- Maps OSC addresses to synth parameters
- Control synth from tablet/phone wirelessly

### 2. Web-Based Control
- Build HTML/JavaScript interface using WebSocket → OSC bridge
- Remote control from any device with web browser
- Custom UI tailored to specific presets

### 3. Multi-Computer Setup
- Control synth from another computer on network
- Distributed performance setup
- Remote studio access

### 4. Visual Programming Integration
- Max/MSP, Pure Data send OSC to control synth
- Bidirectional communication (synth → visual tools)

### 5. Automation & Scripting
- Python scripts send OSC for automated testing
- Integration with DAWs that support OSC
- Show control integration

## Migration Path

1. **Phase 1** (Core): Implement `OscDeviceManager` and basic integration
2. **Phase 2** (CV Module): Create `OSCCVModuleProcessor` with standard patterns
3. **Phase 3** (UI): Add device management UI
4. **Phase 4** (Advanced): Controller modules, OSC output, pattern learning

Each phase is independently useful and can be released incrementally.

## Dependencies

- **JUCE OSC Module**: Already included (juce_osc)
- **No External Libraries**: Pure JUCE implementation
- **Network Stack**: Uses system UDP socket APIs (via JUCE)

## Future Enhancements

1. **OSC Learning**: "Learn" mode to automatically map incoming OSC to module parameters
2. **OSC Templates**: Pre-configured templates for common controllers (TouchOSC, Lemur)
3. **OSC Bundle Support**: Handle OSC bundles for atomic multi-message delivery
4. **OSC Query**: Implement OSC query protocol for device discovery
5. **Bi-directional Feedback**: Modules send OSC messages back to controllers (visual feedback)

## Comparison: MIDI vs OSC

| Feature | MIDI | OSC |
|---------|------|-----|
| **Transport** | Hardware/USB/software | Network (UDP) |
| **Discovery** | OS enumeration | Manual IP:port |
| **Filtering** | Device + Channel | Address pattern |
| **Typing** | Bytes (weak) | Typed (strong) |
| **Range** | Local | Network-wide |
| **Use Case** | Hardware controllers | Network/software controllers |

Both complement each other: MIDI for local hardware, OSC for network/software control.

