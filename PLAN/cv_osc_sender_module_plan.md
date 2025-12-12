# CV to OSC Sender Module Plan

## Overview

Create a new module that converts CV/Audio/Gate signals from the modular synth into OSC messages. This is the reverse of the `OSCCVModuleProcessor` - it takes internal signals and sends them out over the network as OSC.

## Module Name

**`CVOSCSenderModuleProcessor`** (or `OSCTransmitterModuleProcessor`)

Node name: `cv_osc_sender`

## Core Functionality

### Input Requirements
- **Flexible Inputs**: Accept any type of signal (CV, Audio, Gate) on any number of input channels
- **User-Defined Mapping**: User can assign an OSC address to each input channel
- **Dynamic Pins**: Number of inputs configurable (up to a reasonable limit, e.g., 16)

### OSC Message Generation
- **Address Assignment**: Each input channel maps to a user-defined OSC address
- **Data Type Detection**: Automatically determine appropriate OSC type:
  - **CV Signals** (0.0-1.0 range): Send as `float32`
  - **Gate Signals** (0.0 or 1.0): Send as `float32` (0.0 or 1.0) or `int32` (0 or 1)
  - **Audio Signals**: Send as `float32` (sample-averaged or peak value)
- **Send Rate**: Configurable send rate (per-sample, per-block, throttled)
- **Change Detection**: Optional - only send when value changes (with threshold)

### Network Configuration
- **Destination**: IP address and port (default: localhost:57120)
- **Connection Management**: Connect/disconnect button
- **Status Indicator**: Show connection status and send activity

## Architecture

### Class Structure

```cpp
class CVOSCSenderModuleProcessor : public ModuleProcessor
{
public:
    CVOSCSenderModuleProcessor();
    ~CVOSCSenderModuleProcessor() override;
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    const juce::String getName() const override { return "cv_osc_sender"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // UI
    void drawParametersInNode(...) override;
    void drawIoPins(...) override;

private:
    // OSC Sender
    std::unique_ptr<juce::OSCSender> oscSender;
    
    // Configuration
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameters
    juce::AudioParameterBool* enabledParam;
    juce::AudioParameterChoice* sendModeParam; // "Per Block", "Throttled", "On Change"
    juce::AudioParameterFloat* throttleRateParam; // Messages per second
    juce::AudioParameterFloat* changeThresholdParam; // Minimum change to trigger send
    
    // Network settings (stored as strings in APVTS)
    juce::String targetHost;
    int targetPort;
    
    // Input mapping (channel -> OSC address)
    struct InputMapping {
        juce::String oscAddress;      // e.g., "/cv/pitch", "/gate/1"
        PinDataType inputType;        // CV, Gate, Audio
        bool enabled;                 // Is this input active?
    };
    std::vector<InputMapping> inputMappings;
    
    // Per-input state tracking
    struct InputState {
        float lastSentValue;
        juce::uint64 lastSendTime;    // For throttling
        bool lastGateState;
    };
    std::vector<InputState> inputStates;
    
    // Thread safety
    juce::CriticalSection mappingLock;
    
    // Activity tracking for UI
    std::atomic<int> messagesSentThisBlock;
    std::atomic<bool> isConnected;
    
    // Helper methods
    void updateConnection();
    void sendOSCMessage(int channel, float value);
    float computeOutputValue(int channel, const juce::AudioBuffer<float>& buffer);
    bool shouldSend(int channel, float currentValue, float lastValue);
};
```

## Parameters (APVTS)

### Network Configuration
- `enabled` (bool): Master enable/disable
- `target_host` (string): Target IP address (default: "localhost")
- `target_port` (int): Target port (default: 57120, range: 1-65535)

### Send Behavior
- `send_mode` (choice):
  - "Per Block" (0): Send once per audio block (lowest rate)
  - "Throttled" (1): Send at configurable rate (messages/sec)
  - "On Change" (2): Send only when value changes (with threshold)
  - "Sample Accurate" (3): Send every sample (very high rate, use with caution)
  
- `throttle_rate` (float): Messages per second (range: 1-1000, default: 30)
- `change_threshold` (float): Minimum change to trigger send (range: 0.001-1.0, default: 0.01)

### Input Mapping
- Each input channel has its own OSC address (stored as member variable, not APVTS)
- User can add/remove inputs dynamically
- Inputs can be enabled/disabled individually

## UI Design

