# 🧩 Meta Module / Inlet / Outlet Status Guide

**Version**: 0.2  
**Last Updated**: 2025-11-10  
**Relevant Code**: `ModularSynthProcessor`, `MetaModuleProcessor`, `InletModuleProcessor`, `OutletModuleProcessor`, `ImGuiNodeEditorComponent`, `PinDatabase`

---

## 🎯 Purpose
Provide a single source of truth on what currently works, what is still missing, and how we intend to finish the Meta module pipeline. This guide exists so that anyone touching sub-patching knows the exact behaviour and the remaining gaps before the feature becomes production-ready.

---

## ✅ Current Implementation Snapshot

### 1. Factory & UI Wiring
- `getModuleFactory()` now registers `"meta"` alongside the existing aliases (`ModularSynthProcessor.cpp` `reg("meta", …)`), so the palette button successfully instantiates a Meta module.
- `PinDatabase.cpp` defines entries for `"meta"`, `"inlet"`, and `"outlet"`, exposing up to 16 audio pins per node so they are selectable and connectable in the editor.

### 2. MetaModuleProcessor Runtime Behaviour
- Maintains an internal `ModularSynthProcessor` and serialises its state via `getExtraStateTree` / `setExtraStateTree`, using base64 for persistence (lines 282-358).
- `getStateInformation` / `setStateInformation` now delegate to the ValueTree helpers, so preset loading, undo/redo, and the collapse workflow share the same path (lines 330-358).
- `prepareToPlay` pushes sample-rate/block-size into the internal graph, caches inlet/outlet channel counts, and allocates side buffers (`prepareToPlay`, `updateInletOutletCache`).
- `processBlock` copies the host buffer into per-inlet caches, triggers the internal graph, and mixes outlet buffers back to the parent output (lines 63-167). Labels coming from internal inlets/outlets are surfaced on the outer pins (`drawIoPins`, `getAudioInputLabel`, `getAudioOutputLabel`).
- `rebuildBusLayout` sums the cached channel counts and calls `setPlayConfigDetails` to match the meta node’s IO width (lines 361-378).

### 3. Inlet / Outlet Modules
- Each module exposes a channel-count parameter (1–16) and stores `customLabel` + `pinIndex` in `extra` state.
- `InletModuleProcessor::processBlock` copies audio from the parent buffer provided via `setIncomingBuffer`; `OutletModuleProcessor::processBlock` caches the most recent block for the parent to read.
- UI panels allow renaming pins and adjusting channel counts; pin rendering shows one plug per channel.

### 4. Collapse Workflow (`ImGuiNodeEditorComponent::handleCollapseToMetaModule`)
- Scans selected nodes, records boundary connections, and seeds `Inlet`/`Outlet` metadata (including `externalLogicalId`, `externalChannel`, `pinIndex`).
- Serialises the internal sub-graph into a ValueTree (`MetaModuleState`) and injects it into the newly created Meta module via `setExtraStateTree`.
- After instantiation, gathers the generated inlets/outlets, computes cumulative channel offsets, and rewires all external audio connections back through the meta shell.

### 5. Expand Workflow (`expandMetaModule`)
- Reads the stored meta state, recreates modules in the parent graph, restores parameters/extra state, and replays internal + boundary connections.
- Removes the meta shell and repositions reinstated nodes near the original meta node.

### 6. UI Plumbing
- The Meta node renders an “Edit Internal Patch” button that raises a modal. The modal currently displays a placeholder list of internal modules; the recursive ImGui/ImNodes editor has **not** been implemented yet.

---

## ⚠️ Outstanding Gaps & Risks

