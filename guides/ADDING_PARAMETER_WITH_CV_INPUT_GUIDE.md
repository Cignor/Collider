# Guide: Adding a New Parameter Slider with CV Input

## Overview

This guide documents the complete process of adding a new parameter slider with CV modulation support to an existing module. We use the **Dry/Wet slider** addition to the `ShapingOscillatorModuleProcessor` as a reference example.

When adding a new parameter with CV input, you need to modify **4 key locations**:

1. **Module Processor Header** (`.h` file) - Parameter ID and member variables
2. **Module Processor Implementation** (`.cpp` file) - Parameter creation, processing, and UI
3. **Pin Database** (`PinDatabase.cpp`) - I/O pin definitions for the editor
4. **User Manual** (`Nodes_Dictionary.md`) - Documentation for end users

---

## Prerequisites: Understanding the Architecture

### Parameter ID Naming Convention

The codebase uses a strict naming convention for parameter IDs:

- **Base Parameter ID**: Used for the APVTS (AudioProcessorValueTreeState) parameter
  - Example: `paramIdDryWet = "dryWet"`
  - This is the actual parameter that stores the value

- **Virtual Modulation ID**: Used for CV routing (suffix `_mod`)
  - Example: `paramIdDryWetMod = "dryWet_mod"`
  - This is NOT an APVTS parameter - it's a virtual routing target
  - Used by `isParamInputConnected()` and `getParamRouting()`

### Channel Routing Architecture

CV inputs are routed through the **single input bus** (Bus 0) using discrete channels:

- **Channels 0-1**: Typically reserved for audio inputs (L/R)
- **Channels 2+**: Reserved for CV modulation inputs
- Each CV input gets its own channel index
- The channel index must match between:
  - Constructor: `discreteChannels(N)` - total channel count
  - `getParamRouting()`: returns the channel index for each CV input
  - `getAudioInputLabel()`: returns the label for that channel

### Buffer Aliasing Safety (Critical!)

⚠️ **IMPORTANT**: Always read input pointers **BEFORE** any output buffer operations. See `guides/DEBUG_INPUT_IMPORTANT.md` for details.

**Safe Pattern:**
```cpp
// ✅ CORRECT: Read inputs first
const float* cvInput = inBus.getReadPointer(cvChannel);
auto* outL = outBus.getWritePointer(0);

// Then process...
```

**Dangerous Pattern:**
```cpp
// ❌ WRONG: Clearing output can clear aliased input!
outBus.clear();
const float* cvInput = inBus.getReadPointer(cvChannel); // May read zeros!
```

---

## Step-by-Step Implementation

### Step 1: Update Module Processor Header

**File:** `juce/Source/audio/modules/YourModuleProcessor.h`

#### 1.1 Add Parameter ID Constants

Add the base parameter ID and virtual modulation ID to the public section:

```cpp
public:
    // Parameter IDs
    static constexpr auto paramIdFrequency    = "frequency";
    static constexpr auto paramIdWaveform     = "waveform";
    static constexpr auto paramIdDrive        = "drive";
    static constexpr auto paramIdDryWet       = "dryWet";  // ✅ NEW: Base parameter ID
    
    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdFrequencyMod = "frequency_mod";
    static constexpr auto paramIdWaveformMod  = "waveform_mod";
    static constexpr auto paramIdDriveMod     = "drive_mod";
    static constexpr auto paramIdDryWetMod    = "dryWet_mod";  // ✅ NEW: Virtual routing ID
```

**Key Points:**
- Base ID (`paramIdDryWet`) is used for the APVTS parameter
- Virtual ID (`paramIdDryWetMod`) is used for CV routing
- Use camelCase for base IDs, snake_case with `_mod` suffix for virtual IDs

#### 1.2 Add Member Variables

Add parameter pointer and smoothed value to the private section:

```cpp
private:
    // Cached parameter pointers
    std::atomic<float>* frequencyParam { nullptr };
    std::atomic<float>* waveformParam  { nullptr };
    std::atomic<float>* driveParam     { nullptr };
    std::atomic<float>* dryWetParam    { nullptr };  // ✅ NEW: Parameter pointer
    
    // Smoothed values to prevent zipper noise
    juce::SmoothedValue<float> smoothedFrequency;
    juce::SmoothedValue<float> smoothedDrive;
    juce::SmoothedValue<float> smoothedDryWet;  // ✅ NEW: Smoothed value
```

