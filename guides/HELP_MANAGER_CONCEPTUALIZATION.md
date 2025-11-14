# Help Manager System - Conceptualization Summary

## Executive Summary

A centralized, tabbed help system that consolidates all user assistance into a single, modern interface. The Help Manager will replace the current modal shortcuts window and add comprehensive documentation browsing capabilities.

## Core Concept

**Single Window, Multiple Tabs** - Following the pattern established by `ThemeEditorComponent`, the Help Manager will be a non-modal window with a tabbed interface, allowing users to access shortcuts, documentation, tutorials, and app information from one place.

## Tab Structure & Content

### Tab 1: "Shortcuts" ⌨️
**Purpose**: Keyboard shortcut management

**Content**:
- Full integration of existing `ShortcutManager` functionality
- Table view: Category | Action | Binding | Source | Options
- Context selector (Global, Node Editor)
- Search box for filtering actions
- Assign/Clear/Reset buttons per action
- Save Changes button

**Implementation Notes**:
- Reuse `renderShortcutEditorContents()` from `ImGuiNodeEditorComponent`
- Remove modal popup wrapper (`BeginPopupModal`)
- Render directly in tab content area
- Maintain all existing functionality

### Tab 2: "Node Dictionary" 📚
**Purpose**: Interactive documentation browser

**Key Features**:
1. **Markdown Parser**
   - Parse `Nodes_Dictionary.md` on first open
   - Convert headers to collapsible sections
   - Parse code blocks, lists, tables
   - Build hierarchical structure

2. **Search Functionality**
   - Real-time filtering as user types
   - Search in: node names, categories, descriptions
   - Auto-expand matching sections
   - Highlight matching text (if possible)
   - "No results" message

3. **Navigation**
   - Table of Contents (collapsible sidebar or top section)
   - Jump links to major sections
   - Breadcrumb navigation (optional)
   - "Back to top" button

4. **Rendering**
   - Headers → `CollapsingHeader` or styled `Text`
   - Code blocks → `InputTextMultiline` (read-only) with copy button
   - Lists → `BulletText()` or `Text()` with indentation
   - Tables → `ImGui::BeginTable()` (if markdown has tables)
   - Links → Tooltip on hover, or clickable if we add URL handling

**Markdown Structure Example**:
```markdown
## VCO - Voltage-Controlled Oscillator
Description text...

### Parameters
- Frequency: CV input
- Waveform: Sine, Square, etc.

### Example Usage
```cpp
// Code example
```
```

**Rendered As**:
```
[▼] VCO - Voltage-Controlled Oscillator
    Description text...
    [▼] Parameters
        • Frequency: CV input
        • Waveform: Sine, Square, etc.
    [▼] Example Usage
        [Code block with copy button]
```

### Tab 3: "Getting Started" 🚀
**Purpose**: Tutorials and onboarding

**Content Sections**:
1. **Welcome**
   - Brief intro to Collider
   - What makes it unique
   - Quick overview

2. **Quick Start Guide**
   - Step 1: Create your first patch
   - Step 2: Add nodes
   - Step 3: Connect nodes
   - Step 4: Play and experiment
   - Visual examples (screenshots or diagrams)

3. **Common Workflows**
   - Creating a bass patch
   - Building a drum machine
   - Using CV modulation
   - Video processing basics

4. **Tips & Tricks**
   - Keyboard shortcuts cheat sheet
   - Hidden features
   - Performance tips
   - Best practices

5. **Resources**
   - Video tutorials (links)
   - Community forums
   - Example patches
   - External documentation

**UI Pattern**:
- Collapsible sections for each topic
- Step-by-step numbered lists
- Visual separators between major sections
- Links to relevant nodes in Dictionary tab

### Tab 4: "About" ℹ️
**Purpose**: Application information

**Content**:
1. **Application Info**
   - Name: Collider Modular Synthesizer
   - Version: [from build system]
   - Build date: [from build system]
   - Platform: Windows/macOS/Linux

2. **Credits**
   - Developers
   - Contributors
   - Libraries used (JUCE, ImGui, ImNodes, etc.)
   - Special thanks

3. **License**
   - License type
   - Copyright notice
   - Link to full license text

4. **System Information** (optional)
   - OS version
   - CPU info
   - Memory info
   - Audio device info
   - Copy system info button

