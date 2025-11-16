# Cut-By-Line Reroute Insertion – Implementation Plan

## Goal
Enable an Alt + Right-click drag gesture that draws a straight “cut line” across the canvas. On release, all cables intersected by this line are split and a `reroute` node is inserted at each intersection, reconnecting the two halves through the reroute. The reroute adopts the data type of the intersected cable and its pins/colors update accordingly.

---

## User Experience (UX)

- Gesture: Hold Alt + Right mouse button, drag to draw a line; release to perform the cut action.
- Visual feedback while dragging:
  - Render a thin cut line between press and current cursor.
  - Highlight cables that will be cut (e.g., brighter color).
  - Optional: small preview dots where intersections would occur.
- On release:
  - Insert a reroute node at each intersection (with slight offset for readability).
  - Reconnect original link as src → reroute → dst.
  - Batch all modifications into a single undo step.
- Shortcuts/modifiers:
  - Optional Shift: constrain cut line to horizontal/vertical.
  - ESC during drag cancels the cut.

---

## Scope and Constraints

- Types: All path types (Audio, CV, Gate, Raw, Video) are cut. Reroute is already type-agnostic and polymorphic.
- Geometry simplification: Treat each cable as a straight line between its endpoint pins in screen/canvas space. No Bezier math (aligns with “simplest” requirement).
- Merging: Merge intersections that are closer than 8 px along a cable to avoid clustered reroutes (configurable).
- Endpoint exclusion: Do not cut within epsilon of pin endpoints to avoid placing reroutes on top of pins.

---

## High-Level Architecture

1. Input handling (ImGuiNodeEditorComponent):
   - Detect Alt + RMB down → set `cutModeActive = true`, `cutStart = mousePos`.
   - While `cutModeActive`, update `cutEnd = mousePos`, render line and highlight candidate links.
   - On RMB release while Alt is held: compute intersections and perform batch insertion.
   - On cancel (ESC) or losing Alt+RMB: clear state.

2. Intersection & Geometry:
   - Represent the cut as segment S = (P0, P1) in grid/canvas coordinates.
   - For each displayed link L (minimal approach):
     - Get endpoints A, B using node grid positions (`ImNodes::GetNodeGridSpacePos(logicalId)`).
     - Early-out via AABB overlap test between S and segment AB.
     - Segment–segment intersection test with tolerance.
     - If intersection found, compute parameter t ∈ [0,1] along AB (for ordering and endpoint exclusion).
   - For each link with multiple intersections:
     - Sort by t.
     - Merge intersections closer than mergeEpsilonPx (default 8 px).
     - Drop intersections with t ∈ [0, tEpsilon] ∪ [1 - tEpsilon, 1].

3. Graph Edits:
   - For each surviving intersection (link L between srcPin and dstPin):
     - Determine link data type from existing helpers (prefer source pin type).
     - Insert reroute at canvas position near the intersection (apply a small offset along the cut line, e.g., 12 px, and stagger if multiple on the same link).
     - Disconnect L once, then connect src → reroute (srcChan → 0), connect reroute → dst (0 → dstChan).
     - Call `reroute->setPassthroughType(linkType)` immediately to keep UI consistent and prevent conversion insertion logic from misfiring.
   - Commit all changes in one undo snapshot.

---

## Detailed Steps

### A) Input & Rendering
- Add state members (ImGuiNodeEditorComponent):
  - `bool cutModeActive = false;`
  - `ImVec2 cutStart{}, cutEnd{};`
  - `std::vector<LinkHit> currentHits; // For highlighting and preview`
- Event flow:
  - On frame: if Alt && RMB just pressed → activate, save `cutStart = mousePos`.
  - While active: update `cutEnd = mousePos`; recompute `currentHits` (fast pass) and draw overlay line.
  - On RMB release (while Alt): run full cut operation using `currentHits` or recompute robustly, then deactivate.
  - On cancel: deactivate and clear.
- Rendering:
  - Use ImDrawList to draw the cut line (theme accent color).
  - Highlight affected cables (temporarily adjust link color/thickness).
  - Optional: draw small circles at candidate intersection points.

### B) Geometry & Intersection
- For each link, get both endpoint node positions in grid space (minimal implementation; upgrade later if needed).
- AABB reject: if bounding boxes do not overlap (with small padding), skip.
- Segment–segment intersection (robust 2D test with epsilon):
  - If intersect, compute t along the link segment (0..1).
  - Store (linkId, t, intersectionPoint, linkType, srcPin, dstPin).
- Per-link post-process:
  - Sort by t ascending.
  - Merge consecutive intersections if distance(intersectionPoints) < mergeEpsilonPx.
  - Drop if t near endpoints (≤ tEpsilon or ≥ 1 - tEpsilon), e.g., tEpsilon = 0.05 normalized, or pixel-based check using segment length.

### C) Batch Insert & Reconnect
- For each final intersection record (ordered per-link to place nodes neatly):
  - Compute node placement:
    - Base at intersection point.
    - Offset by `normalize(cutEnd - cutStart) * 12 px` (or perpendicular if desired), stagger by small multiples to avoid overlap.
  - Insert node:
    - `insertNodeBetween("reroute", srcPin, dstPin, createUndo=false)` or use direct construct/connect if needed for precise positioning.
    - Position the new node at calculated coordinates.
    - Determine type (use `getPinDataTypeForPin(srcPin)` or link cached type).
    - Immediately `setPassthroughType(type)` on the new reroute.
  - Connectivity:
    - If using `insertNodeBetween`, it handles connect/disconnect. Otherwise:
      - Disconnect original src→dst
      - Connect src→reroute (srcChan→0), reroute→dst (0→dstChan)
