# Version Manager & Splash Screen Implementation Plan

## Overview

Implement a version management system and startup splash screen for **Pikon Raditsz** by Monsieur Pimpant, showing version information on startup and in the F1 help menu.

## Objectives

1. Create a centralized version information system
2. Display a splash screen window on application startup
3. Make splash screen dismissible with any key press or mouse click
4. Integrate version information into the F1 help menu
5. Update application name from "Preset Creator" to "Pikon Raditsz"

## Version Information

- **Application Name:** Pikon Raditsz
- **Version:** 0.5
- **Author:** Monsieur Pimpant
- **Build Type:** Beta Test Release

---

## Implementation Steps

### Step 1: Create Version Information File

**File:** `juce/Source/utils/VersionInfo.h` (new file)

Create a centralized version information class that holds:
- Application name
- Version number (major.minor format)
- Build type/phase
- Author/Creator name
- Copyright year (optional)
- Build date (optional)

**Structure:**
```cpp
#pragma once

#include <juce_core/juce_core.h>

class VersionInfo
{
public:
    static constexpr const char* APPLICATION_NAME = "Pikon Raditsz";
    static constexpr const char* VERSION = "0.5";
    static constexpr const char* VERSION_FULL = "0.5.0-beta";
    static constexpr const char* AUTHOR = "Monsieur Pimpant";
    static constexpr const char* BUILD_TYPE = "Beta Test Release";
    static constexpr int VERSION_MAJOR = 0;
    static constexpr int VERSION_MINOR = 5;
    static constexpr int VERSION_PATCH = 0;
    
    static juce::String getVersionString() { return juce::String(VERSION); }
    static juce::String getFullVersionString() { return juce::String(VERSION_FULL); }
    static juce::String getApplicationName() { return juce::String(APPLICATION_NAME); }
    static juce::String getAuthorString() { return juce::String(AUTHOR); }
    static juce::String getBuildInfoString() 
    { 
        return juce::String(APPLICATION_NAME) + " " + VERSION_FULL + " - " + BUILD_TYPE;
    }
};
```

---

### Step 2: Update Application Class

**Files to modify:**
- `juce/Source/preset_creator/PresetCreatorApplication.h`
- `juce/Source/preset_creator/PresetCreatorApplication.cpp`

**Changes:**
1. Replace hardcoded application name with `VersionInfo::APPLICATION_NAME`
2. Replace version string with `VersionInfo::VERSION`
3. Add a flag to track if splash screen should be shown (stored in app properties)
4. Add a method to show/hide splash screen preference

---

### Step 3: Create Splash Screen Component

**File:** `juce/Source/preset_creator/SplashScreenComponent.h` (new file)
**File:** `juce/Source/preset_creator/SplashScreenComponent.cpp` (new file)

**Features:**
- Custom JUCE Component that displays version information
- Styled background (can use theme colors if available)
- Displays:
  - Application name (large, prominent)
  - Version number
  - Author name
  - Build type
  - Optional: "Press any key or click to continue" message
- Responds to:
  - Any keyboard key press
  - Any mouse button click
  - Click on the window itself

**Design considerations:**
- Window should be modal or semi-modal (blocks input until dismissed)
- Centered on screen
- Reasonable default size (e.g., 600x400)
- Auto-dismiss after a timeout? (Optional, maybe 5-10 seconds)
- Smooth fade-in animation (optional)

**Window configuration:**
- No title bar decorations (or minimal)
- Centered on parent/main screen
- Cannot be resized initially (or allow resizing)
- Can use `juce::DialogWindow` or custom `juce::DocumentWindow`

**Dismissal logic:**
```cpp
bool keyPressed(const juce::KeyPress& key) override
{
    dismissSplashScreen();
    return true;
}

void mouseDown(const juce::MouseEvent& e) override
{
    dismissSplashScreen();
}
```

---

### Step 4: Integrate Splash Screen into Application Startup

**File:** `juce/Source/preset_creator/PresetCreatorMain.cpp`

**Location:** In `PresetCreatorApplication::initialise()` method, after window creation but before showing the window, or immediately after showing.

**Flow:**
1. Create main window
2. Show main window (or keep hidden initially)
3. Check if splash should be shown (user preference, first launch, etc.)
4. If yes, create and show splash screen
5. Wait for splash dismissal
6. Ensure main window is visible and focused

**Implementation notes:**
- Splash screen should be shown BEFORE main window content is fully loaded
- Consider using a modal message loop or async approach
- Store "show splash on startup" preference in app properties (default: true for first launch)

---

### Step 5: Add F1 Help Menu

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** In the main ImGui rendering loop, likely in the menu bar section (around line 895).

