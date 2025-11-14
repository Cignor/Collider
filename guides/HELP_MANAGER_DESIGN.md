# Help Manager System - Design Concept

## Overview

A centralized, tabbed help system that consolidates documentation, shortcuts, and user assistance into a single, modern interface accessible from the Help menu.

## Architecture

### Core Component: `HelpManagerComponent`

Similar to `ThemeEditorComponent`, this will be a self-contained component that:
- Manages its own window state (`m_isOpen`)
- Renders a tabbed interface using `ImGui::BeginTabBar()`
- Handles content rendering for each tab
- Integrates with existing systems (ShortcutManager, file system for markdown)

### Location in Menu

**Current**: `Settings > Keyboard Shortcuts...`  
**New**: `Help > Help Manager...` (or `Help > Help...`)

The shortcut manager will move from Settings to Help, and become a tab within the Help Manager.

## Tab Structure

### Tab 1: "Shortcuts" 
**Purpose**: Keyboard shortcut management and customization

**Content**:
- Integrate existing `ShortcutManager` functionality
- Same table view as current implementation
- Context selection, search, assign/clear/reset buttons
- All existing shortcut editor features

**Implementation**:
- Reuse `renderShortcutEditorContents()` and related functions
- Remove the modal popup wrapper
- Render directly in tab content area

### Tab 2: "Node Dictionary"
**Purpose**: Interactive documentation browser for all available nodes

**Features**:
- **Markdown Renderer**: Parse and display `Nodes_Dictionary.md`
- **Collapsible Sections**: Use `ImGui::CollapsingHeader()` for each node category
- **Search Functionality**: Filter nodes by name, category, or description
- **Quick Navigation**: Table of contents with jump links
- **Syntax Highlighting**: Code blocks, parameter lists, examples
- **Copy to Clipboard**: Copy node names, examples, etc.

**Content Structure**:
```
[Search Box]
[Table of Contents - Collapsible]
  ├─ Source Nodes
  │   ├─ VCO (collapsible)
  │   ├─ PolyVCO (collapsible)
  │   └─ ...
  ├─ Effect Nodes
  ├─ Modulator Nodes
  └─ ...
```