- After all intersections processed:
  - `pushSnapshot()` once (single undo group).
  - `synth->commitChanges()` once.

---

## Data Structures (suggested)
```cpp
struct LinkHit {
    int linkId;
    float tOnLink;         // 0..1
    ImVec2 intersection;   // screen/canvas space
    PinDataType type;      // link’s data type
    PinID srcPin;
    PinID dstPin;
};
```

---

## Configuration Knobs
- `mergeEpsilonPx` (default 8 px): merge nearby intersections per link.
- `endpointTEpsilon` (default 0.05): drop intersections too close to endpoints.
- `placementOffsetPx` (default 12 px): offset for readability.
- `highlightColor` and `lineColor`: theme-driven.

---

## Risks & Mitigations

- Performance: Iterating all links every frame while dragging may be heavy in large graphs.
  - Mitigation: Early AABB rejects, only approximate during preview, compute full set on release.
  - Optionally throttle (e.g., recompute preview at ~30–60 Hz).

- Precision: Segment approximation may visually differ from drawn curved cables.
  - Mitigation: User accepted “simplest” approach; document limitations. Later upgrade to Bezier flattening if needed.

- Overlapping reroutes: Multiple cuts close together may clutter layout.
  - Mitigation: Merge proximity; stagger placement offsets.

- Edge cuts near pins: Reroute may overlap pin UIs.
  - Mitigation: Endpoint `tEpsilon` guard; pixel-distance guard.

- Path-type conversions: Existing auto-insert logic (e.g., raw->map_range) could fire erroneously.
  - Mitigation: Immediately set reroute type before any compatibility checks (we already fixed this pattern elsewhere).

- Undo granularity: Many edits flooding the history.
  - Mitigation: Single snapshot after batch edits.

---

## Rollout Strategy (Difficulty Levels)

- Level 1 – Minimal (Low risk)
  - Straight-segment intersection only.
  - No preview dots, just line and highlight.
  - Single offset placement, no staggering.
  - Batch insert using existing `insertNodeBetween` + immediate type set.

- Level 2 – Polished (Moderate risk)
  - Add preview dots per intersection.
  - Stagger placement for multiple intersections on the same link.
  - Configurable thresholds (merge epsilon, endpoint epsilon) in settings.

- Level 3 – Advanced (Higher risk/effort)
  - Bezier flattening for more accurate intersections.
  - Snap cut line (Shift) and visual grid alignment.
  - Multi-select cuts with rectangular lasso or polygonal cuts.

---

## Testing Plan

- Unit-level utility tests (math helpers):
  - Segment–segment intersection correctness (varied angles, parallel, colinear, near-endpoint).
  - Merge behavior under small distances.

- Manual graph tests:
  - Sparse and dense link fields; long vs short links.
  - Every type (Audio, CV, Gate, Raw, Video) including mixed graphs.
  - Multiple intersections on the same link; near endpoints; overlapping nodes.

- UX sanity:
  - Undo/redo integrity (single undo step).
  - Performance at scale (large patch while dragging).

---

## Confidence

- Confidence: High for Level 1, Moderate for Level 2, Lower for Level 3.
- Strong points:
  - Reuses existing insert-on-link and reroute systems (stable and tested).
  - Geometry kept simple (segment-based) per your preference.
  - Clear guardrails for edge cases (endpoint epsilon, merge).
- Weak points:
  - Visual mismatch on curved cables at extreme curvature (addressable later).
  - Potential layout clutter if many cuts; mitigated but not eliminated.

---

## Code Research Notes

- Coordinate systems in `ImGuiNodeEditorComponent.cpp`:
  - Zoom available via `ImNodes::EditorContextGetZoom()` (used for display only; not needed for grid-space math).
  - Panning tracked via `lastEditorPanning`; mouse grid coordinates computed as `mouseGridPos = mouseScreenPos - canvas_p0 - panning`.
  - We’ll capture `cutStart`/`cutEnd` in grid space using the same approach (operate in grid space for math; convert to screen space only for drawing).
- Node positions:
  - `ImNodes::GetNodeGridSpacePos(logicalId)` is used everywhere for positioning. We’ll use node centers as link endpoints for minimal implementation.
  - Later upgrade: derive approximate pin positions if needed.
- Insert-between:
  - `insertNodeBetween(nodeType, srcPin, dstPin, createUndoSnapshot=false)` exists and already sets reroute type from the source. We will reuse it and perform one `pushSnapshot()` at the end of the batch.

---

## Next Steps

- Implement Level 1:
  - Input state machine + overlay rendering
  - Intersection utilities (AABB, segment–segment)
  - Batch insert + reconnect + immediate reroute type set
  - Single undo snapshot
- Expose minimal settings (merge epsilon, endpoint epsilon) if desired.

If approved, I’ll start with Level 1 and keep the code structured to allow later upgrades to Level 2/3 without rewrites.


