# Moofy: Drag-Based Node Suggestion System

> Use `MOOFYS\moofy_drag_node_suggestions.ps1` to export all context into `moofy_DragNodeSuggestions.txt` before sharing with external experts.

# Assertion Crash Context
- For the current ImNodes scope assertion issue, run `MOOFYS\moofy_drag_node_assert.ps1` to generate `moofy_DragInsertAssertion.txt`.
- Summary doc: `MOOFYS/moofys_drag_assertion.md`.

## Mission Summary
- We want to enhance the ImGui node editor so that **dragging a cable onto empty canvas** opens a context-aware popup with modules compatible with the dragged signal.
- The system must reuse existing right-click “Insert Node” infrastructure but add **real-time compatibility filtering**, **auto-wiring**, and **optional drag continuation**.
- External helper should understand JUCE/ImGui architecture, modular synth graph, pin typing, and current insertion pathways.

## Key Questions for Helper
1. Best way to intercept imnodes drag state to know when a link is dropped on empty space?
2. How to build a reusable compatibility engine covering audio/CV/Gate/Raw/Video & mod pins?
3. Auto-wiring heuristics (mono vs stereo, modulation routing) that won’t break undo/redo?
4. Patterns for unifying new drag popup with existing right-click `Insert Node` menu?
5. UX tips for minimal, keyboard-friendly popup (ranking, suggested converters)?

## Primary Source Files to Review
1. `juce/Source/preset_creator/ImGuiNodeEditorComponent.h`
2. `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`
3. `juce/Source/preset_creator/PinDatabase.h`
4. `juce/Source/preset_creator/PinDatabase.cpp`
5. `juce/Source/audio/modules/ModuleProcessor.h` (dynamic pins, `PinDataType`)
6. `juce/Source/audio/graph/ModularSynthProcessor.cpp` (`getConnectionsInfo`, add/connect API)

## Supporting Context
- Planning doc: drag suggestion plan (internal).
- Guides: `guides/IMGUI_NODE_DESIGN_GUIDE.md`, `guides/TEMPO_CLOCK_INTEGRATION_GUIDE.md` (pin type conventions).
- Existing Moofy: `MOOFYS/moofy_Essentials.txt` (core architecture dump).
- Recent analysis: BPM monitor guide (for style reference); timeline remediation (example of gap analysis).

## Current Behaviour Recap
```3760:3869;7526:7627;7866:7939:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
// Right-click cable -> capture link -> open popup per data type
// insertNodeBetween / insertNodeOnLink handle rewiring + VST paths
```
```4515:4578:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
// ImNodes::IsLinkCreated -> auto-insert converter (Attenuverter/Comparator/MapRange)
```
```8038:8133:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
// getPinDataTypeForPin combines dynamic pins + PinDatabase fallbacks
```

## Pain Points / Gaps
- Dragging an output into empty space silently cancels; no popup or suggestions.
- Right-click popup lists are hard-coded per type; no ranking, duplicates across workflows.
- No unified suggestion logic: auto converters handled separately from manual insert.
- Undo handling inconsistent (some helpers call `pushSnapshot`, others rely on `snapshotAfterEditor` flags).
- Need to support modulation pins (`isMod` true) and dynamic video/CV pins.

## Proposed Implementation Steps
1. **Drag State Capture** – Add fields to track drag start pin (`ImNodes::IsLinkStarted`), monitor release on empty canvas, cancel on ESC/right-click.
2. **Compatibility Engine** – Build helper returning ranked module suggestions using `PinDatabase` + dynamic pin lookups; include converter options.
3. **Drag Popup UI** – New ImGui popup near drop position with signal badge, list, keyboard navigation; share data model with right-click popup.
4. **Auto-Wiring** – Connect source/destination automatically using suggestion metadata; support stereo pairs and modulation param routing; wrap in single undo snapshot.
5. **Refactor Existing Menus** – Make right-click `Insert Node` use the same compatibility engine to avoid divergent lists.
6. **UX Polish** – Optional inline tooltip during drag, user preferences (enable/disable popup, auto-continue drag).
7. **Testing** – Manual matrix (audio, CV, gate, video, mod pins); confirm undo, dynamic pin modules, VST insertion; ensure `graphNeedsRebuild` & `pushSnapshot` sequencing OK.

## Open Questions for External Expert
- Are there better hooks in imnodes (e.g., `IsLinkDropped`) to detect drag release reliably?
- Recommended data structure for caching module compatibility (performance vs dynamic graph updates)?
- Strategies to detect stereo pairs or multi-channel groups elegantly?
- UX best practices for ranking modules (frequency-based vs static categories) within ImGui constraints?

## Deliverables Needed
- Reviewed architecture notes or proposals clarifying drag detection and popup management.
- Pseudocode / class diagrams for compatibility engine integration.
- Suggestions on bridging auto converters with manual selection.
- Potential pitfalls with JUCE message thread vs audio thread when triggering `pushSnapshot`/`graphNeedsRebuild`.

## How to Engage
- Provide annotated snippets or pseudo-implementation for new drag workflow.
- If needed, request additional files (list above) or clarifications about graph API.
- Respond with concrete steps or code patterns; we will handle integration in repo.

_Thanks!_

| Date       | Moofy Name                           | Focus                          | Notes |
|------------|--------------------------------------|--------------------------------|-------|
| 2025-11-09 | `moofy_meta_module_recovery.ps1`     | Meta modules / Inlet / Outlet | Added to brief external helper on restoring sub-patching (factory alias, state plumbing, channel mapping, collapse wiring). |

Además, recuerda que cada moofy debe vivir en la carpeta `MOOFYS/` y seguir el formato del script de ejemplo.

