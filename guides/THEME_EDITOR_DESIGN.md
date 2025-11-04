# 🎨 Theme Editor Design Document

**Version**: 1.0  
**Purpose**: Foundation and architecture for a visual theme editor that allows users to modify and save custom themes.

---

## 📋 Overview

The Theme Editor will provide a comprehensive UI for modifying all themeable properties in real-time, with the ability to save custom themes to the `exe/themes/` directory.

---

## 🏗️ Architecture

### 1. Core Components

#### **ThemeEditorComponent**
- **Type**: ImGui-based modal/dockable window
- **Location**: `juce/Source/preset_creator/theme/ThemeEditorComponent.h/cpp`
- **Responsibilities**:
  - Render the theme editor UI
  - Handle user input for all theme properties
  - Apply changes in real-time
  - Save/load custom themes

#### **ThemeManager Extensions**
- **New Methods**:
  - `getEditableTheme()` - Returns mutable reference to current theme
  - `setProperty()` - Set individual theme properties
  - `applyChanges()` - Apply pending changes to the UI
  - `resetToDefault()` - Reset current section to defaults
  - `saveCustomTheme(const juce::String& name)` - Save as new theme

---

## 🎨 UI Design

### Window Layout

```
┌─────────────────────────────────────────────────────────┐
│ Theme Editor                                [×]         │
├─────────────────────────────────────────────────────────┤
│ [Apply] [Reset] [Save As...] [Close]                   │
├─────────────────────────────────────────────────────────┤
│ ┌─────┐ ┌─────────────────────────────────────────────┐ │
│ │Tab  │ │ Content Area (scrollable)                   │ │
│ │ImGui│ │                                             │ │
│ │Style│ │ • Padding: [8.0] [8.0]                     │ │
│ │     │ │ • Rounding: [4.0]                           │ │
│ │     │ │ • Border: [1.0]                             │ │
│ │Text │ │                                             │ │
│ │     │ │ Colors:                                     │ │
│ │ImGui│ │ [Color Picker] Text                        │ │
│ │Color│ │ [Color Picker] TextDisabled                 │ │
│ │     │ │ ...                                         │ │
│ │ImNodes│                                             │ │
│ │Links│ │                                             │ │
│ │Canvas│ │                                             │ │
│ │Layout│ │                                             │ │
│ │Modules│                                             │ │
│ └─────┘ └─────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### Tab Organization

1. **ImGui Style** - Padding, rounding, borders, spacing
2. **ImGui Colors** - All ImGuiCol_* colors (organized by category)
3. **Accent Color** - Global accent color
4. **Text Colors** - Section headers, warnings, errors, etc.
5. **Status Colors** - Edited, saved indicators
6. **Header Colors** - Recent, Samples, Presets, System (TriState)
7. **ImNodes** - Category colors, pin colors, node states
8. **Links** - Link colors, preview, labels
9. **Canvas** - Grid, scale, drop target
10. **Layout** - Sizes, padding, spacing
11. **Fonts** - Font sizes and paths
12. **Windows** - Alpha values, dimensions
13. **Modulation** - Frequency, timbre, amplitude, filter colors
14. **Meters** - Safe, warning, clipping colors
15. **Timeline** - Marker colors
16. **Modules** - Module-specific colors (Scope, VideoFX, StrokeSeq)

---

## 🔧 Implementation Plan

### Phase 1: Foundation (Current Task)
- [x] Create `ThemeEditorComponent` class structure
- [x] Add getter/setter methods to `ThemeManager`
- [x] Create basic window structure
- [x] Implement tab system

### Phase 2: Core Editors
- [ ] Color picker helpers (ImGui ColorEdit4)
- [ ] Float/drag editors for numeric values
- [ ] Vec2 editors for padding/spacing
- [ ] TriStateColor editor component

### Phase 3: Section Editors
- [ ] ImGui Style editor
- [ ] ImGui Colors editor (organized by category)
- [ ] Text/Status/Header editors
- [ ] ImNodes editor (category/pin color maps)
- [ ] Link/Canvas/Layout editors
- [ ] Module-specific editors

### Phase 4: Save/Load
- [ ] Save dialog (name input)
- [ ] Validation (prevent overwriting built-ins)
- [ ] Custom theme management
- [ ] Load custom themes in theme menu

### Phase 5: Polish
- [ ] Real-time preview
- [ ] Reset section to default
- [ ] Import/export
- [ ] Preset quick adjustments

---

## 📝 API Design

### ThemeManager Extensions

```cpp
class ThemeManager {
public:
    // Get mutable reference for editing
    Theme& getEditableTheme();
    
    // Apply changes immediately
    void applyTheme();
    
    // Save current theme as custom theme
    bool saveCustomTheme(const juce::String& themeName);
    
    // Reset to default
    void resetToDefault();
    
    // Get list of custom themes
    juce::StringArray getCustomThemeNames();
};
```

### ThemeEditorComponent

```cpp
class ThemeEditorComponent {
public:
    void render();  // Main render function
    
private:
    void renderTabs();
    void renderImGuiStyleTab();
    void renderImGuiColorsTab();
    void renderAccentTab();
    void renderTextColorsTab();
    // ... other tab renderers
    
    // Helper components
    bool colorEdit4(const char* label, ImVec4& color);
    bool dragFloat(const char* label, float& value, ...);
    bool dragFloat2(const char* label, ImVec2& value, ...);
    bool triStateColorEdit(const char* label, TriStateColor& tsc);
    
    // State
    bool m_isOpen = false;
    Theme m_workingCopy;  // Working copy of theme
    bool m_hasChanges = false;
};
```

---

## 🎯 Key Features

### Real-Time Preview
- Changes apply immediately to the UI
- No "Apply" button needed (optional confirmation)
- Visual feedback on all changes

### Organization
- Tabs for logical grouping
- Collapsible sections within tabs
- Search/filter (future enhancement)

### Save System
- Custom themes saved to `exe/themes/Custom_*.json`
- Built-in themes protected from overwrite
- Name validation (no spaces, special chars)
- Auto-save option (future)

### User Experience
- Clear labels and tooltips
- Color previews
- Reset to default per section
- Undo/redo (future enhancement)

---

## 📁 File Structure

```
juce/Source/preset_creator/theme/
├── Theme.h                    (existing)
├── ThemeManager.h/cpp        (existing + extensions)
├── ThemeEditorComponent.h    (NEW)
├── ThemeEditorComponent.cpp  (NEW)
└── presets/
    ├── *.json                (existing presets)
    └── Custom_*.json         (user-created themes)
```

---

## 🔄 Integration Points

1. **Menu Entry**: Add "Edit Current Theme..." to Settings → Theme menu
2. **Window Management**: Use ImGui window flags for modal/docking
3. **Theme Manager**: Extend with editable access methods
4. **File System**: Custom themes saved alongside presets

---

## 🚀 Next Steps

1. Create `ThemeEditorComponent` class files
2. Add editable access methods to `ThemeManager`
3. Implement basic window structure with tabs
4. Add first complete tab (ImGui Style) as proof of concept
5. Extend to other tabs incrementally

---

## 💡 Future Enhancements

- **Theme Presets**: Quick adjustment presets (darker, brighter, higher contrast)
- **Import/Export**: Share themes between users
- **Color Harmony**: Automatic color scheme generation
- **Preview Window**: Mini preview of node editor
- **Undo/Redo**: History of changes
- **Theme Marketplace**: Download community themes

