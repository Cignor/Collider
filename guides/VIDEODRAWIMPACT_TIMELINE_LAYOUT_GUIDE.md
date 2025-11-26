## Video Draw Impact – Layout & Timeline Recovery

**Status:** Blocker  
**Owners:** Video Toolkit / ImGui Node UI  
**Last Update:** 2025‑11‑26  
**Scope:** `juce/Source/audio/modules/VideoDrawImpactModuleProcessor.*`

---

### 1. Context

The `video_draw_impact` node is supposed to mimic the UX quality of `MidiLogger` and `MIDIPlayer`: compact footprint, constant-width controls, and a timeline that zooms/scrolls smoothly while staying synced to the video source. Instead, the current implementation regressed in two ways:

1. **Node stretches vertically & horizontally.**  
   Each keyframe injects an `ImGui::InvisibleButton`, which advances ImGui’s cursor and keeps extending the child layout downward. Combined with unbounded `PushItemWidth`, the node grows until it fills the whole patcher.

2. **Timeline zoom is truncated.**  
   The timeline child clamps `totalWidth` to its visible width and mixes draw commands with UI widgets. As soon as you zoom in, the right half of the events list disappears, horizontal scrolling stops at ~25%, and a vertical scrollbar appears.

Our reference for correct behaviour lives in:

* `juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp` (phase‑2 piano roll timeline)
* `juce/Source/audio/modules/MidiLoggerModuleProcessor.cpp`

Both follow the same recipe: fixed-width outer child, single canvas per timeline, scroll-to-zoom centred on the playhead, and hit-testing done via math instead of placing widgets.

---

### 2. Expected Behaviour

* Node body width matches `getCustomNodeSize().x` (≈480 px) and never exceeds it. Height only grows with content and never depends on the number of timeline markers.
* Timeline zoom slider mirrors MIDIPlayer: wheel adjusts zoom in ±10 px/s steps, and the playhead stays under the cursor while the scrollbar range expands to cover the entire duration.
* Timeline canvas has **no** vertical scrollbar. All labels (`Time … / …`, hints) live outside the canvas child window.
* Right-click erase and keyframe dragging work regardless of zoom level; hit areas track the mouse because coordinates are in content space (`mouseXContent = mouse.x - canvasMin.x + scrollX`).

---

### 3. Fix Strategy

1. **Constrain the outer layout.**
   - Wrap everything in `ImGui::BeginChild("VideoDrawImpactContent", ImVec2(baseWidth, 0.f), false, ImGuiWindowFlags_NoScrollbar);`.
   - Derive a single `contentWidth = baseWidth - padding * 2` and use it for all sliders/buttons so controls never request extra width.

2. **Dedicated timeline canvas.**
   - Create `ImGui::BeginChild("TimelineView", ImVec2(contentWidth, timelineHeight), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);`.
   - Inside, reserve the drawing surface with **one** `ImGui::InvisibleButton("##timelineCanvas", ImVec2(totalWidth, timelineHeight));` and immediately grab `canvasMin/canvasMax = GetItemRectMin/Max`.
   - Everything (background, grid, keyframes, playhead, tooltips) must render through the draw list between a single `PushClipRect/PopClipRect`. Do **not** place text or other widgets in this child.

3. **Math-based hit testing (no per-keyframe widgets).**
   - Remove the per-keyframe `InvisibleButton`. Instead, use distance checks (like `MidiLogger`) to detect hover/drag.
   - This keeps ImGui’s cursor stationary, preventing the child height from increasing with every keyframe.

4. **Scroll-to-zoom like MIDIPlayer.**
   - Read `scrollX = ImGui::GetScrollX();`.
   - On wheel input (only when window is hovered and no item active), compute:
     ```
     playheadX = playheadTime * zoomPxPerSecond;
     playheadVisible = playheadX - scrollX;
     zoom += step;
     totalWidth = max(timelineWidth, zoom * totalDuration);
     newScroll = playheadX - playheadVisible;
     SetScrollX(clamp(newScroll, 0, totalWidth - timelineWidth));
     ```
   - Store the clamped value back into `scrollX` for subsequent math.

