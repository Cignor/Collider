# 🎨 Theme Manager Integration Guide

**Version**: 1.0  
**Last Updated**: 2025-01-27  
**Purpose**: Comprehensive documentation of all UI elements (colors, fonts, sizes, spacing) in the Preset Creator to prepare for theme manager implementation.

---

## 📋 Overview

This guide documents **every UI-related element** in the Preset Creator that should be managed by a future theme system. The goal is to identify all hardcoded values so they can be centralized in a theme manager.

**Scope**: All visual elements including:
- Colors (ImGui, ImNodes, custom drawing)
- Font sizes and families
- Spacing and padding values
- Window dimensions and positions
- Border widths and rounding
- Alpha/opacity values
- Button sizes and styles

---

## 🎨 1. ImGui Core Colors

### 1.1 Base Style Colors

**Location**: `ImGuiNodeEditorComponent.cpp:244`

```cpp
ImGui::StyleColorsDark();
```

**Current**: Uses ImGui's default dark theme. This should be replaced with custom theme colors.

**Theme Manager Integration**: 
- Replace `StyleColorsDark()` with theme-aware color application
- All `ImGuiCol_*` enums should be configurable

### 1.2 Status Overlay Colors

**Location**: `ImGuiNodeEditorComponent.cpp:430-432`

```cpp
// Status: EDITED
ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: EDITED");

// Status: SAVED  
ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: SAVED");
```

**Colors**:
- Edited: Yellow `(1.0, 1.0, 0.0, 1.0)`
- Saved: Green `(0.0, 1.0, 0.0, 1.0)`

**Also Used**: `ImGuiNodeEditorComponent.cpp:1055-1057` (duplicate)

**Theme Keys Needed**:
- `theme.status.edited`
- `theme.status.saved`

---

### 1.3 Text Colors - General

**Location**: Multiple locations

```cpp
// Section headers (gray)
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Section Name");

// Warning/Info (yellow)
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Warning Text");

// Success (green)
ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Success Text");

// Error (red/orange)
ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Error Text");

// Disabled text
ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 255));

// Active/Connected indicators
ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255)); // Green active
```

**Theme Keys Needed**:
- `theme.text.section_header`
- `theme.text.warning`
- `theme.text.success`
- `theme.text.error`
- `theme.text.disabled`
- `theme.text.active`

---

### 1.4 Module Category Header Colors

**Location**: `ImGuiNodeEditorComponent.cpp:1134-1144, 1266-1324, 1445-1449`

**Category Colors** (with hover/active variants):

```cpp
// Gold (for "Recent" section)
ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(218, 165, 32, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(238, 185, 52, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 205, 72, 255));

// Cyan (for Samples section)
ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 180, 180, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(20, 200, 200, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(40, 220, 220, 255));

// Purple (for Presets section)
ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(180, 120, 255, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(200, 140, 255, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(220, 160, 255, 255));

// Neutral Grey (for System section)
ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(80, 80, 80, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(100, 100, 100, 255));
ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(120, 120, 120, 255));
```

**Theme Keys Needed**:
- `theme.header.recent` (base, hovered, active)
- `theme.header.samples` (base, hovered, active)
- `theme.header.presets` (base, hovered, active)
- `theme.header.system` (base, hovered, active)

---

## 🎯 2. ImNodes Colors

### 2.1 Node Title Bar Colors by Category

**Location**: `ImGuiNodeEditorComponent.cpp:8293-8322`

**Function**: `getImU32ForCategory(ModuleCategory category, bool hovered)`

**Category Colors**:

```cpp
Source:      IM_COL32(50, 120, 50, 255)     // Green
Effect:      IM_COL32(130, 60, 60, 255)     // Red
Modulator:   IM_COL32(50, 50, 130, 255)     // Blue
Utility:     IM_COL32(110, 80, 50, 255)     // Orange
Seq:         IM_COL32(90, 140, 90, 255)     // Light Green
MIDI:        IM_COL32(180, 120, 255, 255)   // Vibrant Purple
Analysis:    IM_COL32(100, 50, 110, 255)    // Purple
TTS_Voice:   IM_COL32(255, 180, 100, 255)   // Peach/Coral
Special_Exp: IM_COL32(50, 200, 200, 255)    // Cyan
OpenCV:      IM_COL32(255, 140, 0, 255)     // Bright Orange
Sys:         IM_COL32(120, 100, 140, 255)   // Lavender
Comment:     IM_COL32(80, 80, 80, 255)      // Grey
Plugin:      IM_COL32(50, 110, 110, 255)    // Teal
Default:     IM_COL32(70, 70, 70, 255)      // Grey
```

