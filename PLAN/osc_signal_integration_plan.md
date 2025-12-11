# OSC Signal Integration - Detailed Implementation Plan

## Executive Summary

This plan details the implementation of OSC (Open Sound Control) protocol support in the modular synthesizer, following the established MIDI architecture patterns. OSC will enable network-based control signals, expanding the signal input capabilities beyond hardware MIDI devices.

## Risk Assessment

### Overall Risk Rating: **MEDIUM** (6/10)

**Risk Factors:**
- ✅ JUCE OSC is mature and well-documented
- ✅ Architecture pattern already proven with MIDI
- ⚠️ Network latency/jitter could affect real-time audio
- ⚠️ Thread safety complexity increases with network thread
- ⚠️ Testing requires external OSC tools/controllers
- ⚠️ Potential firewall/network configuration issues for users

### Risk Mitigation Strategies

1. **Network Latency**: Use buffering with size limits, detect and warn on high latency
2. **Thread Safety**: Mirror proven MIDI threading model exactly
3. **Testing**: Provide test utility module and documentation for external tools
4. **User Experience**: Sensible defaults (localhost), clear error messages for network issues

## Difficulty Levels

### Level 1: Core OSC Infrastructure (Foundation)
**Difficulty: EASY (3/10)**
- Create `OSCDeviceManager` class (mirror `MidiDeviceManager`)
- Basic OSC message reception and buffering
- Single device, localhost only
- **Time Estimate: 8-12 hours**

### Level 2: Module Integration (Core Functionality)
**Difficulty: MEDIUM (5/10)**
- Create `OSCCVModuleProcessor` (mirror `MIDICVModuleProcessor`)
- Integrate into `ModularSynthProcessor` signal routing
- Basic OSC address pattern matching
- **Time Estimate: 16-20 hours**

### Level 3: Multi-Device & UI (Full Feature Set)
**Difficulty: MEDIUM-HARD (7/10)**
- Multiple OSC devices (different IPs/ports)
- UI for device management (scan, enable/disable, configure)
- Activity visualization
- Address pattern configuration UI
- **Time Estimate: 20-24 hours**

### Level 4: Advanced Features (Polish)
**Difficulty: HARD (8/10)**
- Wildcard pattern matching (`/cv/*`)
- OSC sender capability (bidirectional communication)
- OSC bundle support (atomic multi-message delivery)
- Network discovery (optional mDNS/Bonjour)
- **Time Estimate: 24-32 hours**

## Confidence Rating

### Overall Confidence: **HIGH (8/10)**

**Strong Points:**
1. ✅ **Proven Architecture**: MIDI implementation provides exact blueprint
2. ✅ **Mature Technology**: JUCE OSC is stable, well-documented, used in production
3. ✅ **Clear Requirements**: Similar functionality to MIDI, just different protocol
4. ✅ **No External Dependencies**: JUCE OSC is included, no license conflicts
5. ✅ **Incremental Development**: Can implement in phases, test at each level

**Weak Points:**
1. ⚠️ **Network Reliability**: UDP can drop packets, need graceful degradation
2. ⚠️ **Performance Unknowns**: Network thread → audio thread buffering needs profiling
3. ⚠️ **User Configuration**: Network setup can confuse non-technical users
4. ⚠️ **Testing Complexity**: Requires external tools, harder to automate unit tests

## Implementation Phases

### Phase 1: Proof of Concept (Level 1)
**Goal**: Basic OSC reception working end-to-end

**Tasks:**
1. Create `OSCDeviceManager` class
   - Inherit from `juce::OSCReceiver::Listener<>`
   - Basic UDP port listening
   - Message buffering with `CriticalSection`
   - Single hardcoded localhost device (port 57120)

2. Create `OSCMessageWithSource` struct
   - Mirror `MidiMessageWithSource` structure
   - Store `juce::OSCMessage`, source info, timestamp

3. Integration point in `ModularSynthProcessor`
   - Add `processOSCWithSourceInfo()` method (similar to `processMidiWithDeviceInfo()`)
   - Call from main audio thread (via timer or direct)

4. Basic test module
   - Simple logger module that prints received OSC messages
   - Verify messages are received and routed correctly

**Deliverables:**
- `OSCDeviceManager.h/.cpp`
- `OSCMessageWithSource` struct (in `ModuleProcessor.h` or new header)
- Basic integration in `ModularSynthProcessor`
- Test module for validation

**Success Criteria:**
- Can receive OSC messages from external tool (e.g., TouchOSC, OSCulator)
- Messages appear in test module output
- No crashes or audio glitches

**Potential Problems:**
- **Port binding failures**: OS may block port, other app using port
  - *Solution*: Clear error message, try alternative ports, admin privileges check
