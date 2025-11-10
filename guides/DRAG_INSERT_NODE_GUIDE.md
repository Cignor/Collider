# Drag-Insert Node Creation Guide

## Overview

This guide documents the “drag from pin → drop on canvas → insert compatible node” workflow in the Preset Creator. It explains the intended UX, summarizes the current implementation in `ImGuiNodeEditorComponent`, highlights behavioural gaps (stereo wiring, dynamic pin awareness, filtering, undo atomicity), and proposes a concrete recovery plan. The goal is to give an external JUCE/ImNodes expert enough context to stabilise and extend the feature without auditing the whole codebase.

---

## Intended Workflow

- Detect when the user starts a cable drag from any pin (audio, CV, gate, video, modulation).
- If the mouse is released over empty canvas, pop a context palette scoped to that pin’s data type and offer the most relevant modules first.
- Support fast selection: keyboard arrows + Enter, hotkeys, and mouse clicks.
- Spawn the chosen module at the drop point, auto-wire it back to the originating pin, and (when sensible) multi-channel wire sibling pins (e.g. stereo audio).
- Optionally let the user “continue” the drag from the new node’s open pins to keep patching without restarting the gesture.
- Make the whole operation a single undo/redo snapshot, consistent with right-click “Insert Node”.
- Reuse the same suggestion intelligence for right-click popups and future keyboard command palettes.

---

## Current Implementation Snapshot

- **Gesture detection** – During the node editor frame, the component tracks link drags via `ImNodes::IsLinkStarted`, caches the originating pin, and, on mouse release over empty canvas, opens a popup at the cursor position.

```3782:3816:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
    if (ImNodes::IsLinkStarted(&linkStartAttr))
    {
        dragInsertActive = true;
        dragInsertStartAttrId = linkStartAttr;
        dragInsertStartPin = decodePinId(linkStartAttr);
        shouldOpenDragInsertPopup = false;
        juce::Logger::writeToLog("[DragInsert] Started drag from attr " + juce::String(linkStartAttr));
    }
    if (dragInsertActive)
    {
        const bool cancelRequested = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                     ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        if (cancelRequested)
        {
            juce::Logger::writeToLog("[DragInsert] Drag cancelled.");
            cancelDragInsert();
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const bool editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                              ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            if (!pinHoveredDuringEditor && !nodeHoveredDuringEditor && !linkHoveredDuringEditor && editorHovered)
            {
                dragInsertDropPos = ImGui::GetMousePos();
                shouldOpenDragInsertPopup = true;
```

- **State reset helper** – A frame-local lambda clears the drag state when cancelled.

```2121:2127:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
    attrPositions.clear(); // Clear the cache at the beginning of each frame.
    auto cancelDragInsert = [this]()
    {
        dragInsertActive = false;
        dragInsertStartAttrId = -1;
        dragInsertStartPin = PinID{};
        shouldOpenDragInsertPopup = false;
    };
```

- **Suggestion cache** – At construction we prebuild `dragInsertSuggestions` from the static `PinDatabase`, seeding each `PinDataType` with curated utility modules and deduplicating by module type.

```9587:9639:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
void ImGuiNodeEditorComponent::populateDragInsertSuggestions()
{
    dragInsertSuggestions.clear();

    const auto& pinDb = getModulePinDatabase();

    auto addModuleForType = [this](PinDataType type, const juce::String& moduleType)
    {
        auto& modules = this->dragInsertSuggestions[type];
        if (std::find(modules.begin(), modules.end(), moduleType) == modules.end())
            modules.push_back(moduleType);
    };

    // Seed with curated converter/utilities for top ranking.
    addModuleForType(PinDataType::Audio, "attenuverter");
    addModuleForType(PinDataType::Audio, "comparator");
    addModuleForType(PinDataType::Audio, "mixer");
    // ...
    for (const auto& entry : pinDb)
    {
        const juce::String& moduleType = entry.first;
        const auto& info = entry.second;

        for (const auto& pin : info.audioIns)
            addModuleForType(pin.type, moduleType);
        for (const auto& pin : info.modIns)
            addModuleForType(pin.type, moduleType);
    }

    for (auto& entry : dragInsertSuggestions)
        std::sort(entry.second.begin(), entry.second.end(),
                  [](const juce::String& a, const juce::String& b)
                  {
                      return a.compareIgnoreCase(b) < 0;
                  });
}
```

- **Popup rendering** – On drop we open `DragInsertPopup`, show a simple menu of compatible modules, and call `insertNodeFromDragSelection` on click.

```3844:3873:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
    if (ImGui::BeginPopup("DragInsertPopup"))
    {
        const PinDataType displayType = dragInsertStartPin.isMod
            ? PinDataType::CV
            : getPinDataTypeForPin(dragInsertStartPin);
        const auto& suggestions = getDragInsertSuggestionsFor(dragInsertStartPin);

        if (suggestions.empty())
        {
            ImGui::TextDisabled("No compatible modules found.");
            if (ImGui::MenuItem("Close"))
            {
                dragInsertStartAttrId = -1;
                dragInsertStartPin = PinID{};
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::Text("Insert node for %s", pinDataTypeToString(displayType));
            ImGui::Separator();

            for (const auto& moduleType : suggestions)
            {
                if (ImGui::MenuItem(moduleType.toRawUTF8()))
                {
                    insertNodeFromDragSelection(moduleType);
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
```