**Hovered Multiplier**: `1.3x` brightness increase

**Usage**: `ImGuiNodeEditorComponent.cpp:1903-1905`
```cpp
ImNodes::PushColorStyle(ImNodesCol_TitleBar, getImU32ForCategory(moduleCategory));
ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, getImU32ForCategory(moduleCategory, true));
ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, getImU32ForCategory(moduleCategory, true));
```

**Theme Keys Needed**:
- `theme.nodes.category.{category_name}` (base, hovered, selected)

---

### 2.2 Special Node States

**Location**: `ImGuiNodeEditorComponent.cpp:1910-1918`

```cpp
// Hovered link highlight (overrides category color)
ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(255, 220, 0, 255)); // Yellow

// Muted node (overrides category color and hover)
ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(80, 80, 80, 255)); // Grey
ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f); // 50% opacity
ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, ImVec2(8, 8));
```

**Theme Keys Needed**:
- `theme.nodes.hovered_link_highlight`
- `theme.nodes.muted`
- `theme.nodes.muted_alpha`
- `theme.nodes.muted_padding`

---

### 2.3 Pin Colors by Data Type

**Location**: `ImGuiNodeEditorComponent.cpp:7875-7886`

**Function**: `getImU32ForType(PinDataType type)`

```cpp
CV:    IM_COL32(100, 150, 255, 255)  // Blue
Audio: IM_COL32(100, 255, 150, 255)  // Green
Gate:  IM_COL32(255, 220, 100, 255)  // Yellow
Raw:   IM_COL32(255, 100, 100, 255)  // Red
Video: IM_COL32(0, 200, 255, 255)    // Cyan
Default: IM_COL32(150, 150, 150, 255) // Grey
```

**Usage**: `ImGuiNodeEditorComponent.cpp:2631, 2705, 2776, 2805`
```cpp
ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
```

**Theme Keys Needed**:
- `theme.pins.{type}` (CV, Audio, Gate, Raw, Video, Default)

---

### 2.4 Pin Connection States

**Location**: `ImGuiNodeEditorComponent.cpp:1776-1777`

```cpp
const ImU32 colPin = IM_COL32(150, 150, 150, 255);        // Grey for disconnected
const ImU32 colPinConnected = IM_COL32(120, 255, 120, 255); // Green for connected
```

**Theme Keys Needed**:
- `theme.pins.disconnected`
- `theme.pins.connected`

---

### 2.5 Link Colors

**Location**: `ImGuiNodeEditorComponent.cpp:3304-3312`

```cpp
// Normal link color (based on pin type)
ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);

// Hovered link
ImNodes::PushColorStyle(ImNodesCol_LinkHovered, IM_COL32(255, 255, 0, 255)); // Yellow

// Selected link
ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 255, 0, 255)); // Yellow

// Highlighted link (when hovering over connection)
ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(255, 255, 0, 255)); // Yellow
```

**Theme Keys Needed**:
- `theme.links.normal` (derived from pin type)
- `theme.links.hovered`
- `theme.links.selected`
- `theme.links.highlighted`

---

## 🖼️ 3. Canvas & Grid Colors

### 3.1 Grid Colors

**Location**: `ImGuiNodeEditorComponent.cpp:1721-1722`

```cpp
const ImU32 GRID_COLOR = IM_COL32(50, 50, 50, 255);        // Dark grey grid lines
const ImU32 GRID_ORIGIN_COLOR = IM_COL32(80, 80, 80, 255);  // Lighter grey for origin lines
```

**Grid Size**: `64.0f` pixels (line 287)

