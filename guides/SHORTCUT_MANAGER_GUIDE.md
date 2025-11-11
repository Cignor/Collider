# Shortcut Manager Guide

## Overview

Collider now routes keyboard shortcuts through a central manager backed by Dear ImGui. Actions register once, provide human-readable metadata, and receive default bindings. At runtime, the manager merges defaults with per-user overrides, resolves conflicts, and dispatches callbacks only when the active context matches. Users can search, rebind, and reset shortcuts through the new editor window.

- **Actions** (`collider::ShortcutAction`) describe what a shortcut does.
- **Bindings** associate an action with a `KeyChord` in a given context.
- **Contexts** scope shortcuts to parts of the UI (e.g. `Global`, `NodeEditor`).
- **Defaults** live in `assets/default_shortcuts.json`. User overrides persist to `%APPDATA%/Collider/user_shortcuts.json`.

The manager exposes a singleton (`ShortcutManager::getInstance()`) that components use to register actions and process key input each frame.

## For Developers

### Registering a new action

1. Define a stable `juce::Identifier` (e.g. `actions.view.toggleGrid`).
2. Call `shortcutManager.registerAction` with the action metadata and a callback. Callbacks can flip atomics, invoke methods, or enqueue work on the UI thread:

```cpp
registerAction(ShortcutActionIds::viewToggleGrid,
               "Toggle Grid Overlay",
               "Show or hide the editor grid.",
               "View",
               { ImGuiKey_G, true, false, false, false },
               shortcutToggleGridRequested);
```

3. Provide a default `KeyChord` matching existing behaviour. Defaults should also be listed in `assets/default_shortcuts.json` (see below).
4. When the shortcut should act, check and consume the atomic flag (or call your handler directly) inside the component’s render/update loop.

**Important:** resource lifetimes matter. Call `unregisterAction` from the component’s destructor to avoid dangling callbacks.

### Processing key input

Inside your component’s frame loop:

```cpp
if (imguiIO != nullptr && !shortcutCaptureState.isCapturing)
    shortcutManager.processImGuiIO(*imguiIO);
```

The manager scans ImGui key events, builds a `KeyChord` for each new press, and dispatches any matching action in the active context.

### Context management

Contexts prevent shortcuts from colliding across panels. Use `ScopedShortcutContext` to set a context for the duration of a render scope:

```cpp
using collider::ScopedShortcutContext;

void TimelinePanel::renderImGui()
{
    ScopedShortcutContext ctx(shortcutManager, timelineContextId);
    shortcutManager.processImGuiIO(io);
    // render panel …
}
```

For transient popups/modals, create a scoped guard inside `ImGui::BeginPopup…`. When the guard goes out of scope, the previous context is restored automatically.

### Assigning from other UI controls

To expose “Assign Shortcut…” on right-click:

```cpp
if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    ImGui::OpenPopup("assign-shortcut");

if (ImGui::BeginPopup("assign-shortcut"))
{
    if (ImGui::MenuItem("Assign Shortcut…"))
    {
        beginShortcutCapture(actionId, contextId);
        showShortcutEditorWindow = true;
    }
    ImGui::EndPopup();
}
```

You can keep action IDs generic (e.g. `params.assignToSelection`) and store the parameter metadata somewhere the callback can access when triggered.

## Default bindings asset

`assets/default_shortcuts.json` is the canonical source for shipping defaults. The schema matches `ShortcutManager::bindingFromVar`:

```json
{
  "version": 1,
  "bindings": [
    {
      "actionId": "actions.file.save",
      "context": "NodeEditor",
      "key": 564,
      "ctrl": true,
      "shift": false,
      "alt": false,
      "super": false
    }
  ]
}
```

- `key` is the integer value of `ImGuiKey` (see Dear ImGui’s `imgui.h`).
- Include every action/context pair you want to ship by default.
- Increment `version` if you change the defaults and add migration handling later if needed.

During startup, the node editor loads this file first, then applies user overrides from `%APPDATA%/Collider/user_shortcuts.json`. If files are missing or invalid, the manager logs the error (`juce::Logger`) and falls back to in-code defaults.

## Shortcut Editor UI

- **Open** via `F1` (toggle help/editor window) or Use the modal triggered by “Assign Shortcut…”.
- **Search** actions by name, description, or category.
- **Assign**: click `Assign`, press the desired chord. Assignment is immediate; conflicts are replaced automatically. Press `Esc` to cancel capture.
- **Clear** removes the user override (the action falls back to its default/global binding).
- **Reset** removes the user override and restores the default binding explicitly.
- **Save Changes** writes overrides to `%APPDATA%/Collider/user_shortcuts.json`. The file is also saved automatically when the editor closes and bindings are dirty.

## File locations

- Defaults: `assets/default_shortcuts.json` (inside the application bundle).
- User overrides: `%APPDATA%/Collider/user_shortcuts.json` (created on demand).
- Logging: see `juce::Logger` output for parse/write errors.

## Tips

- Keep action IDs stable; changing them breaks user overrides.
- Group related actions with consistent category strings so the editor table sorts cleanly.
- When adding new contexts, document expected usage in code and update this guide.
- For multi-step operations triggered by shortcuts, prefer using atomics or queues so the callback stays lightweight and deterministic.

Questions or enhancements? Ping the UI systems channel so we can keep the manager cohesive. Happy hacking!