- **Thread safety issues**: Race conditions in buffering
  - *Solution*: Mirror MIDI's exact locking pattern, use static analysis tools
- **Network firewall blocking**: Windows Firewall may block UDP
  - *Solution*: Document firewall exceptions, provide installer script option

---

### Phase 2: OSC to CV Conversion (Level 2)
**Goal**: Convert OSC messages to CV/Gate signals like MIDI

**Tasks:**
1. Create `OSCCVModuleProcessor` class
   - Inherit from `ModuleProcessor`
   - Implement `handleOSCSignal()` virtual method
   - Parameter: OSC address pattern to listen to (e.g., `/cv/pitch`)
   - Output: CV values from OSC float arguments

2. Add OSC signal handling to `ModuleProcessor` base class
   - Define `OSCSignalHandler` interface (similar to MIDI handling)
   - Virtual method `handleOSCSignal(const std::vector<OSCMessageWithSource>&)`

3. Implement address pattern matching
   - Simple exact match initially (e.g., `/cv/pitch` matches only `/cv/pitch`)
   - Extract argument values by type (float32, int32)
   - Handle missing/wrong-type arguments gracefully

4. Create CV outputs
   - Pitch output (from `/cv/pitch` or `/synth/note/on` → MIDI note conversion)
   - Gate output (from `/gate/trigger` or `/synth/note/on` → note on/off)
   - Velocity output (from `/cv/velocity` or `/synth/note/on` velocity)
   - Mod wheel output (from `/cv/modwheel`)
   - Additional CV outputs (user-configurable address mappings)

5. Integration with `ModularSynthProcessor`
   - Route OSC messages to modules implementing `handleOSCSignal()`
   - Call during audio processing (similar to MIDI routing)

**Deliverables:**
- `OSCCVModuleProcessor.h/.cpp`
- `OSCSignalHandler` interface in `ModuleProcessor.h`
- Module registration in module factory
- UI integration (parameter panel for address configuration)

**Success Criteria:**
- OSC messages control CV outputs correctly
- Can drive oscillator pitch from OSC
- Gate triggers envelope correctly
- No audio dropouts or glitches

**Potential Problems:**
- **Address pattern conflicts**: Multiple modules listening to same address
  - *Solution*: Allow it (multiple consumers), document behavior
- **Type mismatches**: OSC message has wrong argument type
  - *Solution*: Validate types, log warning, use default value (0.0)
- **Missing arguments**: OSC message has no arguments
  - *Solution*: Log warning, maintain last valid value or use 0.0
- **Performance impact**: Pattern matching in audio thread
  - *Solution*: Pre-filter on network thread, cache compiled patterns

---

### Phase 3: Device Management & UI (Level 3)
**Goal**: Full multi-device support with user interface

**Tasks:**
1. Extend `OSCDeviceManager` for multiple devices
   - Support multiple UDP ports (different devices on different ports)
   - Device configuration: IP (for sending), port (for receiving)
   - Enable/disable per device
   - Device scanning/configuration UI

2. Device persistence
   - Save/load device configurations (similar to MIDI device preferences)
   - Remember enabled state across sessions

3. UI Components
   - Device list panel (similar to MIDI device list)
   - Add/Remove device buttons
   - IP address and port input fields
   - Enable/disable checkboxes
   - Activity indicators (show when OSC messages received)

4. Activity tracking
   - Per-device activity monitoring (similar to MIDI activity tracking)
   - Visual feedback in UI (LED indicators, activity bars)
   - Frame-based fade-out for activity visualization

5. Error handling and user feedback
   - Port binding errors (port already in use)
   - Network errors (invalid IP, unreachable host)
   - Clear error messages in UI

**Deliverables:**
- Extended `OSCDeviceManager` with multi-device support
- UI panel for OSC device management
- Persistent device configuration
- Activity visualization

**Success Criteria:**
- Can configure multiple OSC devices
- Devices persist across application restarts
- UI shows device status and activity
- Clear error messages for network issues

**Potential Problems:**
- **Port conflicts**: Multiple devices can't use same port
  - *Solution*: Validate uniqueness, disable conflicting device, show error
- **IP address validation**: User enters invalid IP format
  - *Solution*: Input validation, regex pattern matching, clear error messages
- **UI complexity**: Too many options confuse users
  - *Solution*: Sensible defaults, hide advanced options, tooltips/help text

---

### Phase 4: Advanced Features (Level 4)
**Goal**: Production-ready OSC implementation with advanced capabilities

**Tasks:**
1. Wildcard pattern matching
   - Support `*` wildcard in address patterns (e.g., `/cv/*` matches all `/cv/...`)
   - Support multiple wildcards (e.g., `/synth/*/on`)
   - Performance optimization (compiled pattern matching, caching)