### Main Panel

```
┌─────────────────────────────────────┐
│ CV → OSC Sender                     │
├─────────────────────────────────────┤
│ Network:                            │
│  [✓] Enabled                        │
│  Host: [localhost        ]          │
│  Port: [57120]                      │
│  Status: ● Connected                │
│                                     │
│ Send Mode: [Per Block        ▼]    │
│ Throttle: [30] msg/sec              │
│ Threshold: [0.01]                   │
│                                     │
│ Input Mappings:                     │
│  ┌─────────────────────────────┐   │
│  │ Input 1                     │   │
│  │ Address: [/cv/pitch    ] ✓ │   │
│  │ Type: [CV] Last: 0.523     │   │
│  ├─────────────────────────────┤   │
│  │ Input 2                     │   │
│  │ Address: [/gate/1      ] ✓ │   │
│  │ Type: [Gate] Last: 1.0     │   │
│  ├─────────────────────────────┤   │
│  │ [+ Add Input]               │   │
│  └─────────────────────────────┘   │
│                                     │
│ Activity: ████░░░░ 42 msgs/sec     │
└─────────────────────────────────────┘
```

### Key UI Elements

1. **Network Section**:
   - Enable checkbox
   - Host input field
   - Port input field
   - Connection status indicator (green=connected, red=disconnected)
   - "Test Connection" button (optional)

2. **Send Behavior Section**:
   - Send mode dropdown
   - Throttle rate slider (when throttled mode)
   - Change threshold slider (when on-change mode)

3. **Input Mappings Section**:
   - Scrollable list of input mappings
   - Each mapping shows:
     - Input number/label
     - OSC address input field
     - Enable/disable checkbox
     - Signal type indicator (CV/Gate/Audio)
     - Last sent value (for monitoring)
   - "+ Add Input" button to add new mappings
   - Delete button for each mapping

4. **Activity Monitor**:
   - Messages sent per second
   - Visual activity bar
   - Connection status

## Pin Configuration

### Dynamic Input Pins
- Start with 1 input pin
- User can add up to 16 input pins
- Each pin can accept any signal type (CV, Gate, Audio)
- Pin labels show the OSC address (or "Unnamed" if not set)

### No Output Pins
- This module only sends OSC, doesn't output audio/CV

## Implementation Details

### ProcessBlock Logic

```cpp
void CVOSCSenderModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, 
    juce::MidiBuffer& midiMessages)
{
    if (!enabledParam->get() || !isConnected.load())
        return;
    
    const int numInputs = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    
    messagesSentThisBlock.store(0);
    
    for (int ch = 0; ch < numInputs && ch < inputMappings.size(); ++ch)
    {
        auto& mapping = inputMappings[ch];
        if (!mapping.enabled || mapping.oscAddress.isEmpty())
            continue;
        
        // Compute representative value for this block
        float value = computeOutputValue(ch, buffer);
        auto& state = inputStates[ch];
        
        // Check if we should send
        if (shouldSend(ch, value, state.lastSentValue))
        {
            sendOSCMessage(ch, value);
            state.lastSentValue = value;
            messagesSentThisBlock++;
        }
    }
}
```

### Value Computation

```cpp
float CVOSCSenderModuleProcessor::computeOutputValue(
    int channel, 
    const juce::AudioBuffer<float>& buffer)
{
    if (channel >= buffer.getNumChannels())
        return 0.0f;
    
    const float* channelData = buffer.getReadPointer(channel);
    const int numSamples = buffer.getNumSamples();
    
    // Different strategies based on input type
    switch (inputMappings[channel].inputType)
    {
        case PinDataType::Gate:
            // For gates, check if any sample is > 0.5
            for (int i = 0; i < numSamples; ++i)
                if (channelData[i] > 0.5f)
                    return 1.0f;
            return 0.0f;
            
        case PinDataType::CV:
            // For CV, use average or last value
            float sum = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                sum += channelData[i];
            return sum / numSamples;
            
        case PinDataType::Audio:
            // For audio, use peak or RMS
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = std::max(peak, std::abs(channelData[i]));
            return peak;
            
        default:
            return 0.0f;
    }
}
```

### Send Decision Logic

