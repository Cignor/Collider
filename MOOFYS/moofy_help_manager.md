# Help Manager System - Context Bundle for External Expert

## Project Context

**Project**: Collider Modular Synthesizer  
**Language**: C++17  
**Framework**: JUCE + ImGui + ImNodes  
**Current Task**: Design and implement a centralized Help Manager system with tabbed interface

## Current Situation

### Existing Systems

1. **ShortcutManager** (`juce/Source/preset_creator/ShortcutManager.h/cpp`)
   - Fully functional data-driven shortcut system
   - Supports contexts, rebinding, persistence
   - Currently accessed via `Settings > Keyboard Shortcuts...` (modal popup)
   - Needs to be integrated as a tab in new Help Manager

2. **ThemeEditorComponent** (`juce/Source/preset_creator/theme/ThemeEditorComponent.h/cpp`)
   - Reference implementation for tabbed UI components
   - Uses `ImGui::BeginTabBar()` pattern
   - Manages window state, working copy, apply/reset logic
   - Good pattern to follow for HelpManagerComponent

3. **ImGuiNodeEditorComponent** (`juce/Source/preset_creator/ImGuiNodeEditorComponent.h/cpp`)
   - Main editor component
   - Contains menu bar with Help menu
   - Currently has `showShortcutsWindow` and `renderShortcutEditorWindow()`
   - Needs integration point for HelpManagerComponent

### Documentation Files

1. **Nodes_Dictionary.md** (`USER_MANUAL/Nodes_Dictionary.md`)
   - Large markdown file (~2500+ lines)
   - Contains documentation for all node types
   - Structured with headers (##, ###), code blocks, lists, tables
   - Needs to be rendered in a searchable, collapsible format

2. **IMGUI_NODE_DESIGN_GUIDE.md** (`guides/IMGUI_NODE_DESIGN_GUIDE.md`)
   - Design patterns for ImGui UI
   - Best practices for tabs, collapsing headers, etc.
   - Reference for UI implementation

## Requirements

### Primary Goals

1. **Create HelpManagerComponent** - Tabbed help system
2. **Move Shortcuts to Help Menu** - From Settings to Help
3. **Add Node Dictionary Tab** - Markdown renderer with search
4. **Add Additional Helpful Tabs** - Getting Started, About, FAQ

### Key Features Needed

#### Tab 1: Shortcuts
- Integrate existing ShortcutManager UI
- Remove modal popup wrapper
- Render directly in tab content

#### Tab 2: Node Dictionary
- Parse and render markdown file
- Collapsible sections (by header level)
- Search functionality (filter by name/category/description)
- Table of contents navigation
- Code block rendering
- Copy-to-clipboard for examples

#### Tab 3: Getting Started
- Tutorial content
- Step-by-step guides
- Tips and tricks
- Quick reference

#### Tab 4: About
- Version info
- Credits
- System info
- External links

## Technical Questions for Expert

1. **Markdown Rendering**:
   - Best approach for parsing markdown in C++/JUCE?
   - Should we use a library (cmark, markdown-it) or simple custom parser?
   - How to handle code syntax highlighting in ImGui?
   - Best way to render tables from markdown?

2. **Search Implementation**:
   - Real-time filtering performance with large markdown files?
   - Should we pre-index content or parse on-demand?
   - How to highlight search matches in rendered text?

3. **UI/UX Patterns**:
   - Best way to implement collapsible sections with ImGui?
   - How to handle deep nesting in markdown (multiple header levels)?
   - Scroll-to-section functionality - best approach?
   - Copy-to-clipboard for code blocks - JUCE clipboard API?

4. **Architecture**:
   - Should HelpManagerComponent be a singleton or instance?
   - How to handle markdown parsing - lazy load or parse on open?
   - Memory management for parsed markdown content?
   - Should we cache parsed markdown or re-parse each time?

5. **Integration**:
   - Best way to integrate with existing menu system?
   - How to handle F1 shortcut (currently opens shortcuts)?
   - Context menu integration - "View in Dictionary" from node right-click?

## Files to Review

### Core Architecture
- `juce/Source/preset_creator/ShortcutManager.h` - Shortcut system API
- `juce/Source/preset_creator/ShortcutManager.cpp` - Implementation
- `juce/Source/preset_creator/theme/ThemeEditorComponent.h` - Tabbed UI pattern
- `juce/Source/preset_creator/theme/ThemeEditorComponent.cpp` - Implementation
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h` - Main editor
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` - Menu integration

### Documentation
- `USER_MANUAL/Nodes_Dictionary.md` - Content to render
- `guides/IMGUI_NODE_DESIGN_GUIDE.md` - UI patterns
- `guides/HELP_MANAGER_DESIGN.md` - Design document (just created)

### Reference
- `imgui_demo.cpp` - ImGui examples and patterns
- `juce/Source/preset_creator/theme/ThemeManager.cpp` - Singleton pattern example

## Design Constraints

1. **Performance**: Must handle large markdown files smoothly
2. **Memory**: Efficient parsing and caching strategy
3. **UX**: Fast search, smooth scrolling, responsive UI
4. **Maintainability**: Easy to add new tabs or content
5. **Consistency**: Follow existing code patterns (ThemeEditorComponent style)

## Success Criteria

- Help Manager opens from Help menu
- Shortcuts tab fully functional (migrated from Settings)
- Node Dictionary tab renders markdown with search
- Getting Started and About tabs provide useful content
- F1 shortcut opens Help Manager
- Context menu integration works ("View in Dictionary")
- Performance is smooth with large content
- UI is consistent with existing theme system

