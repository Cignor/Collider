#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include "ShortcutManager.h" // We need this for the capture state and manager reference

// Forward declaration
class ImGuiNodeEditorComponent;

/**
 * @class HelpManagerComponent
 * Manages the non-modal, tabbed Help window.
 *
 * This component consolidates:
 * 1. Shortcut Editor (migrated from ImGuiNodeEditorComponent)
 * 2. Node Dictionary (Markdown renderer)
 * 3. Getting Started (Markdown renderer)
 * 4. About Page
 *
 * It follows the same self-contained window pattern as ThemeEditorComponent.
 */
class HelpManagerComponent
{
public:
    HelpManagerComponent(ImGuiNodeEditorComponent* parent);
    ~HelpManagerComponent() = default;

    // === Public API ===
    void render();
    void open();
    void close();
    bool isOpen() const { return m_isOpen; }

    /**
     * @brief Sets the currently active tab by its index.
     * 0 = Shortcuts, 1 = Node Dictionary, 2 = Getting Started, 3 = FAQ, 4 = About, 5 = UI Tips
     * This is used by external triggers (like F1 or context menus) to
     * open the manager to a specific tab.
     * @param tabIndex The index of the tab to select.
     */
    void setActiveTab(int tabIndex) 
    { 
        m_currentTab = tabIndex; 
        m_shouldSetTab = true; // Flag that we want to programmatically set this tab
    }
    
    /**
     * @brief Opens the Help Manager to the Node Dictionary tab and scrolls to a specific node.
     * @param nodeAnchor The anchor of the node to scroll to (e.g., "vco", "track-mixer").
     *                   The anchor should match the format used in the markdown file.
     */
    void openToNode(const juce::String& nodeAnchor);

private:
    // === Window State ===
    bool m_isOpen = false;
    int m_currentTab = 0; // 0:Shortcuts, 1:Dictionary, 2:GettingStarted, 3:FAQ, 4:About, 5:UITips
    bool m_shouldSetTab = false; // Flag to programmatically set tab (e.g., from F1)
    ImGuiNodeEditorComponent* parentEditor = nullptr;
    collider::ShortcutManager& shortcutManager;

    // === Tab Rendering Functions ===
    void renderShortcutsTab();
    void renderNodeDictionaryTab();
    void renderGettingStartedTab();
    void renderFaqTab();
    void renderAboutTab();
    void renderUiTipsTab();

    // === Markdown Parsing & Rendering (for Node Dictionary and Getting Started) ===
    struct MarkdownSection
    {
        juce::String title;
        int level; // 1 = ##, 2 = ###, 3 = ####
        juce::String content; // Text content before subsections
        std::vector<MarkdownSection> children;
        juce::String anchor; // For anchor links (e.g., #vco)
        
        // Helper to check if this section or any child matches search
        bool matchesSearch(const juce::String& searchTerm) const;
        
        // Helper to check if this section or any child contains the given anchor
        bool containsAnchor(const juce::String& targetAnchor) const;
    };

    // Node Dictionary state
    juce::String nodeDictionarySearchTerm;
    std::vector<MarkdownSection> nodeDictionarySections;
    bool nodeDictionaryLoaded = false;
    juce::File nodeDictionaryFile;

    // Getting Started state
    juce::String gettingStartedSearchTerm;
    std::vector<MarkdownSection> gettingStartedSections;
    bool gettingStartedLoaded = false;
    juce::File gettingStartedFile;

    // FAQ state
    juce::String faqSearchTerm;
    std::vector<MarkdownSection> faqSections;
    bool faqLoaded = false;
    juce::File faqFile;

    // About state
    std::vector<MarkdownSection> aboutSections;
    bool aboutLoaded = false;
    juce::File aboutFile;

