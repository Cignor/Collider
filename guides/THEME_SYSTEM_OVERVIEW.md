# 🎨 Theme System Overview & Maintenance Guide

**Status**: November 2025 audit  
**Scope**: ThemeManager runtime behaviour, ThemeEditor interactions, JSON preset workflow, and integration touchpoints inside the preset creator UI.

---

## 🧭 Quick Summary

- The theme stack is split between a **runtime singleton** (`ThemeManager`) and an **ImGui-side editor** (`ThemeEditorComponent`).
- All UI entry points, colour lookups, and font rebuilds go through `ThemeManager`, so any feature that needs colours or layout constants should query it instead of duplicating state.
- The editor works on a **working copy** of the current theme. `Apply Changes` pushes the working copy into the singleton and requests a font rebuild; `Save As...` serialises to `exe/themes/<name>.json`.
- A `.last_theme` marker in the same folder remembers the last chosen preset and is loaded automatically the next time the OpenGL context is created.

---

## 🧱 Key Components

### ThemeManager (`juce/Source/preset_creator/theme/ThemeManager.{h,cpp}`)

- Singleton created on first use; constructs a default theme via `loadDefaultTheme()`.
- Exposes getters for every colour/layout/font group plus mutable access through `getEditableTheme()`.
- `applyTheme()` copies `Theme::style` into the active ImGui style object and patches accent-related colours. ImNodes colours are pushed ad-hoc by the node editor render loop.
- `loadTheme()`/`saveTheme()` translate between the `Theme` struct and JSON files. Loading always starts from defaults to guarantee sane fallbacks for missing keys.
- Font handling:
  - `requestFontReload()` sets an atomic flag consumed by `ImGuiNodeEditorComponent::renderOpenGL()`.
  - `applyFonts(io)` clears the atlas, resolves the font path relative to the executable, adds the font (or reverts to ImGui default), and adjusts `FontGlobalScale`.
- Persistence helpers read/write `.last_theme` inside the bundled `themes/` folder so user selections survive restarts.

### ThemeEditorComponent (`juce/Source/preset_creator/theme/ThemeEditorComponent.{h,cpp}`)

- Owned by `ImGuiNodeEditorComponent` and rendered every frame; visibility toggled via Settings → Theme → *Edit Current Theme...*.
- Maintains `m_workingCopy`, a `Theme` cloned from the manager. Changes are tracked by `m_hasChanges`.
- Sixteen tabs organise the editable sections (ImGui style/colours, accent, ImNodes, links, canvas, layout, fonts, windows, modulation, meters, timeline, modules).
- Utility helpers (`colorEdit4`, `colorEditU32`, `dragFloat*`, `triStateColorEdit`) wrap ImGui widgets while flipping `m_hasChanges` and logging to the JUCE logger.
- Fonts workflow:
  - `scanFontFolder()` enumerates `exe/fonts` recursively (limited to TTF/OTF/TTC).
  - Selecting or browsing a font updates the working copy and calls `previewFontChanges()` which patches the live `ThemeManager` font settings and triggers a deferred atlas rebuild.
- Eyedropper support delegates to the parent node editor via `setStartPicker`, which in turn captures framebuffer pixels after the frame is rendered.

### Integration Touchpoints

- `ImGuiNodeEditorComponent::newOpenGLContextCreated()` loads the saved preset (if present) and calls `ThemeManager::applyTheme()` when needed.
- During rendering:
  - `renderOpenGL()` checks `ThemeManager::consumeFontReloadRequest()` and calls `rebuildFontAtlas()` when required.
  - The node canvas section continuously queries `ThemeManager` for grid colours, node accent colours, and pin colours when drawing custom overlays or evaluating connection states.
- UI menus: Settings → Theme includes a curated list of shipped presets and refreshes the editor copy post-load via `themeEditor.refreshThemeFromManager()`.

---

## ⚠️ Current Gaps & Observations

1. **Reset Tab Stub**  
   `ThemeEditorComponent::resetCurrentTab()` still reloads the entire theme instead of restoring only the active section. This prevents selective undo and is inconsistent with the UI hint.

2. **Unsaved Changes Protection**  
   Closing the editor with dirty changes simply discards the working copy (`close()` logs TODO). A confirmation dialog or auto-save option is still missing.

3. **Verbose Logging Noise**  
   Every colour widget and drag control logs to `juce::Logger` each frame. This floods the console while dragging sliders. Consider gating behind `JUCE_LOGGING_LEVEL` or adding a throttled debug macro.

4. **ImNodes Style Drift**  
   The node editor currently pushes ImNodes colours manually each frame (`ImGuiNodeEditorComponent::renderImGui`). If new ImNodes properties are themed, remember to update that block; `ThemeManager::applyTheme()` does not touch ImNodes styles directly.

5. **JSON Schema Evolution**  
   `loadTheme()` silently ignores unknown keys and relies on defaults for missing keys. Introduce version stamps or validation if you plan to ship schema-breaking changes.

6. **Font Path Feedback**  
   When a font path fails to load, the fallback to ImGui default is logged but the UI still shows the custom file name. Resetting `m_selectedFontIndex` on failure would keep the editor state accurate.

---

## 🛠️ Extending the System

### Adding New Theme Properties

1. Append the property to the `Theme` struct.
2. Initialise it in `ThemeManager::loadDefaultTheme()`.
3. Update `loadTheme()`/`saveTheme()` to parse and serialise the value (use existing helper lambdas where possible).
4. Expose a getter on `ThemeManager` if runtime consumers need easy access.
5. Add editing controls to the appropriate tab inside `ThemeEditorComponent`.

### Supporting New Module Categories

- Extend `ModuleCategory` in `Theme.h`.
- Update `ThemeManager::moduleCategoryToString()` / `stringToModuleCategory()` along with default colours in `loadDefaultTheme()`.
- Add the category to the ImNodes tab list in `ThemeEditorComponent` so designers can tweak it.
- Ensure any categorisation logic (e.g. search keywords in `ImGuiNodeEditorComponent::getModuleCategory`) is aware of the new label.

### Shipping a New Preset

1. Export the theme via the editor (`Save As...`).  
2. Commit the JSON file into `juce/Source/preset_creator/theme/presets/` and bundle it into the runtime `themes` folder.  
3. Add an entry to the Settings → Theme menu list with a friendly name and matching filename.

---

## ✅ Testing Checklist

- Launch the preset creator, open Settings → Theme, and verify the theme list loads without stderr warnings.
- Edit colours, hit `Apply Changes`, and ensure grid/pin colours update immediately.
- Switch fonts, confirm the atlas rebuilds on the next frame (watch for `[ThemeManager] Rebuilding font atlas...` logs) and that text scales according to `FontGlobalScale`.
- Save a custom preset, close the app, relaunch, and check that `.last_theme` re-applies the saved file automatically.
- Regression test ImNodes rendering (selected/hovered/muted node states) after any theming change.

---

## 📌 Follow-Up Tasks

- Implement per-tab reset logic (load defaults for the active category without touching other sections).
- Add confirmation when closing with `m_hasChanges == true`.
- Trim logging noise or guard it with a compile-time flag.
- Factor out the duplicated ImGui colour maps between `loadTheme()` and `saveTheme()` to avoid drift when new entries are added.
- Provide user feedback inside the Fonts tab when a selected file is missing or fails to load (reset selection + banner message).

---

**Last reviewed**: 11 Nov 2025  
**Maintainer**: Collider Modular Synth team


