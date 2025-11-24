# Parallel Pin Layout Guide

## Summary
The modular node editor previously rendered IO rows with `ImGui::Columns(...)`, which caused node widths to fluctuate as nodes moved horizontally and left a wide gap between output labels and pins. We replaced that column-driven helper with a manual SameLine layout inside `helpers.drawParallelPins` (`ImGuiNodeEditorComponent.cpp`). This guide explains the new helper and how to apply it to other modules.

## Goals
- Keep input labels inline with their pins.
- Right‑align every output label so it touches its pin (matching stock ImNodes).
- Avoid feeding column widths back into node sizing, which eliminates the “node stretch” issue.

## Implementation Overview
1. **Manual Layout**
   - Capture `rowStartX = ImGui::GetCursorPosX()` at the start of each row.
   - Render the input label inside `BeginInputAttribute`, marking `hasItemOnLine`.
   - For the output label compute `desiredStart = rowStartX + max(0, nodeContentWidth - textWidth - gap)`.
   - If the row already contains content, call `SameLine(0.0f, spacing)` before `SetCursorPosX(desiredStart)`.
   - Render the output label and pin inside the same `BeginOutputAttribute`, then cache the pin center via `GetItemRectMin/Max` so overlays remain accurate.

2. **Helper Location**
   - The lambda lives in `ImGuiNodeEditorComponent::drawGraph` when we build `NodePinHelpers`.
   - Any module using `helpers.drawParallelPins(...)` now automatically gets the new layout.

3. **Custom Node Widths**
   - Some modules (e.g. MultiSequencer) still need more horizontal space. Override `ImVec2 getCustomNodeSize() const` in the module processor (example: `return {360.0f, 0.0f};`).

## How to Use the Helper
1. In your module processor’s `drawIoPins`, replace manual ImGui code with helper calls:
   ```cpp
   helpers.drawParallelPins("Input", inputChannel, "Output", outputChannel);
   helpers.drawParallelPins(nullptr, -1, "Only Output", outputChannel);
   helpers.drawParallelPins("Only Input", inputChannel, nullptr, -1);
   ```
2. The helper manages spacing, pin colors, tooltips, and attrPosition tracking.
3. Do **not** mix `ImGui::Columns` or ad-hoc `SameLine` calls for the same row—let the helper control the layout.

## Troubleshooting
- **Node still cramped** → implement `getCustomNodeSize()` or adjust the module’s `nodeContentWidth`.
- **Label appears truncated** → ensure the node body is wide enough; the helper right-aligns to whatever space it is given.
- **Links misaligned** → confirm the module no longer bypasses `helpers.drawParallelPins` (custom drawing may skip attrPosition updates).
- **Dynamic pin modules still show old layout** → modules that return dynamic pins must opt into the manual layout by overriding `usesCustomPinLayout()` to return `true`; otherwise the editor assumes the pin metadata is enough and will skip your custom `drawIoPins()` implementation.

## Next Steps for New Modules
- Audit existing modules for manual IO layouts and migrate them to the helper.
- For grouped pin sections, insert headers/spacers between groups but still call the helper per row.
- If a bespoke layout is unavoidable, mimic the SameLine technique rather than reintroducing columns.

