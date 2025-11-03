# 🎨 Notification System Integration Guide

**Objective**: Design and implement a robust, non-blocking notification system for the Preset Creator application. This system will replace the current, basic save notifications with a more flexible, informative, and visually appealing solution.

---

## 1. Current System Overview

The current notification system is a proof-of-concept implemented for save operations. It has several limitations that the new system should address.

### Existing Implementation

- **`ImGuiNodeEditorComponent::showSaveNotification()`**: A simple function that displays a `juce::Label` in the top-right corner.

- **`ImGuiNodeEditorComponent::renderImGui()`**: Contains logic to display a temporary, animated "Saving..." spinner while the `isSaveInProgress` flag is true.

### Code Snippets

**Notification Display (`showSaveNotification`)**

```cpp
// In ImGuiNodeEditorComponent.cpp (lines ~5520-5544)

void ImGuiNodeEditorComponent::showSaveNotification(const juce::String& message, juce::Colour color)
{
    if (!saveStatusLabel) {
        saveStatusLabel = std::make_unique<juce::Label>();
        addAndMakeVisible(*saveStatusLabel);
        saveStatusLabel->setJustificationType(juce::Justification::centred);
        saveStatusLabel->setColour(juce::Label::textColourId, juce::Colours::white);
        saveStatusLabel->setColour(juce::Label::outlineColourId, juce::Colours::black);
    }
    
    saveStatusLabel->setText(message, juce::dontSendNotification);
    saveStatusLabel->setColour(juce::Label::backgroundColourId, color);
    
    // Position it in the top-right corner, below the main menu bar
    saveStatusLabel->setBounds(getWidth() - 310, 30, 300, 40);
    saveStatusLabel->setVisible(true);
    saveStatusLabel->toFront(true);

    // Auto-hide after 4 seconds
    juce::Timer::callAfterDelay(4000, [this]() { 
        if (saveStatusLabel) 
            saveStatusLabel->setVisible(false); 
    });
}
```

**Save Progress Spinner (`renderImGui`)**

```cpp
// In ImGuiNodeEditorComponent.cpp (lines ~428-455)

void ImGuiNodeEditorComponent::renderImGui()
{
    // ...
    
    // --- NEW: In-Progress Save Indicator ---
    if (isSaveInProgress.load())
    {
        const float windowWidth = 200.0f;
        const float windowHeight = 50.0f;
        const float padding = 10.0f;
        
        ImGui::SetNextWindowPos(ImVec2(getWidth() - windowWidth - padding, ImGui::GetFrameHeight() + padding));
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::SetNextWindowBgAlpha(0.8f);

        ImGui::Begin("Save Progress", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

        // Simple animated spinner
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float radius = 10.0f;
        ImVec2 spinnerCenter = ImVec2(ImGui::GetCursorScreenPos().x + radius + 5, ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() / 2 + 5);
        float time = (float)ImGui::GetTime();
        drawList->PathClear();
        drawList->PathArcTo(spinnerCenter, radius, time * 2.0f - 1.57f, time * 2.0f + 1.57f, 32);
        drawList->PathStroke(IM_COL32(255, 255, 255, 255), 0, 2.0f);
        
        ImGui::SameLine(35.0f);
        ImGui::Text("Saving...");

        ImGui::End();
    }
    // --- END NEW INDICATOR ---
    
    // ...
}
```

### Limitations of the Current System

- **Single Message**: Can only display one notification at a time.
- **No Queuing**: New messages immediately overwrite the current one.
- **Limited Types**: Only supports a colored background; no distinct icons or styles for success, error, etc.
- **JUCE-Based**: It uses a `juce::Label`, which is drawn *under* the ImGui context, making it feel disconnected from the main UI. The new system should be pure ImGui.
- **Not Centralized**: The logic is split between two different functions and tied specifically to the save process.

---

## 2. Architecture Context

The application uses a hybrid JUCE/ImGui architecture.

