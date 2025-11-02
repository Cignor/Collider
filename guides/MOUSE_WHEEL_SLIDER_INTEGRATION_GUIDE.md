# Mouse Wheel Slider Integration Guide

## Overview

This guide demonstrates how to properly integrate mouse scroll wheel (edit wheel) functionality for ImGui sliders in JUCE modules. The codebase provides a best-practice helper function that automatically calculates appropriate step sizes based on parameter types.

## Best Practice: Using `adjustParamOnWheel` Helper

The **recommended approach** is to use the static helper function `ModuleProcessor::adjustParamOnWheel()`. This function automatically:
- Checks if the slider is hovered
- Calculates appropriate step sizes based on parameter ID/name
- Handles Float, Choice, and Int parameter types
- Clamps values to valid ranges

### Location

The helper function is defined in:
- **File**: `juce/Source/audio/modules/ModuleProcessor.h`
- **Lines**: 441-508

### Function Signature

```cpp
static void adjustParamOnWheel(
    juce::RangedAudioParameter* parameter,
    const juce::String& idOrName,
    float displayedValue
)
```

## Implementation Pattern

### Standard Slider Pattern

For most sliders, follow this pattern after drawing your `ImGui::SliderFloat`:

```cpp
// 1. Draw the slider
float value = isModulated ? getLiveParamValueFor(...) : paramPtr->load();
if (isModulated) ImGui::BeginDisabled();
if (ImGui::SliderFloat("Label", &value, min, max, format))
{
    if (!isModulated)
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)) = value;
}
// 2. Add mouse wheel support (only when not modulated)
if (!isModulated)
    adjustParamOnWheel(apvts.getParameter(paramId), paramId, value);
if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
if (isModulated) ImGui::EndDisabled();
```

### Complete Example: Compressor Module

Reference implementation from `CompressorModuleProcessor.cpp`:

```cpp
auto drawSlider = [&](const char* label, const juce::String& paramId, 
                      const juce::String& modId, float min, float max, 
                      const char* format, const char* tooltip) 
{
    bool isMod = isParamModulated(modId);
    float value = isMod 
        ? getLiveParamValueFor(modId, paramId + juce::String("_live"), 
                               ap.getRawParameterValue(paramId)->load())
        : ap.getRawParameterValue(paramId)->load();
    
    if (isMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat(label, &value, min, max, format))
        if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
    // Mouse wheel support
    if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    if (tooltip) { ImGui::SameLine(); HelpMarkerComp(tooltip); }
};
```

## Automatic Step Size Calculation

The `adjustParamOnWheel` function automatically calculates appropriate step sizes based on parameter ID/name patterns:

### Frequency Parameters
- **Pattern**: `"hz"`, `"freq"`, `"cutoff"`, `"rate"`
- **Step**: Logarithmic scaling based on current value
- **Example**: 440 Hz → ~1 Hz steps, 10000 Hz → ~10 Hz steps

### Time Parameters
- **Pattern**: `"ms"`, `"time"`
- **Step**: Logarithmic scaling based on current value
- **Example**: 5 ms → ~0.1 ms steps, 500 ms → ~10 ms steps

### Decibel Parameters
- **Pattern**: `"db"`, `"gain"`
- **Step**: 0.5 dB (fixed)

### Normalized Parameters
- **Pattern**: `"mix"`, `"depth"`, `"amount"`, `"resonance"`, `"q"`, `"size"`, `"damp"`, `"pan"`, `"threshold"`
- **Step**: 0.01 (1% of range)

### Sequencer Steps
- **Pattern**: `"step"`
- **Step**: 0.05 (fixed for fine control)

### Default Behavior
- **For ranges > 1.0**: 0.5% of range span
- **For ranges ≤ 1.0**: 0.01

### Choice/Enum Parameters
- Steps through choices one at a time
- Automatically wraps/clamps to valid indices

### Int Parameters
- Steps by 1 unit
- Automatically clamps to parameter range

## Manual Implementation (When Needed)

For special cases where custom step sizes are required, you can implement mouse wheel handling manually:

### Manual Pattern

```cpp
// After drawing the slider
if (!modConnected)
{
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float delta = (wheel > 0 ? customStep : -customStep);
            float newValue = juce::jlimit(minValue, maxValue, currentValue + delta);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(
                apvts.getParameter(paramId)))
            {
                *p = newValue;
            }
        }
    }
}
```

### Example: Step Sequencer (Fixed Delta)

Reference from `StepSequencerModuleProcessor.cpp`:

```cpp
// Wheel fine-tune: identical semantics to drag
if (!modConnected)
{
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float delta = (wheel > 0 ? 0.05f : -0.05f);
            float newBaseValue = juce::jlimit(0.0f, 1.0f, baseValue + delta);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(
                apvts.getParameter("step" + juce::String(i + 1))))
            {
                *p = newBaseValue;
            }
        }
    }
}
```

## Important Considerations

### 1. Modulation State

**Always check modulation state before enabling mouse wheel:**

```cpp
if (!isModulated)
    adjustParamOnWheel(...);
```

Or for manual implementation:
```cpp
if (!modConnected)
{
    // mouse wheel code here
}
```

### 2. Hover Detection

The helper function automatically checks `ImGui::IsItemHovered()`. If implementing manually, you must check it:

```cpp
if (ImGui::IsItemHovered())
{
    // mouse wheel code
}
```

### 3. Value Clamping

Always clamp values to the parameter's valid range:

```cpp
float newValue = juce::jlimit(range.start, range.end, currentValue + delta);
```

### 4. Parameter Type Safety

Always use dynamic_cast to safely get the parameter type:

```cpp
if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
    *p = newValue;
```

### 5. Vertical Sliders (VSliderFloat)

The same pattern applies to vertical sliders:

```cpp
if (ImGui::VSliderFloat(label, size, &value, min, max, format))
{
    // handle slider drag
}
// Mouse wheel support
if (!modConnected)
    adjustParamOnWheel(apvts.getParameter(paramId), paramId, value);
```

## Common Patterns by Module Type

### Audio Effect Modules (Compressor, Limiter, Phaser, etc.)

```cpp
auto drawSlider = [&](const char* label, const juce::String& paramId, 
                      const juce::String& modId, float min, float max, 
                      const char* format, const char* tooltip) 
{
    bool isMod = isParamModulated(modId);
    float value = isMod ? getLiveParamValueFor(...) : paramPtr->load();
    
    if (isMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat(label, &value, min, max, format))
        if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
    if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isMod) ImGui::EndDisabled();
};
```

### Oscillator Modules (VCO, PolyVCO, etc.)

```cpp
float freq = freqParam->load();
if (ImGui::SliderFloat("Frequency", &freq, minFreq, maxFreq, "%.2f Hz"))
{
    if (!freqMod) *freqParam = freq;
}
if (!freqMod) adjustParamOnWheel(ap.getParameter("frequency"), "frequencyHz", freq);
```

### Sequencer Modules

For sequencer step sliders with custom step sizes:

```cpp
if (ImGui::VSliderFloat(label, ImVec2(w, h), &value, 0.0f, 1.0f, ""))
{
    if (!modConnected) *param = value;
}
// Custom wheel handling for fine control
if (!modConnected)
{
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float delta = (wheel > 0 ? 0.05f : -0.05f);
            float newValue = juce::jlimit(0.0f, 1.0f, baseValue + delta);
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(param))
                *p = newValue;
        }
    }
}
```

### Enum/Choice Parameters

```cpp
int currentChoice = choiceParam->getIndex();
const char* preview = choiceParam->choices[currentChoice].toRawUTF8();
if (ImGui::BeginCombo("Choice", preview))
{
    // ... combo items ...
}
adjustParamOnWheel(choiceParam, "choiceParamId", (float)currentChoice);
```

## Testing Checklist

When adding mouse wheel support, verify:

- [ ] Mouse wheel works when hovering over the slider
- [ ] Mouse wheel is disabled when parameter is modulated
- [ ] Step sizes are appropriate for the parameter type
- [ ] Values are correctly clamped to parameter range
- [ ] Works with both horizontal (`SliderFloat`) and vertical (`VSliderFloat`) sliders
- [ ] No crashes or asserts when wheel is scrolled
- [ ] Works correctly with Choice/Int parameters

## Troubleshooting

### Mouse wheel not working

1. **Check hover detection**: Ensure `ImGui::IsItemHovered()` returns true
2. **Check modulation state**: Verify `!isModulated` condition
3. **Verify parameter exists**: Ensure `apvts.getParameter(paramId)` returns non-null
4. **Check ImGui IO**: Verify `ImGui::GetIO().MouseWheel != 0.0f`

### Wrong step size

1. **Check parameter ID**: The helper uses the ID to determine step size
2. **Verify parameter name**: Ensure your parameter ID contains expected keywords (hz, ms, db, etc.)
3. **Consider manual implementation**: For very specific requirements

### Values not updating

1. **Check parameter type**: Ensure using correct cast (`AudioParameterFloat`, `AudioParameterInt`, etc.)
2. **Verify parameter assignment**: Check that `*p = newValue` syntax is correct
3. **Check range**: Ensure value is within parameter's valid range

## Reference Files

### Best Practice Examples

1. **`juce/Source/audio/modules/ModuleProcessor.h`** (lines 441-508)
   - Helper function implementation

2. **`juce/Source/audio/modules/CompressorModuleProcessor.cpp`** (line 259)
   - Clean lambda pattern with modulation support

3. **`juce/Source/audio/modules/ChorusModuleProcessor.cpp`** (lines 252, 264, 276)
   - Multiple parameter examples

4. **`juce/Source/audio/modules/TempoClockModuleProcessor.cpp`** (lines 304, 334)
   - BPM and swing parameter examples

### Manual Implementation Examples

1. **`juce/Source/audio/modules/StepSequencerModuleProcessor.cpp`** (lines 577-591, 638-651)
   - Custom step size for sequencer steps

2. **`juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp`** (lines 469-473, 505-508)
   - Similar pattern for multi-sequencer

## Summary

**Always prefer `adjustParamOnWheel()` for mouse wheel integration** because it:
- Automatically calculates appropriate step sizes
- Handles multiple parameter types
- Includes hover detection
- Clamps values correctly
- Follows codebase conventions

Use manual implementation only when you need very specific custom step sizes that don't fit the automatic calculation patterns.