**Markdown Parsing**:
- Parse headers (`##`, `###`) as collapsible sections
- Parse code blocks (```) with syntax highlighting
- Parse lists and tables
- Parse links and anchors
- Handle special formatting (bold, italic, inline code)

### Tab 3: "Getting Started"
**Purpose**: Tutorials and quick-start guides

**Content**:
- Welcome message
- Quick start tutorial (step-by-step)
- Common workflows
- Tips and tricks
- Video links (if applicable)
- Keyboard shortcuts cheat sheet (quick reference)

**Features**:
- Step-by-step walkthroughs
- Collapsible sections for different topics
- Visual examples/diagrams (if possible)
- Links to external resources

### Tab 4: "About"
**Purpose**: Application information and credits

**Content**:
- Application name and version
- Build information
- Credits and acknowledgments
- License information
- System information (optional)
- Links to:
  - GitHub repository
  - Documentation website
  - Community forums
  - Bug tracker

**Features**:
- Version display
- Copy system info button
- Links to external resources

### Tab 5: "FAQ" (Optional)
**Purpose**: Frequently asked questions

**Content**:
- Common questions and answers
- Troubleshooting tips
- Known issues
- Performance tips
- Collapsible Q&A format

## Technical Implementation

### Class Structure

```cpp
class HelpManagerComponent
{
public:
    HelpManagerComponent(ImGuiNodeEditorComponent* parent);
    void open();
    void close();
    void render();
    
private:
    // Window state
    bool m_isOpen = false;
    int m_currentTab = 0;
    
    // Shortcuts tab
    void renderShortcutsTab();
    
    // Node Dictionary tab
    void renderNodeDictionaryTab();
    void parseMarkdownFile(const juce::File& file);
    void renderMarkdownContent();
    juce::String m_searchTerm;
    std::vector<MarkdownSection> m_parsedSections;
    
    // Getting Started tab
    void renderGettingStartedTab();
    
    // About tab
    void renderAboutTab();
    
    // FAQ tab (optional)
    void renderFaqTab();
    
    // Markdown parsing helpers
    struct MarkdownSection
    {
        juce::String title;
        int level; // 1, 2, 3 for ##, ###, ####
        juce::String content;
        std::vector<MarkdownSection> children;
    };
    
    ImGuiNodeEditorComponent* parentEditor;
};
```

### Markdown Rendering Strategy

**Option 1: Simple Parser (Recommended for MVP)**
- Parse markdown on load
- Convert to ImGui widgets:
  - Headers → `CollapsingHeader` or `Text` with styling
  - Code blocks → `InputTextMultiline` with read-only flag
  - Lists → `BulletText()` or `Text()`
  - Links → `Text` with hover tooltip or clickable (if we add URL handling)

**Option 2: Full Markdown Library**
- Use a C++ markdown parser (e.g., `cmark`, `markdown-it`)
- More complex but handles edge cases better
- Better for future extensibility

**Recommendation**: Start with Option 1, upgrade if needed.

### Search Implementation

For Node Dictionary:
```cpp
// Filter sections based on search term
bool matchesSearch(const MarkdownSection& section, const juce::String& term)
{
    return section.title.containsIgnoreCase(term) 
        || section.content.containsIgnoreCase(term);
}

// When rendering, skip non-matching sections
// Auto-expand sections that match
```

### Integration Points

1. **Menu Integration**:
   ```cpp
   // In ImGuiNodeEditorComponent::renderMenuBar()
   if (ImGui::BeginMenu("Help"))
   {
       if (ImGui::MenuItem("Help Manager...", "F1"))
       {
           helpManager.open();
       }
       ImGui::Separator();
       // Keep existing "Keyboard Shortcuts" as shortcut to that tab?
       // Or remove it entirely?
   }
   ```

2. **Shortcut Integration**:
   - F1 opens Help Manager (currently opens old shortcuts window)
   - Focus on "Shortcuts" tab if opened via F1
   - Focus on "Node Dictionary" tab if opened from context menu

3. **Context Menu Integration**:
   - Right-click node → "View in Dictionary" → Opens Help Manager to Node Dictionary tab, scrolls to that node

## UI/UX Considerations

### Window Behavior
- **Size**: Large enough for content (800x600 minimum, resizable)
- **Position**: Centered on first open, remembers position
- **Modal**: Non-modal window (allows interaction with main editor)
- **Background**: Uses theme's `WindowBg` color (fully opaque)

### Tab Design
- Use `ImGuiTabBarFlags_Reorderable` for user customization?
- Icons for tabs? (optional, adds complexity)
- Keyboard shortcuts to switch tabs? (Ctrl+1, Ctrl+2, etc.)

### Search UX
- Real-time filtering as user types
- Highlight matching text
- "No results" message
- Clear search button (X icon)

### Markdown Rendering UX
- Smooth scrolling
- Auto-expand matching sections when searching
- "Back to top" button for long content
- Copy code blocks button
- Syntax highlighting for code (basic color coding)

## Future Enhancements

1. **Interactive Examples**: Run code snippets in sandbox
2. **Video Embedding**: If we add video support
3. **User Notes**: Allow users to add personal notes to documentation
4. **Bookmarks**: Save favorite sections
5. **Print/Export**: Export documentation to PDF
6. **Multi-language Support**: If we internationalize
7. **Dark/Light Mode Toggle**: Quick theme switcher in About tab
8. **Update Checker**: Check for new versions in About tab

## Migration Path

1. **Phase 1**: Create `HelpManagerComponent` with Shortcuts and About tabs
2. **Phase 2**: Add Node Dictionary tab with basic markdown rendering
3. **Phase 3**: Add Getting Started and FAQ tabs
4. **Phase 4**: Enhance markdown rendering, add search
5. **Phase 5**: Add context menu integration, polish UX

## Files to Create/Modify

### New Files
- `juce/Source/preset_creator/HelpManagerComponent.h`
- `juce/Source/preset_creator/HelpManagerComponent.cpp`
- `juce/Source/preset_creator/MarkdownParser.h` (optional, if we create a parser)
- `juce/Source/preset_creator/MarkdownParser.cpp`

### Modified Files
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h` - Add HelpManagerComponent member
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` - Integrate into menu, remove old shortcuts window
- `juce/CMakeLists.txt` - Add new source files

## Design Principles

1. **Consistency**: Follow existing patterns (ThemeEditorComponent)
2. **Performance**: Lazy-load markdown content, cache parsed results
3. **Accessibility**: Keyboard navigation, clear visual hierarchy
4. **Maintainability**: Separate concerns (parsing, rendering, UI)
5. **Extensibility**: Easy to add new tabs or content types

