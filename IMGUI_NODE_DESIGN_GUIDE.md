# 📐 ImGui Node UI Design Guide

**Version**: 2.3.1  
**Last Updated**: 2025-10-24  
**Based on**: `imgui_demo.cpp` best practices + **official imnodes examples**

---

## 🎯 Purpose

This guide documents proven UI patterns for designing professional, consistent, and user-friendly node interfaces in our modular synth environment. All patterns are derived from `imgui_demo.cpp` and real-world refinement.

**Golden Rule**: *Every UI element must respect node boundaries and provide clear, immediate feedback to the user.*

---

## 📏 1. Node Layout & Structure

### 1.1 Item Width Management

**Rule**: Always respect the `itemWidth` parameter passed to `drawParametersInNode()`.

```cpp
void drawParametersInNode(float itemWidth, ...) override
{
    ImGui::PushItemWidth(itemWidth);
    
    // All controls here...
    
    ImGui::PopItemWidth();
}
```

**Why**: This ensures controls don't overflow node boundaries.

---

### 1.2 Section Separators (CRITICAL!)

**❌ NEVER USE** `ImGui::Separator()` or `ImGui::SeparatorText()` inside nodes!

These functions consume full available width and **extend beyond node boundaries**, creating visual glitches.

**✅ CORRECT APPROACH**:

```cpp
// Option 1: Simple text title + spacing
ImGui::Text("Section Name");
ImGui::Spacing();

// Option 2: Color-coded section title (preferred)
ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Section Name");
ImGui::Spacing();

// Option 3: Custom separator (advanced)
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImVec2 pos = ImGui::GetCursorScreenPos();
ImVec2 lineEnd = ImVec2(pos.x + itemWidth, pos.y);
drawList->AddLine(pos, lineEnd, IM_COL32(100, 100, 100, 255), 1.0f);
ImGui::Dummy(ImVec2(0, 1)); // Reserve space for line
ImGui::Spacing();
```

**Reference**: See `MIDIFadersModuleProcessor.cpp` lines 180-185 for working examples.

---

### 1.3 Vertical Spacing

**Best Practice**: Use `ImGui::Spacing()` liberally for visual breathing room.

```cpp
// Between major sections
ImGui::Spacing();
ImGui::Spacing();

// Between related controls
ImGui::Spacing();
```

**From imgui_demo.cpp**: Consistent spacing improves readability significantly.

---

## 🎨 2. Visual Feedback & Indicators

### 2.1 Color-Coded Modulation States

**Pattern**: Use distinct colors to show when parameters are under CV control.

```cpp
const bool isModulated = isParamModulated(paramId);

if (isModulated)
{
    // Cyan for CV modulation
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
}

if (isModulated) ImGui::BeginDisabled();
// ... slider/control here ...
if (isModulated) ImGui::EndDisabled();

if (isModulated) ImGui::PopStyleColor(3);
```

**Color Scheme**:
- **Cyan** (0.4, 0.8, 1.0): Frequency/Pitch modulation
- **Orange** (1.0, 0.8, 0.4): Timbre/Waveform modulation
- **Magenta** (1.0, 0.4, 1.0): Amplitude/Level modulation
- **Green** (0.4, 1.0, 0.4): Filter/EQ modulation

**Reference**: `VCOModuleProcessor.h` lines 59-64, 151-155

---

### 2.2 Real-Time Meters & Displays

**Pattern**: Use `ImGui::ProgressBar()` for level meters with color coding.

```cpp
float level = lastOutputValues[0]->load();
float absLevel = std::abs(level);

// Color-coded by level
ImVec4 meterColor;
if (absLevel < 0.7f)
    meterColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green (safe)
else if (absLevel < 0.9f)
    meterColor = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Yellow (hot)
else
    meterColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // Red (clipping!)

ImGui::PushStyleColor(ImGuiCol_PlotHistogram, meterColor);
ImGui::ProgressBar(absLevel, ImVec2(itemWidth, 0), "");
ImGui::PopStyleColor();

ImGui::SameLine(0, 5);
ImGui::Text("%.3f", level);
```

**Reference**: `VCOModuleProcessor.h` lines 202-213

---

### 2.3 Waveform/Data Visualization

**Pattern**: Use `ImGui::PlotLines()` for waveform previews.

```cpp
float waveformData[128];
// ... populate waveformData ...

ImGui::PlotLines(
    "##wavepreview",           // Hidden label (## prefix)
    waveformData,              // Data array
    128,                       // Array size
    0,                         // Values offset
    nullptr,                   // Overlay text
    -1.2f,                     // Scale min
    1.2f,                      // Scale max
    ImVec2(itemWidth, 80)      // Size
);
```

**From imgui_demo.cpp**: Lines 1982-1985, 2012-2013