5. **Consistent coordinate conversions.**
   - Convert mouse to content space via `mouseXContent = (mouse.x - canvasMin.x) + scrollX;`.
   - Convert Y via `mouseYNorm = clamp((mouse.y - canvasMin.y) / timelineHeight, 0, 1);`.
   - Use these values for erasing and for dragging so zoom level no longer inverts the axes.

---

### 4. Acceptance Criteria

* No visual growth when adding dozens of impacts; node width = 480 px, height stable.
* Horizontal scrollbar covers 0‑100 % of timeline; zooming never truncates the right edge.
* No vertical scrollbar inside the timeline child.
* Right-click erase removes the marker directly under the cursor at any zoom level.
* Linter (`read_lints juce/Source/audio/modules/VideoDrawImpactModuleProcessor.cpp`) reports no new warnings.

---

### 5. Verification Checklist

1. Load a looping video (≥30 s). Draw impacts across the entire duration.
2. Zoom from 10 px/s to 500 px/s while observing the playhead and scrollbar – they must stay in sync.
3. Scroll to both extremes; confirm the last keyframe is reachable and still visible.
4. Add 30+ keyframes and confirm the node height remains unchanged.
5. Drag keyframes and erase them at minimum and maximum zoom levels.
6. Restart Collider, reload the preset, and verify the timeline state and zoom settings persist.

---

### 6. Follow-up

* Port these fixes to any future video timeline nodes to prevent regression.
* Keep this guide next to the node so new contributors understand why we forbid per-keyframe widgets and why we mirror the MIDI modules.
## Video Draw Impact – Timeline & Layout Guide

**Status:** WIP – follow-up to multiple review rounds  
**Owners:** UI/Timeline maintainers  
**Last Update:** 2025‑11‑26  
**Scope:** `juce/Source/audio/modules/VideoDrawImpactModuleProcessor.*`

---

### 1. Problem Statement

The `video_draw_impact` node has diverged from the proven timeline/layout patterns used by `MIDIPlayer` and `MidiLogger`. Two user-visible regressions keep appearing:

1. **Unbounded node width/height.**  
   The root widget used `ImGui::Dummy(GetContentRegionAvail().x)` and left the entire node body unconstrained. When placed in the graph the node expands to the full canvas width/height, obscuring neighbouring nodes (see screenshot in the bug report).

2. **Broken timeline zoom/scroll interaction.**  
   The timeline child window was sized to the global `GetContentRegionAvail()` and the “canvas length” was clamped to that width. As soon as the user zoomed in, the right-hand side of the events list was clipped and the horizontal scrollbar could not reach the real end of the dataset. In addition a stray vertical scrollbar appeared because the timeline child contained multiple widgets (dummy + labels + instructions) instead of a single clipped canvas.

These two issues made it impossible to use the node in large patches – the UI consumed the entire viewport and the timeline could not be navigated accurately.

---

### 2. Reference Implementations

Study how the team solved the same problems elsewhere:

* `juce/Source/audio/modules/MIDIPlayerModuleProcessor.cpp` (Phase‑2 piano roll).  
  - Wraps all controls in a fixed-width `BeginChild`.  
  - Timeline canvas reserves space using a single `Dummy` + draw list + `Scrollable` child.  
  - Scroll-to-zoom keeps the playhead centred and clamps the scroll offset to `[0, totalWidth - visibleWidth]`.
* `juce/Source/audio/modules/MidiLoggerModuleProcessor.cpp`.  
  - Uses the same pattern for looped timelines and track grids.  
  - All widgets honour an explicit `nodeWidth` and never call `GetContentRegionAvail()` to grow past the intended footprint.