5. **Links**
   - GitHub repository
   - Documentation website
   - Community forums
   - Bug tracker
   - Donate/Support

**UI Pattern**:
- Clean, organized layout
- Version prominently displayed
- Collapsible sections for details
- External links clearly marked

### Tab 5: "FAQ" ❓ (Optional - Future)
**Purpose**: Common questions

**Content**:
- Q: How do I...?
- A: [Answer with links to relevant docs]
- Collapsible Q&A format
- Searchable
- Categories: Installation, Usage, Troubleshooting, etc.

## Technical Architecture

### Component Structure

```cpp
class HelpManagerComponent
{
public:
    HelpManagerComponent(ImGuiNodeEditorComponent* parent);
    void open();
    void close();
    void render();
    
    // Tab navigation
    void setActiveTab(int tabIndex);
    int getActiveTab() const { return m_currentTab; }
    
private:
    // Window state
    bool m_isOpen = false;
    int m_currentTab = 0;
    ImVec2 m_windowSize = ImVec2(900, 700);
    ImVec2 m_windowPos = ImVec2(100, 100);
    
    // Shortcuts tab
    void renderShortcutsTab();
    // Reuse existing shortcut editor functions
    
    // Node Dictionary tab
    void renderNodeDictionaryTab();
    void loadMarkdownFile();
    void parseMarkdown(const juce::String& content);
    void renderMarkdownSection(const MarkdownSection& section, int depth = 0);
    juce::String m_searchTerm;
    std::vector<MarkdownSection> m_parsedSections;
    bool m_markdownLoaded = false;
    
    // Getting Started tab
    void renderGettingStartedTab();
    
    // About tab
    void renderAboutTab();
    juce::String getVersionString() const;
    juce::String getBuildInfo() const;
    
    // Markdown data structures
    struct MarkdownSection
    {
        juce::String title;
        int level; // 1, 2, 3 for ##, ###, ####
        juce::String content; // Text before subsections
        std::vector<MarkdownSection> children;
        juce::String anchor; // For jump links
    };
    
    ImGuiNodeEditorComponent* parentEditor;
};
```

### Markdown Parsing Strategy

**Recommended Approach: Simple Custom Parser**

1. **Parse on First Open** (lazy load)
   - Read `Nodes_Dictionary.md` file
   - Parse line by line
   - Build hierarchical structure
   - Cache parsed result

