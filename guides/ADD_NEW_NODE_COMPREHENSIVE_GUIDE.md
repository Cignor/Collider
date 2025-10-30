# Complete Guide: Adding a New Node (Module) to All Menu Systems

## Overview

This guide documents **every location** where a new module needs to be registered to appear in all menu systems. We use the **Drive** node as a reference example throughout.

When adding a new module to the synthesizer, you need to register it in **8 different locations**:

1. **Module Processor Implementation** (C++ class files)
2. **Module Factory Registration** 
3. **Pin Database** (I/O pin definitions)
4. **Module Descriptions** (tooltip text)
5. **Left Panel Menu** (categorized buttons)
6. **Right-Click Context Menu** (Add Node at Mouse)
7. **Top Bar "Insert Between" Menu** (insert on selected node's cables)
8. **Insert on Cable(s) Menu** (right-click on cable)
9. **Search System** (fuzzy search and categorization)

---

## 1. Module Processor Implementation

First, create your module's processor files:

### File: `juce/Source/audio/modules/YourModuleProcessor.h`

```cpp
#pragma once

#include "ModuleProcessor.h"

class YourModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs (use constexpr for consistency)
    static constexpr auto paramIdYourParam = "your_param";
    
    YourModuleProcessor();
    ~YourModuleProcessor() override = default;
    
    // CRITICAL: This name MUST match the factory registration key
    const juce::String getName() const override { return "your_module"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, 
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif
    
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, 
                         int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::AudioProcessorValueTreeState apvts;
    
    // Your member variables here
};
```

### File: `juce/Source/audio/modules/YourModuleProcessor.cpp`

**Key Method - `getName()`:**

```cpp
const juce::String getName() const override { return "your_module"; }
```

⚠️ **CRITICAL**: The string returned by `getName()` **MUST** match:
- The factory registration key (step 2)
- The internal type name used in all menus
- The pin database key (step 3)

---

## 2. Module Factory Registration

**File:** `juce/Source/audio/graph/ModularSynthProcessor.cpp`

**Location:** Inside the `getModuleFactory()` function (~line 630-730)

**What to add:**

```cpp
// Inside getModuleFactory() function
auto& factory = getFactoryInstance();
if (!initialised)
{
    auto reg = [&](const juce::String& key, std::function<std::unique_ptr<juce::AudioProcessor>()> fn) {
        factory[key.toLowerCase()] = fn;
    };
    
    // ... existing registrations ...
    
    // ADD YOUR MODULE HERE:
    reg("your_module", []{ return std::make_unique<YourModuleProcessor>(); });
    
    initialised = true;
}
```

### Drive Example (line 705):

```cpp
reg("drive", []{ return std::make_unique<DriveModuleProcessor>(); });
```

### Important Notes:
- The key string (`"your_module"`) should be **lowercase with underscores** for spaces
- This key MUST match the string returned by `YourModuleProcessor::getName()`
- The factory uses a case-insensitive lookup, but lowercase is the convention

---

## 3. Pin Database (I/O Pin Definitions)

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

**Location:** Inside the `populateModulePinDatabase()` function (~line 100-900)

**What to add:**

```cpp
// Inside populateModulePinDatabase()
db["your_module"] = ModulePinInfo(
    NodeWidth::Small,  // Options: Small, Medium, Big
    {  // Input pins
        AudioPin("In L", 0, PinDataType::Audio),
        AudioPin("In R", 1, PinDataType::Audio),
        AudioPin("Param Mod", 2, PinDataType::CV)  // Optional modulation inputs
    },
    {  // Output pins
        AudioPin("Out L", 0, PinDataType::Audio),
        AudioPin("Out R", 1, PinDataType::Audio)
    },
    {}  // Modulation outputs (leave empty unless you have mod outputs)
);
```

### Drive Example (lines 277-282):

```cpp
db["drive"] = ModulePinInfo(
    NodeWidth::Small,
    { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
    { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
    {}
);
```

### Pin Data Types:
- `PinDataType::Audio` - Audio signals
- `PinDataType::CV` - Control voltage (modulation)
- `PinDataType::Gate` - Triggers and gates
- `PinDataType::Mod` - Modulation outputs (e.g., from LFO)

### Node Widths:
- `NodeWidth::Small` - 2-channel I/O, simple processing
- `NodeWidth::Medium` - 3-4 channel I/O, moderate complexity
- `NodeWidth::Big` - 5+ channels, complex modules

---

## 4. Module Descriptions (Tooltips)

**File:** `juce/Source/preset_creator/PinDatabase.cpp`

**Location:** Inside the `populateModuleDescriptions()` function (~line 1-100)

**What to add:**

```cpp
// Inside populateModuleDescriptions()
void populateModuleDescriptions()
{
    auto& descriptions = getModuleDescriptions();
    if (!descriptions.empty()) return; // Only run once
    
    // ... existing descriptions ...
    
    // ADD YOUR MODULE DESCRIPTION:
    descriptions["your_module"] = "Brief description of what your module does.";
}
```

### Drive Example (line 39):

```cpp
descriptions["drive"] = "A waveshaping distortion effect.";
```

### Tips:
- Keep descriptions concise (one sentence)
- Describe the primary function
- These appear as tooltips in the UI

---

## 5. Left Panel Menu (Categorized Buttons)

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the `drawLeftPanel()` function (~line 1300-1550)

**What to add:**

Find the appropriate category section and add a button:

```cpp
// Find the correct category section, e.g., for Effects:
pushCategoryColor(ModuleCategory::Effect);
bool effectsExpanded = ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen);
ImGui::PopStyleColor(3);
if (effectsExpanded) {
    // ... existing buttons ...
    
    // ADD YOUR BUTTON:
    addModuleButton("Your Module Display Name", "your_module");
}
```

### Drive Example (line 1475):

```cpp
// Inside the Effects section
addModuleButton("Drive", "drive");
```

### Available Categories:
- **Sources** - Oscillators, noise, inputs
- **Effects** - Audio effects and processors
- **Modulators** - LFOs, envelopes, etc.
- **Utilities & Logic** - Math, mixers, gates
- **Analysis** - Scopes, debug tools
- **Sequencers** - Step sequencers, MIDI
- **MIDI** - MIDI controllers and converters
- **Computer Vision** - OpenCV modules
- **Special** - Physics, animation, etc.

### Category Locations in Code:
- Sources: ~line 1360
- Sequencers: ~line 1390
- MIDI: ~line 1425
- Effects: ~line 1463 (Drive is here)
- Modulators: ~line 1484
- Utilities & Logic: ~line 1497
- Analysis: ~line 1518
- Computer Vision: ~line 1527
- Special: ~line 1539

---

## 6. Right-Click Context Menu (Add Node at Mouse)

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the context menu drawing code (~line 3400-3600)

**What to add:**

Find the appropriate submenu and add a menu item:

```cpp
// Inside the right-click context menu
if (ImGui::BeginMenu("Effects")) {
    // ... existing items ...
    
    // ADD YOUR MENU ITEM:
    if (ImGui::MenuItem("Your Module Display Name")) addAtMouse("your_module");
    
    ImGui::EndMenu();
}
```

### Drive Example (line 3480):

```cpp
if (ImGui::BeginMenu("Effects")) {
    // ... other effects ...
    if (ImGui::MenuItem("Drive")) addAtMouse("drive");
    // ... more effects ...
    ImGui::EndMenu();
}
```

### Available Submenus:
- Sources
- Sequencers
- MIDI
- Effects
- Modulators
- Utilities & Logic
- Analysis
- Computer Vision
- Special

---

## 7. Top Bar "Insert Between" Menu

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the top menu bar code (~line 690-740)

**What to add:**

This menu appears in the top bar and inserts a node between the selected node and all its outgoing connections.

```cpp
// Inside the "Insert Between" menu
if (ImGui::BeginMenu("Audio Path", isNodeSelected))
{
    // ... existing items ...
    
    // ADD YOUR MENU ITEM:
    if (ImGui::MenuItem("Your Module")) { insertNodeBetween("your_module"); }
    
    ImGui::EndMenu();
}
```

### Drive Example (line 703):

```cpp
if (ImGui::BeginMenu("Audio Path", isNodeSelected))
{
    // ... other modules ...
    if (ImGui::MenuItem("Drive")) { insertNodeBetween("drive"); }
    // ... more modules ...
    ImGui::EndMenu();
}
```

### Available Submenus:
- **Audio Path** (line ~692) - For audio processors
- **Modulation Path** (line ~721) - For CV/modulation processors

---

## 8. Insert on Cable(s) Menu

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the `drawInsertNodeOnLinkPopup()` function (~line 6200-6250)

**What to add:**

This menu appears when right-clicking on a cable or selecting multiple cables.

```cpp
void ImGuiNodeEditorComponent::drawInsertNodeOnLinkPopup()
{
    if (ImGui::BeginPopup("InsertNodeOnLinkPopup"))
    {
        // Find the appropriate map (audio or modulation)
        const std::map<const char*, const char*> audioInsertable = {
            // ... existing entries ...
            
            // ADD YOUR MODULE:
            {"Your Display Name", "your_module"},
        };
        
        // OR for modulation:
        const std::map<const char*, const char*> modInsertable = {
            {"Your Display Name", "your_module"},
        };
        
        // ... rest of function ...
    }
}
```

### Drive Example (line 6214):

```cpp
const std::map<const char*, const char*> audioInsertable = {
    {"VCF", "vcf"}, {"VCA", "vca"}, {"Delay", "delay"}, {"Reverb", "reverb"},
    {"Chorus", "chorus"}, {"Phaser", "phaser"}, {"Compressor", "compressor"},
    {"Recorder", "recorder"}, {"Limiter", "limiter"}, {"Gate", "gate"}, 
    {"Drive", "drive"},  // <-- Drive entry
    {"Graphic EQ", "graphic_eq"}, {"Waveshaper", "waveshaper"},
    // ... more entries ...
};
```

### Notes:
- Use `audioInsertable` for audio path modules
- Use `modInsertable` for CV/modulation path modules
- Format: `{"Display Name", "internal_type"}`

---

## 9. Search System

The search system has **three** components that need updating:

### 9.1. Search Database (Fuzzy Search)

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the search database initialization (~line 7150-7250)

**What to add:**

```cpp
// Inside the allModulesSearch map
static const std::map<const char*, std::pair<const char*, const char*>> allModulesSearch = {
    // ... existing entries ...
    
    // ADD YOUR MODULE:
    // Format: {"Display Name", {"internal_type", "description"}}
    {"Your Module", {"your_module", "Brief description for search results"}},
};
```

### Drive Example (line 7170):

```cpp
{"Drive", {"drive", "Distortion/overdrive"}},
```

### 9.2. Category Matcher (Color Coding)

**File:** `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location:** Inside the `getModuleCategory()` function (~line 7050-7100)

**What to add:**

```cpp
ModuleCategory ImGuiNodeEditorComponent::getModuleCategory(const juce::String& moduleName)
{
    juce::String lower = moduleName.toLowerCase();
    
    // ... existing checks ...
    
    // --- Effects (Red) ---
    if (lower.contains("vcf") || lower.contains("delay") || 
        lower.contains("drive") ||  // <-- Drive is here
        lower.contains("your_keyword") ||  // ADD YOUR KEYWORD
        // ... more keywords ...
        )
        return ModuleCategory::Effect;
    
    // ... more categories ...
}
```

### Drive Example (line 7070):

```cpp
if (lower.contains("vcf") || lower.contains("delay") || 
    lower.contains("reverb") || lower.contains("chorus") || 
    lower.contains("phaser") || lower.contains("compressor") || 
    lower.contains("drive") || lower.contains("shaper") ||  // Drive is here
    lower.contains("filter") || lower.contains("waveshaper") ||
    // ... more keywords ...
    return ModuleCategory::Effect;
```

### Available Categories:
- `ModuleCategory::Source` - Green
- `ModuleCategory::Effect` - Red
- `ModuleCategory::Modulator` - Blue
- `ModuleCategory::Utility` - Orange
- `ModuleCategory::Analysis` - Purple
- `ModuleCategory::Comment` - Grey
- `ModuleCategory::Plugin` - Teal

---

## Complete Checklist for Adding a New Module

Use this checklist to ensure you've registered your module everywhere:

- [ ] **1. Module Processor Files Created**
  - [ ] `YourModuleProcessor.h` created
  - [ ] `YourModuleProcessor.cpp` created
  - [ ] `getName()` returns correct identifier
  - [ ] Includes added to relevant build files

- [ ] **2. Factory Registration** (`ModularSynthProcessor.cpp` ~line 705)
  - [ ] `reg("your_module", []{ return std::make_unique<YourModuleProcessor>(); });`

- [ ] **3. Pin Database** (`PinDatabase.cpp` ~line 277)
  - [ ] Input pins defined
  - [ ] Output pins defined
  - [ ] Node width set appropriately

- [ ] **4. Module Description** (`PinDatabase.cpp` ~line 39)
  - [ ] Tooltip description added

- [ ] **5. Left Panel Menu** (`ImGuiNodeEditorComponent.cpp` ~line 1475)
  - [ ] `addModuleButton("Display Name", "your_module");` added to correct category

- [ ] **6. Right-Click Menu** (`ImGuiNodeEditorComponent.cpp` ~line 3480)
  - [ ] `if (ImGui::MenuItem("Display Name")) addAtMouse("your_module");` added

- [ ] **7. Top Bar Insert Between Menu** (`ImGuiNodeEditorComponent.cpp` ~line 703)
  - [ ] `if (ImGui::MenuItem("Display Name")) { insertNodeBetween("your_module"); }` added

- [ ] **8. Insert on Cable Menu** (`ImGuiNodeEditorComponent.cpp` ~line 6214)
  - [ ] Entry added to `audioInsertable` or `modInsertable` map

- [ ] **9. Search System** (`ImGuiNodeEditorComponent.cpp`)
  - [ ] Search database entry added (~line 7170)
  - [ ] Category keyword added (~line 7070)

---

## Naming Conventions

### Internal Type Names (used in code):
- **Format:** lowercase with underscores
- **Examples:** `drive`, `graphic_eq`, `sample_loader`, `midi_player`
- **Used in:** Factory registration, `getName()`, pin database, all menu registrations

### Display Names (shown to users):
- **Format:** Title Case with spaces
- **Examples:** "Drive", "Graphic EQ", "Sample Loader", "MIDI Player"
- **Used in:** Menu items, buttons, search results

### Key Matching Rule:
```cpp
// In YourModuleProcessor.h
const juce::String getName() const override { return "your_module"; }  // Must match factory key

// In ModularSynthProcessor.cpp
reg("your_module", []{ return std::make_unique<YourModuleProcessor>(); });  // Must match getName()

// In PinDatabase.cpp
db["your_module"] = ModulePinInfo(...);  // Must match factory key
```

---

## Common Pitfalls

### ❌ Mistake 1: Inconsistent Naming
```cpp
// BAD - Different names in different places
getName() returns "YourModule"
Factory registered as "your_module"
Pin database uses "yourmodule"
```

✅ **Solution:** Use the exact same internal name everywhere (lowercase with underscores)

### ❌ Mistake 2: Missing from Search
```cpp
// BAD - Added to menus but not to search database
// User searches for "drive" and nothing appears
```

✅ **Solution:** Always add to all 3 search components (database, category, keywords)

### ❌ Mistake 3: Wrong Category in Left Panel
```cpp
// BAD - Drive module added to "Utilities" instead of "Effects"
// Users can't find it where they expect
```

✅ **Solution:** Choose the category that best matches the module's primary function

### ❌ Mistake 4: Forgetting Insert on Cable Menu
```cpp
// BAD - Can't insert the module on an existing cable
// Forces users to disconnect and reconnect manually
```

✅ **Solution:** Add to both top bar "Insert Between" AND right-click on cable menu

---

## Testing Your New Module

After adding your module, test that it appears in all locations:

1. **Left Panel**: Click the category, verify button appears
2. **Right-Click Empty Space**: Check "Add Node" submenu
3. **Top Bar Insert Between**: Select a node, check menu appears
4. **Right-Click Cable**: Right-click a cable, verify module in list
5. **Search**: Type module name in search box, verify it appears
6. **Factory**: Actually create the module and verify it loads
7. **Pin Database**: Verify pins appear correctly in the node
8. **Tooltip**: Hover over the module, verify description appears

---

## Example: Complete Registration for Drive Module

Here's a complete reference showing Drive in all 9 locations:

### 1. Module Files
- `juce/Source/audio/modules/DriveModuleProcessor.h`
- `juce/Source/audio/modules/DriveModuleProcessor.cpp`
- `getName()` returns `"drive"`

### 2. Factory (line 705)
```cpp
reg("drive", []{ return std::make_unique<DriveModuleProcessor>(); });
```

### 3. Pin Database (line 277)
```cpp
db["drive"] = ModulePinInfo(
    NodeWidth::Small,
    { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
    { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
    {}
);
```

### 4. Description (line 39)
```cpp
descriptions["drive"] = "A waveshaping distortion effect.";
```

### 5. Left Panel (line 1475)
```cpp
addModuleButton("Drive", "drive");  // In Effects section
```

### 6. Right-Click Menu (line 3480)
```cpp
if (ImGui::MenuItem("Drive")) addAtMouse("drive");  // In Effects submenu
```

### 7. Top Bar Insert Between (line 703)
```cpp
if (ImGui::MenuItem("Drive")) { insertNodeBetween("drive"); }  // In Audio Path
```

### 8. Insert on Cable (line 6214)
```cpp
{"Drive", "drive"},  // In audioInsertable map
```

### 9. Search System
- **Database** (line 7170): `{"Drive", {"drive", "Distortion/overdrive"}}`
- **Category** (line 7070): `lower.contains("drive")`

---

## Advanced: Auto-Connection Features

If you want to add a button in your module that auto-creates and connects other modules (like the "Build Drum Kit" button), see:

- **Guide:** `guides/STROKE_SEQUENCER_BUILD_DRUM_KIT_GUIDE.md`
- **Example:** StrokeSequencerModuleProcessor

### Quick Overview:
1. Add an `std::atomic<bool>` flag to your processor
2. Set the flag when button is clicked in `drawParametersInNode()`
3. Check the flag in `ImGuiNodeEditorComponent::checkForAutoConnectionRequests()`
4. Implement a handler function to create and connect modules

---

## Conclusion

Adding a new module requires updating **9 distinct locations** across **3 files**:

**Files to modify:**
1. `juce/Source/audio/modules/YourModuleProcessor.h` (new file)
2. `juce/Source/audio/modules/YourModuleProcessor.cpp` (new file)
3. `juce/Source/audio/graph/ModularSynthProcessor.cpp` (factory)
4. `juce/Source/preset_creator/PinDatabase.cpp` (pins + description)
5. `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` (5 menu locations + search)

**Total edits needed:**
- 2 new files (module implementation)
- 1 factory registration
- 1 pin database entry
- 1 description
- 5 menu locations
- 3 search components

Follow this guide systematically and use the checklist to ensure your module appears everywhere users expect to find it!

---

**Last Updated:** October 30, 2025  
**Reference Module:** Drive (`DriveModuleProcessor`)  
**Author:** Collider Modular Synthesizer Project