```cpp
bool CVOSCSenderModuleProcessor::shouldSend(
    int channel, 
    float currentValue, 
    float lastValue)
{
    int sendMode = sendModeParam->getIndex();
    
    switch (sendMode)
    {
        case 0: // Per Block
            return true; // Always send once per block
            
        case 1: // Throttled
        {
            auto& state = inputStates[channel];
            juce::uint64 now = juce::Time::getMillisecondCounter();
            float throttleMs = 1000.0f / throttleRateParam->get();
            
            if (now - state.lastSendTime >= throttleMs)
            {
                state.lastSendTime = now;
                return true;
            }
            return false;
        }
        
        case 2: // On Change
        {
            float threshold = changeThresholdParam->get();
            return std::abs(currentValue - lastValue) >= threshold;
        }
        
        case 3: // Sample Accurate (send every sample)
            return true; // Send every block (best we can do)
            
        default:
            return false;
    }
}
```

### OSC Message Sending

```cpp
void CVOSCSenderModuleProcessor::sendOSCMessage(int channel, float value)
{
    if (!oscSender || !oscSender->isConnected())
        return;
    
    const auto& mapping = inputMappings[channel];
    juce::OSCMessage msg(mapping.oscAddress);
    
    // Add value based on type
    switch (mapping.inputType)
    {
        case PinDataType::Gate:
            // Send as int32 (0 or 1) or float32
            msg.addFloat32(value >= 0.5f ? 1.0f : 0.0f);
            break;
            
        case PinDataType::CV:
        case PinDataType::Audio:
            msg.addFloat32(value);
            break;
            
        default:
            msg.addFloat32(value);
            break;
    }
    
    oscSender->send(msg);
}
```

## Integration Points

### PinDatabase
Add entry:
```cpp
descriptions["cv_osc_sender"] = "Converts CV/Audio/Gate signals to OSC messages";
db["cv_osc_sender"] = ModulePinInfo(
    NodeWidth::Medium,
    {
        AudioPin("Input 1", 0, PinDataType::CV)  // Start with 1, user adds more
    },
    {}, // No outputs
    {}
);
```

### Module Factory
Add to `ModularSynthProcessor::createModuleProcessor()`:
```cpp
else if (type == "cv_osc_sender")
    return std::make_unique<CVOSCSenderModuleProcessor>();
```

### Menu Integration
Add to MIDI/OSC category in `ImGuiNodeEditorComponent.cpp`:
- Under "OSC" section
- Name: "CV → OSC Sender"

## Advanced Features (Future)

### Phase 2 Enhancements

1. **Preset Support**:
   - Save/load input mappings
   - Preset templates for common apps (TouchOSC, Lemur, etc.)

2. **Multi-Message Support**:
   - Send multiple values in one OSC bundle
   - Group related inputs together

3. **Value Scaling**:
   - Map input range (0-1) to custom OSC value range
   - Offset and scaling per input

4. **Bundles**:
   - Option to send all inputs in one OSC bundle
   - Timestamped bundles for sync

5. **OSC Query**:
   - Respond to OSC queries about available inputs
   - Dynamic discovery

## Testing Strategy

### Unit Tests
- Value computation for different signal types
- Send rate throttling
- Change detection threshold
- Connection management

### Integration Tests
- Send to local OSC receiver
- Verify message format and timing
- Test with real CV signals

### User Testing
- Test with TouchOSC
- Test with custom OSC applications
- Verify network performance

## File Structure

```
juce/Source/audio/modules/
  CVOSCSenderModuleProcessor.h
  CVOSCSenderModuleProcessor.cpp
```

## Dependencies

- `juce_osc` (already linked)
- `juce_audio_basics` (for AudioBuffer)
- `ModuleProcessor` base class
- Existing pin system

## Implementation Order

1. **Phase 1: Basic Functionality**
   - Create class structure
   - Single input, single OSC address
   - Basic send per block
   - Network connection

2. **Phase 2: UI & Configuration**
   - Parameter layout
   - Network settings UI
   - Input mapping UI
   - Activity indicators

3. **Phase 3: Advanced Send Modes**
   - Throttled sending
   - Change detection
   - Multiple inputs

4. **Phase 4: Polish**
   - Dynamic pins
   - Preset support
   - Error handling
   - Documentation

## Notes

- OSC sending should be non-blocking (JUCE's OSCSender is already async)
- Consider rate limiting to prevent network flooding
- Handle disconnection gracefully
- Provide clear feedback when sending fails
- Support both localhost and remote IP addresses
- Consider IPv6 support (JUCE handles this)