2. **Parsing Rules**:
   - `## Header` → Level 1 section
   - `### Header` → Level 2 subsection
   - `#### Header` → Level 3 sub-subsection
   - Code blocks (```) → Extract and store separately
   - Lists (`-` or `*`) → Store as array
   - Links `[text](url)` → Extract text and URL
   - Bold/italic → Store with formatting markers

3. **Rendering**:
   - Level 1 → `CollapsingHeader` (always visible when expanded)
   - Level 2+ → Nested `CollapsingHeader` or `TreeNode`
   - Code blocks → `InputTextMultiline` with read-only + copy button
   - Lists → `BulletText()` or indented `Text()`
   - Links → `TextColored()` with hover tooltip

### Search Implementation

**Simple Text Matching**:
```cpp
bool matchesSearch(const MarkdownSection& section, const juce::String& term)
{
    if (term.isEmpty()) return true;
    
    juce::String lowerTerm = term.toLowerCase();
    return section.title.toLowerCase().contains(lowerTerm)
        || section.content.toLowerCase().contains(lowerTerm);
}

// When rendering, skip non-matching sections
// Auto-expand sections that match
```

**Performance Considerations**:
- Search is O(n) where n = number of sections
- For ~100 sections, this should be instant
- No need for complex indexing initially
- Can optimize later if needed

### Integration Points

1. **Menu Integration**:
```cpp
// In ImGuiNodeEditorComponent::renderMenuBar()
if (ImGui::BeginMenu("Help"))
{
    if (ImGui::MenuItem("Help Manager...", "F1"))
    {
        helpManager.open();
        helpManager.setActiveTab(0); // Shortcuts tab
    }
    ImGui::Separator();
    
    // Optional: Quick links to specific tabs
    if (ImGui::MenuItem("Keyboard Shortcuts", "F1"))
    {
        helpManager.open();
        helpManager.setActiveTab(0);
    }
    if (ImGui::MenuItem("Node Dictionary"))
    {
        helpManager.open();
        helpManager.setActiveTab(1);
    }
}
```

2. **F1 Shortcut**:
- Currently: Opens shortcuts window
- New: Opens Help Manager, focuses Shortcuts tab
- Update shortcut action to call `helpManager.open()` and `setActiveTab(0)`

3. **Context Menu Integration** (Future):
```cpp
// Right-click node → "View in Dictionary"
if (ImGui::MenuItem("View in Dictionary"))
{
    helpManager.open();
    helpManager.setActiveTab(1); // Node Dictionary tab
    helpManager.scrollToNode(nodeName); // Scroll to that node's section
}
```

## UI/UX Design

### Window Appearance
- **Size**: 900x700 (default), resizable
- **Position**: Centered on first open, remembers position
- **Modal**: Non-modal (allows interaction with editor)
- **Background**: Uses theme's `WindowBg` (fully opaque)
- **Title**: "Help Manager" or "▼ Help Manager"

### Tab Bar
- Horizontal tab bar at top
- Tab names: "Shortcuts", "Dictionary", "Getting Started", "About"
- Active tab highlighted
- Keyboard shortcuts: Ctrl+1, Ctrl+2, Ctrl+3, Ctrl+4 (optional)

### Content Area
- Scrollable content region
- Consistent padding and spacing
- Follows theme colors
- Responsive to window resize

### Search Box
- Prominent placement (top of Dictionary tab)
- Real-time filtering
- Clear button (X icon)
- Placeholder text: "Search nodes, categories, descriptions..."

## Implementation Phases

### Phase 1: Foundation (MVP)
1. Create `HelpManagerComponent` class
2. Implement window and tab structure
3. Integrate Shortcuts tab (migrate from modal)
4. Add About tab (basic info)
5. Menu integration

### Phase 2: Node Dictionary
1. Implement markdown parser
2. Render markdown as collapsible sections
3. Add search functionality
4. Add table of contents
5. Code block rendering with copy

### Phase 3: Getting Started
1. Create tutorial content
2. Implement Getting Started tab
3. Add visual examples
4. Links to external resources

### Phase 4: Polish
1. Context menu integration
2. Scroll-to-section functionality
3. Performance optimization
4. UI/UX refinements
5. Keyboard shortcuts for tabs

## Additional Considerations

### Performance
- **Lazy Loading**: Parse markdown only when Dictionary tab is first opened
- **Caching**: Cache parsed markdown structure (don't re-parse on every open)
- **Search**: Simple text matching is fast enough for ~100 sections
- **Rendering**: Only render visible sections (virtual scrolling if needed)

### Memory
- Parse markdown once, store in memory
- ~2500 lines ≈ ~100KB text, very manageable
- Clear cache on close? Or keep for faster reopening?

### Extensibility
- Easy to add new tabs
- Easy to add new content to existing tabs
- Markdown parser can be extended for new features
- Search can be enhanced with fuzzy matching, etc.

### Accessibility
- Keyboard navigation between tabs
- Keyboard shortcuts documented
- Clear visual hierarchy
- High contrast (follows theme)

## Success Metrics

1. ✅ Help Manager opens from Help menu
2. ✅ Shortcuts tab fully functional (migrated from Settings)
3. ✅ Node Dictionary renders markdown with search
4. ✅ Getting Started provides useful tutorials
5. ✅ About tab shows app information
6. ✅ F1 shortcut opens Help Manager
7. ✅ Performance is smooth (no lag)
8. ✅ UI is consistent with existing theme
9. ✅ Code follows existing patterns

## Next Steps

1. Review this conceptualization
2. Decide on markdown parsing approach (custom vs library)
3. Create `HelpManagerComponent` class structure
4. Implement Phase 1 (Foundation)
5. Test and iterate
6. Implement Phase 2 (Node Dictionary)
7. Continue with remaining phases

---

**Note**: This conceptualization is based on existing code patterns (`ThemeEditorComponent`), ImGui best practices (`imgui_demo.cpp`), and the project's design guide (`IMGUI_NODE_DESIGN_GUIDE.md`). The design prioritizes consistency, maintainability, and user experience.