**Theme Keys Needed**:
- `theme.canvas.grid.color`
- `theme.canvas.grid.origin_color`
- `theme.canvas.grid.size`

---

### 3.2 Scale Marker Colors

**Location**: `ImGuiNodeEditorComponent.cpp:1801`

```cpp
const ImU32 SCALE_TEXT_COLOR = IM_COL32(150, 150, 150, 80); // Reduced opacity
```

**Scale Interval**: `400.0f` grid units

**Theme Keys Needed**:
- `theme.canvas.scale.text_color`
- `theme.canvas.scale.interval`

---

### 3.3 Preset Drop Target Overlay

**Location**: `ImGuiNodeEditorComponent.cpp:1740`

```cpp
drawList->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(218, 165, 32, 80)); // Preset Gold color
```

**Theme Keys Needed**:
- `theme.canvas.drop_target_overlay`

---

### 3.4 Mouse Position Display

**Location**: `ImGuiNodeEditorComponent.cpp:1853`

```cpp
ImGui::GetForegroundDrawList()->AddText(ImVec2(canvas_p0.x + 10, canvas_p1.y - 25), 
                                         IM_COL32(200, 200, 200, 150), posStr);
```

**Theme Keys Needed**:
- `theme.canvas.mouse_position_text`

---

## 📏 4. Spacing & Layout

### 4.1 Window Padding

**Location**: Multiple locations

```cpp
// Status overlay padding
const float padding = 10.0f;

// Probe scope window padding
ImGui::SetNextWindowPos(ImVec2((float)getWidth() - 270.0f, menuBarHeight + padding), ...);

// Notification padding
const float padding = 10.0f; // NotificationManager.cpp:72
```

**Theme Keys Needed**:
- `theme.layout.window_padding`
- `theme.layout.status_overlay_padding`
- `theme.layout.notification_padding`

---

### 4.2 Node Padding

**Location**: `ImGuiNodeEditorComponent.cpp:1916`

```cpp
// Muted nodes
ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, ImVec2(8, 8));

// Default node padding (from ImNodes style)
// Currently uses ImNodes default
```

**Theme Keys Needed**:
- `theme.nodes.default_padding`
- `theme.nodes.muted_padding`

---

### 4.3 Item Spacing

**Location**: `ImGuiNodeEditorComponent.cpp:2752`

```cpp
const float spacing = ImGui::GetStyle().ItemSpacing.x;
```

**Usage**: Used for pin label alignment calculations

**Theme Keys Needed**:
- `theme.layout.item_spacing.x`
- `theme.layout.item_spacing.y`

---

### 4.4 Node Vertical Padding (Layout)

**Location**: `ImGuiNodeEditorComponent.cpp:6039`

```cpp
const float NODE_VERTICAL_PADDING = 50.0f;
```

Used when arranging nodes in columns.

**Theme Keys Needed**:
- `theme.layout.node_vertical_padding`

---

### 4.5 Vertical Spacing (Preset Loading)

**Location**: `ImGuiNodeEditorComponent.cpp:8927`

```cpp
const float verticalPadding = 100.0f;
```

**Theme Keys Needed**:
- `theme.layout.preset_vertical_padding`

---

## 🔤 5. Fonts & Text Sizing

### 5.1 Font Loading

**Location**: `ImGuiNodeEditorComponent.cpp:10025-10048`

```cpp
// Default English font
io.Fonts->AddFontDefault();

// Chinese font (NotoSansSC)
auto fontFile = appFile.getParentDirectory().getChildFile("../../Source/assets/NotoSansSC-VariableFont_wght.ttf");
io.Fonts->AddFontFromFileTTF(fontFile.getFullPathName().toRawUTF8(), 16.0f, &config, ranges);
```

**Font Size**: `16.0f` pixels

**Theme Keys Needed**:
- `theme.fonts.default.size`
- `theme.fonts.default.path`
- `theme.fonts.chinese.size`
- `theme.fonts.chinese.path`

---

### 5.2 Text Wrap Width

**Location**: Multiple locations

```cpp
// Standard tooltip wrap
ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);

// Compact tooltip wrap
ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
```

