# Node Visualization Best Practices

The Collider UI is extremely sensitive to how every module renders its custom ImGui content.  
This guide explains the checklist we now follow whenever we add a visualization to a node so that it:

1. Stays in sync with the theme editor (colors, fonts, paddings).  
2. Never corrupts the ImGui / ImNodes state stack or the minimap.  
3. Keeps the audio thread isolated from UI logic.

---

## 1. Gather Live Data Safely

1. **Extend the processor header** with a `VizData` struct that only contains `std::atomic` fields (floats or fixed arrays).  
   - Never pass raw `juce::AudioBuffer` pointers to the UI thread.  
   - Example reference: `DriveModuleProcessor::VizData`, `GateModuleProcessor::VizData`, `Limiters/Compressors`.

2. **Capture audio snapshots inside `processBlock`**:
   - Copy the current block into a temporary buffer (or reuse an existing dry buffer) and down-sample to your `VizData::waveformPoints`.  
   - Update all `VizData` fields atomically before the function returns.
   - Keep heavy work behind `#if defined(PRESET_CREATOR_UI)` guards so audio builds stay lean.

3. **Throttle expensive work** (e.g. by updating every N samples or by down-sampling) to avoid blowing up CPU.  
   - Example: FrequencyGraph skips new FFT pushes whenever its FIFO already has unread frames.
4. **Reuse buffers instead of allocating every block.**  
   - Keep `juce::AudioBuffer`/`std::vector` members (e.g. `FrequencyGraphModuleProcessor::inputCopyBuffer`, `combinedInputBuffer`) and resize them only when `numSamples` grows.  
   - This avoids heap churn when multiple instances (two Frequency Graphs, two Reverbs, etc.) are onscreen.

---

## 2. Theme Compliance

1. Always fetch the current theme via:
   ```cpp
   const auto& theme = ThemeManager::getInstance().getCurrentTheme();
   ```
2. Derive colors from `theme.modules.*`, `theme.modulation.*`, `theme.accent`, etc., and convert them with `ImGui::ColorConvertFloat4ToU32`.
3. Fall back to `ThemeManager::getInstance().getCanvasBackground()` if a theme slot is `0`.
4. Never hard-code fonts or paddings—let ImGui’s style define layout. If you need a padded area, use child windows or `ImGui::PushStyleVar` and restore it.

---

## 3. Clip Rects, Child Windows, and State Hygiene

1. **One visualization region = one clip rect.**  
   - Call `drawList->AddRectFilled(...)` to paint a background, then `ImGui::PushClipRect(origin, rectMax, true);`  
   - Always `ImGui::PopClipRect()` before leaving the scope.  
   - Never nest clip rects unless you pair pushes/pops immediately (imbalance corrupts the minimap).

2. **Prefer child windows for complex layouts.**  
   - `ImGui::BeginChild("LimiterLevelHistory", size, ...)` gives you automatic clipping and separate ID space.  
   - Always `ImGui::EndChild()` before you render the next slider/section so the clip rect and ID stack return to normal.  
   - `FrequencyGraphModuleProcessor` and `ReverbModuleProcessor` were both updated to draw **all** visual elements inside child windows, which fixed the minimap glitch when duplicating nodes.

3. **Protect global ID space.**  
   - Wrap every node UI with `ImGui::PushID(this);` … `ImGui::PopID();` to avoid conflicts when multiple nodes share identical labels.

4. **No stray ImGui state changes.**  
   - Every `Push*` call (ClipRect, Style, Color, ID, ItemWidth, Disabled) must have a matching `Pop*`.  
   - Tooltips: only call `ImGui::EndTooltip()` when `ImGui::BeginItemTooltip()` returns true.

---

## 4. Drawing Guidelines

1. **Use arrays of primitives (lines, rects) instead of large polygons.**  
   - Big `AddConvexPolyFilled` calls can overflow the ImGui command buffer and break the canvas/minimap.  
   - If you need fills, down-sample (e.g., 32 rectangles for the Reverb activity halo).

2. **Clamp coordinates.**  
   - Ensure Y values stay within `[origin.y, rectMax.y]` before passing them to `AddLine`.  
   - This prevents visuals from spilling outside the node.

3. **Down-sample for performance.**  
   - A typical waveform uses `256` points. Use `stride = samples / waveformPoints` and pick the nearest sample.

4. **Add contextual labels.**  
   - Show live parameter values (Drive, Mix, Threshold, etc.) near the visualization so users get immediate feedback when dragging sliders.

---

## 5. UI/Audio Separation Checklist

| Step | Audio Thread (processBlock) | UI Thread (drawParametersInNode) |
|------|-----------------------------|----------------------------------|
| Capture | Copy buffers, compute metrics, write to `VizData` atomics. | Read atomics into local arrays. |
| Guarding | Surround capture code with `#if defined(PRESET_CREATOR_UI)` to avoid release build cost. | No guards needed (UI build only). |
| Thread Safety | Use `std::atomic<float>` for all shared values. | Avoid pointers/reference to audio buffers. |

---

## 6. Testing Checklist

1. **Canvas sanity:** Create several nodes (Drive, Gate, Limiter, etc.) and reposition them to verify the minimap never disappears.  
2. **Theme switching:** Open the Theme Editor, tweak base colors, and confirm the visualization updates instantly.  
3. **Modulation:** Toggle CV inputs; visualizations must keep working even when parameters are modulated/disabled.  
4. **Performance:** Profile with multiple nodes active—visuals should not spike CPU usage.

---

## 7. Recommended Reference Implementations

- `DriveModuleProcessor` – multi-trace waveform (dry/wet/mix) with harmonic meter.  
- `GateModuleProcessor` – threshold-aware envelope monitor + gate state meter.  
- `LimiterModuleProcessor` – child-window histories that avoid minimap corruption.  
- `ChorusModuleProcessor` – stereo modulation arcs + timeline, showing how to mix several panels cleanly.

Use these as templates when building new node visualizations.