**Changes:**
1. Add "Help" menu to the menu bar (if not already present)
2. Add "About" menu item in Help menu
3. Create an "About" dialog window using ImGui that shows:
   - Application name
   - Version information
   - Author
   - Build type
   - Optional: Copyright notice, links, etc.

**Keyboard shortcut:**
- Register F1 key to open Help/About dialog
- Can use existing shortcut manager system (`ShortcutManager`) if available
- Or handle directly in `keyPressed()` method

**ImGui About dialog structure:**
```cpp
if (showAboutDialog)
{
    ImGui::OpenPopup("About Pikon Raditsz");
    
    if (ImGui::BeginPopupModal("About Pikon Raditsz", &showAboutDialog, 
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(VersionInfo::APPLICATION_NAME);
        ImGui::Separator();
        ImGui::Text("Version %s", VersionInfo::VERSION_FULL);
        ImGui::Text("Build: %s", VersionInfo::BUILD_TYPE);
        ImGui::Spacing();
        ImGui::Text("By %s", VersionInfo::AUTHOR);
        ImGui::Spacing();
        
        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            showAboutDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
```

---

### Step 6: Update All References

**Files to search and update:**
- Search codebase for hardcoded "Preset Creator" strings
- Replace with `VersionInfo::APPLICATION_NAME` or appropriate getter
- Update window titles, file dialogs, log messages, etc.

**Key locations:**
- Window title bars
- File dialogs (save/load dialogs)
- About dialogs (if any exist)
- Log messages
- Configuration file names/paths

---

## Technical Considerations

### Threading
- Splash screen should run on the main/UI thread
- Ensure no blocking operations during splash display
- Use JUCE's message thread for all UI operations

### Performance
- Splash screen should not impact startup time significantly
- Consider lazy loading main window content while splash is displayed
- Cache version info (it's static anyway)

### User Preferences
- Add "Show splash screen on startup" checkbox in preferences (future)
- Store preference in `appProperties`
- Default: show on first launch, then remember user choice

### Platform Compatibility
- Ensure splash screen works on Windows, macOS, Linux
- Test keyboard input handling on all platforms
- Verify window centering works correctly

### Accessibility
- Ensure splash screen can be dismissed with keyboard (important for accessibility)
- Consider adding a "Skip" or "Continue" button as an alternative
- Ensure text is readable and properly contrasted

---

## Testing Checklist

- [ ] Splash screen appears on application startup
- [ ] Splash screen can be dismissed with any keyboard key
- [ ] Splash screen can be dismissed with mouse click
- [ ] Splash screen displays correct version information
- [ ] F1 key opens About dialog
- [ ] Help menu shows About option
- [ ] About dialog displays correct information
- [ ] Application name is updated everywhere (window titles, etc.)
- [ ] No "Preset Creator" strings remain in user-facing text
- [ ] Splash screen preference is saved/loaded correctly
- [ ] Works on Windows
- [ ] Works on macOS
- [ ] Works on Linux
- [ ] Multiple launches (splash should respect preference after first launch)

---

## Future Enhancements

1. **Splash Screen Customization**
   - Allow user to customize splash screen appearance
   - Add logo/image support
   - Theme integration (match application theme)

2. **Changelog Display**
   - Show recent changes in About dialog
   - Link to full changelog/documentation

3. **Update Checking**
   - Check for updates on startup (optional)
   - Show update notification in About dialog

4. **License Information**
   - Display license/copyright information
   - Link to license file

5. **Credits Screen**
   - Show contributors, libraries used, etc.

---

## Implementation Order

1. ✅ Create `VersionInfo.h` - Foundation for all version info
2. ✅ Update `PresetCreatorApplication` - Update app name and version getters
3. ✅ Create `SplashScreenComponent` - Build the splash screen UI
4. ✅ Integrate splash into startup - Hook into application initialization
5. ✅ Add F1 help menu - Integrate About dialog into ImGui menu bar
6. ✅ Update all references - Replace "Preset Creator" strings
7. ✅ Testing and refinement - Ensure everything works correctly

---

## Files to Create

- `juce/Source/utils/VersionInfo.h`
- `juce/Source/preset_creator/SplashScreenComponent.h`
- `juce/Source/preset_creator/SplashScreenComponent.cpp`

## Files to Modify

- `juce/Source/preset_creator/PresetCreatorApplication.h`
- `juce/Source/preset_creator/PresetCreatorApplication.cpp`
- `juce/Source/preset_creator/PresetCreatorMain.cpp`
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h` (if needed for F1 handling)
- Any files with hardcoded "Preset Creator" strings

---

## Notes

- Keep splash screen simple and fast-loading
- Consider making splash screen optional after first launch
- Version info should be easily updatable for future releases
- Ensure consistent branding across all user-facing elements
- Test with different screen resolutions and DPI settings