    // Markdown parsing functions
    void loadNodeDictionary();
    void loadGettingStarted();
    void loadFaq();
    void loadAbout();
    void parseMarkdown(const juce::String& content, std::vector<MarkdownSection>& sections);
    void renderMarkdownSection(const MarkdownSection& section, const juce::String& searchTerm, bool parentMatches = true, bool forceExpand = false);
    void renderAboutSection(const MarkdownSection& section); // Special renderer for About tab (non-collapsible)
    void renderAboutText(const juce::String& text); // Special text renderer for About tab (better paragraph handling)
    void renderMarkdownText(const juce::String& text);
    void renderFormattedText(const juce::String& text); // Renders text with inline formatting (bold, code)
    juce::String extractAnchor(const juce::String& headerLine);
    juce::String replaceShortcutPlaceholders(const juce::String& text); // Replaces hardcoded shortcuts with actual shortcuts from ShortcutManager
    juce::String replaceVersionInfoPlaceholders(const juce::String& text); // Replaces VersionInfo placeholders ({{APPLICATION_NAME}}, etc.)
    
    // Helper to get category color from section title
    ImU32 getCategoryColorForSection(const juce::String& sectionTitle) const;
    
    // Helper to scroll to anchor (stores anchor to scroll to)
    juce::String scrollToAnchor;
    bool scrollToSectionIfNeeded(const juce::String& anchor);
    
    // Split-pane navigation state
    struct NavigationItem
    {
        juce::String title;
        juce::String anchor;
        int level; // Indentation level
        bool isCategory; // True for category headers (like "SOURCE NODES")
    };
    std::vector<NavigationItem> nodeDictionaryNavItems; // Flattened navigation list
    void buildNavigationList(const std::vector<MarkdownSection>& sections, std::vector<NavigationItem>& navItems, int level = 0);
    void renderNavigationSidebar(const std::vector<NavigationItem>& navItems, const juce::String& searchTerm);
    void renderNodeDictionaryContent(const std::vector<MarkdownSection>& sections, const juce::String& searchTerm);
    std::unordered_map<juce::String, float> sectionScrollPositions; // Anchor -> scroll position
    float findSectionScrollPosition(const juce::String& anchor, const std::vector<MarkdownSection>& sections, float currentPos = 0.0f);

    // === START: State Migrated from ImGuiNodeEditorComponent ===
    // This state is moving here to make the Help Manager self-contained.
    juce::String shortcutsSearchTerm;
    juce::Identifier shortcutContextSelection;
    bool shortcutsDirty { false };
    juce::File defaultShortcutFile;
    juce::File userShortcutFile;

    // Struct for shortcut capture state
    struct ShortcutCaptureState
    {
        bool isCapturing { false };
        juce::Identifier actionId;
        juce::Identifier context;
        collider::KeyChord captured;
        bool hasCaptured { false };
        juce::Identifier conflictActionId;
        juce::Identifier conflictContextId;
        bool conflictIsUserBinding { false };
    };

    ShortcutCaptureState shortcutCaptureState;

    // Helper functions for the shortcut editor
    void renderShortcutEditorTable(const juce::Identifier& context);
    void renderShortcutRow(const collider::ShortcutAction& action,
                           const juce::Identifier& actionId,
                           const juce::Identifier& context,
                           bool categoryChanged);
    void renderShortcutCapturePanel();
    void beginShortcutCapture(const juce::Identifier& actionId, const juce::Identifier& context);
    void updateShortcutCapture();
    void cancelShortcutCapture();
    void applyShortcutCapture(bool forceReplace);
    void evaluateShortcutCaptureConflict();
    void clearShortcutForContext(const juce::Identifier& actionId, const juce::Identifier& context);
    void resetShortcutForContext(const juce::Identifier& actionId, const juce::Identifier& context);
    void saveUserShortcutBindings();
    juce::String getBindingLabelForContext(const juce::Identifier& actionId,
                                           const juce::Identifier& context,
                                           juce::String& sourceLabel) const;
    // === END: State Migrated from ImGuiNodeEditorComponent ===
};

