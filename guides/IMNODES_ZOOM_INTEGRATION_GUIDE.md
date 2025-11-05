# 🔍 ImNodes Zoom Integration Guide

**Version**: 1.0  
**Last Updated**: 2025-11-05  
**Purpose**: Explain the architecture and integration steps for adding smooth zooming to the node editor using the approach introduced in the imnodes PR commit "Add zoom functionality".

Reference: [Add zoom functionality to node editor (commit 0cda3b8)](https://github.com/Nelarius/imnodes/pull/192/commits/0cda3b818e9bc4ede43b094796ec95792bf5fbb7)

---

## 📋 Overview

The referenced change implements node editor zoom by introducing a second ImGui context specifically used to render the imnodes canvas at a different scale. All editor interactions (mouse, hover, selection, links) remain fully functional because input state is mirrored into the zoom context and draw data is projected back into the original context.

High-level:
- **Two ImGui contexts**: the app's original context, and a zoom context used only during node editor rendering.
- **Scaled rendering**: Nodes, pins, links, and grid are rendered at `editor.ZoomScale` inside the zoom context.
- **Input mirroring**: Relevant `ImGuiIO` fields are copied into the zoom context each frame so hit-testing matches the scaled rendering.
- **Draw data copy-back**: Command lists produced in the zoom context are transformed and appended to the original context draw list at the correct canvas origin.
- **Camera invariants**: Panning and zoom are coordinated to maintain cursor-centered zooming.

---

## 🏗️ Architecture

### 1) Dual-context rendering pipeline

- A new `ZoomImgCtx` is created and owned by the imnodes editor context.
- When rendering the editor, code switches to `ZoomImgCtx`, copies IO and style as needed, and renders imnodes UI at scale.
- After rendering, code switches back to the original context and appends transformed draw data.

Key responsibilities (from the commit):
- Copy IO config into the zoom context each frame to ensure all input state (mouse pos, buttons, key mods, delta time, display size, font scale, etc.) is consistent with scaled rendering.
- Compute `CanvasOriginalOrigin` in original context coordinates; use it to position the zoomed draw data correctly when copying back.
- Transform primitives when appending draw data so thickness and positions look correct under zoom.

### 2) Coordinate systems and transforms

- `editor.ZoomScale` scales all canvas-space rendering within `ZoomImgCtx`.
- `editor.Panning` remains in canvas units; zoom changes reposition panning to keep zoom centered under the cursor.
- Thickness-sensitive drawing (e.g., link thickness) is adjusted by dividing by `ZoomScale` so perceived thickness remains visually stable when zooming:
  - Example: link thickness uses `GImNodes->Style.LinkThickness / editor.ZoomScale`.

### 3) Hover/interaction correctness

- Hover detection and click targeting are evaluated in the zoom context to match the scaled geometry.
- The helper `MouseInCanvas()` returns editor hover state considering the canvas rect and window focus. The commit adjusts hover logic to rely on internal `GImNodes->IsHovered` populated via the zoom-aware path.

### 4) Zoom centering math

To zoom around a position `zoom_centering_pos` (usually mouse position on canvas), the panning is adjusted so the point remains stationary on screen across zoom levels:

```
zoom_centering_pos -= GImNodes->CanvasOriginalOrigin;
editor.Panning += zoom_centering_pos / new_zoom - zoom_centering_pos / editor.ZoomScale;
```

Mouse pos is also rescaled inside the zoom context so interaction maps correctly:

```
GImNodes->ZoomImgCtx->IO.MousePos *= editor.ZoomScale / new_zoom;
```

---

## 🧩 Public API Additions/Changes

As per the commit:
- `float EditorContextGetZoom();`
- `void EditorContextSetZoom(float zoom_scale, ImVec2 zoom_center);`
- `bool IsEditorHovered(); // returns MouseInCanvas()`

Zoom constraints: clamped to `[0.1, 10.0]` in the patch.

Usage patterns:
- Read current zoom to display a HUD or minimap scale.
- Set new zoom on mouse wheel with Ctrl, trackpad pinch, or UI buttons; pass the mouse position (in screen space) as `zoom_center` for intuitive zoom.

---

## 🔧 Key Internal Implementation Points

This section maps notable changes from the commit to concepts you must replicate or respect when integrating.

- **IO mirroring to zoom context**
  - Copy relevant `ImGuiIO` fields each frame before rendering imnodes inside `ZoomImgCtx` (the commit "Copy all of the ImGui IO config section into zoom context").
  - Ensure `DisplaySize`, `DeltaTime`, `MousePos`, `MouseWheel`, `KeyMods`, backend flags, etc., reflect the current frame.

- **Draw data copy-back**
  - After rendering, obtain `ImGui::GetDrawData()` from `ZoomImgCtx` and, back in the original context, append via an `AppendDrawData(cmd_list, CanvasOriginalOrigin, ZoomScale)` routine.
  - This function translates vertices and scales thickness so visual output matches the intended zoom.

- **Thickness scaling**
  - Where applicable, divide stroke widths by `ZoomScale` so lines look consistently thick across zoom levels (links, outlines, grid lines if needed).

- **Grid rendering**
  - Grid steps and origin markers should respect zoom; larger scales show denser lines, smaller scales thin out or fade minor lines.
  - The commit routes grid drawing through the zoomed context so it naturally scales; consider LOD/fade for minor lines at extreme zooms.

- **Hover detection**
  - Replace ad-hoc `IsWindowHovered` checks with the editor’s internal hover flag updated during zoomed rendering. The commit evolves `MouseInCanvas()` and internal hover to avoid desync.

- **Minimap and editor-space conversions**
  - The commit later adds helpers `ConvertToEditorContextSpace` and `ConvertFromEditorContextSpace` (another commit in the PR) to convert between screen and editor space with zoom/pan taken into account. Replicate equivalents if needed in our codebase.

---

## 🧭 Integration Strategy for Our Codebase (JUCE + ImGui + ImNodes)

Follow these steps to adopt the zoom model without invasive rewrites.

### 1) Extend editor context

- Add `ZoomScale` (default `1.0f`) and keep `Panning` in canvas units.
- Add storage for a secondary ImGui context pointer: `ZoomImgCtx`.
- Add `CanvasOriginalOrigin` in screen space for placing appended draw data.

### 2) Create and manage the zoom ImGui context

- On editor initialization (after ImGui backend setup), create a new `ImGuiContext` dedicated to zoomed rendering.
- Share fonts/styles as appropriate; you may need to rebuild fonts for the zoom context to match the primary.
- On each frame when rendering the node editor:
  - Save original context; set current to `ZoomImgCtx`.
  - Copy IO state from original context (display size, delta, mouse state, key mods, backend flags, etc.).
  - Adjust `IO.MousePos` for the current `ZoomScale` if you are changing zoom this frame.
  - Render the editor (nodes, links, grid) inside this context, applying `ZoomScale` to canvas transforms.
  - Capture draw data.
  - Restore original context.
  - Append transformed draw lists into the original draw list using `CanvasOriginalOrigin` and `ZoomScale`.

### 3) Implement API surface

- Provide `EditorContextGetZoom()` and `EditorContextSetZoom(zoom, center)`. In `SetZoom`:
  - Clamp zoom to `[0.1, 10.0]`.
  - Convert `zoom_center` to canvas-relative (`zoom_center -= CanvasOriginalOrigin`).
  - Apply panning compensation: `Panning += zoom_center / new_zoom - zoom_center / old_zoom`.
  - Rescale `ZoomImgCtx->IO.MousePos` by `old_zoom / new_zoom` to keep interaction under the cursor.

### 4) Input handling

- Bind zoom controls:
  - Ctrl + Mouse Wheel to zoom at mouse position.
  - Optional: trackpad pinch gesture mapping to `SetZoom`.
  - Optional: `+`/`-` hotkeys and a fit-to-content button.
- Ensure panning (middle drag or space+drag) remains in canvas units; do not scale panning deltas by `ZoomScale`.

### 5) Drawing adjustments

- For stroke widths (links, outlines), divide by `ZoomScale` so visual width remains constant.
- If labels or text look off at extreme zoom, you may choose to keep font size constant and only scale geometry; the PR keeps regular ImGui text sizing, relying on geometric scale.

### 6) Hover and selection

- Evaluate hover in the zoom context so geometry and hit-testing agree.
- Update any helper like `MouseInCanvas()` to rely on the editor’s hover state computed during zoomed rendering.

### 7) Persistence

- Persist `ZoomScale` in user settings and restore per-session.
- Consider per-editor-instance zoom if multiple editors can be open.

---

## 🧪 Testing Checklist

- Zoom in/out around cursor preserves the point under cursor.
- Panning behaves identically at all zoom levels.
- Selection/hover of nodes, pins, and links works at all zoom levels.
- Link thickness and outlines remain visually consistent across zoom.
- Grid density/visibility behaves sensibly at extreme zoom in/out.
- Minimap (if present) uses correct transforms and reflects zoom.
- No assertion failures switching ImGui contexts mid-frame.
- No input desyncs (dragging, rubber-band selection) at fractional zooms.

---

## ⚠️ Edge Cases & Pitfalls

- Forgetting to copy `ImGuiIO` fields leads to hover/drag mismatch.
- Not adjusting panning when zooming will cause jumpy zoom focus.
- Double-scaling thickness will make links look too thin/thick.
- Failing to restore original ImGui context after zoom pass will corrupt subsequent UI rendering.
- If your backend uses multiple viewports, ensure the correct viewport/display size is mirrored to the zoom context.

---

## 🧱 Minimal Pseudocode (Frame Render)

```cpp
void renderEditorFrame(EditorContext& editor) {
    // 1) Compute canvas origin in original context
    editor.CanvasOriginalOrigin = ImGui::GetCursorScreenPos();

    // 2) Switch to zoom context and mirror IO
    ImGuiContext* original = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(editor.ZoomImgCtx);
    mirrorIOFrom(original, editor.ZoomImgCtx);

    // 3) Begin scaled group and render
    ImGui::BeginGroup();
    ImGui::PushID("NodeEditorZoomed");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    // ... render nodes/links/grid using editor.ZoomScale ...
    ImGui::PopStyleVar();
    ImGui::PopID();

    // 4) Capture draw data from zoom context
    ImDrawData* zoomDrawData = ImGui::GetDrawData();

    // 5) Restore original context
    ImGui::SetCurrentContext(original);

    // 6) Append transformed draw data into original context
    for (int i = 0; i < zoomDrawData->CmdListsCount; ++i) {
        AppendDrawData(zoomDrawData->CmdLists[i], editor.CanvasOriginalOrigin, editor.ZoomScale);
    }

    ImGui::EndGroup();
}
```

---

## 🔌 API Usage Examples

```cpp
// Get current zoom for UI
const float z = ImNodes::EditorContextGetZoom();
ImGui::Text("Zoom: %.2fx", z);

// Ctrl+Wheel zoom handler
if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
    const float step = 1.0f + (ImGui::GetIO().MouseWheel > 0.0f ? 0.1f : -0.1f);
    const float newZoom = z * step;
    ImNodes::EditorContextSetZoom(newZoom, ImGui::GetMousePos());
}
```

---

## 🧩 Migration Notes for Existing Code

- Replace any manual canvas scaling hacks with the dual-context approach for correctness.
- Audit any absolute pixel widths in link/node drawing; convert to thickness divided by `ZoomScale`.
- If you previously cached mouse positions in screen space, reconvert where necessary after zoom changes.
- Ensure any overlays drawn outside imnodes (e.g., tooltips/labels) use the original context coordinates; convert between editor and screen space as needed.

---

## 📚 References

- PR commit introducing zoom and APIs: [Add zoom functionality — 0cda3b8](https://github.com/Nelarius/imnodes/pull/192/commits/0cda3b818e9bc4ede43b094796ec95792bf5fbb7)

---

## 🧭 Summary

- Zoom is implemented by rendering the node editor in a dedicated ImGui context at a scale, then copying draw data back to the original context with transformed coordinates.
- Input is mirrored so hit-testing matches the scaled scene; panning is compensated to keep zoom centered under the cursor.
- A small public API (`GetZoom`, `SetZoom`) enables app-level controls; a set of careful adjustments (thickness scaling, IO mirroring, context switching) delivers a robust, artifact-free zoom.

---

End of Guide | Version 1.0 | 2025-11-05


