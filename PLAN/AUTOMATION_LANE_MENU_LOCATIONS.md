# Automation Lane - Exact Menu Locations

## 📍 Location 1: Left Panel Menu

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`  
**Lines:** 3190-3204

### Visual Structure:
```
┌─ Left Panel ─────────────────────┐
│                                   │
│  ▼ Sequencers                    │  ← Click to expand
│     • Sequencer                  │
│     • Multi Sequencer            │
│     • Tempo Clock                │
│     • Snapshot Sequencer         │
│     • Stroke Sequencer           │
│     • Chord Arp                  │
│     • Timeline                   │
│     • Automation Lane            │  ← HERE (Line 3203)
│                                   │
└───────────────────────────────────┘
```

**Code Location:**
```3190:3204:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
        pushCategoryColor(ModuleCategory::Seq);
        bool sequencersExpanded =
            ImGui::CollapsingHeader("Sequencers", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(4);
        if (sequencersExpanded)
        {
            addModuleButton("Sequencer", "sequencer");
            addModuleButton("Multi Sequencer", "multi_sequencer");
            addModuleButton("Tempo Clock", "tempo_clock");
            addModuleButton("Snapshot Sequencer", "snapshot_sequencer");
            addModuleButton("Stroke Sequencer", "stroke_sequencer");
            addModuleButton("Chord Arp", "chord_arp");
            addModuleButton("Timeline", "timeline");
            addModuleButton("Automation Lane", "automation_lane");
        }
```

---

## 📍 Location 2: Right-Click Context Menu

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`  
**Lines:** 6783-6801

### Visual Structure:
```
Right-click on canvas → Context Menu appears:

┌─────────────────────────────┐
│ Sources                     │
│ Effects                     │
│ Modulators                  │
│ Utilities & Logic           │
│ ▶ Sequencers                │  ← Click to expand
│   • Sequencer               │
│   • Multi Sequencer         │
│   • Tempo Clock             │
│   • Snapshot Sequencer      │
│   • Stroke Sequencer        │
│   • Chord Arp               │
│   • Timeline                │
│   • Automation Lane         │  ← HERE (Line 6799)
│ MIDI                        │
│ Analysis                    │
│ ...                         │
└─────────────────────────────┘
```

**Code Location:**
```6783:6801:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
                if (ImGui::BeginMenu("Sequencers"))
                {
                    if (ImGui::MenuItem("Sequencer"))
                        addAtMouse("sequencer");
                    if (ImGui::MenuItem("Multi Sequencer"))
                        addAtMouse("multi_sequencer");
                    if (ImGui::MenuItem("Tempo Clock"))
                        addAtMouse("tempo_clock");
                    if (ImGui::MenuItem("Snapshot Sequencer"))
                        addAtMouse("snapshot_sequencer");
                    if (ImGui::MenuItem("Stroke Sequencer"))
                        addAtMouse("stroke_sequencer");
                    if (ImGui::MenuItem("Chord Arp"))
                        addAtMouse("chord_arp");
                    if (ImGui::MenuItem("Timeline"))
                        addAtMouse("timeline");
                    if (ImGui::MenuItem("Automation Lane"))
                        addAtMouse("automation_lane");
                    ImGui::EndMenu();
                }
```

---

## 📍 Location 3: Search Database (Fuzzy Search)

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`  
**Line:** 13253

### Search Database Entry:
The search database is part of `getModuleRegistry()` which is used by the fuzzy search system.

**Database Entry:**
```13252:13254:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
        {"Function Generator", {"function_generator", "Custom function curves"}},
        {"Automation Lane", {"automation_lane", "Draw automation curves on scrolling timeline"}},
        {"Shaping Oscillator", {"shaping_oscillator", "Oscillator with waveshaping"}},
```

### How to Use Search:
1. **Type in search box:** Type "automation" or "lane"
2. **Results appear:** Should show "Automation Lane" with tooltip description
3. **Click result:** Creates the node

**Search keywords that should match:**
- "automation" ✅
- "lane" ✅
- "automation lane" ✅
- "draw" (from description) ✅
- "timeline" (from description) ✅

---

## 📍 Location 4: Search Category Detection

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`  
**Line:** 13075

**Category Detection:**
```13074:13076:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
    if (lower.contains("sequencer") || lower.contains("tempo_clock") || lower == "timeline" ||
        lower == "chord_arp" || lower == "automation_lane")
        return ModuleCategory::Seq;
```

This ensures Automation Lane appears in the Sequencers category when filtering by category.

---

## 🎯 Summary of All Locations

| Location | File | Line | Method |
|----------|------|------|--------|
| **Left Panel** | `ImGuiNodeEditorComponent.cpp` | 3203 | `addModuleButton("Automation Lane", "automation_lane")` |
| **Right-Click Menu** | `ImGuiNodeEditorComponent.cpp` | 6799-6800 | `ImGui::MenuItem("Automation Lane")` → `addAtMouse("automation_lane")` |
| **Search Database** | `ImGuiNodeEditorComponent.cpp` | 13253 | `{"Automation Lane", {"automation_lane", "Draw automation curves on scrolling timeline"}}` |
| **Category Detection** | `ImGuiNodeEditorComponent.cpp` | 13075 | `lower == "automation_lane"` → `ModuleCategory::Seq` |

---

## ✅ Verification Steps

After successful build and restart:

1. **Left Panel:**
   - Look at the left side of the screen
   - Find "Sequencers" section
   - Expand it (should be open by default)
   - Scroll to bottom - "Automation Lane" should be last item

2. **Right-Click Menu:**
   - Right-click anywhere on the empty canvas
   - Navigate to "Sequencers" submenu
   - "Automation Lane" should be the last item

3. **Search:**
   - Look for search box (usually top of screen)
   - Type "automation" 
   - Should see "Automation Lane" in results
   - Tooltip should show: "Draw automation curves on scrolling timeline"

---

## 🔍 If You Don't See It

**Possible reasons:**
1. ❌ Build didn't complete (linker error)
2. ❌ Application not restarted after build
3. ❌ Build cache - need clean rebuild

**Check:**
- Is "Timeline" visible in the same menus? (If yes, Automation Lane should be right after it)
- Does the build complete without errors?
- Did you restart the application after building?

---

**All registrations are correct. The node is at the exact locations shown above.**

