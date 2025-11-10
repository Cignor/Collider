# 🎛️ Meta Module Usage Guide

**Version**: 1.0  
**Last Updated**: 2025-11-09  
**Based on**:  
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`  
- `juce/Source/audio/modules/MetaModuleProcessor.cpp`  
- `juce/Source/audio/modules/InletModuleProcessor.*`  
- `juce/Source/audio/modules/OutletModuleProcessor.*`

---

## 1. Purpose

Meta Modules let you collapse any selection of nodes into a reusable sub-patch with its own audio IO. This guide explains how to create, edit, and deploy Meta Modules using the latest channel-aware implementation.

---

## 2. Prerequisites

- Build the preset creator with `PRESET_CREATOR_UI` enabled.  
- Update to the codebase containing the Meta-module fixes (commit ≥ 2025-11-09).  
- Launch the preset creator UI; the Module Browser must be visible.

---

## 3. Creating a Meta Module

### 3.1 From the Module Browser
1. Open **Modules → System**.  
2. Click `Meta`. The node spawns at the mouse cursor.  
3. Use it as a blank shell if you want to wire the internals manually.

### 3.2 Collapsing Existing Nodes
1. Multi-select at least **two** nodes in the graph.  
2. Use `Ctrl + Shift + M` (or right-click → **Collapse to Meta Module**).  
3. The selection is replaced with a Meta node at the geometric average of the original nodes.  
4. External cables automatically reconnect to the new Meta node.

Behind the scenes, `ImGuiNodeEditorComponent::handleCollapseToMetaModule()` serialises the selection, constructs Inlet/Outlet nodes, and loads the sub-graph into the new Meta shell.

---

## 4. Editing the Internal Patch

1. Select the Meta node.  
2. Click **Edit Internal Patch** (button in node UI).  
3. The internal modular editor opens.  
4. Add or remove modules just as you would on the main canvas.  
5. Close the editor when done; changes persist immediately.

The internal graph is stored using `MetaModuleProcessor::get/setStateInformation()`, so editing is undoable and survives preset saves.

---

## 5. Inlets and Outlets

### 5.1 Adding IO
Inside the internal editor:
- Add `Inlet` nodes for each audio input you need.
- Add `Outlet` nodes for each audio output you want to expose.

Each module has:
- **Label**: Display name for the pin (`InletModuleProcessor::customLabel`).  
- **Channels**: Number of audio channels (1–16).

### 5.2 Channel Mapping
- Meta automatically sums channel counts from all inlets/outlets (`MetaModuleProcessor::updateInletOutletCache`).  
- Pins on the outer Meta node appear **per channel** (e.g., `Input`, `Input 2` for stereo).  
- Internal pins are mapped to external channels using deterministic `pinIndex` metadata stored in the Inlet/Outlet extra state.

### 5.3 Labeling Tips
- Use short names: e.g., `Kick`, `Bus A`.  
- Stereo inputs/outputs automatically append channel numbers (`Bus A 1`, `Bus A 2`).  
- Renaming an inlet/outlet updates the external pin labels instantly.

---

## 6. Working with Meta Modules

### 6.1 Undo/Redo
- Collapse is undoable (`Ctrl+Z`) and redoable (`Ctrl+Y`).  
- Editing the internal graph also participates in the undo stack.

### 6.2 Nesting
- You can place one Meta module inside another.  
- Keep an eye on channel counts; nested Metas follow the same rules.

### 6.3 Preset Save/Load
- `MetaModuleProcessor::getExtraStateTree()` serialises the entire internal graph as Base64.  
- Saving a preset captures all nested Metas; loading restores them with pin order and channel counts intact.

---

## 7. Testing Checklist

1. **Instantiation**  
   - Add `Meta`, `Inlet`, `Outlet` from the browser; confirm pins appear.

2. **Collapse/Undo**  
   - Collapse a three-node chain.  
   - Undo and redo; ensure wiring restores correctly.

3. **Channel Variations**  
   - Build a meta patch with mono in → stereo out.  
   - Confirm external pins read `Input`, `Output 1`, `Output 2`.  
   - Play audio to verify channel mapping.

4. **Preset Persistence**  
   - Save a preset containing the meta patch.  
   - Reload; check internal graph and pin labels.

5. **Nested Meta** (optional)  
   - Place a meta inside another meta; ensure the outer pins still route correctly.

---

## 8. Troubleshooting

| Symptom | Fix |
|---------|-----|
| Meta button does nothing | Ensure the alias `"meta"` is registered (`ModularSynthProcessor::getModuleFactory`). |
| Pins show wrong channel order | Delete the Meta node, collapse again; `pinIndex` ordering is rebuilt during collapse. |
| No audio after collapse | Check each `Inlet`/`Outlet` channel count and the external wiring. |
| Preset loads without internal graph | Ensure you are on the latest build with `MetaModuleProcessor::setStateInformation()` override. |

---

## 9. Best Practices

- **Name your pins**: a useful label beats “In 1 / Out 1”.  
- **Group by function**: keep related inlets/outlets adjacent; the collapse rewriter preserves order.  
- **Document complex metas**: use `Comment` nodes inside the sub-patch to explain intent.  
- **Limit channel counts**: only expose the channels you actually need; extra pins clutter the canvas.

---

## 10. Reference Implementation Highlights

- `MetaModuleProcessor::processBlock()` now bridges audio using channel layouts rather than stereo pairs.  
- `Inlet/OutletModuleProcessor` store `pinIndex` in their extra state; the collapse workflow assigns these indices.  
- `ImGuiNodeEditorComponent::handleCollapseToMetaModule()` rewires external connections using the new metadata.

---

**End of Guide** | Version 1.0 | 2025-11-09