- **JUCE**: Manages the window, audio processing, and OpenGL context. `ImGuiNodeEditorComponent` is a `juce::Component`.
- **ImGui**: Renders the entire user interface, including the node editor, within the `renderOpenGL()` callback.
- **Threading**:
    - **UI Thread (Message Thread)**: Handles all user input and ImGui rendering. It must remain responsive.
    - **Background Threads**: A `juce::ThreadPool` is used for slow operations like saving presets (`SavePresetJob`).
    - **Audio Thread**: Handles all real-time audio processing.

**Key Takeaway**: The new notification system must be rendered entirely within the ImGui `renderImGui()` loop and must be thread-safe to allow messages to be posted from background threads (like `SavePresetJob`).

---

## 3. Requirements Specification

The new system should be a significant upgrade, providing clear and consistent user feedback.

### Message Types

The system must support 5 distinct message types, each with a unique icon and color:

| Type | Icon | Color (ImGui) | Description |
| :--- | :--- | :--- | :--- |
| **Status** | 🔄 (Spinner) | Neutral (Grey/Blue) | For ongoing, non-blocking operations (e.g., "Scanning plugins..."). |
| **Success**| ✅ | Green | For successful completion of an action (e.g., "Preset saved"). |
| **Error** | ❌ | Red | For critical failures (e.g., "Failed to load file"). |
| **Warning**| ⚠️ | Yellow/Orange | For non-critical issues (e.g., "Preset loaded with 2 missing connections"). |
| **Info** | ℹ️ | Blue | For general information (e.g., "Auto-connected to Track Mixer"). |

### Functional Requirements

- **Message Queuing**: The system must maintain a queue of notifications. New messages should be added to the queue and displayed without overwriting existing ones.
- **Positioning**: Notifications should appear in the **top-right corner** of the screen and stack downwards.
- **Animation**:
    - **Fade In/Out**: Each notification should smoothly fade in upon arrival and fade out upon dismissal.
    - **Slide In/Out**: Notifications should slide in from the right edge of the screen and slide out to the right.
- **Auto-Dismissal**: Success, Info, and Warning messages should automatically dismiss after a configurable duration (e.g., 5 seconds). Error messages should persist until clicked.
- **Manual Dismissal**: All notifications can be dismissed by clicking on them.
- **Thread Safety**: It must be possible to post a notification from any thread (UI, background save thread, etc.).

### Visual Styling

- Semi-transparent background.
- Clear iconography on the left.
- Message text on the right.
- A subtle progress bar at the bottom showing the remaining time until auto-dismissal.

---

## 4. Integration Points

The new notification system should be used in the following places:

- **Save Operations**:
    - `savePresetToFile()`: Post a **Status** message ("Saving...") when the job starts and a **Success** or **Error** message in the `onSaveComplete` callback.

**Current Save Implementation** (`ImGuiNodeEditorComponent.cpp`, lines ~5441-5496):

```cpp
void ImGuiNodeEditorComponent::savePresetToFile(const juce::File& file)
{
    // ... save logic ...
    
    auto* job = new SavePresetJob(synthState, uiState, file);
    job->onSaveComplete = [this](const juce::File& savedFile, bool success) {
        if (success) {
            showSaveNotification("Saved: " + savedFile.getFileNameWithoutExtension(), 
                               juce::Colours::darkgreen);
            // Should become: NotificationManager::getInstance().post(Notification::Type::Success, "Saved: " + ...);
        } else {
            showSaveNotification("Error: Failed to save preset!", 
                               juce::Colours::darkred);
            // Should become: NotificationManager::getInstance().post(Notification::Type::Error, "Failed to save preset!");
        }
    };
}
```

- **Load Operations**:
    - `loadPresetFromFile()`: Post a **Success** message on successful load. If validation/healing occurs, post a **Warning** message with a summary of issues found.

- **Undo/Redo**:
    - `pushSnapshot()`: Post an **Info** message ("State saved to undo history").

