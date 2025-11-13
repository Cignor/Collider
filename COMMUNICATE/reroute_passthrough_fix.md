# Reroute Node Passthrough and Dynamic Naming Fix

## Current Situation

The reroute module has two critical issues:

1. **Not Actually Passthrough**: The `processBlock` implementation is clearing the output buffer and copying, but signals aren't passing through correctly. Looking at `ScopeModuleProcessor` as a reference, it uses `juce::FloatVectorOperations::copy` with an aliasing check (`if (dst != srcCh)`).

2. **No Dynamic Naming**: The node title always shows "reroute" instead of adapting to show the signal type (e.g., "Reroute Raw", "Reroute CV", "Reroute Video").

3. **Pin Colors**: While the dynamic pin system should handle colors, we need to ensure both input and output pins use the same color based on the adopted type.

## Implementation Plan

### 1. Fix Passthrough in `processBlock`

The current implementation:
```cpp
output.clear();
for (int ch = 0; ch < numChannels; ++ch)
    output.copyFrom(ch, 0, input, ch, 0, numSamples);
```

Should be changed to match the pattern from `ScopeModuleProcessor`:
```cpp
const int n = buffer.getNumSamples();
if (in.getNumChannels() > 0 && out.getNumChannels() > 0)
{
    auto* srcCh = in.getReadPointer(0);
    auto* dst = out.getWritePointer(0);
    if (dst != srcCh) juce::FloatVectorOperations::copy(dst, srcCh, n);
}
```

This avoids clearing and handles potential buffer aliasing correctly.

### 2. Dynamic Node Title

The node title is rendered at line 2522 in `ImGuiNodeEditorComponent.cpp`:
```cpp
ImGui::TextUnformatted (type.toRawUTF8());
```

Where `type` comes from `getTypeForLogical(lid)`. For reroute nodes, we need to:
- Check if the module is a reroute
- Query its `getPassthroughType()`
- Format the display name as "Reroute [Type]" (e.g., "Reroute Raw", "Reroute CV", "Reroute Video")
- Use a helper function to convert `PinDataType` to a display string

### 3. Pin Color Consistency

The dynamic pin system should already handle this via `getDynamicInputPins()` and `getDynamicOutputPins()`, both returning the same `PinDataType`. We just need to verify the UI respects these types when drawing pins.

## Questions for External Expert

1. **Buffer Passthrough**: Is the `FloatVectorOperations::copy` with aliasing check the correct pattern for true passthrough in JUCE's bus system? Should we handle multi-channel differently?

2. **Dynamic Node Names**: Is modifying the title bar rendering (line 2522) the right approach, or should we add a virtual method like `getDisplayName()` to `ModuleProcessor` that modules can override?

3. **Type String Conversion**: What's the best way to convert `PinDataType` enum values to user-friendly strings? Should we create a helper function like `pinDataTypeToDisplayString(PinDataType)`?

4. **Performance**: Will querying the reroute's type on every frame (for title rendering) cause performance issues, or is the atomic read fast enough?

## Files to Modify

1. `juce/Source/audio/modules/RerouteModuleProcessor.cpp` - Fix `processBlock`
2. `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` - Add dynamic title rendering for reroute nodes (around line 2522)
3. Possibly add a helper function for `PinDataType` to string conversion