| Area | Issue | Impact |
|------|-------|--------|
| Internal Editor | Modal is a placeholder; we cannot add/remove nodes, wires, or inlets/outlets after collapse. | Meta nodes are effectively read-only; users cannot iterate on sub-patches. |
| Pin Type Coverage | Pin database and draw code only handle audio pins. CV/Gate/Raw connections collapse into audio-only inlets/outlets. | Non-audio patches lose semantic colour/type information; future data-type routing blocks us. |
| Channel Layout Updates | `Inlet`/`Outlet` constructors hard-code stereo buses; channel-count changes do not resize buses, cached buffers, or telemetry vectors. | Multi-channel inlets/outlets beyond stereo either down-mix incorrectly or read garbage. |
| Live Channel Changes | Changing the channel-count parameter after collapse does not notify the parent; `rebuildBusLayout` only runs during `prepareToPlay`/state load. | UI sliders appear to work but external pins never update without removing/re-adding the meta node. |
| Metadata Persistence | Collapse writes `externalLogicalId`/`externalChannel` into the Inlet/Outlet `extra` state, but `getExtraStateTree()` does not persist those properties. | Re-opening or re-saving a meta graph loses the mapping needed by `expandMetaModule`, so expansion after reload is broken. |
| Modulation / MIDI Routing | Collapse/Expand only consult `getConnectionsInfo()` (audio graph). Modulation routes created via `addModulationRoute` and MIDI bindings are ignored. | Collapsing a sub-patch drops all modulation and MIDI plumbing, breaking complex patches. |
| Bus Layout API | `rebuildBusLayout()` calls `setPlayConfigDetails(...)`. For correctness we should build explicit `BusesLayout` objects and call `setBusesLayout` to avoid transport defects in certain hosts. | Potential host integration issues once this becomes a plugin (variable bus layouts). |
| Performance | `processBlock` allocates `internalBuffer` every call and copies full buffers even when channels are idle. | Extra CPU/memory churn; acceptable today but should be addressed before release. |
| Parameter Exposure | No mechanism to map internal parameters to the Meta node’s APVTS; the UI label is the only configurable field. | Users cannot automate or tweak key parameters without drilling into the internal graph (which is currently impossible). |
| Testing | No regression coverage for collapse/expand, channel reconfiguration, or nested undo/redo. | Every change risks reintroducing blank meta modules or broken rewiring. |

---

## 🗺️ Recovery Plan (Implementation Roadmap)

1. **Ship a Real Internal Editor**
   - Embed an ImGui/ImNodes canvas inside the meta modal that operates on `metaModule->getInternalGraph()`.
   - Provide palette, node moving, connections, undo/redo, and a close/apply workflow that triggers `updateInletOutletCache()` and `rebuildBusLayout()`.

2. **Dynamic Pin Management**
   - Allow users to add/remove `Inlet`/`Outlet` modules from within the internal editor.
   - Watch their APVTS state and refresh the parent meta pins (graph rebuild + external rewiring).

3. **Persist Boundary Metadata**
   - Extend `InletModuleProcessor::getExtraStateTree()` / `setExtraStateTree()` and their Outlet counterparts to include `externalLogicalId`, `externalChannel`, and `externalIsOutput` so expansion works after reload.

4. **Correct Bus Handling**
   - Replace `setPlayConfigDetails` with explicit `BusesProperties` per channel count.
   - Resize `inletBuffers`, `outletBuffers`, telemetry vectors, and cached buffers whenever counts change.

5. **Support CV / Gate / Raw**
   - Introduce pin-type metadata for inlets/outlets (either via dedicated module variants or by tagging channels).
   - Update collapse/expand to preserve pin data types and route them correctly when rewiring.

6. **Modulation & MIDI Bridging**
   - Capture modulation routes (`addModulationRoute*`) and MIDI bindings during collapse, recreate them on expand, and provide equivalent inlet/outlet nodes for those signal types.

7. **Parameter Surfacing**
   - Implement parameter forwarding so selected internal parameters appear on the Meta node’s APVTS (for automation and quick tweaks).

8. **Performance & Telemetry Polish**
   - Cache `internalBuffer` per block, only copy active channels, and update telemetry vectors based on actual channel counts.

9. **Regression Safeguards**
   - Add scripted tests (or a Moofy bundle) covering collapse/expand, save/load, undo/redo, channel adjustments, and nested meta modules.

---

## ✅ Verification Checklist (When Feature Is Complete)
- Instantiate a blank Meta module, open the internal editor, add inlets/outlets, and confirm pins appear/vanish dynamically.
- Collapse a multi-node patch with CV, gate, and audio connections; ensure the meta shell exposes correctly typed pins and audio flows.
- Undo/redo the collapse, then expand the meta module after saving/reloading the preset.
- Adjust inlet/outlet channel counts inside the meta editor; confirm the parent pins update and wiring follows cumulative channel offsets.
- Verify modulation and MIDI routes survive collapse/expand and remain functional.
- Confirm parameter mappings on the Meta node respond in real time and serialize with presets.
- Run performance profiling: meta modules must not allocate per block or spike CPU usage compared to the uncollapsed patch.

---

**End of Guide** | Version 0.2 | 2025-11-10