**Reference**: `VCOModuleProcessor.h` lines 178-192

---

## 💡 3. Tooltips & Help System

### 3.1 HelpMarker Function (Standard)

**REQUIRED**: Implement this helper in every `drawParametersInNode()`:

```cpp
auto HelpMarker = [](const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
};
```

**From imgui_demo.cpp**: Lines 273-282

**Usage**:
```cpp
ImGui::SliderFloat("Frequency", &freq, 20.0f, 20000.0f);
HelpMarker("Sets the fundamental frequency.\nCan be modulated by CV input.");
```

---

### 3.2 Tooltip Best Practices

**Do**:
- ✅ Explain what the parameter does
- ✅ Mention CV modulation behavior
- ✅ Provide typical ranges or values
- ✅ Use `\n` for multi-line explanations

**Don't**:
- ❌ Write essays (keep under 3 lines)
- ❌ Repeat the obvious
- ❌ Forget to call `HelpMarker()` after EVERY control

**Reference**: `VCOModuleProcessor.h` lines 85, 175, 217

---

## 🎛️ 4. Control Patterns

### 4.1 Slider with Label Pattern

**Standard**:
```cpp
if (ImGui::SliderFloat("##paramId", &value, min, max, "%.1f", flags))
{
    if (!isModulated)
        *apvtsParam = value;
}
if (ImGui::IsItemDeactivatedAfterEdit())
    onModificationEnded();

ImGui::SameLine();
ImGui::Text("Parameter Name");
HelpMarker("Description here");
```

**Why separate label?**: Allows color-coding the text independently of the slider.

---

### 4.2 Quick Preset Buttons

**Pattern**: Provide common values as one-click buttons.

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
float btnWidth = (itemWidth - 12) / 4.0f;  // 4 buttons with spacing

if (ImGui::Button("A4", ImVec2(btnWidth, 0)))
{
    *frequencyParam = 440.0f;
    onModificationEnded();
}
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("440 Hz (Concert A)");

ImGui::SameLine();
// ... more buttons ...

ImGui::PopStyleVar();
```

**Reference**: `VCOModuleProcessor.h` lines 103-140

---

### 4.3 Combo Box (Dropdown) Pattern

```cpp
const char* items[] = { "Option 1", "Option 2", "Option 3" };
int currentItem = 0;

if (ImGui::Combo("##comboId", &currentItem, items, IM_ARRAYSIZE(items)))
{
    *apvtsParam = currentItem;
}
if (ImGui::IsItemDeactivatedAfterEdit())
    onModificationEnded();

ImGui::SameLine();
ImGui::Text("Selector Name");
HelpMarker("Choose from available options");
```

---

## 🔄 5. Tables & Multi-Control Layouts

### 5.1 Table Flags (From MIDI Modules Experience)

**REQUIRED FLAGS**:
```cpp
ImGuiTableFlags flags = 
    ImGuiTableFlags_SizingFixedFit |    // Fixed column widths
    ImGuiTableFlags_NoHostExtendX |     // Don't extend beyond itemWidth
    ImGuiTableFlags_Borders |           // Show cell borders
    ImGuiTableFlags_RowBg;              // Alternating row colors

if (ImGui::BeginTable("##tableId", numColumns, flags))
{
    // Setup columns with FIXED width
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    // ... content ...
    ImGui::EndTable();
}
```

**Critical**: ALL columns must use `ImGuiTableColumnFlags_WidthFixed` when using `NoHostExtendX`.

**Reference**: `MIDIFadersModuleProcessor.cpp` table implementation (lines ~400-500)

---

### 5.2 Button Grids

**Pattern**: Use calculated widths for perfect alignment.

```cpp
float spacing = 4.0f;
float btnWidth = (itemWidth - spacing * (numButtons - 1)) / numButtons;

ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

for (int i = 0; i < numButtons; ++i)
{
    if (i > 0) ImGui::SameLine();
    
    ImGui::PushID(i);  // CRITICAL for unique IDs!
    if (ImGui::Button("Button", ImVec2(btnWidth, 0)))
    {
        // Action
    }
    ImGui::PopID();
}

ImGui::PopStyleVar();
```

---

## 🆔 6. ID Management

### 6.1 Unique IDs Rule

**Every widget must have a unique ImGui ID!**

**Methods**:

1. **Hidden Label Suffix** (`##`):
```cpp
ImGui::SliderFloat("##freq", &freq, ...);  // ID = "##freq"
ImGui::SameLine();
ImGui::Text("Frequency");  // Visible label separate
```

2. **Push/Pop ID Scope**:
```cpp
for (int i = 0; i < count; ++i)
{
    ImGui::PushID(i);
    ImGui::Button("Learn");  // Unique ID: "Learn/0", "Learn/1", etc.
    ImGui::PopID();
}
```

