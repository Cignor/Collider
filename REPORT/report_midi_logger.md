# MidiLoggerModuleProcessor Issue Report

## Executive Summary
The `MidiLoggerModuleProcessor` fails to record and display information correctly due to a combination of **rendering bugs**, **concurrency violations**, and **missing synchronization logic**. The most critical issues involve a race condition that can cause crashes and a rendering layering issue that makes recorded notes invisible.

## 1. Critical Stability & Logic Issues

### 🔴 Race Condition (Severe)
- **The Issue:** The `tracks` vector is resized and modified inside `processBlock` (Audio Thread) via `ensureTrackExists`, while simultaneously being read by `drawParametersInNode` (UI Thread).
- **Impact:** `std::vector` resizing invalidates iterators and memory locations. If the UI tries to read `tracks` while the audio thread is resizing it, the application will read garbage memory or crash.
- **Fix:** Track creation must be decoupled. The audio thread should request a track, and the main thread should handle the allocation/resizing safely, or a fixed pool of tracks should be pre-allocated.

### 🔴 Real-Time Safety Violation
- **The Issue:** `processBlock` calls `new MidiTrack()` and `std::vector::resize`.
- **Impact:** Memory allocation is non-deterministic and can block the audio thread, causing audio dropouts (clicks/pops), especially as the vector grows.
- **Fix:** Pre-allocate a reasonable number of tracks (e.g., 16 or 32) in `prepareToPlay` and simply toggle them as active/inactive.

### 🟠 Missing BPM Synchronization
- **The Issue:** `currentBpm` is initialized to `120.0` and **never updated**.
- **Impact:** If the host tempo is not 120 BPM, the visual representation (Grid/Piano Roll) will be completely desynchronized from the recorded audio. The "samples per beat" calculation will be wrong, making notes appear at the wrong times or with wrong durations.
- **Fix:** Update `currentBpm` from `getPlayHead()` inside `processBlock`.

## 2. Display & Rendering Bugs

### 🔴 Z-Order / Visibility Issue
- **The Issue:** The code obtains the `drawList` from the parent window (`MainContent`) but draws the notes *after* creating the `PianoRoll` child window.
- **Impact:** Standard ImGui child windows have a background. The `PianoRoll` window is likely drawing its background *on top* of the notes you drew on the parent's layer, making them invisible.
- **Fix:** Use `ImGui::GetWindowDrawList()` *inside* the `PianoRoll` child scope to draw on the correct layer, or ensure the child has no background.

### 🟠 Clipping / Width Issue
- **The Issue:** `ImGui::BeginChild("PianoRoll", ...)` is called with a width of `0` (auto/view width).
- **Impact:** Even though you draw notes at X positions corresponding to the timeline, the `PianoRoll` window itself is only as wide as the visible view. Notes that extend beyond the current view width are clipped by the child window's bounds, even if the parent scrollbar is used.
- **Fix:** The `PianoRoll` child window must be initialized with the full `totalWidth` (calculated from loop length) so it matches the timeline ruler and allows proper scrolling.

## 3. Recording Logic Observations

- **Gate Threshold:** Recording triggers when the gate signal crosses `0.5f`. Ensure your input signals are strong enough (standard CV gates are usually 5V or 10V, mapped to 1.0 in JUCE usually, but ensure they aren't extremely weak).
- **Playhead Reset:** The playhead resets to 0 when Recording starts. This is standard for a simple recorder but prevents overdubbing at a specific point.

## Recommended Action Plan

1.  **Refactor Data Structure:** Remove dynamic allocation from `processBlock`. Switch to a fixed-size array or pre-allocated vector of Tracks.
2.  **Fix Concurrency:** Use a lock or atomic flags to manage track active states safely between threads.
3.  **Fix Rendering:**
    - Update `currentBpm` in `processBlock`.
    - Set correct width for `PianoRoll` child.
    - Draw on the correct `ImDrawList`.
4.  **Verify Inputs:** Ensure the `gate` signals are actually reaching the processor (using a simple debug print or visualizer).
