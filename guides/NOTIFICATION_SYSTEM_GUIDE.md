# 🎯 Notification System – Design, API, and Integration

**Objective**: Provide a concise, implementation-ready guide for a robust, ImGui-rendered, thread-safe notification system that integrates cleanly with the JUCE + ImGui architecture of the Preset Creator. This complements the existing `guides/NOTIFICATION_SYSTEM_INTEGRATION_GUIDE.md` with a focused API/architecture spec, migration checklist, and test plan for external contributors.

---

## 1) Context & Constraints
- **UI stack**: JUCE windowing + OpenGL, full UI drawn via ImGui in `renderImGui()`.
- **Threads**: UI (Message thread), background jobs (`juce::ThreadPool`), real-time audio thread.
- **Requirement**: Rendering must happen on the UI thread; posting must be safe from any thread.
- **Current stopgap**: `showSaveNotification(juce::String, juce::Colour)` + a temporary spinner. Needs replacement with a centralized, extensible system.

---

## 2) Functional Requirements
- **Types**: Status (spinner), Success, Error, Warning, Info (distinct icon/color).
- **Queueing**: Multiple notifications stack top-right, newest on top, slide/fade in/out.
- **Dismissal**: Auto-dismiss (configurable) for non-error; click-to-dismiss for all; errors persist until clicked.
- **Progress**: Optional time-remaining indicator per notification.
- **Thread-safety**: `post(...)` callable from any thread; all rendering in UI thread.

---

## 3) Data Model
```cpp
// New code – suggested shape
struct Notification {
    enum class Type { Status, Success, Error, Warning, Info };
    Type type;
    juce::String message;

    // lifecycle
    double createdAtSeconds = 0.0;     // ImGui::GetTime() snapshot at creation
    double durationSeconds = 5.0;      // auto-dismiss for non-error
    bool   persistent = false;         // true for Error by default

    // UI animation state (message-thread-only)
    float  fade = 0.0f;                // 0.0 → 1.0 during fade-in; reverse on out
    float  slidePx = 0.0f;             // horizontal slide offset

    // optional extras
    std::optional<float> progress01;   // for Status; nullopt if not used
};
```

---

## 4) Manager API (Singleton)
```cpp
class NotificationManager {
public:
    static NotificationManager& getInstance();

    void post(Notification::Type type,
              const juce::String& message,
              std::optional<double> durationSeconds = std::nullopt,
              std::optional<float> progress01 = std::nullopt);

    // message-thread only: called inside renderImGui()
    void render(int viewportWidth, int menuBarHeightPx);

    // optional helpers
    void clearAll();                      // message-thread only
    void tickStatusProgress(float delta); // message-thread only; bulk progress update

private:
    NotificationManager() = default;

    juce::CriticalSection lock;                 // protects inbox only
    std::vector<Notification> inbox;           // cross-thread posts land here
    std::vector<Notification> active;          // message-thread only, rendered/animated
};
```

Design notes:
- **Inbox vs Active**: `post(...)` pushes to `inbox` under lock from any thread. In `render(...)`, swap/drain `inbox` into `active` (message-thread only), then animate and draw `active`.
- **Timing**: Use `ImGui::GetTime()` in `render(...)` to compute fade/slide and auto-dismiss.
- **Icons/colors**: Map by type; use small left glyph (emoji or icon font) + colored background.

---

## 5) Rendering & UX Guidelines
- **Position**: Top-right, below menu bar. Example margins: 10 px from right edge; 8 px vertical spacing.
- **Animation**: 150–200 ms fade and 120–160 px slide-in from the right; mirror on exit.
- **Layout**: Icon (left, fixed width ~20–24 px) + text; rounded, semi-transparent panel.
- **Auto-dismiss**: Only if `!persistent`; progress bar at bottom shows remaining time.
- **Stacking**: Render newest first at the top; as items exit, compact the stack.

