# Shortcut System Scan

## Objective Recap

The user wants a **shortcut manager** comparable to Blender's: a searchable list of all keyboard shortcuts plus direct reassignment from UI (right-click a control → assign shortcut). We need to locate the existing shortcut handling code, understand how shortcuts are wired today, and identify integration points for a centralized manager that can enumerate/modify bindings.

---

## Where Shortcuts Live Today

- **`juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`**
  - The entire shortcut implementation is hard-coded inside the main `render()` loop.
  - Global toggle: `F1` flips `showShortcutsWindow` (`ImGui::IsKeyPressed(ImGuiKey_F1, false)` around the top of the frame).
  - **Keyboard handling block** (`~L4814-5102`): wrapped in `if (!ImGui::GetIO().WantCaptureKeyboard)` before checking modifiers.
    - File operations: `Ctrl+S`, `Ctrl+O`, `Ctrl+P`, `Ctrl+Alt+S` (save variants), undo/redo (`Ctrl+Z` / `Ctrl+Y`).
    - Layout utilities: `Ctrl+B` (beautify), `Ctrl+A` (select all), `F` (frame), `Home` / `Ctrl+Home`.
    - Graph actions: `Ctrl+T` (insert mixer), `Ctrl+I` (insert-node popup), `Ctrl+R` (record output & parameter reset), `Alt+D` (disconnect), `M` (mute), `O` (route to output), `Ctrl+Shift+D` (system diagnostics).
    - Node chaining shortcuts (`C`, `G`, `B`, `Y`, `R`, `V`) handled under the "Keyboard Shortcuts for Node Chaining" section (`~L3981-4016`).
  - `showShortcutsWindow` draws the static help window enumerating shortcuts (`~L5281-5344`).
  - Debounce state (`mixerShortcutCooldown`, `insertNodeShortcutCooldown`) stored as member variables to avoid repeated triggers.
  - Other inline handlers: cable splitting (Ctrl + Middle click), minimap enlarge (`ImGuiKey_Comma`), modal dismiss (`Escape`).

- **`juce/Source/preset_creator/ImGuiNodeEditorComponent.h`**
  - Declares `showShortcutsWindow`, cooldown booleans, and handler methods like `handleNodeChaining()`, `handleColorCodedChaining()`, `handleRecordOutput()` invoked from shortcut checks.

- **Helper Methods** (same file): logic for actions triggered by shortcuts (e.g., `handleRecordOutput`, `handleRandomizePatch`, `handleBeautifyLayout`). They currently assume invocation from hard-coded keybindings.

- **Other files**
  - `PresetCreatorComponent.cpp` only requests keyboard focus (`setWantsKeyboardFocus(true)`); no shortcut logic elsewhere.
  - No central JUCE `ApplicationCommandManager` usage or ImGui shortcut map—everything routes through direct `ImGui::IsKeyPressed()` calls.

---

## Current Behaviour Summary

```3171:3344:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
// Ctrl+T inserts a Mixer after the selected node
if ((triggerInsertMixer || (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_T))) && !mixerShortcutCooldown)
{
    mixerShortcutCooldown = true;
    ... // create mixer, reroute connections, push snapshot
}

// Ctrl+I opens insert-node popup
if (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_I) && !insertNodeShortcutCooldown)
{
    insertNodeShortcutCooldown = true;
    showInsertNodePopup = true;
}
```

- Shortcuts are evaluated every frame, so modifiers/reset logic must be manually maintained.
- `ImGui::GetIO().WantCaptureKeyboard` gate attempts to avoid conflicts with ImGui widgets, but there is no per-context routing.
- The help window is static text; there is no data structure representing registered shortcuts.

---

## Identified Gaps vs. Desired Shortcut Manager

| Requirement | Current State | Gaps |
|-------------|---------------|------|
| Enumerate all shortcuts | No registry; only static help text | Need a canonical data table describing actions, default bindings, categories |
| Rebind via UI | Hard-coded checks; no storage for custom bindings | Need persistent mapping (probably per-profile, saved to disk) and dynamic evaluation |
| Right-click assign on any parameter | No linkage between UI widgets and shortcuts | Need metadata per parameter/control, plus context menu hook to call into manager |
| Collision handling | Manual; developers ensure no duplicates | Need conflict detection and resolution workflow |
| Persistence | Defaults baked into code | Need config (likely JSON/ValueTree) saved alongside user prefs |
| Multi-context support | Single global block in node editor | Need to support other components (timeline editor, modal popups) and scopes |

---

## Integration Hotspots for New Manager

1. **Input Dispatch** – Replace the manual `ImGui::IsKeyPressed` cascade with a central dispatcher that resolves current bindings, handles repeats, and routes actions.
2. **Shortcut Registry** – Introduce a data structure (e.g., `struct ShortcutDefinition { juce::Identifier actionId; juce::String defaultBinding; ... }`) living in a dedicated module (e.g., `ShortcutManager` under `preset_creator/`).
3. **UI Hooks** – Modify ImGui controls (sliders, buttons, menu items) to expose `right-click → Assign Shortcut` context menus. This likely piggybacks on existing parameter rendering helpers.
4. **Persistence Layer** – Store overrides via `ValueTree` or JSON in the preset creator settings directory (look at existing theme/config save paths for consistency).
5. **Help Window** – Rebuild F1 dialog to query the registry dynamically instead of static bullet text.

---

## Immediate Questions Raised

- How do we scope shortcuts (global vs. per-module vs. per-parameter)?
- Where should user-defined bindings be saved, and do we need profile support?
- Do we need to support multi-key sequences, mouse combos, or just single chord shortcuts?
- How should conflicts be surfaced (blocking assignment, warning, auto-remap)?
- Does the audio engine already need to know about shortcuts (e.g., transport commands), or can everything stay in UI thread?

These will be forwarded to the external helper via the upcoming Moofy request.