3. **String ID in Popups/Combos**:
```cpp
ImGui::OpenPopup("SavePreset##nodeId");  // Unique per node
```

**From imgui_demo.cpp**: ID management examples throughout, especially lines 1000-1200 (widgets section)

---

## 🎨 7. Custom Drawing (Advanced)

### 7.1 Circular Indicators (Jog Wheel Pattern)

```cpp
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImVec2 center = ImGui::GetCursorScreenPos();
center.x += itemWidth * 0.5f;
center.y += 80.0f;

float radius = 40.0f;

// Background circle
drawList->AddCircle(center, radius, IM_COL32(100, 100, 100, 255), 32, 2.0f);

// Indicator needle
float angle = value * 2.0f * M_PI;  // Convert value to radians
ImVec2 needleEnd(
    center.x + std::cos(angle - M_PI / 2.0f) * (radius - 5.0f),
    center.y + std::sin(angle - M_PI / 2.0f) * (radius - 5.0f)
);

drawList->AddLine(center, needleEnd, IM_COL32(100, 200, 255, 255), 3.0f);
drawList->AddCircleFilled(center, 4.0f, IM_COL32(100, 200, 255, 255));

ImGui::Dummy(ImVec2(itemWidth, 160));  // Reserve space
```

**Reference**: `MIDIJogWheelModuleProcessor.cpp` lines 310-340

---

### 7.2 Clipping Regions

**Pattern**: Constrain drawing to specific area.

```cpp
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImVec2 pos = ImGui::GetCursorScreenPos();
ImVec2 size(itemWidth, 100);

drawList->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
// ... drawing code ...
drawList->PopClipRect();
```

**From imgui_demo.cpp**: Lines 6800-6850 (custom rendering examples)

---

## 📦 8. Complete Node Template

```cpp
#if defined(PRESET_CREATOR_UI)
void drawParametersInNode(float itemWidth,
                          const std::function<bool(const juce::String&)>& isParamModulated,
                          const std::function<void()>& onModificationEnded) override
{
    // 1. HelpMarker helper
    auto HelpMarker = [](const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    ImGui::PushItemWidth(itemWidth);

    // 2. Section 1: Main Controls
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Main Controls");
    ImGui::Spacing();
    
    // 3. Modulation-aware slider
    const bool isModulated = isParamModulated("param1");
    
    if (isModulated)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
    }
    
    if (isModulated) ImGui::BeginDisabled();
    
    float param1 = param1Param->load();
    if (ImGui::SliderFloat("##param1", &param1, 0.0f, 1.0f))
    {
        if (!isModulated)
            *param1Param = param1;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        onModificationEnded();
    
    if (isModulated) ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (isModulated)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Parameter 1 (CV)");
        ImGui::PopStyleColor(3);
    }
    else
    {
        ImGui::Text("Parameter 1");
    }
    HelpMarker("This parameter does something important.\nRange: 0 to 1");

    ImGui::Spacing();
    ImGui::Spacing();

    // 4. Section 2: Visualization
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output");
    ImGui::Spacing();
    
    // 5. Real-time meter
    float level = lastOutputValues[0]->load();
    ImVec4 color = (std::abs(level) < 0.7f) 
        ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)   // Green
        : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // Red
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(std::abs(level), ImVec2(itemWidth, 0), "");
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 5);
    ImGui::Text("%.3f", level);
    HelpMarker("Current output level");

    ImGui::PopItemWidth();
}

void drawIoPins(const NodePinHelpers& helpers) override
{
    helpers.drawAudioInputPin("In", 0);
    helpers.drawAudioOutputPin("Out", 0);
}
#endif
```

---

## 🔌 8. Pin Label Spacing (CRITICAL!)

### 8.1 Output Pin Text Alignment

**Problem**: Default spacing between pin labels and pin circles creates visual disconnection.

**From imgui_demo.cpp**: Line 447 shows `ImGui::SameLine(0, 0)` for minimal spacing between elements.

**Best Practice**: Use `ImGui::Indent()` for right-alignment (IMNODES OFFICIAL PATTERN).

```cpp
// ✅ CORRECT: Use Indent() - this is how ALL imnodes examples do it!
// From: color_node_editor.cpp:353, save_load.cpp:77, multi_editor.cpp:73
auto rightLabelWithinWidth = [&](const char* txt, float nodeContentWidth)
{
    const ImVec2 textSize = ImGui::CalcTextSize(txt);
    
    // Indent by (nodeWidth - textWidth) to right-align
    // ImNodes uses Indent(), NOT Dummy() + SameLine()!
    const float indentAmount = juce::jmax(0.0f, nodeContentWidth - textSize.x);
    ImGui::Indent(indentAmount);
    ImGui::TextUnformatted(txt);
    ImGui::Unindent(indentAmount);  // CRITICAL: Reset indent!
};
```