**Why Smoothing?**
- Prevents "zipper noise" when parameters change abruptly
- Provides smooth transitions for audio-rate parameter changes
- Standard practice for all user-controllable parameters

---

### Step 2: Update Module Processor Implementation

**File:** `juce/Source/audio/modules/YourModuleProcessor.cpp`

#### 2.1 Add Parameter to Parameter Layout

In `createParameterLayout()`, add the new parameter:

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout YourModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ... existing parameters ...
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdDrive, "Drive",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.01f, 0.5f), 1.0f));

    // ✅ NEW: Add dry/wet parameter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdDryWet, "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));  // Range: 0-1, default: 1.0 (100% wet)

    // ... rest of parameters ...
    
    return { params.begin(), params.end() };
}
```

**Parameter Range Guidelines:**
- **0.0-1.0**: Standard for mix/blend parameters (dry/wet, crossfade, etc.)
- **Use logarithmic scaling** for frequency-related parameters: `NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f)`
- **Default value**: Usually 1.0 for mix parameters (100% wet), or a musically useful center value

#### 2.2 Initialize Parameter Pointer in Constructor

In the constructor, cache the parameter pointer:

```cpp
YourModuleProcessor::YourModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(6), true)  // ✅ Updated: Was 5, now 6
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "YourModuleParams", createParameterLayout())
{
    frequencyParam = apvts.getRawParameterValue(paramIdFrequency);
    waveformParam  = apvts.getRawParameterValue(paramIdWaveform);
    driveParam     = apvts.getRawParameterValue(paramIdDrive);
    dryWetParam    = apvts.getRawParameterValue(paramIdDryWet);  // ✅ NEW: Cache parameter pointer
    
    // ... rest of initialization ...
}
```

**Critical:** Update the input bus channel count! If you're adding a CV input at channel 5, the bus must have at least 6 channels (0-5).

#### 2.3 Reset Smoothed Value in prepareToPlay

```cpp
void YourModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // ... oscillator setup ...
    
    smoothedFrequency.reset(sampleRate, 0.01);  // 10ms smoothing time
    smoothedDrive.reset(sampleRate, 0.01);
    smoothedDryWet.reset(sampleRate, 0.01);  // ✅ NEW: Reset smoothed value
    
    // ... rest of setup ...
}
```

**Smoothing Time:**
- **0.01 seconds (10ms)**: Standard for most parameters
- **Shorter (0.001-0.005)**: For fast, responsive parameters
- **Longer (0.05-0.1)**: For slow, gentle transitions

#### 2.4 Add CV Input Reading in processBlock

**CRITICAL:** Follow buffer aliasing safety rules - read inputs BEFORE any output operations!

```cpp
void YourModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inBus  = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    // ✅ Check for CV connections
    const bool isFreqMod  = isParamInputConnected(paramIdFrequencyMod);
    const bool isWaveMod  = isParamInputConnected(paramIdWaveformMod);
    const bool isDriveMod = isParamInputConnected(paramIdDriveMod);
    const bool isDryWetMod = isParamInputConnected(paramIdDryWetMod);  // ✅ NEW: Check connection

    // ✅ SAFE: Get input pointers BEFORE any output operations
    const float* audioInL = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* audioInR = inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* freqCV   = isFreqMod  && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* waveCV   = isWaveMod  && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* driveCV  = isDriveMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;
    const float* dryWetCV = isDryWetMod && inBus.getNumChannels() > 5 ? inBus.getReadPointer(5) : nullptr;  // ✅ NEW: CV input pointer

    // ✅ SAFE: Get output pointers (after inputs are read)
    auto* outL = outBus.getWritePointer(0);
    auto* outR = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : outL;

    // ✅ Load base parameter values
    const float baseFrequency = frequencyParam != nullptr ? frequencyParam->load() : 440.0f;
    const int   baseWaveform  = waveformParam  != nullptr ? (int) waveformParam->load()  : 0;
    const float baseDrive     = driveParam     != nullptr ? driveParam->load()     : 1.0f;
    const float baseDryWet    = dryWetParam    != nullptr ? dryWetParam->load()    : 1.0f;  // ✅ NEW: Base value

    // ✅ Process sample-by-sample
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // ... process other parameters ...
        
        // ✅ NEW: Process dry/wet CV modulation
        float currentDryWet = baseDryWet;
        if (isDryWetMod && dryWetCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, dryWetCV[i]);  // Clamp CV to 0-1 range
            currentDryWet = cv;  // Direct mapping (CV 0-1 → Dry/Wet 0-1)
        }

        // ✅ Update smoothed values
        smoothedFrequency.setTargetValue(currentFreq);
        smoothedDrive.setTargetValue(currentDrive);
        smoothedDryWet.setTargetValue(currentDryWet);  // ✅ NEW: Update smoothed value

        // ... oscillator processing ...
        
        // ✅ Use smoothed value in audio processing
        const float wet = smoothedDryWet.getNextValue();  // Get smoothed value
        const float dry = 1.0f - wet;
        const float wetSignalL = shaped * inL;
        const float wetSignalR = shaped * inR;
        
        outL[i] = dry * inL + wet * wetSignalL;  // ✅ Mix dry and wet
        outR[i] = dry * inR + wet * wetSignalR;

        // ✅ Update live parameter telemetry (for UI display)
        if ((i & 0x3F) == 0)  // Every 64 samples
        {
            setLiveParamValue("frequency_live", smoothedFrequency.getCurrentValue());
            setLiveParamValue("waveform_live", (float) currentWave);
            setLiveParamValue("drive_live", smoothedDrive.getCurrentValue());
            setLiveParamValue("dryWet_live", smoothedDryWet.getCurrentValue());  // ✅ NEW: Telemetry
        }
    }
}
```

**Key Implementation Details:**

1. **Connection Check**: `isParamInputConnected(paramIdDryWetMod)` checks if a cable is connected
2. **Pointer Safety**: Always check `inBus.getNumChannels() > channelIndex` before getting pointer
3. **CV Clamping**: Use `juce::jlimit(0.0f, 1.0f, cv)` to ensure CV stays in valid range
4. **Smoothing**: Use `getNextValue()` in the audio loop for smooth parameter changes
5. **Telemetry**: Update `_live` values for UI display (every 64 samples for efficiency)

**CV Mapping Strategies:**

- **Direct Mapping** (used here): CV 0-1 → Parameter 0-1
  ```cpp
  currentDryWet = cv;  // Simple 1:1 mapping
  ```

- **Relative Modulation**: CV modulates around base value
  ```cpp
  const float multiplier = std::pow(4.0f, cv - 0.5f);  // 0.25x to 4x range
  currentDryWet = baseDryWet * multiplier;
  ```

- **Absolute Mapping**: CV maps to full parameter range
  ```cpp
  currentDryWet = juce::jmap(cv, 0.0f, 1.0f);  // Maps to full range
  ```

#### 2.5 Add UI Slider in drawParametersInNode

```cpp
#if defined(PRESET_CREATOR_UI)
void YourModuleProcessor::drawParametersInNode(float itemWidth,
                                                const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    // ... get other parameter values ...
    
    // ✅ NEW: Get dry/wet value (check if modulated)
    const bool dryWetIsMod = isParamModulated(paramIdDryWetMod);
    float dryWet = dryWetIsMod ? getLiveParamValueFor(paramIdDryWetMod, "dryWet_live", dryWetParam ? dryWetParam->load() : 1.0f)
                               : (dryWetParam ? dryWetParam->load() : 1.0f);

    ImGui::PushItemWidth(itemWidth);

    // ... other UI elements ...
    
    // ✅ NEW: Add dry/wet slider
    if (dryWetIsMod) ImGui::BeginDisabled();  // Disable slider when CV is connected
    if (ImGui::SliderFloat("Dry/Wet", &dryWet, 0.0f, 1.0f, "%.2f"))
    {
        if (!dryWetIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDryWet))) *p = dryWet;
    }
    if (!dryWetIsMod) adjustParamOnWheel(ap.getParameter(paramIdDryWet), "dryWet", dryWet);  // Mouse wheel support
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (dryWetIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }  // Show "(mod)" label
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mix between dry input (0.0) and wet processed signal (1.0)");

    ImGui::PopItemWidth();
}
#endif
```

**UI Implementation Details:**

1. **Modulation Detection**: `isParamModulated(paramIdDryWetMod)` checks if CV is connected
2. **Live Value Display**: When modulated, show live telemetry value instead of static parameter
3. **Slider Disable**: Disable slider when CV is connected (prevents conflicts)
4. **Modulation Indicator**: Show "(mod)" label when parameter is being modulated
5. **Mouse Wheel Support**: `adjustParamOnWheel()` enables mouse wheel adjustment
6. **Tooltip**: Provide helpful description for users

#### 2.6 Add Routing in getParamRouting

```cpp
bool YourModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;  // Always use input bus 0
    if (paramId == paramIdFrequencyMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdWaveformMod)  { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdDriveMod)     { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdDryWetMod)    { outChannelIndexInBus = 5; return true; }  // ✅ NEW: Route to channel 5
    return false;
}
```

**Critical:** The channel index (5) must match:
- The channel used in `processBlock` (`getReadPointer(5)`)
- The channel count in constructor (`discreteChannels(6)` means channels 0-5)
- The channel label in `getAudioInputLabel()`

#### 2.7 Update Input/Output Labels

```cpp
void YourModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In L", 0, "Out L", 0);
    helpers.drawParallelPins("In R", 1, "Out R", 1);
    helpers.drawParallelPins("Freq Mod", 2, nullptr, -1);
    helpers.drawParallelPins("Wave Mod", 3, nullptr, -1);
    helpers.drawParallelPins("Drive Mod", 4, nullptr, -1);
    helpers.drawParallelPins("Dry/Wet Mod", 5, nullptr, -1);  // ✅ NEW: Add pin visualization
}

