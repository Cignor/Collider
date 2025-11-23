# VST Browser Unification Plan

## Overview
Unify VST plugin scanning with the existing Presets/Samples/MIDI browser pattern to provide consistent UX and functionality.

---

## Phase 1: Analysis & Preparation

### 1.1 Current VST Functionality to Preserve

**Critical Features:**
- ✅ Plugin scanning via `PluginDirectoryScanner` (synchronous, uses JUCE's scanner)
- ✅ Plugin list storage in `KnownPluginList` (persisted to XML)
- ✅ Plugin filtering by VST folder location (`exe/VST`)
- ✅ Adding plugins as nodes via `synth->addVstModule(formatManager, desc)`
- ✅ Plugin info display (tooltips with name, manufacturer, version, etc.)
- ✅ Deduplication logic (name + manufacturer as unique key)
- ✅ Access to `PluginFormatManager` and `KnownPluginList` from application

**Current Locations:**
- Menu item: `File > Scan for Plugins...` (lines ~1124-1194 in ImGuiNodeEditorComponent.cpp)
- Plugin list display: `addPluginModules()` function (lines ~11030-11130)
- Plugin filtering: Hardcoded to `exe/VST` folder
- Storage: `appDataDir/known_plugins.xml`

### 1.2 Current Browser Pattern (Presets/Samples/MIDI)

**Common Structure:**
- Manager class with `DirectoryNode` tree structure
- `scanDirectory()`, `clearCache()`, `getRootNode()` methods
- Sidebar UI: Path display, "Change Path", "Scan", Search, Tree view
- Path stored in properties file
- Default paths: `exe/presets`, `exe/samples`, `exe/midi`

---

## Phase 2: Create VstManager Class

### 2.1 Create `VstManager.h` and `VstManager.cpp`

**Location:** `juce/Source/preset_creator/VstManager.h` and `.cpp`

**Structure:**
```cpp
class VstManager
{
public:
    struct VstInfo
    {
        juce::PluginDescription description;
        juce::String name;           // Plugin name
        juce::String manufacturer;   // Manufacturer name
        juce::String version;        // Plugin version
        juce::File pluginFile;       // Path to .vst3 file
        bool isInstrument;           // Instrument vs Effect
        int numInputs;               // Input channels
        int numOutputs;              // Output channels
    };

    struct DirectoryNode
    {
        juce::String name;           // Folder name or "Manufacturer Name"
        juce::File directory;        // Physical directory (if grouping by folder)
        std::vector<VstInfo> plugins;
        std::vector<std::unique_ptr<DirectoryNode>> subdirectories;
    };

    VstManager();
    DirectoryNode* getRootNode() const;
    void scanDirectory(const juce::File& directory, 
                      juce::AudioPluginFormatManager& formatManager,
                      juce::KnownPluginList& knownPluginList);
    void clearCache();
    
    // Search functionality
    std::vector<VstInfo> searchPlugins(const juce::String& searchTerm) const;
    
private:
    void scanRecursively(DirectoryNode* node, 
                        juce::AudioPluginFormatManager& formatManager,
                        juce::KnownPluginList& knownPluginList,
                        const juce::File& scanPath);
    
    std::unique_ptr<DirectoryNode> rootNode;
};
```

**Key Implementation Details:**
- **Tree Organization:** Group plugins by manufacturer (primary) or folder structure (fallback)
- **Scanning:** Use `PluginDirectoryScanner` internally, but build tree structure from results
- **Filtering:** Only include plugins whose file path is within the scanned directory
- **Deduplication:** Use name + manufacturer as unique key (preserve existing logic)

### 2.2 Integration Points

**Dependencies:**
- `PresetCreatorApplication::getApp()` - Access to `PluginFormatManager` and `KnownPluginList`
- `ModularSynthProcessor::addVstModule()` - For adding plugins as nodes
- Properties file - For storing scan path

---

## Phase 3: Sidebar Integration

### 3.1 Add VST Browser to Sidebar

**Location:** `ImGuiNodeEditorComponent.cpp` - After MIDI browser section

**UI Components (matching existing pattern):**
1. **Path Display** (read-only InputText)
2. **"Change Path" Button** - Opens FileChooser for directory selection
3. **"Scan" Button** - Triggers plugin scan
4. **Search Bar** - Filters plugins by name/manufacturer
5. **Tree View** - Hierarchical display (grouped by manufacturer)

**Code Structure:**
```cpp
// === VST BROWSER ===
pushHeaderColors(theme.headers.vst);  // Need to add VST header color
bool vstExpanded = ImGui::CollapsingHeader("VST");
ImGui::PopStyleColor(4);
if (vstExpanded)
{
    // 1. Path Display
    // 2. Change Path Button
    // 3. Scan Button
    // 4. Search Bar
    // 5. Tree View (drawVstTree)
}
```

### 3.2 Add Member Variables

**In `ImGuiNodeEditorComponent.h`:**
```cpp
VstManager m_vstManager;
juce::File m_vstScanPath;
juce::String m_vstSearchTerm;
std::unique_ptr<juce::FileChooser> vstPathChooser;
```

### 3.3 Initialize VST Path

**In `ImGuiNodeEditorComponent` constructor:**
- Load `vstScanPath` from properties (default: `exe/vst`)
- Migration: If old path exists, migrate to new location
- Create directory if it doesn't exist

---

## Phase 4: Migration Steps

### 4.1 Step-by-Step Implementation

**Step 1: Create VstManager Class**
- [ ] Create `VstManager.h` with structure matching other managers
- [ ] Create `VstManager.cpp` with implementation
- [ ] Implement `scanDirectory()` using `PluginDirectoryScanner`
- [ ] Build `DirectoryNode` tree from scan results
- [ ] Implement search functionality
- [ ] Test VstManager in isolation

**Step 2: Add Sidebar UI**
- [ ] Add VST browser section to sidebar (after MIDI)
- [ ] Add path display, buttons, search bar
- [ ] Implement `drawVstTree()` lambda (similar to other trees)
- [ ] Add drag-and-drop support for plugins (optional enhancement)

**Step 3: Integrate with Existing Systems**
- [ ] Connect VstManager to `PluginFormatManager` and `KnownPluginList`
- [ ] Update `addPluginModules()` to use VstManager (or keep as fallback)
- [ ] Ensure plugin adding still works via tree selection
- [ ] Preserve tooltip functionality

**Step 4: Path Management**
- [ ] Add `vstScanPath` initialization in constructor
- [ ] Add path migration logic (if old `exe/VST` path exists)
- [ ] Save/load path from properties file
- [ ] Implement "Change Path" button handler

**Step 5: Remove/Update Old Menu Item**
- [ ] Keep menu item as backup OR remove it
- [ ] Update any references to old scanning method
- [ ] Ensure `KnownPluginList` persistence still works

**Step 6: Testing & Validation**
- [ ] Test plugin scanning from new sidebar
- [ ] Test path changing
- [ ] Test search functionality
- [ ] Test adding plugins as nodes
- [ ] Test with empty VST folder
- [ ] Test with plugins in subdirectories
- [ ] Verify plugin list persistence

---

## Phase 5: Functionality Preservation Checklist

### 5.1 Core Functionality
- [ ] **Plugin Scanning:** `PluginDirectoryScanner` still used internally
- [ ] **Plugin Storage:** `KnownPluginList` still populated and saved to XML
- [ ] **Plugin Filtering:** Only plugins in configured VST folder are shown
- [ ] **Plugin Adding:** `synth->addVstModule()` still works
- [ ] **Deduplication:** Name + manufacturer deduplication preserved
- [ ] **Plugin Info:** Tooltips with plugin details still shown

### 5.2 Enhanced Functionality (New)
- [ ] **Configurable Path:** User can change VST scan path
- [ ] **Search:** Filter plugins by name/manufacturer
- [ ] **Tree View:** Hierarchical display (grouped by manufacturer)
- [ ] **Consistent UX:** Matches Presets/Samples/MIDI browser pattern

### 5.3 Backward Compatibility
- [ ] **Old Path Migration:** If `exe/VST` exists, migrate to `exe/vst` (lowercase)
- [ ] **Plugin List:** Existing `known_plugins.xml` still loads correctly
- [ ] **Menu Item:** Keep as backup or document removal

---

## Phase 6: Implementation Details

### 6.1 VstManager::scanDirectory() Implementation

```cpp
void VstManager::scanDirectory(const juce::File& directory,
                               juce::AudioPluginFormatManager& formatManager,
                               juce::KnownPluginList& knownPluginList)
{
    // 1. Find VST3 format
    juce::VST3PluginFormat* vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        if (auto* format = formatManager.getFormat(i); format->getName() == "VST3")
        {
            vst3Format = dynamic_cast<juce::VST3PluginFormat*>(format);
            break;
        }
    }
    
    if (!vst3Format) return;
    
    // 2. Setup scanner
    juce::FileSearchPath searchPath;
    searchPath.add(directory);
    
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile(PresetCreatorApplication::getApp().getApplicationName());
    
    juce::PluginDirectoryScanner scanner(knownPluginList, *vst3Format, searchPath, true,
                                         appDataDir.getChildFile("dead_plugins.txt"), true);
    
    // 3. Perform scan
    juce::String pluginBeingScanned;
    while (scanner.scanNextFile(true, pluginBeingScanned))
    {
        // Scan happens here - results go to knownPluginList
    }
    
    // 4. Build tree from knownPluginList
    rootNode = std::make_unique<DirectoryNode>();
    rootNode->name = directory.getFileName();
    rootNode->directory = directory;
    
    // 5. Group plugins by manufacturer
    std::map<juce::String, std::vector<juce::PluginDescription>> byManufacturer;
    for (const auto& desc : knownPluginList.getTypes())
    {
        juce::File pluginFile(desc.fileOrIdentifier);
        if (pluginFile.existsAsFile() && 
            (pluginFile.getParentDirectory().isAChildOf(directory) || 
             pluginFile.getParentDirectory() == directory))
        {
            byManufacturer[desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName].push_back(desc);
        }
    }
    
    // 6. Build tree structure
    for (const auto& [manufacturer, plugins] : byManufacturer)
    {
        auto manufacturerNode = std::make_unique<DirectoryNode>();
        manufacturerNode->name = manufacturer;
        
        for (const auto& desc : plugins)
        {
            VstInfo info;
            info.description = desc;
            info.name = desc.name;
            info.manufacturer = desc.manufacturerName;
            info.version = desc.version;
            info.pluginFile = juce::File(desc.fileOrIdentifier);
            info.isInstrument = desc.isInstrument;
            info.numInputs = desc.numInputChannels;
            info.numOutputs = desc.numOutputChannels;
            manufacturerNode->plugins.push_back(info);
        }
        
        rootNode->subdirectories.push_back(std::move(manufacturerNode));
    }
}
```

### 6.2 Tree Drawing Function

```cpp
std::function<void(const VstManager::DirectoryNode*)> drawVstTree = 
    [&](const VstManager::DirectoryNode* node)
{
    if (!node || (node->plugins.empty() && node->subdirectories.empty())) return;
    
    // Draw subdirectories (manufacturers) first
    for (const auto& subdir : node->subdirectories)
    {
        if (ImGui::TreeNode(subdir->name.toRawUTF8()))
        {
            drawVstTree(subdir.get());
            ImGui::TreePop();
        }
    }
    
    // Draw plugins in this node
    for (const auto& plugin : node->plugins)
    {
        if (m_vstSearchTerm.isEmpty() || 
            plugin.name.containsIgnoreCase(m_vstSearchTerm) ||
            plugin.manufacturer.containsIgnoreCase(m_vstSearchTerm))
        {
            juce::String displayName = plugin.name;
            if (plugin.manufacturer.isNotEmpty())
                displayName += " (" + plugin.manufacturer + ")";
            
            if (ImGui::Selectable(displayName.toRawUTF8()))
            {
                auto& app = PresetCreatorApplication::getApp();
                auto nodeId = synth->addVstModule(app.getPluginFormatManager(), plugin.description);
                if (nodeId.uid != 0)
                {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const auto logicalId = synth->getLogicalIdForNode(nodeId);
                    pendingNodeScreenPositions[(int)logicalId] = mouse;
                    snapshotAfterEditor = true;
                }
            }
            
            // Tooltip
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Name: %s", plugin.name.toRawUTF8());
                ImGui::Text("Manufacturer: %s", plugin.manufacturer.toRawUTF8());
                ImGui::Text("Version: %s", plugin.version.toRawUTF8());
                ImGui::Text("Type: %s", plugin.isInstrument ? "Instrument" : "Effect");
                ImGui::Text("Inputs: %d, Outputs: %d", plugin.numInputs, plugin.numOutputs);
                ImGui::EndTooltip();
            }
        }
    }
};
```

---

## Phase 7: Testing Plan

### 7.1 Unit Tests
- [ ] VstManager scans directory correctly
- [ ] Tree structure built correctly (grouped by manufacturer)
- [ ] Search filters plugins correctly
- [ ] Path migration works

### 7.2 Integration Tests
- [ ] Sidebar displays VST browser
- [ ] "Change Path" button works
- [ ] "Scan" button triggers scan
- [ ] Plugins appear in tree after scan
- [ ] Clicking plugin adds it as node
- [ ] Search filters tree correctly
- [ ] Path persists across restarts

### 7.3 Edge Cases
- [ ] Empty VST folder
- [ ] VST folder with subdirectories
- [ ] Plugins with same name, different manufacturers
- [ ] Invalid plugin files
- [ ] Network drive VST folder
- [ ] Very large plugin collections

---

## Phase 8: Rollback Plan

### 8.1 If Issues Arise
1. **Keep old menu item** as backup during transition
2. **Feature flag:** Add compile-time or runtime flag to switch between old/new system
3. **Gradual migration:** Keep both systems running in parallel initially

### 8.2 Data Preservation
- `KnownPluginList` XML file is independent of UI - no risk of data loss
- Path migration is one-way but reversible (just change path back)

---

## Phase 9: Documentation Updates

### 9.1 Code Comments
- [ ] Document VstManager class
- [ ] Document tree organization (manufacturer grouping)
- [ ] Document integration points

### 9.2 User Documentation
- [ ] Update user manual with new VST browser location
- [ ] Document path configuration
- [ ] Document search functionality

---

## Phase 10: Cleanup (After Validation)

### 10.1 Remove Old Code
- [ ] Remove menu item "Scan for Plugins..." (or keep as backup)
- [ ] Remove hardcoded `exe/VST` references
- [ ] Update `addPluginModules()` if needed (or keep as fallback)

### 10.2 Code Consolidation
- [ ] Ensure all plugin access goes through VstManager
- [ ] Remove duplicate plugin filtering logic

---

## Success Criteria

✅ **Functionality Preserved:**
- All existing VST functionality works as before
- Plugin scanning, storage, and loading unchanged
- Plugin adding as nodes works identically

✅ **New Functionality Added:**
- Configurable VST scan path
- Search functionality
- Tree view with manufacturer grouping
- Consistent UX with other browsers

✅ **Code Quality:**
- Follows existing manager pattern
- No code duplication
- Proper error handling
- Performance acceptable (scanning may be slow, but UI responsive)

---

## Estimated Implementation Time

- **VstManager class:** 2-3 hours
- **Sidebar integration:** 1-2 hours
- **Path management:** 1 hour
- **Testing & debugging:** 2-3 hours
- **Total:** ~6-9 hours

---

## Notes

- Plugin scanning is inherently slow (JUCE limitation) - UI should show progress/status
- Consider async scanning in future (but out of scope for this unification)
- Manufacturer grouping is a design choice - could also group by folder structure
- Keep menu item as backup initially, remove after validation

