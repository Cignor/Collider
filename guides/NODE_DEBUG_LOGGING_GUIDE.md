# 🧩 Node Debug Logging Guide (`NODE_DEBUG`)

**Version**: 1.0  
**Last Updated**: 2025-11-09  
**Based on**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

---

## 🎯 Purpose

`NODE_DEBUG` is a build-time flag that controls verbose ImNodes logging in the preset creator UI. When enabled, every node render, link hover, and drag event emits logs (e.g. `[ImNodes][RenderNode] Begin vco [lid=1]`). This guide explains how to activate/deactivate it, what the logs mean, and how to use them strategically when chasing ImNodes scope bugs or drag/drop issues.

---

## 📍 Flag Location

```cpp
// juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
#define NODE_DEBUG 0   // flip to 1 to enable verbose node logging
```

- The flag sits near the top of the file, just after the ImNodes depth snapshot helpers.
- `LOG_LINK` macros and the `ImNodesDepthSnapshot` instrumentation respect this flag.
- Default is `0` (silent) to avoid flooding the JUCE logger during normal use.

---

## ✅ What NODE_DEBUG Enables

When set to `1`:

1. **Per-frame Render Logs**
   - `[ImNodes][RenderNode] Begin … / End …` for each node (`lid` = logical ID).
   - Confirms every `BeginNode` has a matching `EndNode`.

2. **Hover/Drag Diagnostics**
   - `[LINK] …` messages show link hover state, pin availability, and missing attribute cases.

3. **Depth Snapshots**
   - `ImNodesDepthSnapshot` warns if `Begin/End` scopes or input/output attributes become unbalanced (`[ImNodes][DepthLeak] …`).

4. **Optional Stack Guards (Debug Only)**
   - `ImGuiStackBalanceChecker` validates ImGui push/pop sequences inside module UIs.

These logs are invaluable when assertions like `GImNodes->CurrentScope == ImNodesScope_None` fire; they let you trace which module left a scope open or misplaced an `Indent/Unindent`.

---

## 🚫 Why Keep It Off Normally?

- **Log Flooding**: Even with a small graph, the render loop runs every frame. Keeping the flag on produces hundreds of lines per second, overwhelming the console.
- **Performance**: String formatting and logging on every frame adds overhead, especially when the graph is complex.

Leave `NODE_DEBUG` set to `0` unless you’re actively debugging ImNodes layout issues.

---

## 🔄 Toggling the Flag

### Activate (Verbose Logging)
1. Open `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`.
2. Change `#define NODE_DEBUG 0` ➜ `1`.
3. Rebuild the preset creator.
4. Run the app; logs will appear in the JUCE logger (console or log file, depending on configuration).

### Deactivate (Silent)
1. Revert the define to `0`.
2. Rebuild; logging ceases immediately.

⚠️ This is a compile-time flag. You must rebuild after each change.

---

## 🛠️ Strategic Usage

1. **Chasing Scope Assertions**  
   - Enable `NODE_DEBUG`, reproduce the issue, watch the `[RenderNode]` sequence. If a node logs `Begin` without `End`, its UI likely exits early without closing an ImNodes scope.

2. **Diagnosing Link Insert Issues**  
   - The `[LINK]` logs show whether source/destination attributes exist. Missing entries reveal race conditions or UI failing to register pins.

3. **Balancing ImGui Stacks**  
   - `ImGuiStackBalanceChecker` (active in debug builds) logs `[ImGui][StackLeak]` if `PushID/PopID`, `Indent/Unindent`, or `Begin/End` pairs mismatch. Use alongside `NODE_DEBUG` to spot modules that break layout rules documented in `IMGUI_NODE_DESIGN_GUIDE.md`.

4. **Regression Testing**  
   - Before shipping a complex UI change, flip `NODE_DEBUG` on briefly to ensure render logs look healthy (Begin/End always paired, no depth leaks).

---

## 🔍 Related Files & Helpers

| File | Purpose |
|------|---------|
| `ImGuiNodeEditorComponent.cpp` | Defines `NODE_DEBUG`, render loop instrumentation, depth snapshots |
| `IMGUI_NODE_DESIGN_GUIDE.md` | Best practices to avoid scope bugs (indent rules, Begin/End pairing) |
| `moofys_drag_assertion.md` | Context on debug tooling and scope-leak debugging |

---

## 🧪 Quick Sanity Checklist

- [ ] Toggle `NODE_DEBUG` ➜ `1`, rebuild, confirm `[ImNodes][RenderNode]` logs appear.
- [ ] Revert to `0` after debugging; rebuild to silence logging.
- [ ] If logs mention `DepthLeak`, investigate the named module’s `drawParametersInNode`/`drawIoPins`.
- [ ] Keep the flag off in production builds to avoid log spam and performance hits.

---

**End of Guide** | Version 1.0 | 2025-11-09