juce::String YourModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Freq Mod";
        case 3: return "Wave Mod";
        case 4: return "Drive Mod";
        case 5: return "Dry/Wet Mod";  // ✅ NEW: Return label for channel 5
        default: return {};
    }
}
```

---

### Step 3: Update Pin Database

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

#### 3.1 Add CV Input Pin

Find your module's entry in `populatePinDatabase()` and add the new CV input:

```cpp
db["shaping_oscillator"] = ModulePinInfo(
    NodeWidth::Medium,
    { 
        AudioPin("In L", 0, PinDataType::Audio), 
        AudioPin("In R", 1, PinDataType::Audio), 
        AudioPin("Freq Mod", 2, PinDataType::CV), 
        AudioPin("Wave Mod", 3, PinDataType::CV), 
        AudioPin("Drive Mod", 4, PinDataType::CV),
        AudioPin("Dry/Wet Mod", 5, PinDataType::CV)  // ✅ NEW: Add CV input pin
    },
    { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
    { 
        ModPin("Frequency", "frequency_mod", PinDataType::CV), 
        ModPin("Waveform", "waveform_mod", PinDataType::CV), 
        ModPin("Drive", "drive_mod", PinDataType::CV),
        ModPin("Dry/Wet", "dryWet_mod", PinDataType::CV)  // ✅ NEW: Add modulation pin
    }
);
```

**Pin Database Structure:**

1. **AudioPin (Inputs)**: `AudioPin("Display Name", channelIndex, PinDataType::CV)`
   - `"Dry/Wet Mod"`: Display name shown in UI
   - `5`: Channel index (must match `getParamRouting()`)
   - `PinDataType::CV`: Blue pin for control voltage

2. **ModPin (Modulation Metadata)**: `ModPin("Display Name", "parameterId_mod", PinDataType::CV)`
   - `"Dry/Wet"`: Display name for the parameter
   - `"dryWet_mod"`: Must match `paramIdDryWetMod` constant
   - Used by UI to disable/enable parameter widgets when CV is connected

**Why Both?**
- **AudioPin**: Defines the actual connectable socket in the editor
- **ModPin**: Metadata that tells the UI "this parameter can be modulated" and disables the slider when connected

---

### Step 4: Update User Manual

**File:** `USER_MANUAL/Nodes_Dictionary.md`

Find your module's section and update the documentation:

```markdown
### shaping_oscillator
**Oscillator with Built-in Waveshaper**

An oscillator with integrated waveshaping for generating harmonically rich tones.

#### Inputs
- `In L` (Audio) - External audio input (optional, can shape external audio)
- `In R` (Audio) - External audio input right channel
- `Freq Mod` (CV) - Frequency modulation
- `Wave Mod` (CV) - Waveform modulation
- `Drive Mod` (CV) - Drive modulation
- `Dry/Wet Mod` (CV) - ✅ NEW: Dry/wet mix modulation

#### Outputs
- `Out L` (Audio) - Shaped oscillator output left channel
- `Out R` (Audio) - Shaped oscillator output right channel

#### Parameters
- `Frequency` (20 Hz - 20 kHz) - Oscillator frequency
- `Waveform` (Choice) - Base waveform
- `Drive` (0-10) - Waveshaping amount
- `Dry/Wet` (0-1) - ✅ NEW: Mix between dry input (0.0) and wet processed signal (1.0)

#### How to Use
1. Set frequency for desired pitch
2. Choose base waveform
3. Increase drive to add harmonics via waveshaping
4. ✅ NEW: Adjust dry/wet to blend between input and processed signal
5. Can also process external audio through the shaper
6. Great for thick, harmonically rich tones
```

**Documentation Guidelines:**

1. **Inputs Section**: List all inputs including the new CV input
2. **Parameters Section**: Document the new parameter with its range
3. **How to Use Section**: Add usage tips for the new parameter
4. **Be Specific**: Include ranges, default values, and practical examples

---

## Verification Checklist

After implementing, verify each step:

### Code Implementation
- [ ] Parameter ID constants added (base + `_mod` suffix)
- [ ] Parameter added to `createParameterLayout()`
- [ ] Parameter pointer cached in constructor
- [ ] Smoothed value reset in `prepareToPlay()`
- [ ] CV input reading in `processBlock()` (with buffer aliasing safety)
- [ ] Live telemetry updated (`setLiveParamValue()`)
- [ ] UI slider added in `drawParametersInNode()`
- [ ] Routing added in `getParamRouting()` (channel index matches)
- [ ] Input bus channel count updated in constructor
- [ ] Input labels updated (`getAudioInputLabel()`, `drawIoPins()`)

### Pin Database
- [ ] CV input pin added to `audioIns` vector
- [ ] Modulation pin added to `modIns` vector
- [ ] Channel indices match implementation
- [ ] Parameter IDs match constants

### Documentation
- [ ] Input listed in manual
- [ ] Parameter documented with range
- [ ] Usage tips added

### Testing
- [ ] Slider works when no CV connected
- [ ] Slider disabled when CV connected
- [ ] "(mod)" label appears when CV connected
- [ ] CV modulation works correctly
- [ ] Smooth transitions (no zipper noise)
- [ ] Live value updates in UI
- [ ] Pin appears in editor with correct label
- [ ] Pin color is correct (blue for CV)

---

## Common Pitfalls and Solutions

### Pitfall 1: CV Input Always Reads Zero

**Symptom:** CV input pointer is valid, but value is always 0.0

**Cause:** Buffer aliasing - output buffer operations cleared the input

**Solution:** 
- Read input pointers BEFORE any output operations
- Don't call `outBus.clear()` if you write to all channels
- See `guides/DEBUG_INPUT_IMPORTANT.md` for details

### Pitfall 2: Channel Index Mismatch

**Symptom:** CV input doesn't work, or wrong channel receives the signal

**Cause:** Channel index mismatch between:
- Constructor: `discreteChannels(N)`
- `getParamRouting()`: return value
- `processBlock()`: `getReadPointer(channel)`
- Pin database: `AudioPin(..., channel, ...)`

**Solution:** 
- Use a single constant or comment to track channel assignments
- Verify all locations use the same index

### Pitfall 3: Parameter Not Smoothing

**Symptom:** Zipper noise or clicks when parameter changes

**Cause:** Not using smoothed value, or smoothing not reset in `prepareToPlay()`

**Solution:**
- Always use `smoothedParam.getNextValue()` in audio loop
- Call `smoothedParam.reset(sampleRate, 0.01)` in `prepareToPlay()`
- Use `setTargetValue()` to update target, not direct assignment

### Pitfall 4: UI Slider Not Disabling

**Symptom:** Slider still editable when CV is connected

**Cause:** Missing `isParamModulated()` check or incorrect parameter ID

**Solution:**
- Verify `isParamModulated(paramIdDryWetMod)` uses the `_mod` suffix ID
- Check that ModPin in database uses matching parameter ID
- Ensure `getParamRouting()` returns true for the modulation ID

### Pitfall 5: Pin Not Appearing in Editor

**Symptom:** CV input pin doesn't show up in node editor

**Cause:** Pin database not updated, or wrong module name key

**Solution:**
- Verify module name in database matches `getName()` return value (lowercase)
- Check that `AudioPin` is in `audioIns` vector, not `audioOuts`
- Ensure channel index is valid (within bus channel count)

---

## Advanced: Relative vs Absolute Modulation

Some parameters support both relative and absolute modulation modes. Here's how to implement it:

### Add Mode Parameter

```cpp
// In createParameterLayout()
params.push_back(std::make_unique<juce::AudioParameterBool>(
    "relativeDryWetMod", "Relative Dry/Wet Mod", true));  // Default: relative mode
```

### Process with Mode Check

```cpp
const bool relativeDryWetMode = relativeDryWetModParam != nullptr && relativeDryWetModParam->load() > 0.5f;

float currentDryWet = baseDryWet;
if (isDryWetMod && dryWetCV)
{
    const float cv = juce::jlimit(0.0f, 1.0f, dryWetCV[i]);
    
    if (relativeDryWetMode)
    {
        // RELATIVE: CV modulates around base value (±50% range)
        const float offset = (cv - 0.5f) * 1.0f;  // -0.5 to +0.5
        currentDryWet = juce::jlimit(0.0f, 1.0f, baseDryWet + offset);
    }
    else
    {
        // ABSOLUTE: CV directly maps to full range
        currentDryWet = cv;  // 0-1 maps to 0-1
    }
}
```

### Add UI Toggle

```cpp
bool relativeDryWetMod = relativeDryWetModParam ? (relativeDryWetModParam->load() > 0.5f) : true;
if (ImGui::Checkbox("Relative Dry/Wet Mod", &relativeDryWetMod))
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDryWetMod")))
        *p = relativeDryWetMod;
}
if (ImGui::IsItemHovered()) 
    ImGui::SetTooltip("Relative: CV modulates around slider\nAbsolute: CV directly controls value");
```

---

## Summary

Adding a new parameter with CV input requires careful coordination across multiple files:

1. **Header**: Add parameter IDs and member variables
2. **Implementation**: 
   - Create parameter in layout
   - Initialize pointers and smoothing
   - Read CV input safely (buffer aliasing!)
   - Process with smoothing
   - Update UI and routing
3. **Pin Database**: Add input pin and modulation metadata
4. **Documentation**: Update user manual

**Golden Rules:**
- 🌟 Read inputs BEFORE output operations (buffer aliasing safety)
- 🌟 Use smoothed values for all user parameters
- 🌟 Match channel indices across all locations
- 🌟 Use `_mod` suffix for virtual routing IDs
- 🌟 Update pin database and documentation

---

## Related Guides

- `guides/DEBUG_INPUT_IMPORTANT.md` - Buffer aliasing and input safety
- `guides/ADD_NEW_NODE_COMPREHENSIVE_GUIDE.md` - Complete node creation
- `guides/PIN_DATABASE_SYSTEM_GUIDE.md` - Pin database architecture

---

*Last Updated: December 2024*  
*Based on implementation of Dry/Wet parameter for ShapingOscillatorModuleProcessor*