**Theme Keys Needed**:
- `theme.text.tooltip_wrap_multiplier.standard` (35.0)
- `theme.text.tooltip_wrap_multiplier.compact` (25.0)

---

### 5.3 Text Size Calculations

**Location**: Multiple locations

```cpp
// Text size calculations for layout
const ImVec2 textSize = ImGui::CalcTextSize(txt);
const float label_width = ImGui::CalcTextSize(label).x;
```

These are dynamic calculations, but font size affects them.

---

## 🎛️ 6. Module-Specific UI Colors

### 6.1 VideoFX Module

**Location**: `VideoFXModule.cpp:699-878`

```cpp
// Section headers
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source ID In: %d");
ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Color Adjustments");
ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Filters & Effects");
ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "More Filters");
ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Advanced Effects");
```

**Theme Keys Needed**:
- `theme.modules.videofx.section_header`
- `theme.modules.videofx.section_subheader`

---

### 6.2 Scope Module

**Location**: `ScopeModuleProcessor.cpp:100-193`

```cpp
// Section headers
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scope Settings");
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live Waveform");
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Signal Statistics");

// Plot colors
const ImU32 bg = IM_COL32(30, 30, 30, 255);      // Background
const ImU32 fg = IM_COL32(100, 200, 255, 255);  // Foreground (cyan)
const ImU32 colMax = IM_COL32(255, 80, 80, 255); // Red (max)
const ImU32 colMin = IM_COL32(255, 220, 80, 255); // Yellow (min)

// Text colors
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // Red for max
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.86f, 0.31f, 1.0f)); // Yellow for min
```

**Theme Keys Needed**:
- `theme.modules.scope.section_header`
- `theme.modules.scope.plot.background`
- `theme.modules.scope.plot.foreground`
- `theme.modules.scope.plot.max_color`
- `theme.modules.scope.plot.min_color`
- `theme.modules.scope.text.max`
- `theme.modules.scope.text.min`

---

### 6.3 Stroke Sequencer Module

**Location**: `StrokeSequencerModuleProcessor.cpp:556-738`

```cpp
// 80s LCD Yellow theme
draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(60, 55, 20, 255), 0.0f, 0, 3.0f); // Dark border

// Frame backgrounds
ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.28f, 0.1f, 0.7f)); // Dark yellow-brown
ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.4f, 0.38f, 0.15f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.5f, 0.48f, 0.2f, 0.9f));
```

**Theme Keys Needed**:
- `theme.modules.stroke_sequencer.border`
- `theme.modules.stroke_sequencer.frame_bg`
- `theme.modules.stroke_sequencer.frame_bg_hovered`
- `theme.modules.stroke_sequencer.frame_bg_active`

---

## 🎨 7. Window Backgrounds & Alpha

### 7.1 Window Background Alpha

**Location**: Multiple locations

```cpp
// Status overlay
ImGui::SetNextWindowBgAlpha(0.5f);  // 50% opacity

// Probe scope
ImGui::SetNextWindowBgAlpha(0.85f); // 85% opacity

// Preset status overlay
ImGui::SetNextWindowBgAlpha(0.7f);  // 70% opacity

// Notifications
ImGui::SetNextWindowBgAlpha(0.92f); // 92% opacity (NotificationManager.cpp:107)
```

**Theme Keys Needed**:
- `theme.windows.status_overlay.alpha`
- `theme.windows.probe_scope.alpha`
- `theme.windows.preset_status.alpha`
- `theme.windows.notifications.alpha`

---

### 7.2 Muted Node Alpha