**⚠️ CRITICAL**: Always call `Unindent()` to match `Indent()`! Indent is **persistent** and will affect all subsequent ImGui elements until reset.

**❌ WRONG**: Using Dummy() + SameLine() (imgui_demo.cpp pattern doesn't work for imnodes!)
```cpp
// BAD: This works in regular ImGui windows but NOT in imnodes!
// Causes layout issues and scrollbars in node contexts
ImGui::Dummy(ImVec2(dummyWidth, 0));
ImGui::SameLine(0, 0);
ImGui::TextUnformatted(txt);  // Wrong for imnodes!
```

**❌ WRONG**: Manual cursor positioning
```cpp
// BAD: SetCursorPosX() fights with ImNodes' internal layout
const float leftEdge = ImGui::GetCursorPosX();
ImGui::SetCursorPosX(leftEdge + nodeContentWidth - textWidth);  // Breaks!
```

**❌ WRONG**: GetContentRegionAvail() causes infinite scrollbars!
```cpp
// BAD: GetContentRegionAvail() changes as you add content!
// Creates feedback loop in fixed-size nodes
const float availWidth = ImGui::GetContentRegionAvail().x;
float x = cursorX + (availWidth - textWidth);  // Infinite scaling!
```

**Why `Indent()` is the Correct Pattern for ImNodes**:
- **Official ImNodes pattern**: Used in ALL official imnodes examples
- **Works with ImNodes layout**: Respects node padding and spacing
- **No side effects**: Doesn't trigger scrollbars or layout recalculation
- **Simple and clean**: Single function call, no SameLine() needed

**Example from Official ImNodes (color_node_editor.cpp:351-356)**:
```cpp
ImNodes::BeginOutputAttribute(node.id);
const float label_width = ImGui::CalcTextSize("result").x;
ImGui::Indent(node_width - label_width);  // ← This is the pattern!
ImGui::TextUnformatted("result");
ImNodes::EndOutputAttribute();
```

**Key Insight**: `Dummy()` is used in imnodes to SET node minimum width (line 425), NOT for text alignment!

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 1976-1985

---

### 8.2 Output Pin Circle Positioning (CRITICAL!)

**Problem**: The pin circle itself can be positioned with an offset from the text, creating additional gap.

**❌ WRONG**: Adding offset to pin position
```cpp
const float PIN_CIRCLE_OFFSET = 8.0f;  // Creates 8px gap!
float x_pos = pinMax.x + PIN_CIRCLE_OFFSET;
attrPositions[attr] = ImVec2(x_pos, y_center);
```

**✅ CORRECT**: Zero offset - pin circle at text edge
```cpp
// Text is right-aligned to nodeContentWidth
// Pin circle positioned RIGHT at the edge
float x_pos = pinMax.x;  // No offset!
attrPositions[attr] = ImVec2(x_pos, y_center);
```

**Complete Flow**:
1. Text is right-aligned using `rightLabelWithinWidth` (0px padding)
2. `pinMax.x` captures the rightmost edge of the text area
3. Pin circle is positioned AT `pinMax.x` with no additional offset
4. Result: Pin circle touches text, which touches node border

**Reference**: `ImGuiNodeEditorComponent.cpp` lines 2175-2181, 2061-2067

---

### 8.3 SameLine Spacing Patterns

**From imgui_demo.cpp**:

```cpp
// Zero spacing (tight alignment)
ImGui::Text("Label");
ImGui::SameLine(0, 0);  // Second param = 0 removes extra spacing
ImGui::Button("Button");

// Small spacing (5px)
ImGui::ProgressBar(0.5f);
ImGui::SameLine(0, 5);  // 5px gap
ImGui::Text("50%%");

// Default spacing
ImGui::Checkbox("Option", &flag);
ImGui::SameLine();  // Uses style.ItemSpacing.x
HelpMarker("Tooltip text");
```

**Rule**: Use `SameLine(0, 0)` when you want elements visually "touching" (like pin labels to pins).

---

## 📦 9. ImNodes-Specific Patterns (FROM OFFICIAL EXAMPLES)

### 9.1 Node Content Width Management

**Pattern**: Use fixed widths, NOT GetContentRegionAvail()

```cpp
// From color_node_editor.cpp:314, 363, 415, 482
const float node_width = 100.0f;  // Fixed width for predictable layout
```

**Use Dummy() to set minimum width**:
```cpp
// From color_node_editor.cpp:425
ImGui::Dummy(ImVec2(node_width, 0.f));  // At START of node content
```

**Why Fixed Width**:
- Nodes don't resize dynamically like windows
- Prevents feedback loops and scrollbars
- All examples use hardcoded values (100.0f, 120.0f)

---

### 9.2 Input Attribute Pattern (Left Side)

**Structure**: Text first, then optional control

```cpp
// From color_node_editor.cpp:321-331
ImNodes::BeginInputAttribute(pin_id);
const float label_width = ImGui::CalcTextSize("left").x;
ImGui::TextUnformatted("left");

// If pin not connected, show input control
if (not_connected)
{
    ImGui::SameLine();
    ImGui::PushItemWidth(node_width - label_width);
    ImGui::DragFloat("##hidelabel", &value, 0.01f);
    ImGui::PopItemWidth();
}
ImNodes::EndInputAttribute();
```

**Key Points**:
- Label first, control after (if shown)
- Use `SameLine()` to put control next to label
- `PushItemWidth(node_width - label_width)` ensures control fits

---

### 9.3 Output Attribute Pattern (Right Side)

**Structure**: Indent first, text, then Unindent (NO SameLine!)

```cpp
// From color_node_editor.cpp:351-356 (with Unindent() added for persistence prevention)
ImNodes::BeginOutputAttribute(pin_id);
const float label_width = ImGui::CalcTextSize("result").x;
ImGui::Indent(node_width - label_width);  // ← Right-align!
ImGui::TextUnformatted("result");
ImGui::Unindent(node_width - label_width);  // ← Reset indent! CRITICAL!
ImNodes::EndOutputAttribute();
```

**Critical**: 
- Use `Indent()` for right-alignment, NOT Dummy() + SameLine()!
- **ALWAYS** call `Unindent()` to match `Indent()` - indent is persistent!

**Why Unindent() is Required**:
The imnodes examples work because they only have ONE output per node, so the indent doesn't affect anything else. In our multi-pin nodes, failing to unindent causes ALL subsequent elements to be indented cumulatively, creating the "red line" alignment bug where all text appears at the same wrong X position.

---

### 9.4 Static Attribute Pattern (No Pin)

**Structure**: For controls that don't connect to anything

```cpp
// From save_load.cpp:69-73, multi_editor.cpp:65-69
ImNodes::BeginStaticAttribute(attr_id);
ImGui::PushItemWidth(120.f);
ImGui::DragFloat("value", &value, 0.01f);
ImGui::PopItemWidth();
ImNodes::EndStaticAttribute();
```

**Use Cases**:
- Node settings/parameters
- Controls that affect node behavior
- UI elements without data flow

---

### 9.5 Node Layout Best Practices

**From ALL imnodes examples**:

1. **Title bar always first**:
```cpp
ImNodes::BeginNode(node_id);
ImNodes::BeginNodeTitleBar();
ImGui::TextUnformatted("node name");
ImNodes::EndNodeTitleBar();
// ... content ...
ImNodes::EndNode();
```

2. **Use Spacing() between sections**:
```cpp
// From color_node_editor.cpp:348, 400, 441, 458, 508
ImGui::Spacing();  // Visual separation
```

3. **Input pins before output pins**:
```cpp
// Input pins (left side)
ImNodes::BeginInputAttribute(...);
// Output pins (right side)  
ImNodes::BeginOutputAttribute(...);
```

4. **Calculate widths based on text**:
```cpp
const float label_width = ImGui::CalcTextSize("label").x;
```

---

### 9.6 Common Mistakes to Avoid

**❌ DON'T**: Forget to call Unindent() (CRITICAL BUG!)
```cpp
// BAD: Indent persists and affects all subsequent elements!
ImGui::Indent(node_width - text_width);
ImGui::Text("out");
// Missing Unindent() causes "red line" alignment bug!
```

**✅ DO**: Always match Indent() with Unindent()
```cpp
ImGui::Indent(node_width - text_width);
ImGui::Text("out");
ImGui::Unindent(node_width - text_width);  // CRITICAL!
```

**❌ DON'T**: Use Dummy() + SameLine() for output text
```cpp
// This is for ImGui windows, NOT imnodes!
ImGui::Dummy(ImVec2(width, 0));
ImGui::SameLine();
ImGui::Text("out");
```

**❌ DON'T**: Use GetContentRegionAvail() or -1 width in nodes
```cpp
float w = ImGui::GetContentRegionAvail().x;  // Causes scrollbars!
ImGui::ProgressBar(progress, ImVec2(-1, 0), "");  // -1 width also causes infinite scaling!
```

**✅ DO**: Use fixed widths from itemWidth parameter
```cpp
const float node_width = 240.0f;
ImGui::ProgressBar(progress, ImVec2(itemWidth, 0), "");  // Fixed width!
```

**Real-world bug**: MIDI Player initially used `ImVec2(-1, 0)` for progress bar, causing infinite right-side scaling. Fixed by using `ImVec2(itemWidth, 0)`.

**❌ DON'T**: Manually position with SetCursorPosX()
```cpp
ImGui::SetCursorPosX(x);  // Fights with ImNodes layout
```

**✅ DO**: Let ImNodes handle positioning with Indent()
```cpp
ImGui::Indent(amount);
```

---

### 9.7 Complete Node Example (From Official Examples)

```cpp
const float node_width = 100.0f;
ImNodes::BeginNode(node_id);

// Title
ImNodes::BeginNodeTitleBar();
ImGui::TextUnformatted("add");
ImNodes::EndNodeTitleBar();

// Input pin with optional control
ImNodes::BeginInputAttribute(input_id);
const float label_width = ImGui::CalcTextSize("left").x;
ImGui::TextUnformatted("left");
if (!is_connected)
{
    ImGui::SameLine();
    ImGui::PushItemWidth(node_width - label_width);
    ImGui::DragFloat("##hide", &value, 0.01f);
    ImGui::PopItemWidth();
}
ImNodes::EndInputAttribute();

ImGui::Spacing();  // Visual separation

// Output pin (right-aligned)
ImNodes::BeginOutputAttribute(output_id);
const float out_label_width = ImGui::CalcTextSize("result").x;
ImGui::Indent(node_width - out_label_width);
ImGui::TextUnformatted("result");
ImNodes::EndOutputAttribute();

ImNodes::EndNode();
```

**Reference**: color_node_editor.cpp:313-359, save_load.cpp:57-82, multi_editor.cpp:54-78

---

## ✅ 10. Pre-Flight Checklist

Before committing any node UI:

- [ ] No `ImGui::Separator()` or `ImGui::SeparatorText()` used
- [ ] All sections use `ImGui::TextColored()` + `ImGui::Spacing()`
- [ ] Every control has a `HelpMarker()` tooltip
- [ ] Modulated parameters have color-coding
- [ ] All tables use `NoHostExtendX` + `WidthFixed` columns
- [ ] Unique IDs for all widgets (check with `##` or `PushID()`)
- [ ] `ImGui::PushItemWidth(itemWidth)` at start
- [ ] `ImGui::PopItemWidth()` at end
- [ ] Real-time feedback where applicable (meters, plots)
- [ ] Output pins use `Indent()` for right-alignment (NOT Dummy() + SameLine!)
- [ ] Fixed node width used (NOT GetContentRegionAvail())
- [ ] Input pins: label first, control after with SameLine()
- [ ] Output pins: Indent() first, then text
- [ ] Tested with modulation connected and disconnected

---

## 📚 11. Reference Examples

**Best Implementations**:
1. **VCOModuleProcessor.h** (lines 28-220): Complete modern node with all patterns
2. **MIDIFadersModuleProcessor.cpp** (lines 180-600): Table layouts, learn modes
3. **MIDIJogWheelModuleProcessor.cpp** (lines 140-340): Custom drawing, circular indicators

**imgui_demo.cpp Sections**:
- Lines 273-282: HelpMarker implementation
- Lines 1982-2032: PlotLines examples
- Lines 3552-3555: TextColored usage
- Lines 6800-7000: Custom drawing with ImDrawList

**imnodes Official Examples** (H:\0000_CODE\01_collider_pyo\imnode_examples\):
- **hello.cpp**: Basic node structure (lines 17-32)
- **color_node_editor.cpp**: Complete node patterns
  - Fixed width pattern (lines 314, 363, 415, 482)
  - Input attribute with control (lines 321-331)
  - Output attribute with Indent() (lines 351-356)
  - Dummy() for minimum width (line 425)
- **save_load.cpp**: Simple node with static attribute (lines 57-82)
- **multi_editor.cpp**: Multi-context management (lines 54-78)

---

## 🔢 12. Multi-Voice & Collapsible UI Patterns

### 12.1 Collapsible Headers (For Polyphonic/Multi-Instance Modules)

When dealing with many similar controls (e.g., 32 voices in PolyVCO), use collapsible headers with proper state management.

**Pattern**:

```cpp
// Add Expand/Collapse All controls
static bool expandAllState = false;
static bool collapseAllState = false;

if (ImGui::SmallButton("Expand All")) {
    expandAllState = true;
}
ImGui::SameLine();
if (ImGui::SmallButton("Collapse All")) {
    collapseAllState = true;
}

ImGui::Spacing();

for (int i = 0; i < numVoices; ++i)
{
    ImGui::PushID(i);  // CRITICAL: Unique ID per iteration
    
    // Apply expand/collapse state
    if (expandAllState) ImGui::SetNextItemOpen(true);
    if (collapseAllState) ImGui::SetNextItemOpen(false);
    
    // Default open state (first 4 only, on first use)
    ImGui::SetNextItemOpen(i < 4, ImGuiCond_Once);
    
    // Color-code for visual distinction
    float hue = (float)i / (float)maxVoices;
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.7f, 1.0f).Value);
    
    if (ImGui::CollapsingHeader(("Voice " + juce::String(i+1)).toRawUTF8(),
                                ImGuiTreeNodeFlags_SpanAvailWidth | 
                                ImGuiTreeNodeFlags_FramePadding))
    {
        ImGui::PopStyleColor();
        
        // Controls go here (see next section for table layout)
    }
    else
    {
        ImGui::PopStyleColor();
    }
    
    ImGui::PopID();
}

// Reset expand/collapse state after loop
expandAllState = false;
collapseAllState = false;
```

**Key Points**:
- `ImGui::PushID(i)` prevents ID conflicts between similar controls
- `ImGuiCond_Once` ensures default state applies only on first use
- `ImGuiTreeNodeFlags_SpanAvailWidth` prevents layout issues
- Color-coding helps distinguish voices at a glance

---

### 12.2 Table Layout Inside Collapsible Sections

For compact, multi-column layouts inside headers, use tables instead of Indent().

**Pattern**:

```cpp
if (ImGui::CollapsingHeader("Voice 1", ...))
{
    const float columnWidth = itemWidth / 3.0f;
    
    if (ImGui::BeginTable("voiceTable", 3,
                          ImGuiTableFlags_SizingFixedFit |
                          ImGuiTableFlags_NoBordersInBody,
                          ImVec2(itemWidth, 0)))
    {
        // Column 1
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(columnWidth - 8.0f);  // 8px padding
        ImGui::Combo("##wave", &wave, "Sine\0Saw\0Square\0\0");
        ImGui::TextUnformatted("Wave");
        ImGui::PopItemWidth();
        
        // Column 2
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(columnWidth - 8.0f);
        ImGui::SliderFloat("##freq", &freq, 20.0f, 20000.0f, "%.0f", 
                          ImGuiSliderFlags_Logarithmic);
        ImGui::TextUnformatted("Hz");
        ImGui::PopItemWidth();
        
        // Column 3
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(columnWidth - 8.0f);
        ImGui::SliderFloat("##gate", &gate, 0.0f, 1.0f, "%.2f");
        ImGui::TextUnformatted("Gate");
        ImGui::PopItemWidth();
        
        ImGui::EndTable();
    }
}
```

**Why Tables**:
- Avoids cumulative Indent() bugs
- Provides consistent column widths
- Handles overflow better than manual layout
- **CRITICAL**: Don't use `ImGuiTableFlags_RowBg` with fixed-size tables in nodes! Row backgrounds can bleed outside node boundaries. Use plain tables with `NoBordersInBody` only.

---

### 12.3 Parallel Pin Drawing (Multi-Voice Nodes)

For nodes with multiple voices/channels, use `helpers.drawParallelPins()` to align inputs with outputs.

**Pattern**:

```cpp
void drawIoPins(const NodePinHelpers& helpers) override
{
    // Global input (no output pairing)
    helpers.drawParallelPins("NumVoices Mod", 0, nullptr, -1);
    
    for (int i = 0; i < getEffectiveNumVoices(); ++i)
    {
        juce::String idx = juce::String(i + 1);
        
        // Pair primary input with output on same row
        helpers.drawParallelPins(("Freq " + idx + " Mod").toRawUTF8(), 
                                 1 + i,
                                 ("Voice " + idx).toRawUTF8(), 
                                 i);
        
        // Secondary inputs (no output pairing)
        helpers.drawParallelPins(("Wave " + idx + " Mod").toRawUTF8(), 
                                 1 + MAX_VOICES + i, 
                                 nullptr, -1);
        helpers.drawParallelPins(("Gate " + idx + " Mod").toRawUTF8(), 
                                 1 + (2 * MAX_VOICES) + i, 
                                 nullptr, -1);
    }
}
```

**Why Parallel Pins**:
- Creates visual alignment between related inputs/outputs
- Reduces vertical node height
- Makes signal flow clearer
- Matches MultiSequencer pattern for consistency

**Signature**:
```cpp
void drawParallelPins(const char* inputLabel, int inputChannel,
                     const char* outputLabel, int outputChannel);
```

Pass `nullptr` and `-1` for outputLabel/outputChannel when there's no output on that row.

---

### 12.4 Complete Example: PolyVCO Node

See `juce/Source/audio/modules/PolyVCOModuleProcessor.cpp` for the complete, production-ready implementation featuring:
- Expand/Collapse All buttons
- Color-coded collapsible headers (HSV hue cycling)
- 3-column table layout for voice parameters
- Parallel pin drawing with 3 inputs per voice + 1 output
- Live modulation feedback with "(mod)" indicators
- First 4 voices open by default

---

## 🔄 13. Update Log

| Date | Version | Changes |
|------|---------|---------|
| 2025-10-24 | **2.3.1** | **🐛 CRITICAL FIX**: Removed `ImGuiTableFlags_RowBg` from PolyVCO tables!<br>• **Problem**: Blue row backgrounds were bleeding outside node boundaries<br>• **Root cause**: `RowBg` creates backgrounds that extend beyond fixed-size table constraints in nodes<br>• **Solution**: Use `SizingFixedFit + NoBordersInBody` WITHOUT `RowBg` flag<br>• Updated Section 12.2 with warning about RowBg in fixed-size nodes<br>• Based on imgui_demo.cpp analysis: RowBg is for scrollable/dynamic tables, not fixed-size property grids |
| 2025-10-24 | **2.3** | **🎯 NEW PATTERNS**: Multi-Voice & Collapsible UI!<br>• Added Section 12: Complete patterns for polyphonic nodes<br>• **12.1**: Collapsible headers with Expand/Collapse All<br>• **12.2**: Table-based layouts inside headers (avoids Indent bugs)<br>• **12.3**: Parallel pin drawing for multi-voice nodes<br>• **12.4**: PolyVCO as reference implementation<br>• Fixed PolyVCO node (32 voices, 3-column tables, parallel pins)<br>• Documented stable ID management and HSV color-coding |
| 2025-10-24 | **2.2** | **🚨 CRITICAL BUG FIX**: Documented `-1` width issue in ProgressBar!<br>• **Real-world bug**: MIDI Player used `ImVec2(-1, 0)` for progress bar width<br>• **Symptom**: Infinite right-side scaling, unusable node<br>• **Fix**: Use `ImVec2(itemWidth, 0)` with fixed width parameter<br>• Updated Section 9.6 with progress bar example<br>• Added warning about `-1` width alongside `GetContentRegionAvail()` issue |
| 2025-10-24 | **2.1** | **🚨 CRITICAL BUG FIX**: Added `Unindent()` to match every `Indent()` call!<br>• **Root cause**: Indent() is persistent and was affecting all subsequent elements<br>• **Symptom**: All output labels appeared at same X position ("red line" bug)<br>• **Fix**: Always call `ImGui::Unindent(amount)` after `ImGui::Indent(amount)`<br>• Updated Section 9.3 with Unindent() requirement<br>• Added new Common Mistake #1: Forgetting Unindent()<br>**Why imnodes examples didn't show this**: They only have ONE output per node! |
| 2025-10-24 | **2.0** | **🎯 MAJOR UPDATE**: Analyzed ALL official imnodes examples. Discovered `ImGui::Indent()` is the CORRECT pattern (NOT Dummy()!).<br>• Added comprehensive Section 9: ImNodes-Specific Patterns<br>• Documented input/output attribute patterns from official examples<br>• Added complete node example with all best practices<br>• Updated all code to use Indent() for output pin alignment<br>• Expanded reference section with imnodes examples<br>**Breaking insight**: imgui_demo.cpp patterns don't always apply to imnodes! |
| 2025-10-24 | 1.6 | ~~Dummy() + SameLine() approach~~ (WRONG for imnodes, fixed in v2.0) |
| 2025-10-24 | 1.5 | ~~Manual cursor positioning~~ (WRONG, fixed in v2.0) |
| 2025-10-24 | 1.4 | ~~GetContentRegionAvail()~~ (caused scrollbars, fixed in v2.0) |
| 2025-10-24 | 1.3 | **CRITICAL FIX**: Eliminated PIN_CIRCLE_OFFSET (was 8px). Added Section 8.2: Output Pin Circle Positioning. Pin circles now positioned at pinMax.x with zero offset for perfect node border alignment. |
| 2025-10-24 | 1.2 | Further reduced pin padding from 2px to 0px for maximum tightness. Updated all examples and checklist. |
| 2025-10-24 | 1.1 | Added Section 8: Pin Label Spacing (CRITICAL!). Fixed output pin text gap issue by reducing padding from 8px to 2px. Added SameLine spacing patterns from imgui_demo.cpp. |
| 2025-10-24 | 1.0 | Initial guide created. Added separator fix, color-coding patterns, tooltip system. |

---

## 📝 12. Contributing to This Guide

When you discover a new pattern or fix an issue:

1. Document it here with a clear example
2. Reference the source file and line numbers
3. Explain WHY this pattern is better
4. Add to the Pre-Flight Checklist if applicable
5. Update the version number and log

**This guide is a living document!** Update it after every successful node redesign.

---

**End of Guide** | Version 2.3.1 | 2025-10-24

