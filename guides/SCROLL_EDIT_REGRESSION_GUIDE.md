# Scroll Edit Regression Guide

## Overview

Scroll-edit (mouse-wheel editing) is a core affordance in Collider’s node editor. Every ImGui control that tweaks a JUCE parameter should react to the scroll wheel when the control is hovered and the parameter is not being modulated. This guide explains how to audit existing nodes, recognize broken behaviours, and patch them consistently.

## What “Right” Looks Like

- Hovering a slider, drag float, combo, or vertical slider lets the user adjust values with the mouse wheel.
- Wheel edits obey modulation rules: if a parameter is CV-controlled, scroll is disabled.
- Values clamp to the parameter’s declared range and trigger `onModificationEnded()` so state persists.
- Log-style parameters (frequency, time, BPM) get meaningful steps; enums advance by one entry in the intuitive direction (scroll down moves to the next option; scroll up moves to the previous).

## Typical Failure Cases

| Symptom | Root Cause |
| --- | --- |
| Wheel does nothing | Missing `adjustParamOnWheel` call or hover check |
| Wheel adjusts while modulated | Forgot to guard on `isParamModulated` / `modConnected` |
| Wheel jumps unpredictably | Manual implementation without clamping or with wrong step size |
| Dropdown scroll direction inverted | Added custom handler but forgot to invert delta (down should increment) |
| Gate/frequency sliders react when disabled | Did not wrap controls in `ImGui::BeginDisabled()`/`EndDisabled()` |

## Audit Strategy

1. **Search** for `ImGui::Slider`, `ImGui::VSliderFloat`, `ImGui::DragFloat`, `ImGui::Combo`, and `BeginCombo` in the node’s `drawParametersInNode()` function.
2. **Check modulation flow**: locate the `isParamModulated`/`isParamInputConnected` flag that disables manual edits. Scroll handling must sit inside the `!isModulated` branch.
3. **Verify helper usage**:
   - Preferred: `adjustParamOnWheel(ap.getParameter(paramId), paramId, displayedValue);`
   - Manual (rare): custom hover handler with explicit range clamps.
4. **Confirm enum handling**: combos need explicit wheel logic if `adjustParamOnWheel` can’t see the hover context (e.g., table cells). Ensure delta sign matches expected direction.
5. **Exercise UI**: run Preset Creator, hover each control, and scroll to confirm expected response.

## Fix Patterns

### 1. Standard Sliders (horizontal or vertical)

```cpp
float value = isModulated ? getLiveParamValueFor(...) : param->load();
if (isModulated) ImGui::BeginDisabled();
if (ImGui::SliderFloat("Label", &value, min, max, "%.2f")) {
    if (!isModulated) *param = value;
}
if (ImGui::IsItemDeactivatedAfterEdit() && !isModulated) onModificationEnded();
if (!isModulated) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
if (isModulated) ImGui::EndDisabled();
```

- Works for `ImGui::SliderFloat`, `ImGui::VSliderFloat`, `ImGui::DragFloat`, `ImGui::SliderInt`.
- `adjustParamOnWheel` lives in `ModuleProcessor.h` and auto-derives sensible step sizes from the parameter ID (Hz, ms, dB, normalized, etc.).

### 2. Manual Wheel Handling (Sequencer steps, per-step gates)

Use manual handlers when you need a fixed delta or when the control is not backed by a single parameter pointer (e.g., arrays of per-step values).

```cpp
if (!modConnected && ImGui::IsItemHovered()) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        const float delta = wheel > 0.0f ? customStep : -customStep;
        const float newValue = juce::jlimit(min, max, baseValue + delta);
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
            *p = newValue;
    }
}
```

### 3. Combo/Enum Controls in Tables

`adjustParamOnWheel` cannot automatically hook into combo widgets that return immediately (e.g., PolyVCO table cells). Add a hover-sensitive handler:

```cpp
if (!isModulated && ImGui::IsItemHovered()) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        const int newIndex = juce::jlimit(0, maxIndex, currentIndex + (wheel > 0.0f ? -1 : 1));
        if (newIndex != currentIndex && param) {
            *param = newIndex;
            onModificationEnded();
        }
    }
}
```

Note the inverted sign so scrolling down advances forward in the list.

### 4. Global Inputs (Num Steps, Num Voices, BPM)

Use `adjustParamOnWheel` with the underlying `AudioParameterInt` to guarantee integer steps and clamping:

```cpp
if (!isModulated) adjustParamOnWheel(ap.getParameter("numSteps"), "numSteps", (float)displayedSteps);
```

### 5. Transport-Sensitive Gates/Triggers

When manual wheel handling updates per-step values, ensure you write to the actual parameter (not the derived gate envelope) so audio + UI stay in sync.

## Modules Already Patched

- `MultiSequencerModuleProcessor`: per-step pitch, gate, trigger sliders use manual wheel handling; global controls use `adjustParamOnWheel`.
- `PolyVCOModuleProcessor`: master voice count, portamento slider, per-voice frequency/gate drag floats, and waveform combos now react to scroll.
- `StepSequencerModuleProcessor`: legacy reference for manual per-step handling.

Use these files as references when touching other nodes.

## Implementation Checklist for Any Node

1. **Identify** every ImGui control tied to a JUCE parameter.
2. **Wrap** controls in modulation guards (`if (isModulated) ImGui::BeginDisabled();`).
3. **Add** `adjustParamOnWheel` for each slider/drag float when not modulated.
4. **Implement** custom hover handlers for controls where the helper cannot hook (combos, bespoke widgets).
5. **Clamp** values using `juce::jlimit` or parameter ranges.
6. **Call** `onModificationEnded()` after wheel-based assignments to trigger undo/state saves.
7. **Test** hover + scroll for both enabled and modulated states.
8. **Verify** directionality for combos (scroll down should advance).

## Troubleshooting Playbook

- **Nothing happens**: confirm `ImGui::IsItemHovered()` is true and the handler runs only when the widget exists (e.g., inside table rows).
- **Scroll works while modulated**: ensure the wheel call sits inside `if (!isModulated)`.
- **Jumping values**: check delta sizing or rely on `adjustParamOnWheel` to leverage the central heuristics.
- **Enum mismatch**: ensure the parameter accepts integer indices (use `AudioParameterChoice::operator=`).

## Next Steps

- Use this guide as a regression script each time a module’s UI is touched.
- For new nodes, enforce scroll support during code review; add `TODO` markers if a bespoke widget needs manual wheel handling.
- Consider writing automated UI smoke tests once ImGui automation is available.


