

## Guide to Creating a "Best Practice" Module

This document outlines the best practices for creating a new `ModuleProcessor` in this modular synthesizer environment. The `BestPracticeNodeProcessor` serves as a live, working example of these principles. Following this guide ensures that new modules are robust, efficient, and integrate correctly with the UI, modulation system, and audio engine.

-----

### **1. Core Functionality of the Example Node**

[cite\_start]The `BestPracticeNodeProcessor` is a shaping oscillator that can also function as a ring modulator[cite: 148].

  * [cite\_start]**As an Oscillator**: It generates a standard waveform (Sine, Saw, or Square)[cite: 144].
  * [cite\_start]**As a Shaper**: It applies a `tanh` distortion function, controlled by a "Drive" parameter, to the generated waveform[cite: 148].
  * **As a Ring Modulator**: It multiplies its output with an incoming audio signal. [cite\_start]If no audio is connected, the input defaults to `1.0`, and it functions as a simple oscillator[cite: 148].

-----

### **2. The Modulation Input System**

This is the most critical pattern to follow. It ensures that UI parameters can be overridden by CV inputs in a thread-safe and predictable way.

#### **Step 1: Define Parameter and Modulation IDs (`.h`)**

For every parameter that can be modulated, you must define two `constexpr` string IDs in the header file: one for the parameter itself and one "virtual" ID for its modulation input.

  * [cite\_start]The **parameter ID** (e.g., `"frequency"`) is used for the `juce::AudioProcessorValueTreeState` (APVTS)[cite: 153].
  * The **virtual modulation ID** (e.g., `"frequency_mod"`) is used *only* for routing and UI checks. [cite\_start]It **must not** have a corresponding parameter in the APVTS[cite: 153].

**Example from `BestPracticeNodeProcessor.h`:**

```cpp
// Parameter IDs for APVTS
static constexpr auto paramIdFrequency    = "frequency"; [cite_start]// [cite: 153]
static constexpr auto paramIdWaveform     = "waveform"; [cite_start]// [cite: 153]
static constexpr auto paramIdDrive        = "drive"; [cite_start]// [cite: 153]

// Virtual modulation target IDs (no APVTS parameters required)
static constexpr auto paramIdFrequencyMod = "frequency_mod"; [cite_start]// [cite: 153]
static constexpr auto paramIdWaveformMod  = "waveform_mod"; [cite_start]// [cite: 153]
static constexpr auto paramIdDriveMod     = "drive_mod"; [cite_start]// [cite: 153]
```

-----

#### **Step 2: Use a Single, Unified Input Bus (`.cpp`)**

All inputs for a module, both audio and CV, should be defined on a single discrete input bus in the constructor. This simplifies the routing logic significantly.

**Example from `BestPracticeNodeProcessor.cpp`:**

```cpp
BestPracticeNodeProcessor::BestPracticeNodeProcessor()
    : ModuleProcessor(BusesProperties()
                        [cite_start].withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // [cite: 143]
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      // ...
```

[cite\_start]Here, the 5 channels are for Audio In L/R, Freq Mod, Wave Mod, and Drive Mod[cite: 143, 150].

-----

#### **Step 3: Implement `getParamRouting()` (`.cpp`)**

[cite\_start]This function is the bridge between the virtual modulation IDs and their physical channel indices on the input bus [cite: 157-158].

**Example from `BestPracticeNodeProcessor.cpp`:**

```cpp
bool BestPracticeNodeProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus.
    if (paramId == paramIdFrequencyMod) { outChannelIndexInBus = 2; return true; [cite_start]} // [cite: 150]
    if (paramId == paramIdWaveformMod)  { outChannelIndexInBus = 3; return true; [cite_start]} // [cite: 150]
    if (paramId == paramIdDriveMod)     { outChannelIndexInBus = 4; return true; [cite_start]} // [cite: 150]
    return false;
}
```

-----

#### **Step 4: Use Modulation in `processBlock()` (`.cpp`)**

The audio processing logic must follow this pattern:

1.  [cite\_start]Check if a modulation input is connected using `isParamInputConnected()` with the **virtual `_mod` ID**[cite: 143].
2.  [cite\_start]If connected, get a pointer to the CV signal from the correct channel index on the input bus [cite: 143-144].
3.  In the per-sample loop, read the CV value and apply it. [cite\_start]If not connected, use the base value from the APVTS parameter[cite: 144].
4.  Update "live" telemetry values for the UI using `setLiveParamValue()`. [cite\_start]This is crucial for visual feedback[cite: 147].

**Example from `ShapingOscillatorModuleProcessor.cpp`:**