- **Auto-wiring** – After spawning a module, we position it, connect the originating pin to the first compatible socket, and (audio only) attempt to connect the neighbouring channel for simple stereo cases.

```9655:9740:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
void ImGuiNodeEditorComponent::insertNodeFromDragSelection(const juce::String& moduleType)
{
    if (synth == nullptr || dragInsertStartAttrId == -1)
        return;

    auto newNodeId = synth->addModule(moduleType);
    auto newLogicalId = synth->getLogicalIdForNode(newNodeId);

    pendingNodeScreenPositions[(int)newLogicalId] = dragInsertDropPos;

    bool connected = false;
    if (!dragInsertStartPin.isMod)
    {
        if (!dragInsertStartPin.isInput)
        {
            auto srcNodeId = synth->getNodeIdForLogical(dragInsertStartPin.logicalId);
            if (srcNodeId.uid != 0)
            {
                synth->connect(srcNodeId, dragInsertStartPin.channel, newNodeId, 0);
                connected = true;
                // second-channel audio wiring omitted for brevity
            }
        }
        else
        {
            auto dstNodeId = dragInsertStartPin.logicalId == 0
                             ? synth->getOutputNodeID()
                             : synth->getNodeIdForLogical(dragInsertStartPin.logicalId);
            if (dstNodeId.uid != 0)
            {
                synth->connect(newNodeId, 0, dstNodeId, dragInsertStartPin.channel);
                connected = true;
                // mirror logic for stereo inputs
            }
        }
    }

    synth->commitChanges();
    graphNeedsRebuild = true;
    snapshotAfterEditor = true;
    // reset state...
}
```

- **Right-click synergy** – When the global `Add Module` popup opens during a drag insert, its search field is pre-filled with a colon-delimited token list (`:Type:ModuleA:ModuleB`) so keyboard search mirrors the drag suggestions.

```4496:4522:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
        if (ImGui::BeginPopup("AddModulePopup"))
        {
            static char searchQuery[128] = "";
            static int selectedIndex = 0;

            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetWindowFocus();

                if (dragInsertStartAttrId != -1)
                {
                    PinID displayPin = dragInsertStartPin;
                    auto type = displayPin.isMod ? PinDataType::CV : getPinDataTypeForPin(displayPin);

                    juce::String seed = ":" + juce::String(pinDataTypeToString(type));
                    juce::String modules;
                    const auto& suggestions = getDragInsertSuggestionsFor(displayPin);
                    for (size_t i = 0; i < suggestions.size(); ++i)
                        modules += ":" + suggestions[i];

                    juce::String tokenized = seed + modules;
                    auto truncated = tokenized.substring(0, juce::jmin((int)tokenized.length(), (int)sizeof(searchQuery) - 1));
                    std::memset(searchQuery, 0, sizeof(searchQuery));
                    std::memcpy(searchQuery, truncated.toRawUTF8(), (size_t)truncated.getNumBytesAsUTF8());
                }
                else
                {
                    searchQuery[0] = '\0';
                }
                ImGui::SetKeyboardFocusHere(0);
                selectedIndex = 0;
            }
```

- **Shared state** – Persistent members hold the drag origin pin, drop position, popup flag, and suggestion cache.

```321:330:juce/Source/preset_creator/ImGuiNodeEditorComponent.h
    bool dragInsertActive { false };
    int dragInsertStartAttrId { -1 };
    PinID dragInsertStartPin {};
    ImVec2 dragInsertDropPos { 0.0f, 0.0f };
    bool shouldOpenDragInsertPopup { false };

    // Module suggestion cache
    std::map<PinDataType, std::vector<juce::String>> dragInsertSuggestions;
```

---

## What It Is Doing Today

- **Pinned to static metadata** – Suggestion lists come entirely from `PinDatabase`. Dynamic pins added at runtime (e.g., sampler channels, meta-modules) and VST descriptors are invisible.
- **Flat suggestion ranking** – Apart from the hard-coded “seed” entries, all compatible modules are alphabetised. There is no weighting for popularity, existing connections, or converter utility, so long lists become unwieldy.
- **UI is mouse-first** – The popup is a plain ImGui menu. Keyboard focus lands on the first item, but there is no incremental search, arrow navigation feedback, or quick-close on Escape for the popup itself (only for the drag).
- **Audio stereo special-case** – After our latest tweak, dragging from a stereo audio pin attempts to wire channels 0/1 on both source and destination. Other multi-channel cases (CV buses, quad audio) fall back to single-channel wiring.
- **No “continue drag”** – Once the new node is spawned, the gesture ends. Users must start a fresh drag from the new node to keep patching.
- **Undo/redo relies on frame flag** – `snapshotAfterEditor` defers `pushSnapshot()` to the end of `renderImGui()`. If multiple drag inserts happen in one frame (unlikely but possible when scripting), they may share a single snapshot.