- **Auto-Connection Features**:
    - `handleNodeChaining()`, `handleConnectSelectedToTrackMixer()`, etc.: Post an **Info** message summarizing the action (e.g., "Chained 3 nodes").

- **Background Scans**:
    - Plugin Scanner: Post a **Status** message ("Scanning plugins...") and a **Success** message upon completion.

---

## 5. Design & Implementation Recommendations

A singleton manager class is the cleanest approach.

### Proposed Architecture

1. **`Notification` Struct**: A simple struct to hold the data for a single message (type, text, creation time, duration).

2. **`NotificationManager` Singleton**: A thread-safe singleton class to manage the queue of notifications.
    - `std::vector<Notification> notifications`: The active queue.
    - `juce::CriticalSection lock`: To protect access to the queue.
    - `post(type, message)`: A static, thread-safe method to add a new notification.
    - `render()`: An ImGui-based function to be called from `renderImGui()` that handles the drawing, animation, and dismissal logic for all active notifications.

### `SavePresetJob` Integration Example

```cpp
// In SavePresetJob.cpp

SavePresetJob::JobStatus SavePresetJob::runJob()
{
    // ... (save logic) ...
    
    // Signal completion back to the UI thread
    juce::MessageManager::callAsync([this, success = writeSuccess]() {
        if (onSaveComplete) onSaveComplete(fileToSave, success);
        
        // NEW: Post notification from the callback on the UI thread
        if (success) {
            NotificationManager::getInstance().post(Notification::Type::Success, "Saved: " + fileToSave.getFileNameWithoutExtension());
        } else {
            NotificationManager::getInstance().post(Notification::Type::Error, "Error saving preset!");
        }
    });
    
    return jobHasFinished;
}
```

### Migration Path

1. Implement the `Notification` struct and `NotificationManager` class.
2. Add a call to `NotificationManager::getInstance().render()` inside `ImGuiNodeEditorComponent::renderImGui()`.
3. Replace the `showSaveNotification()` function and the "Saving..." spinner logic with calls to `NotificationManager::getInstance().post()`.
4. Gradually integrate `post()` calls into the other integration points listed above.

---

## 6. Questions for the Expert

1. What is the best way to handle the animation timing (fade/slide) within the ImGui `render()` loop?

2. Should the `NotificationManager` be a JUCE singleton or a simple static instance?

3. What's a robust pattern for handling the "time remaining" progress bar for auto-dismissal?

4. For the `post()` method, is a `juce::CriticalSection` sufficient for thread safety, or would a lock-free queue be better?

5. How should the `render()` function be designed to handle the stacking and downward movement of notifications as they are added or removed?

6. Should the notification manager be part of the `ImGuiNodeEditorComponent` or a globally accessible utility?

7. What's the best way to handle potential text overflow for long messages?

8. Can you provide a basic implementation of the `NotificationManager` class, including the `post()` and `render()` methods, that meets these requirements?

---

## 7. Relevant Code Files

### Primary Files

#### 1. `juce/Source/preset_creator/ImGuiNodeEditorComponent.h`

**Key Members Related to Notifications**:

```cpp
// Lines 335-337
std::atomic<bool> isSaveInProgress { false }; // Debouncing flag for save operations
juce::ThreadPool threadPool { 2 };
std::unique_ptr<juce::Label> saveStatusLabel; // Non-blocking notification (to be replaced)

// Line 168
void showSaveNotification(const juce::String& message, juce::Colour color); // To be replaced
```

#### 2. `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Key Functions**:
- `renderImGui()`: Main rendering function (line ~349)
- `showSaveNotification()`: Current notification display (line ~5520)
- `savePresetToFile()`: Save operation that posts notifications (line ~5441)

#### 3. `juce/Source/preset_creator/SavePresetJob.h` and `.cpp`

**Purpose**: Background thread job for saving presets asynchronously

**Key Pattern**: Uses `MessageManager::callAsync()` to communicate completion back to UI thread