```cpp
// 1. Check for connection
const bool isFreqMod  = isParamInputConnected(paramIdFrequencyMod); [cite_start]// [cite: 708]

// 2. Get CV pointer
const float* freqCV   = isFreqMod  && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr; [cite_start]// [cite: 709]

const float baseFrequency = frequencyParam->load();

for (int i = 0; i < buffer.getNumSamples(); ++i)
{
    // 3. Apply CV if connected
    float currentFreq = baseFrequency;
    if (isFreqMod && freqCV)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, freqCV[i]); [cite_start]// [cite: 710]
        // ... logic to apply CV ...
        currentFreq = fMin * std::pow(2.0f, cv * spanOct); [cite_start]// [cite: 710]
    }

    // ... use 'currentFreq' in DSP ...

    // 4. Update telemetry (throttled)
    [cite_start]if ((i & 0x3F) == 0) // [cite: 712]
    {
        setLiveParamValue("frequency_live", smoothedFrequency.getCurrentValue()); [cite_start]// [cite: 712]
    }
}
```

-----

#### **Step 5: Implement UI Feedback in `drawParametersInNode()` (`.h`)**

The UI code must use the `isParamModulated` lambda (which is passed into the function) with the **virtual `_mod` ID** to determine the UI state.

1.  [cite\_start]Check if the parameter is modulated[cite: 148].
2.  If it is, get the live value from the audio thread using `getLiveParamValueFor()`. [cite\_start]This function takes both the virtual `_mod` ID (to re-verify the connection) and the `_live` telemetry key[cite: 148].
3.  [cite\_start]Disable the slider and show an `(mod)` indicator[cite: 148].
4.  [cite\_start]If not modulated, the slider should be active and control the APVTS parameter directly [cite: 148-149].

**Example from `BestPracticeNodeProcessor.h`:**

```cpp
// 1. Check for modulation
const bool freqIsMod = isParamModulated(paramIdFrequencyMod); [cite_start]// [cite: 148]

// 2. Get live or base value
float freq = freqIsMod ? getLiveParamValueFor(paramIdFrequencyMod, "frequency_live", frequencyParam->load())
                       : frequencyParam->load(); [cite_start]// [cite: 148]

// 3. Disable UI and show indicator
if (freqIsMod) ImGui::BeginDisabled(); [cite_start]// [cite: 148]

if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
{
    if (!freqIsMod) // 4. Only update parameter if not modulated
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = freq; [cite_start]// [cite: 148-149]
}

if (freqIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); [cite_start]} // [cite: 149]
```

-----

### **3. Audio Processing Best Practices**

  * [cite\_start]**Prevent Zipper Noise**: For parameters that change rapidly (especially under modulation), use `juce::SmoothedValue` to interpolate values over time and prevent audible clicks[cite: 145]. [cite\_start]Initialize it in `prepareToPlay()` and update its target value inside the `processBlock()` loop[cite: 145].
  * **Safe State Changes**: Avoid expensive operations like memory allocation on the audio thread. [cite\_start]For waveform switching, the `BestPracticeNodeProcessor` uses a clean pattern: it only calls `oscillator.initialise()` when the integer `waveform` choice has actually changed from the previous sample [cite: 145-146].
  * **Avoid In-Place Processing Bugs 🐛**: A common mistake is to clear an output buffer before you have finished reading from the input buffer. In JUCE's `AudioProcessorGraph`, the input and output buffers can be the **same piece of memory**. Clearing the output can instantly wipe out your input signal.
      * **Incorrect Pattern**:
        ```cpp
        auto inBus = getBusBuffer(buffer, true, 0);
        auto outBus = getBusBuffer(buffer, false, 0);
        outBus.clear(); // DANGER: This might have just erased inBus!
        process(inBus); // Now processing silence.
        ```
      * **Correct Pattern**: Always perform your analysis or read your inputs *before* you modify or clear the output buffer.
        ```cpp
        auto inBus = getBusBuffer(buffer, true, 0);
        auto outBus = getBusBuffer(buffer, false, 0);

        // 1. Analyze or process the input first.
        analyze(inBus);

        // 2. Now it's safe to handle the output.
        // For passthrough, copy the original input.
        outBus.copyFrom(0, 0, inBus, 0, 0, numSamples); 
        ```
      * [cite\_start]This exact bug was found and fixed in the `FrequencyGraphModuleProcessor`[cite: 313, 314, 317].
  * [cite\_start]**Clear Pin Definitions**: Provide clear, human-readable labels for all input and output pins by overriding `getAudioInputLabel()` and `getAudioOutputLabel()`[cite: 150]. This information is used by the UI for tooltips and the pin database.