2. OSC Sender capability
   - `OSCSenderManager` class (mirror receiver pattern)
   - Modules can send OSC messages (e.g., send transport state to external controller)
   - Bidirectional communication support

3. OSC Bundle support
   - Handle OSC bundles (multiple messages atomically)
   - Ensure all messages in bundle processed together

4. Network discovery (optional)
   - mDNS/Bonjour discovery for OSC devices
   - Auto-populate device list from network
   - Fallback to manual entry if discovery fails

5. Performance optimization
   - Profile network → audio thread buffering
   - Optimize pattern matching (avoid regex, use simple string comparison with caching)
   - Minimize allocations in audio thread

6. Documentation and examples
   - User manual section on OSC setup
   - Example OSC message patterns
   - Recommended OSC controller apps
   - Troubleshooting guide

**Deliverables:**
- Wildcard pattern matching implementation
- OSC sender functionality
- Bundle support
- Optional network discovery
- Performance optimizations
- Documentation

**Success Criteria:**
- Wildcard patterns work correctly and efficiently
- Can send OSC messages to external controllers
- Bundles processed atomically
- Performance meets real-time audio requirements (<1% CPU overhead)

**Potential Problems:**
- **Wildcard matching performance**: Complex patterns slow down audio thread
  - *Solution*: Compile patterns to efficient data structures, cache matches
- **OSC sender thread safety**: Sending from audio thread can block
  - *Solution*: Queue sends, process on network thread
- **Network discovery complexity**: mDNS requires additional dependencies
  - *Solution*: Make optional, use JUCE's networking if available, or skip initially

---

## Architecture Details

### Class Structure

```cpp
// OSCDeviceManager.h (mirror MidiDeviceManager.h)
class OSCDeviceManager : public juce::OSCReceiver::Listener<>, 
                         public juce::Timer
{
public:
    struct DeviceInfo {
        juce::String identifier;    // "IP:port" format
        juce::String name;           // User-defined name
        int port = 57120;
        juce::String sendIP;         // IP to send to (if bidirectional)
        int sendPort = 57121;
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
    
    // API mirrors MidiDeviceManager
    void scanDevices();  // Load from config or discover
    void enableDevice(const juce::String& identifier);
    void disableDevice(const juce::String& identifier);
    void swapMessageBuffer(std::vector<OSCMessageWithSource>& targetBuffer);
    std::vector<DeviceInfo> getAvailableDevices() const;
    std::vector<DeviceInfo> getEnabledDevices() const;
    
private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void timerCallback() override;  // For hot-plug detection (if needed)
    
    juce::CriticalSection bufferLock;
    std::vector<OSCMessageWithSource> messageBuffer;
    std::map<juce::String, std::unique_ptr<juce::OSCReceiver>> receivers;
    std::map<juce::String, DeviceInfo> devices;
};

// ModuleProcessor.h (additions)
class ModuleProcessor {
    // ... existing code ...
    
    // OSC signal handling interface
    virtual void handleOSCSignal(const std::vector<OSCMessageWithSource>& messages) {}
    
    // Optional: check if module handles OSC
    virtual bool supportsOSCSignals() const { return false; }
};

// OSCCVModuleProcessor.h (mirror MIDICVModuleProcessor.h)
class OSCCVModuleProcessor : public ModuleProcessor
{
public:
    OSCCVModuleProcessor();
    
    void handleOSCSignal(const std::vector<OSCMessageWithSource>& messages) override;
    bool supportsOSCSignals() const override { return true; }
    
    const juce::String getName() const override { return "osc_cv"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
private:
    struct OSCState {
        float currentPitch = 0.0f;
        float currentVelocity = 0.0f;
        float modWheel = 0.0f;
        bool gateHigh = false;
    } oscState;
    
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Parameters
    juce::AudioParameterChoice* deviceFilterParam;
    juce::AudioParameterString* addressPatternParam;  // e.g., "/cv/pitch"
    
    // Pattern matching helpers
    bool matchesPattern(const juce::String& address, const juce::String& pattern) const;
    float extractFloatArg(const juce::OSCMessage& msg, int index, float defaultValue = 0.0f) const;
};
```

### Integration Points

**ModularSynthProcessor.cpp:**
```cpp
void ModularSynthProcessor::processBlock(...)
{
    // ... existing MIDI processing ...
    
    // Process OSC messages (if OSC manager exists)
    if (oscDeviceManager)
    {
        std::vector<OSCMessageWithSource> oscMessages;
        oscDeviceManager->swapMessageBuffer(oscMessages);
        
        if (!oscMessages.empty())
            processOSCWithSourceInfo(oscMessages);
    }
}

void ModularSynthProcessor::processOSCWithSourceInfo(
    const std::vector<OSCMessageWithSource>& messages)
{
    // Distribute to modules
    for (auto* node : internalGraph->getNodes())
    {
        if (auto* processor = node->getProcessor())
        {
            if (auto* module = dynamic_cast<ModuleProcessor*>(processor))
            {
                if (module->supportsOSCSignals())
                    module->handleOSCSignal(messages);
            }
        }
    }
}
```