---

## Gaps vs. Expectations

- **Dynamic awareness gap** – Modules with runtime-declared pins or VST endpoints should appear in suggestions; today they never surface.
- **Context ranking gap** – We need smarter ordering (recently used, converters first, modules already in patch, etc.) and the ability to filter by tag/category, not just raw module names.
- **Accessibility gap** – Popup should expose the same navigation affordances as the right-click “Add Module” list: keyboard arrows, type-to-search, tooltips, optional thumbnail previews.
- **Multi-channel gap** – Only the simple stereo case is covered. There is no generalised bus wiring or CV/gate pairing logic.
- **Continuation gap** – After insertion we should be able to keep dragging from the new node’s free output/input automatically, matching the original product vision.
- **Undo atomicity gap** – Relying on a global flag is brittle. Drag insert should push an undo snapshot immediately after the graph mutation to ensure one history entry per operation.

---

## Fix Strategy

1. **Refine gesture lifecycle**
   - Promote `cancelDragInsert` to a proper member function so it can be reused by other code paths (e.g., when popups close).
   - Track the ImGui ID of the popup window and close it on Escape, matching the expectations for modal menus.
   - Record whether the drop landed on background vs. another pin, so the same handler can drive “insert inline” in the future.

2. **Upgrade suggestion intelligence**
   - Extend `populateDragInsertSuggestions()` to merge static `PinDatabase` data with runtime inspection (`getDynamicInputPins`, VST descriptors, recently created modules).
   - Introduce a scoring model (weights for converter utility, same module family, last-used modules) and sort by score before alphabetical fallback.
   - Cache per-type JSON blobs so external tooling (scripts, tests) can introspect suggestions without touching the UI.

3. **Improve popup UX**
   - Replace the plain menu with a searchable list: `ImGui::InputText` for incremental filtering, arrow-key navigation, Enter to confirm, Escape to cancel.
   - Display module metadata (category, key IO pins) to help users pick the right node quickly.
   - Add optional hotkeys (e.g., number keys) to select from the top N suggestions without moving the mouse.

4. **Generalise auto-wiring**
   - Extract the stereo logic into a helper that can map arbitrary bus layouts (e.g., iterate until either endpoint runs out of pins).
   - Support CV/gate pairing by querying `PinDatabase` pin groups (or introducing explicit pairing metadata).
   - Honour modules that expose dedicated converters (e.g., auto insert `map_range` when CV→audio) by offering bridge modules first.

5. **Support “continue drag”**
   - After creating and wiring the node, detect the nearest free output/input and programmatically start a new ImNodes drag so the user can chain modules without lifting the mouse.
   - Expose a preference toggle for users who prefer the current one-shot behaviour.

6. **Tighten undo/redo and threading**
   - Move the snapshot/commit sequence into `insertNodeFromDragSelection`: take snapshot → add module → connect → mark graph dirty → push snapshot.
   - Batch the two-channel connect calls inside a `ScopedGraphMutation` (already available in `ModularSynthProcessor`) to keep the debug assertions happy.
   - Add integration tests (or scripted UI tests) that perform repeated drag inserts while audio is running to guarantee no ImNodes scope leaks or graph races remain.

---

## Verification Checklist

- [ ] Drag from mono, stereo, and quad audio pins; confirm the popup appears, shows ranked suggestions, and auto-wiring matches channel counts.
- [ ] Drag from CV/Gate pins; ensure converter modules (attenuverter, logic) appear at the top and connections land on the correct modulation sockets.
- [ ] Insert multiple nodes rapidly and undo/redo each step; verify history granularity is exactly one operation per drag insert.
- [ ] Test with modules that expose dynamic pins (samplers, meta-modules) to confirm they populate suggestions and wire successfully.
- [ ] With audio running, drag-insert repeatedly to confirm no ImNodes scope assertions or audio-thread access violations fire.
- [ ] Exercise keyboard navigation: popup opens focused, Arrow/Enter select modules, Escape cancels cleanly.

---

## Related Files

- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h/.cpp` – gesture handling, popup rendering, suggestion cache, auto-wiring.
- `juce/Source/audio/graph/ModularSynthProcessor.h/.cpp` – module creation, connection APIs, undo snapshot plumbing.
- `juce/Source/preset_creator/PinDatabase.cpp` – static metadata for pins and module IO definitions.
- `guides/IMGUI_NODE_DESIGN_GUIDE.md` – ImGui/ImNodes best practices referenced when avoiding scope leaks.

By hardening the drag-insert pipeline around dynamic metadata, richer UI, and robust graph mutations, we can deliver the fluid “drag cable, pick compatible node, keep patching” experience the synthesizer needs.