**Location**: `ImGuiNodeEditorComponent.cpp:1917`

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f); // 50% opacity
```

**Theme Keys Needed**:
- `theme.nodes.muted_alpha`

---

## 📐 8. Window Dimensions & Positions

### 8.1 Sidebar Width

**Location**: `ImGuiNodeEditorComponent.cpp:413`

```cpp
const float sidebarWidth = 260.0f;
```

**Theme Keys Needed**:
- `theme.layout.sidebar_width`

---

### 8.2 Probe Scope Window

**Location**: `ImGuiNodeEditorComponent.cpp:444-445`

```cpp
ImGui::SetNextWindowPos(ImVec2((float)getWidth() - 270.0f, menuBarHeight + padding), ImGuiCond_FirstUseEver);
ImGui::SetNextWindowSize(ImVec2(260, 180), ImGuiCond_FirstUseEver);
```

**Theme Keys Needed**:
- `theme.windows.probe_scope.width`
- `theme.windows.probe_scope.height`
- `theme.windows.probe_scope.offset_x`

---

### 8.3 Node Default Width

**Location**: `ImGuiNodeEditorComponent.cpp:1927`

```cpp
float nodeContentWidth = 240.0f; // Default width
```

**Theme Keys Needed**:
- `theme.nodes.default_width`

---

## 🔘 9. Button & Control Styles

### 9.1 Button Sizes

**Location**: Various module processors

Most buttons use default ImGui button sizes, but some have custom widths calculated from `itemWidth`.

**Theme Keys Needed**:
- `theme.controls.button.default_height`
- `theme.controls.button.min_width`

---

### 9.2 Slider Colors (Modulation States)

**Location**: Various module processors (e.g., `VCOModuleProcessor.h:97-106`)

```cpp
// CV modulation color (cyan)
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
```

**Color Scheme** (from IMGUI_NODE_DESIGN_GUIDE.md):
- **Cyan** (0.4, 0.8, 1.0): Frequency/Pitch modulation
- **Orange** (1.0, 0.8, 0.4): Timbre/Waveform modulation
- **Magenta** (1.0, 0.4, 1.0): Amplitude/Level modulation
- **Green** (0.4, 1.0, 0.4): Filter/EQ modulation

**Theme Keys Needed**:
- `theme.modulation.frequency`
- `theme.modulation.timbre`
- `theme.modulation.amplitude`
- `theme.modulation.filter`

---

### 9.3 Progress Bar Colors

**Location**: Various module processors

```cpp
// Green (safe level)
ImVec4(0.2f, 0.8f, 0.2f, 1.0f)

// Yellow (hot level)
ImVec4(0.9f, 0.7f, 0.0f, 1.0f)

