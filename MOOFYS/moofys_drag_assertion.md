## ImNodes Scope Assertion (`GImNodes->CurrentScope == ImNodesScope_None`)

### What Happened

The preset creator hit the debug assertion inside `imnodes.cpp`:

```
Assertion failed!
Expression: GImNodes->CurrentScope == ImNodesScope_None
```

This fires when the ImNodes state machine finishes a frame with an **unbalanced scope**—most often a missing `EndNode`, `EndInputAttribute`, or similar. In debug builds the CRT breaks into `_wassert` (`ucrtbased.dll!common_assert_to_message_box`), so you see the INT3/`0x80000003` breakpoint.

### Why It’s Not the Meta Module Changes

The recent Meta-module work touched channel routing and collapse rewiring only; we didn’t modify the render stack (`ImGuiNodeEditorComponent::renderImGui`) or the modules’ `drawParametersInNode` implementations. The assertion happens during link drag logic (`ImNodes::IsLinkStarted`) before any of the new code is involved.

### Root Cause

An ImNodes draw routine left the editor in a non-None scope. Typical triggers:

1. Missing `ImNodes::EndInputAttribute()` / `ImNodes::EndOutputAttribute()`.
2. Missing `ImNodes::EndNode()` (often due to early returns).
3. ImGui scope leakage—`ImGui::Indent()` without `ImGui::Unindent()`, or unbalanced `Push/Pop` pairs—causing later nodes to skip their `End*` calls.

Our `IMGUI_NODE_DESIGN_GUIDE.md` documents several critical fixes:

- **Indent/Unindent** (§9.3) – forgetting `Unindent()` persists the scope and breaks ImNodes.
- **Tables / Collapsing headers** (§12) – ensure every `BeginTable()` has a matching `EndTable()` even when conditionals short-circuit.
- **Output pin patterns** (§9.5) – use `Indent()` / `Unindent()` per pin; don’t `return` before the `End` call.

### How to Diagnose

1. Run the preset creator in Debug and replicate the issue.
2. When it breaks, open the call stack: `ImGuiNodeEditorComponent::renderImGui` → `ModuleProcessor::draw...`.
3. Inspect the module at the top of the stack; verify all `Begin`/`End` pairs and `Push`/`Pop` pairs.
4. Compare against patterns in the guide; pay attention to recently edited modules (PolyVCO, MIDI suites, custom nodes).

### Immediate Next Steps

- Audit `drawParametersInNode` and `drawIoPins` for modules with complex UI (PolyVCO, MIDI Faders/Knobs, Stroke Sequencer).
- Check for conditionals that `return` before `End...` calls.
- Add debug logging or asserts around `ImNodes::GetCurrentScope()` if needed.

Once balance is restored, the assertion disappears without touching the Meta module changes.
# Drag Insert Assertion Crash

# Scope Assertion Investigation

## Problem
- Runtime assert `GImNodes->CurrentScope == ImNodesScope_None` persists after drag insert updates.
- Indicates unbalanced `ImNodes::Begin...` / `End...` or ImGui `Push` / `Pop` calls in module UI rendering.
- Compile-time guard using `ImNodes::GetCurrentScope()` removed (not part of shipped API).

## Current Bundle
- `MOOFYS\moofy_drag_node_assert.ps1` exports `moofy_DragNodeAssertion.txt`:
  - `ImGuiNodeEditorComponent.h/.cpp`
  - `VCOModuleProcessor.h/.cpp`
  - `MIDIJogWheelModuleProcessor.h/.cpp`
  - `CommentModuleProcessor.h/.cpp`
  - `IMGUI_NODE_DESIGN_GUIDE.md`
  - `moofys_drag_assertion.md`
  - Scope-audit helper scripts (`moofy_unbalanced_scope_audit.ps1`, `moofy_shortcut_manager.ps1`)
  - Additional references (`guides/SHORTCUT_SYSTEM_SCAN.md`, `.cursor/commands/communicate.md`)

## To Investigate
- Audit all `ModuleProcessor::drawParametersInNode` / `drawIoPins` overrides:
  - Ensure every `ImNodes::BeginInputAttribute/BeginOutputAttribute` has matching `End`.
  - Confirm every `ImGui::Indent()` has `ImGui::Unindent()`; check complex tables for `EndTable`.
  - Pay special attention to PolyVCO, MIDI modules, Meta module helpers.
- Consider adding temporary instrumentation (RAII guards / asserts) per module to detect mismatches.

## Goal
- Identify offending module(s) leaving scopes open across frames.
- Provide clean repro & patch plan for external expert once culprit is located.