### Thread Safety Model

1. **Network Thread** (OSCReceiver callback)
   - Receives UDP packets
   - Performs initial filtering (address pattern pre-check)
   - Adds to thread-safe buffer (CriticalSection protected)
   - Non-blocking, fast return

2. **Audio Thread** (processBlock)
   - Swaps buffer (atomic operation)
   - Processes messages
   - Distributes to modules
   - Must be non-blocking, low latency

3. **UI Thread** (device management, configuration)
   - Reads device state
   - Modifies device configuration
   - Uses message queue or locks for thread-safe communication

### Performance Considerations

**Latency Targets:**
- Network → Audio thread: <5ms typical, <10ms worst case
- Message processing: <1ms per message batch
- Pattern matching: <0.1ms per message

**CPU Usage Targets:**
- OSC reception: <0.5% CPU
- Message routing: <0.5% CPU
- Total overhead: <1% CPU

**Memory Usage:**
- Message buffer: ~1KB per device (configurable max size)
- Pattern cache: ~100 bytes per pattern
- Device state: ~50 bytes per device

## Testing Strategy

### Unit Tests
1. Pattern matching logic
2. Message buffering thread safety
3. Address extraction and type conversion
4. Device enable/disable state management

### Integration Tests
1. End-to-end: External OSC tool → Module → CV output
2. Multiple devices: Multiple OSC sources → Different modules
3. Performance: High message rate stress test (1000 msg/sec)

### Manual Testing Checklist
- [ ] Receive OSC from TouchOSC app (iOS/Android)
- [ ] Receive OSC from OSCulator (macOS/Windows)
- [ ] Receive OSC from custom Python script
- [ ] Multiple devices on different ports
- [ ] Pattern matching (exact and wildcard)
- [ ] Type conversion (float, int, string)
- [ ] Error handling (invalid IP, port conflicts)
- [ ] Activity visualization
- [ ] Device persistence (save/load)

### Test Tools
- **TouchOSC**: Mobile OSC controller app
- **OSCulator**: macOS/Windows OSC routing tool
- **Python script**: Custom OSC sender for automated testing
- **Wireshark**: Network packet inspection (debugging)

## Documentation Requirements

### User Documentation
1. **OSC Setup Guide**
   - What is OSC?
   - How to enable OSC devices
   - Recommended controller apps
   - Common address patterns

2. **Troubleshooting**
   - Port binding errors
   - Firewall configuration
   - Network latency issues
   - Device not receiving messages

### Developer Documentation
1. **Architecture Overview**
   - Signal flow diagram
   - Thread safety model
   - Extension points for new protocols

2. **Adding OSC Support to Modules**
   - Implement `handleOSCSignal()`
   - Pattern matching best practices
   - Type safety guidelines

## Migration & Backward Compatibility

### Breaking Changes
- **None**: OSC is additive, doesn't modify existing MIDI code

### Deprecation Plan
- **N/A**: No existing code to deprecate

### Rollout Plan
1. Implement Phase 1 (proof of concept)
2. Internal testing with team
3. Beta release with Phase 2 (basic OSC CV module)
4. Full release with Phase 3 (UI and multi-device)
5. Phase 4 (advanced features) as optional update

## Success Metrics

### Functional Metrics
- ✅ Can receive OSC messages from external controllers
- ✅ OSC messages convert to CV signals correctly
- ✅ Multiple OSC devices work simultaneously
- ✅ No audio dropouts or glitches
- ✅ UI shows device status and activity

### Performance Metrics
- ✅ <1% CPU overhead for OSC processing
- ✅ <5ms average latency (network → audio)
- ✅ Supports 10+ simultaneous OSC devices
- ✅ Handles 1000+ messages/second without drops

### Quality Metrics
- ✅ Zero crashes related to OSC
- ✅ Clear error messages for all failure modes
- ✅ User documentation complete
- ✅ Unit test coverage >80%

## Conclusion

OSC integration is **feasible and recommended**, following the proven MIDI architecture pattern. JUCE's native OSC support eliminates external dependencies, and the incremental implementation approach allows for testing and validation at each phase. The main challenges are network reliability and user configuration complexity, both manageable with proper error handling and documentation.

**Recommended Approach**: Start with Phase 1 (proof of concept) to validate the architecture, then proceed through phases incrementally based on user feedback and requirements.