// Red (clipping)
ImVec4(0.9f, 0.2f, 0.2f, 1.0f)
```

**Theme Keys Needed**:
- `theme.meters.safe`
- `theme.meters.warning`
- `theme.meters.clipping`

---

## 🎯 10. Special Visual Effects

### 10.1 Link Hover Preview

**Location**: `ImGuiNodeEditorComponent.cpp:3692`

```cpp
ImGui::GetForegroundDrawList()->AddLine(sourcePos, mousePos, IM_COL32(255, 255, 0, 200), 3.0f);
```

**Theme Keys Needed**:
- `theme.links.preview.color`
- `theme.links.preview.width`

---

### 10.2 Timeline Markers

**Location**: `ImGuiNodeEditorComponent.cpp:2096-2097, 2211-2215`

```cpp
// Start/End markers
drawList->AddLine(ImVec2(startX, rectMin.y), ImVec2(startX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);
drawList->AddLine(ImVec2(endX, rectMin.y), ImVec2(endX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);

// Gate/Trigger lines
draw_list->AddLine(ImVec2(gateLineX, p_min.y), ImVec2(gateLineX, p_max.y), IM_COL32(255, 255, 0, 200), 2.0f);
draw_list->AddLine(ImVec2(trigLineX, p_min.y), ImVec2(trigLineX, p_max.y), IM_COL32(255, 165, 0, 200), 2.0f);
```

**Theme Keys Needed**:
- `theme.timeline.marker.start_end`
- `theme.timeline.marker.gate`
- `theme.timeline.marker.trigger`

---

### 10.3 Connection Text Labels

**Location**: `ImGuiNodeEditorComponent.cpp:3518-3520`

```cpp
// Background
IM_COL32(50, 50, 50, 200)

// Text
IM_COL32(255, 255, 100, 255) // Yellow
```

**Theme Keys Needed**:
- `theme.links.label.background`
- `theme.links.label.text`

---

## 📊 11. Color Tracker Module

**Location**: `ColorTrackerModule.cpp` (multiple locations)

**Note**: This module displays actual video frames with color swatches, but the UI around it uses standard theme colors.

**Theme Keys Needed**:
- `theme.modules.color_tracker.swatch_size` (22x22 pixels, line 2454)

---

## 🎨 12. ImGui Style Variables

### 12.1 Current Style Access

**Location**: Multiple locations

```cpp
// Accessing current style
const float spacing = ImGui::GetStyle().ItemSpacing.x;
float btnWidth = itemWidth * 0.45f - ImGui::GetStyle().ItemSpacing.x;
```

**All ImGuiStyle members should be themeable**:
- `WindowPadding`
- `FramePadding`
- `ItemSpacing`
- `ItemInnerSpacing`
- `TouchExtraPadding`
- `IndentSpacing`
- `ScrollbarSize`
- `GrabMinSize`
- `WindowBorderSize`
- `ChildBorderSize`
- `PopupBorderSize`
- `FrameBorderSize`
- `TabBorderSize`
- `WindowRounding`
- `ChildRounding`
- `FrameRounding`
- `PopupRounding`
- `ScrollbarRounding`
- `GrabRounding`
- `TabRounding`
- `WindowTitleAlign`
- `ButtonTextAlign`
- `SelectableTextAlign`
- `DisplayWindowPadding`
- `DisplaySafeAreaPadding`
- `MouseCursorScale`
- `AntiAliasedLines`
- `AntiAliasedLinesUseTex`
- `AntiAliasedFill`
- `CurveTessellationTol`
- `CircleSegmentMaxError`

**Theme Keys Needed**:
- `theme.style.{property_name}` for all ImGuiStyle properties

---

## 🔄 13. Integration Points

### 13.1 Initialization Point

**Location**: `ImGuiNodeEditorComponent.cpp:newOpenGLContextCreated()`

**Current**:
```cpp
ImGui::StyleColorsDark();
```

**Should Become**:
```cpp
ThemeManager::getInstance().applyTheme();
```

---

### 13.2 Module Category Color Function

**Location**: `ImGuiNodeEditorComponent.cpp:8293-8322`

**Function**: `getImU32ForCategory(ModuleCategory category, bool hovered)`

**Should Query Theme**:
```cpp
return ThemeManager::getInstance().getCategoryColor(category, hovered);
```

---

### 13.3 Pin Type Color Function

**Location**: `ImGuiNodeEditorComponent.cpp:7875-7886`

**Function**: `getImU32ForType(PinDataType type)`

**Should Query Theme**:
```cpp
return ThemeManager::getInstance().getPinColor(type);
```

---

### 13.4 Node Drawing

**Location**: `ImGuiNodeEditorComponent.cpp:1903-1918`

All `ImNodes::PushColorStyle()` calls should use theme colors.

---

### 13.5 Module-Specific Drawing

**Location**: Individual module processors

Each module's `drawParametersInNode()` function uses hardcoded colors. These should be replaced with theme lookups.

---

## 📦 14. Theme Manager Structure (Proposed)

### 14.1 Theme Data Structure

```cpp
struct Theme {
    // ImGui Core Colors
    struct ImGuiColors {
        ImVec4 text;
        ImVec4 text_disabled;
        ImVec4 window_bg;
        ImVec4 child_bg;
        ImVec4 popup_bg;
        // ... all ImGuiCol_* colors
    } imgui;
    
    // ImNodes Colors
    struct ImNodesColors {
        // Node colors by category
        std::map<ModuleCategory, ImU32> category_colors;
        std::map<ModuleCategory, ImU32> category_hovered;
        std::map<ModuleCategory, ImU32> category_selected;
        
        // Pin colors by type
        std::map<PinDataType, ImU32> pin_colors;
        ImU32 pin_connected;
        ImU32 pin_disconnected;
        
        // Link colors
        ImU32 link_hovered;
        ImU32 link_selected;
        ImU32 link_highlighted;
        
        // Special states
        ImU32 node_muted;
        ImU32 node_hovered_link;
    } imnodes;
    
    // Canvas & Grid
    struct Canvas {
        ImU32 grid_color;
        ImU32 grid_origin_color;
        float grid_size;
        ImU32 scale_text_color;
        float scale_interval;
        ImU32 drop_target_overlay;
        ImU32 mouse_position_text;
    } canvas;
    
    // Layout
    struct Layout {
        float sidebar_width;
        float window_padding;
        float item_spacing_x;
        float item_spacing_y;
        float node_vertical_padding;
        float node_default_width;
        float node_padding_x;
        float node_padding_y;
    } layout;
    
    // Fonts
    struct Fonts {
        float default_size;
        juce::String default_path;
        float chinese_size;
        juce::String chinese_path;
    } fonts;
    
    // Text
    struct Text {
        ImVec4 section_header;
        ImVec4 warning;
        ImVec4 success;
        ImVec4 error;
        ImVec4 disabled;
        ImVec4 active;
        float tooltip_wrap_standard;
        float tooltip_wrap_compact;
    } text;
    
    // Status
    struct Status {
        ImVec4 edited;
        ImVec4 saved;
    } status;
    
    // Windows
    struct Windows {
        float status_overlay_alpha;
        float probe_scope_alpha;
        float probe_scope_width;
        float probe_scope_height;
        float preset_status_alpha;
        float notifications_alpha;
    } windows;
    
    // Modulation colors
    struct Modulation {
        ImVec4 frequency;
        ImVec4 timbre;
        ImVec4 amplitude;
        ImVec4 filter;
    } modulation;
    
    // Meters
    struct Meters {
        ImVec4 safe;
        ImVec4 warning;
        ImVec4 clipping;
    } meters;
    
    // Module-specific colors (can be extended)
    struct Modules {
        // VideoFX
        ImVec4 videofx_section_header;
        ImVec4 videofx_section_subheader;
        
        // Scope
        ImVec4 scope_section_header;
        ImU32 scope_plot_bg;
        ImU32 scope_plot_fg;
        ImU32 scope_plot_max;
        ImU32 scope_plot_min;
        ImVec4 scope_text_max;
        ImVec4 scope_text_min;
        
        // Stroke Sequencer
        ImU32 stroke_seq_border;
        ImVec4 stroke_seq_frame_bg;
        ImVec4 stroke_seq_frame_bg_hovered;
        ImVec4 stroke_seq_frame_bg_active;
    } modules;
    
    // ImGui Style
    ImGuiStyle style;
};
```

---

### 14.2 Theme Manager Class (Proposed Interface)

```cpp
class ThemeManager {
public:
    static ThemeManager& getInstance();
    
    // Load theme from file
    bool loadTheme(const juce::File& themeFile);
    
    // Save current theme to file
    bool saveTheme(const juce::File& themeFile);
    
    // Apply theme to ImGui/ImNodes
    void applyTheme();
    
    // Get colors
    ImU32 getCategoryColor(ModuleCategory cat, bool hovered = false);
    ImU32 getPinColor(PinDataType type);
    ImU32 getPinConnectedColor();
    ImU32 getPinDisconnectedColor();
    
    // Get layout values
    float getSidebarWidth() const;
    float getNodeDefaultWidth() const;
    float getItemSpacingX() const;
    
    // Get current theme
    const Theme& getCurrentTheme() const;
    
    // Theme switching
    void setTheme(const Theme& theme);
    void resetToDefault();
    
private:
    Theme currentTheme;
    Theme defaultTheme;
    
    void loadDefaultTheme();
    void applyImGuiColors();
    void applyImNodesColors();
    void applyFonts();
};
```

---

## 📝 15. Implementation Checklist

### Phase 1: Core Infrastructure
- [ ] Create `Theme` struct
- [ ] Create `ThemeManager` singleton class
- [ ] Implement default theme (current values)
- [ ] Implement `applyTheme()` method
- [ ] Replace `ImGui::StyleColorsDark()` with theme application

### Phase 2: Color System
- [ ] Migrate all `IM_COL32()` and `ImVec4()` hardcoded colors to theme
- [ ] Update `getImU32ForCategory()` to use theme
- [ ] Update `getImU32ForType()` to use theme
- [ ] Replace all `ImNodes::PushColorStyle()` calls with theme lookups
- [ ] Replace all `ImGui::PushStyleColor()` calls with theme lookups

### Phase 3: Layout & Spacing
- [ ] Migrate all spacing/padding constants to theme
- [ ] Update window dimension calculations
- [ ] Migrate font loading to theme system

### Phase 4: Module-Specific
- [ ] Create module-specific color sections in theme
- [ ] Update VideoFX module colors
- [ ] Update Scope module colors
- [ ] Update Stroke Sequencer module colors
- [ ] Update other module-specific colors

### Phase 5: Serialization
- [ ] Implement theme JSON/XML serialization
- [ ] Implement theme loading from file
- [ ] Implement theme saving to file
- [ ] Add theme selection UI

### Phase 6: Theme Presets
- [ ] Create "Dark" theme (current)
- [ ] Create "Light" theme variant
- [ ] Create "High Contrast" theme variant
- [ ] Create "Colorblind Friendly" theme variant

---

## 🔍 16. Code Search Patterns

Use these patterns to find additional UI elements:

```bash
# Find all color definitions
grep -r "IM_COL32\|ImVec4\|ImColor\|Colour" --include="*.cpp" --include="*.h"

# Find all font references
grep -r "FontSize\|AddFont\|GetFont\|SetFont" --include="*.cpp" --include="*.h"

# Find all spacing/padding
grep -r "Spacing\|Padding\|ItemSpacing\|WindowPadding" --include="*.cpp" --include="*.h"

# Find all alpha/opacity
grep -r "Alpha\|Opacity\|BgAlpha" --include="*.cpp" --include="*.h"

# Find all size/dimension constants
grep -r "SetNextWindowSize\|SetNextItemWidth\|Dummy(ImVec2" --include="*.cpp" --include="*.h"
```

---

## 📚 17. Reference Files

**Main UI Component**:
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (9015 lines)

**Module Processors** (with UI):
- `juce/Source/audio/modules/VideoFXModule.cpp`
- `juce/Source/audio/modules/ScopeModuleProcessor.cpp`
- `juce/Source/audio/modules/StrokeSequencerModuleProcessor.cpp`
- All other module processors with `drawParametersInNode()` implementations

**Supporting Files**:
- `juce/Source/preset_creator/NotificationManager.cpp`

**Design Guide**:
- `guides/IMGUI_NODE_DESIGN_GUIDE.md` (contains modulation color scheme)

---

## 🎯 18. Summary

**Total UI Elements Identified**:
- **Colors**: ~100+ distinct color values
- **Layout Constants**: ~20 spacing/padding/dimension values
- **Font Settings**: 2 fonts (default + Chinese)
- **Style Variables**: All ImGuiStyle properties (~60+)
- **Module-Specific**: ~10 modules with custom colors

**Priority Areas**:
1. **High Priority**: ImNodes colors (nodes, pins, links) - most visible
2. **High Priority**: Category colors - affects all node identification
3. **Medium Priority**: Text colors and status indicators
4. **Medium Priority**: Layout and spacing values
5. **Low Priority**: Module-specific colors (can be done incrementally)

**Estimated Effort**:
- **Phase 1-3**: 2-3 days (core infrastructure + color system)
- **Phase 4**: 1-2 days (module-specific)
- **Phase 5**: 1 day (serialization)
- **Phase 6**: 2-3 days (theme presets)

**Total**: ~1-2 weeks for complete implementation

---

## 📝 19. Notes

- Many colors are duplicated across multiple files (e.g., status colors appear twice)
- Some modules use hardcoded colors that could use standard theme colors
- Font path is hardcoded relative to executable - should be configurable
- Grid size and spacing are currently constants but could be user-configurable
- Some special effects (like muted node alpha) are hardcoded but could be themeable

---

**End of Guide** | Version 1.0 | 2025-01-27