```cpp
// Signal completion back to the UI thread
juce::MessageManager::callAsync([this, success = writeSuccess]() {
    if (onSaveComplete) onSaveComplete(fileToSave, success);
});
```

**Thread-Safe Pattern**: This is the pattern that should be used when posting notifications from background threads.

---

## 8. Thread Safety Considerations

### Current Pattern

**From Background Threads**:
```cpp
// Background thread
bool success = doBackgroundWork();

// Communicate to UI thread
juce::MessageManager::callAsync([this, success]() {
    if (success) {
        showSaveNotification("Success", juce::Colours::green);
    } else {
        showSaveNotification("Error", juce::Colours::red);
    }
});
```

### Recommended Pattern for Notification System

**Option 1: MessageManager Pattern** (Current approach - Recommended)
```cpp
// From any thread
juce::MessageManager::callAsync([this]() {
    NotificationManager::getInstance().post(Notification::Type::Success, "Message");
});
```

**Option 2: Thread-Safe Queue Pattern** (Alternative)
```cpp
// From any thread (thread-safe)
NotificationManager::getInstance().post(Notification::Type::Success, "Message");
// NotificationManager processes queue on UI thread
```

**Recommendation**: Option 1 is simpler and matches existing patterns. Option 2 might be more efficient if many messages are queued, but requires more careful implementation.

### Critical Points

1. **All rendering must be on UI thread** (inside `renderImGui()`)
2. **State changes should be atomic or message-thread-only**
3. **No blocking operations** in notification system
4. **Safe to call from audio thread** (if needed) via `MessageManager::callAsync`

---

## 9. Example Usage Scenarios

### Scenario 1: Save Operation

```cpp
// In savePresetToFile()
NotificationManager::getInstance().post(Notification::Type::Status, "Saving preset...");

// Background save completes
juce::MessageManager::callAsync([this]() {
    NotificationManager::getInstance().post(Notification::Type::Success, "Preset saved: MyPatch.xml");
});
```

### Scenario 2: Load with Validation

```cpp
NotificationManager::getInstance().post(Notification::Type::Status, "Loading preset...");

// Load completes with warnings
juce::MessageManager::callAsync([this, warningCount]() {
    NotificationManager::getInstance().post(
        Notification::Type::Warning,
        "Preset loaded with " + juce::String(warningCount) + " issues (auto-healed)"
    );
});
```

### Scenario 3: Undo/Redo

```cpp
// User presses Ctrl+Z
performUndo();
NotificationManager::getInstance().post(Notification::Type::Info, "Undo: Deleted VCO node");

// User presses Ctrl+Shift+Z
performRedo();
NotificationManager::getInstance().post(Notification::Type::Info, "Redo: Added VCO node");
```

### Scenario 4: Error from Background Thread

```cpp
// Background thread encounters error
juce::MessageManager::callAsync([this, errorMsg]() {
    NotificationManager::getInstance().post(Notification::Type::Error, "Export failed: " + errorMsg);
});
```

---

## 10. Deliverables Expected

1. **NotificationManager Class**
   - Header file (`NotificationManager.h`)
   - Implementation file (`NotificationManager.cpp`)
   - Clear, commented code

2. **Integration Guide**
   - Step-by-step integration instructions
   - Code examples for common use cases
   - Migration path from existing system

3. **API Documentation**
   - Public API reference
   - Thread-safety notes
   - Usage examples

4. **Styling Guide**
   - How to customize colors
   - How to adjust positioning
   - How to modify animations

5. **Testing Recommendations**
   - How to test thread safety
   - How to test animation performance
   - How to test edge cases (many messages, rapid updates)

---

## Conclusion

The current notification system is basic and limited to save operations. We need a centralized, flexible system that can handle multiple message types, queue messages, and integrate with all aspects of the application while maintaining thread safety and performance.

The expert should design a solution that:
- Fits naturally with the existing JUCE/ImGui architecture
- Provides a clean, easy-to-use API
- Handles threading correctly
- Renders efficiently
- Is easy to extend and customize

Thank you for your expertise in designing this system!