Pseudo-render flow (message thread):
```cpp
void NotificationManager::render(int viewportWidth, int menuBarHeightPx) {
    // 1) Drain inbox
    {
        const juce::ScopedLock sl(lock);
        for (auto& n : inbox) active.push_back(n);
        inbox.clear();
    }

    // 2) Animate, layout, draw
    const float padding = 10.0f;
    const float notifWidth = 320.0f;
    float y = static_cast<float>(menuBarHeightPx) + padding;

    const double now = ImGui::GetTime();

    // iterate newest-first so top packs most recent
    for (int i = static_cast<int>(active.size()) - 1; i >= 0; --i) {
        auto& n = active[static_cast<size_t>(i)];
        // update fade/slide based on lifetime
        // draw ImGui window with custom bg, icon, text, optional progress
        // update y by item height + spacing
        // mark for removal if expired and fade-out complete
    }

    // 3) Erase removed
}
```

---

## 6) Threading Patterns
- From background threads: prefer `juce::MessageManager::callAsync([=]{ post(...); });` for simplicity.
- For high-volume posting: direct `post(...)` with `juce::CriticalSection` on `inbox` is acceptable; rendering consumes on UI thread.
- Never render outside UI thread. Never block audio thread; if needed, bounce via `callAsync`.

---

## 7) Integration Checklist
1. Add `NotificationManager` singleton (header/impl) under `juce/Source/ui/notifications/` (or equivalent).
2. Call `NotificationManager::getInstance().render(getWidth(), ImGui::GetFrameHeight());` near the end of `renderImGui()`.
3. Replace `showSaveNotification(...)` and the ad-hoc spinner with:
   - `post(Status, "Saving preset...")` at job start
   - `post(Success, ...)` or `post(Error, ...)` in completion callback
4. Hook additional events: load, undo/redo, auto-connection actions, scanner, etc.
5. Remove old `juce::Label`-based notification elements once parity is reached.

---

## 8) Minimal Usage Examples
```cpp
// Status → Success
NotificationManager::getInstance().post(Notification::Type::Status, "Saving preset...");
juce::MessageManager::callAsync([file]{
    NotificationManager::getInstance().post(Notification::Type::Success,
                                            "Saved: " + file.getFileNameWithoutExtension());
});

// Warning with custom duration
NotificationManager::getInstance().post(Notification::Type::Warning,
                                        "Preset loaded with 2 healed links",
                                        6.0 /* seconds */);

// Error (persistent until click)
NotificationManager::getInstance().post(Notification::Type::Error, "Failed to export audio");
```

---

## 9) Visual Styling Map (suggested)
- **Status**: Neutral grey/blue bg, white text, spinner on left.
- **Success**: Green bg, white text, ✓ icon.
- **Error**: Red bg, white text, ✕ icon, `persistent = true`.
- **Warning**: Yellow/orange bg, dark text, ⚠ icon.
- **Info**: Blue bg, white text, ℹ icon.

Keep alpha ~0.8; corner radius 6–8 px; font matches app body text.

---

## 10) Testing Plan
- **Functional**: Queue multiple types; verify stacking, auto-dismiss, click-dismiss, persistence.
- **Threading**: Post from background jobs and audio callback (bounced via `callAsync`).
- **Stress**: Burst 20+ posts; ensure no stalls, no UI jank; inbox drains smoothly.
- **Layout**: Resize window; verify top-right anchoring; long text wraps/clips gracefully.
- **Perf**: Ensure render cost is O(active) and trivial per frame.

---

## 11) Migration Notes & Pitfalls
- Ensure all old JUCE `Label` notifications are removed to avoid z-order conflicts under ImGui.
- Keep all state mutation of `active` in the UI thread only; reserve the lock for `inbox` only.
- Avoid allocations per frame in `render(...)`; reuse ImGui primitives where possible.
- Don’t block `MessageManager` with heavy work inside `post(...)` or click handlers.

---

## 12) File Placement & Touchpoints
- `juce/Source/ui/notifications/NotificationManager.h/.cpp` (new)
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` → invoke `render(...)` and replace legacy calls
- Background jobs like `SavePresetJob.cpp` → post completion notifications

---

## 13) Ready-to-Implement Acceptance
- Centralized manager added and rendered via ImGui.
- Five message types with icon/color, animations, auto-dismiss, click-dismiss.
- Thread-safe posting from any thread, zero blocking of audio/UI threads.
- Legacy notification code removed.

If additional API surface is needed (e.g., grouping, deduping, or batching), extend the manager with a simple policy layer that runs in the UI thread before render.

