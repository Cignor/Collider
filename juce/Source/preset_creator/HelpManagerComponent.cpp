#include "HelpManagerComponent.h"

#include "ImGuiNodeEditorComponent.h" // Required for parentEditor
#include "theme/ThemeManager.h"      // For themed colors (includes Theme.h with ModuleCategory)
#include "PresetCreatorApplication.h"  // For app properties
#include "NotificationManager.h"     // For notification posting
#include <juce_gui_basics/juce_gui_basics.h>
#include <imgui_internal.h> // For IsKeyPressed
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

// Helper copied from ImGuiNodeEditorComponent.cpp
namespace
{
    [[nodiscard]] juce::String contextDisplayName(const juce::Identifier& contextId)
    {
        if (contextId == collider::ShortcutManager::getGlobalContextIdentifier())
            return "Global";
        if (contextId == ImGuiNodeEditorComponent::nodeEditorContextId)
            return "Node Editor";
        return contextId.toString();
    }

    [[nodiscard]] bool chordsEqual(const collider::KeyChord& a, const collider::KeyChord& b) noexcept
    {
        return a.key == b.key && a.ctrl == b.ctrl && a.shift == b.shift &&
               a.alt == b.alt && a.superKey == b.superKey;
    }
}

HelpManagerComponent::HelpManagerComponent(ImGuiNodeEditorComponent* parent)
    : parentEditor(parent),
      shortcutManager(collider::ShortcutManager::getInstance()),
      shortcutContextSelection(ImGuiNodeEditorComponent::nodeEditorContextId)
{
    // === LOGIC MOVED FROM ImGuiNodeEditorComponent CONSTRUCTOR ===
    // This logic now lives here, making the Help Manager responsible
    // for finding and loading its own shortcut files.
    auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeDir = executable.getParentDirectory();
    auto assetsDir = exeDir.getChildFile("assets");
    defaultShortcutFile = assetsDir.getChildFile("default_shortcuts.json");
    if (defaultShortcutFile.existsAsFile())
        shortcutManager.loadDefaultBindingsFromFile(defaultShortcutFile);
    else
        juce::Logger::writeToLog("[HelpManager] WARNING: Default shortcuts file not found at: " + defaultShortcutFile.getFullPathName());

    juce::File userSettingsDir;
    if (auto* props = PresetCreatorApplication::getApp().getProperties())
        userSettingsDir = props->getFile().getParentDirectory();
    else
        userSettingsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Collider");

    if (!userSettingsDir.isDirectory())
        userSettingsDir.createDirectory();

    userShortcutFile = userSettingsDir.getChildFile("user_shortcuts.json");
    shortcutManager.loadUserBindingsFromFile(userShortcutFile);
    
    // Initialize documentation file paths
    // Try multiple locations: next to executable, or in project root
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir = appFile.getParentDirectory();
    auto userManualDir = appDir.getChildFile("USER_MANUAL");
    
    nodeDictionaryFile = userManualDir.getChildFile("Nodes_Dictionary.md");
    gettingStartedFile = userManualDir.getChildFile("Getting_Started.md");
    faqFile = userManualDir.getChildFile("FAQ.md");
    
    // Fallback: try project root (for development)
    auto projectRoot = appDir.getParentDirectory(); // Go up one level from executable
    auto fallbackUserManualDir = projectRoot.getChildFile("USER_MANUAL");
    
    if (!nodeDictionaryFile.existsAsFile())
    {
        auto fallbackFile = fallbackUserManualDir.getChildFile("Nodes_Dictionary.md");
        if (fallbackFile.existsAsFile())
            nodeDictionaryFile = fallbackFile;
    }
    
    if (!gettingStartedFile.existsAsFile())
    {
        auto fallbackFile = fallbackUserManualDir.getChildFile("Getting_Started.md");
        if (fallbackFile.existsAsFile())
            gettingStartedFile = fallbackFile;
    }
    
    if (!faqFile.existsAsFile())
    {
        auto fallbackFile = fallbackUserManualDir.getChildFile("FAQ.md");
        if (fallbackFile.existsAsFile())
            faqFile = fallbackFile;
    }
    
    juce::Logger::writeToLog("[HelpManager] Initialized and loaded shortcut files.");
    // === END OF MOVED LOGIC ===
}

void HelpManagerComponent::open()
{
    m_isOpen = true;
}

void HelpManagerComponent::close()
{
    // On close, save any dirty shortcuts
    if (shortcutsDirty)
    {
        saveUserShortcutBindings();
    }
    
    // Cancel any pending capture
    if (shortcutCaptureState.isCapturing)
    {
        cancelShortcutCapture();
    }
    
    m_isOpen = false;
}

void HelpManagerComponent::openToNode(const juce::String& nodeAnchor)
{
    // Open the Help Manager
    open();
    
    // Set to Node Dictionary tab (index 1)
    setActiveTab(1);
    
    // Set the scroll target anchor
    // Convert module type format (e.g., "track_mixer") to anchor format (e.g., "track-mixer")
    juce::String anchor = nodeAnchor.toLowerCase();
    anchor = anchor.replace("_", "-");
    scrollToAnchor = anchor;
    
    // Ensure the dictionary is loaded
    if (!nodeDictionaryLoaded)
    {
        loadNodeDictionary();
        if (!nodeDictionarySections.empty())
        {
            buildNavigationList(nodeDictionarySections, nodeDictionaryNavItems);
        }
    }
}

void HelpManagerComponent::render()
{
    if (!m_isOpen)
        return;

    // Set default window size
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    
    // Ensure window is fully opaque (uses WindowBg from theme)
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin("Help Manager", &m_isOpen, ImGuiWindowFlags_None))
    {
        if (ImGui::BeginTabBar("HelpTabs"))
        {
            // --- Tab 1: Shortcuts ---
            bool selectShortcuts = m_shouldSetTab && m_currentTab == 0;
            if (ImGui::BeginTabItem("Shortcuts", nullptr, (selectShortcuts ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                // Only update state on user click (not on default selection)
                if (ImGui::IsItemClicked())
                {
                    m_currentTab = 0;
                    m_shouldSetTab = false; // User click overrides programmatic set
                }
                
                // Acknowledge programmatic set only if this was the target
                if (selectShortcuts)
                    m_shouldSetTab = false;
                
                renderShortcutsTab();
                ImGui::EndTabItem();
            }

            // --- Tab 2: Node Dictionary ---
            bool selectDictionary = m_shouldSetTab && m_currentTab == 1;
            if (ImGui::BeginTabItem("Node Dictionary", nullptr, (selectDictionary ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                // Only update state on user click (not on programmatic selection)
                if (ImGui::IsItemClicked())
                {
                    m_currentTab = 1;
                    m_shouldSetTab = false; // User click overrides programmatic set
                }
                
                // Acknowledge programmatic set only if this was the target
                if (selectDictionary)
                    m_shouldSetTab = false;
                
                renderNodeDictionaryTab();
                ImGui::EndTabItem();
            }

            // --- Tab 3: Getting Started ---
            bool selectGettingStarted = m_shouldSetTab && m_currentTab == 2;
            if (ImGui::BeginTabItem("Getting Started", nullptr, (selectGettingStarted ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                // Only update state on user click
                if (ImGui::IsItemClicked())
                {
                    m_currentTab = 2;
                    m_shouldSetTab = false;
                }
                
                if (selectGettingStarted)
                    m_shouldSetTab = false;
                
                renderGettingStartedTab();
                ImGui::EndTabItem();
            }

            // --- Tab 4: FAQ ---
            bool selectFaq = m_shouldSetTab && m_currentTab == 3;
            if (ImGui::BeginTabItem("FAQ", nullptr, (selectFaq ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                // Only update state on user click
                if (ImGui::IsItemClicked())
                {
                    m_currentTab = 3;
                    m_shouldSetTab = false;
                }
                
                if (selectFaq)
                    m_shouldSetTab = false;
                
                renderFaqTab();
                ImGui::EndTabItem();
            }

            // --- Tab 5: About ---
            bool selectAbout = m_shouldSetTab && m_currentTab == 4;
            if (ImGui::BeginTabItem("About", nullptr, (selectAbout ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                // Only update state on user click
                if (ImGui::IsItemClicked())
                {
                    m_currentTab = 4;
                    m_shouldSetTab = false;
                }
                
                if (selectAbout)
                    m_shouldSetTab = false;
                
                renderAboutTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // If window "X" was clicked, handle shutdown
    if (!m_isOpen)
    {
        close();
    }
}

// === STUBBED TABS (To be implemented in later phases) ===

void HelpManagerComponent::renderNodeDictionaryTab()
{
    // Lazy load the markdown file on first open
    if (!nodeDictionaryLoaded)
    {
        loadNodeDictionary();
        // Build navigation list after loading
        if (!nodeDictionarySections.empty())
        {
            buildNavigationList(nodeDictionarySections, nodeDictionaryNavItems);
        }
    }

    // Search bar
    char searchBuffer[256] = {};
    std::strncpy(searchBuffer, nodeDictionarySearchTerm.toRawUTF8(), sizeof(searchBuffer) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##node-dict-search", "Search nodes...", searchBuffer, sizeof(searchBuffer)))
    {
        nodeDictionarySearchTerm = juce::String(searchBuffer).trim();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Split-pane layout: left sidebar (navigation) + right content
    float sidebarWidth = 280.0f;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float availableWidth = window->ContentRegionRect.GetWidth();
    float contentWidth = availableWidth - sidebarWidth - ImGui::GetStyle().ItemSpacing.x;

    // Left sidebar: Navigation list
    if (ImGui::BeginChild("NodeDictionarySidebar", ImVec2(sidebarWidth, 0), true))
    {
        if (nodeDictionarySections.empty())
        {
            ImGui::TextWrapped("Node Dictionary file not found or could not be loaded.");
        }
        else
        {
            renderNavigationSidebar(nodeDictionaryNavItems, nodeDictionarySearchTerm);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: Main content
    if (ImGui::BeginChild("NodeDictionaryContent", ImVec2(contentWidth, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        if (nodeDictionarySections.empty())
        {
            ImGui::TextWrapped("Node Dictionary file not found or could not be loaded.");
            ImGui::TextWrapped("Expected location: %s", nodeDictionaryFile.getFullPathName().toRawUTF8());
        }
        else
        {
            renderNodeDictionaryContent(nodeDictionarySections, nodeDictionarySearchTerm);
        }
    }
    ImGui::EndChild();
}

void HelpManagerComponent::renderGettingStartedTab()
{
    // Lazy load the markdown file on first open
    if (!gettingStartedLoaded)
    {
        loadGettingStarted();
    }

    // Search bar
    char searchBuffer[256] = {};
    std::strncpy(searchBuffer, gettingStartedSearchTerm.toRawUTF8(), sizeof(searchBuffer) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##getting-started-search", "Search...", searchBuffer, sizeof(searchBuffer)))
    {
        gettingStartedSearchTerm = juce::String(searchBuffer).trim();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Render the markdown content
    if (ImGui::BeginChild("GettingStartedContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        if (gettingStartedSections.empty())
        {
            ImGui::TextWrapped("Getting Started file not found or could not be loaded.");
            ImGui::TextWrapped("Expected location: %s", gettingStartedFile.getFullPathName().toRawUTF8());
        }
        else
        {
            for (const auto& section : gettingStartedSections)
            {
                renderMarkdownSection(section, gettingStartedSearchTerm, true, false);
            }
        }
    }
    ImGui::EndChild();
}

void HelpManagerComponent::renderFaqTab()
{
    // Lazy load the markdown file on first open
    if (!faqLoaded)
    {
        loadFaq();
    }

    // Search bar
    char searchBuffer[256] = {};
    std::strncpy(searchBuffer, faqSearchTerm.toRawUTF8(), sizeof(searchBuffer) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##faq-search", "Search FAQ...", searchBuffer, sizeof(searchBuffer)))
    {
        faqSearchTerm = juce::String(searchBuffer).trim();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Render the markdown content
    if (ImGui::BeginChild("FaqContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        if (faqSections.empty())
        {
            ImGui::TextWrapped("FAQ file not found or could not be loaded.");
            ImGui::TextWrapped("Expected location: %s", faqFile.getFullPathName().toRawUTF8());
        }
        else
        {
            for (const auto& section : faqSections)
            {
                renderMarkdownSection(section, faqSearchTerm, true, false);
            }
        }
    }
    ImGui::EndChild();
}

void HelpManagerComponent::renderAboutTab()
{
    ImGui::Text("Collider Modular Synthesizer");
    ImGui::Text("Version 1.2 (Hypothetical)"); // TODO: Pull this from a central version header
    ImGui::Separator();
    ImGui::TextWrapped("Built with JUCE, Dear ImGui, imnodes, and the Collider Core audio engine.");
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Use dummy buttons for link look-and-feel
    if (ImGui::Button("GitHub Repository"))
    {
        juce::URL("https://github.com/Moof-Moof/Collider").launchInDefaultBrowser();
    }
    ImGui::SameLine();
    if (ImGui::Button("Full Documentation"))
    {
        // TODO: Add link to documentation website
    }
}

// === START: CODE MOVED FROM ImGuiNodeEditorComponent.cpp ===
// All this logic is now part of the HelpManagerComponent

void HelpManagerComponent::renderShortcutsTab()
{
    // This is the logic from the old `renderShortcutEditorContents()`
    
    updateShortcutCapture();
    const auto& globalContext = collider::ShortcutManager::getGlobalContextIdentifier();
    const juce::Identifier contexts[] = { globalContext, ImGuiNodeEditorComponent::nodeEditorContextId };
    int selectedIndex = (shortcutContextSelection == globalContext) ? 0 : 1;
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::BeginCombo("Context", contextDisplayName(shortcutContextSelection).toRawUTF8()))
    {
        for (int i = 0; i < 2; ++i)
        {
            const bool isSelected = (selectedIndex == i);
            if (ImGui::Selectable(contextDisplayName(contexts[i]).toRawUTF8(), isSelected))
                shortcutContextSelection = contexts[i];
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    char searchBuffer[128] = {};
    std::strncpy(searchBuffer, shortcutsSearchTerm.toRawUTF8(), sizeof(searchBuffer) - 1);
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::InputTextWithHint("##shortcut-search", "Search actions…", searchBuffer, sizeof(searchBuffer)))
    {
        shortcutsSearchTerm = juce::String(searchBuffer).trim();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save Changes"))
    {
        saveUserShortcutBindings();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves changes to user_shortcuts.json");
    ImGui::Separator();
    
    if (ImGui::BeginChild("ShortcutTableChild"))
    {
        renderShortcutEditorTable(shortcutContextSelection);
    }
    ImGui::EndChild();
    renderShortcutCapturePanel();
}

void HelpManagerComponent::renderShortcutEditorTable(const juce::Identifier& context)
{
    const auto& registry = shortcutManager.getRegistry();
    std::vector<std::pair<juce::Identifier, collider::ShortcutAction>> actions;
    actions.reserve(registry.size());
    for (const auto& entry : registry)
        actions.emplace_back(entry.first, entry.second);

    std::sort(actions.begin(), actions.end(), [](const auto& a, const auto& b)
    {
        int categoryCompare = a.second.category.compareIgnoreCase(b.second.category);
        if (categoryCompare != 0)
            return categoryCompare < 0;
        return a.second.name.compareIgnoreCase(b.second.name) < 0;
    });

    if (ImGui::BeginTable("shortcut-editor-table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable))
    {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableHeadersRow();

        juce::String previousCategory;
        for (const auto& [actionId, action] : actions)
        {
            if (shortcutsSearchTerm.isNotEmpty())
            {
                const juce::String search = shortcutsSearchTerm;
                if (!action.name.containsIgnoreCase(search)
                    && !action.description.containsIgnoreCase(search)
                    && !action.category.containsIgnoreCase(search))
                {
                    continue;
                }
            }

            const bool categoryChanged = previousCategory != action.category;
            renderShortcutRow(action, actionId, context, categoryChanged);
            previousCategory = action.category;
        }

        ImGui::EndTable();
    }
}

void HelpManagerComponent::renderShortcutRow(const collider::ShortcutAction& action,
                                             const juce::Identifier& actionId,
                                             const juce::Identifier& context,
                                             bool categoryChanged)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (categoryChanged)
        ImGui::TextUnformatted(action.category.toRawUTF8());
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(action.name.toRawUTF8());
    if (!action.description.isEmpty() && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(action.description.toRawUTF8());
        ImGui::EndTooltip();
    }
    ImGui::TableSetColumnIndex(2);
    juce::String sourceLabel;
    juce::String bindingLabel = getBindingLabelForContext(actionId, context, sourceLabel);
    ImGui::TextUnformatted(bindingLabel.toRawUTF8());
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(sourceLabel.toRawUTF8());
    ImGui::TableSetColumnIndex(4);
    juce::String assignId = "Assign##" + actionId.toString() + ":" + context.toString();
    if (ImGui::Button(assignId.toRawUTF8()))
    {
        beginShortcutCapture(actionId, context);
    }
    ImGui::SameLine();
    juce::String clearId = "Clear##" + actionId.toString() + ":" + context.toString();
    if (ImGui::Button(clearId.toRawUTF8()))
    {
        clearShortcutForContext(actionId, context);
    }
    ImGui::SameLine();
    juce::String resetId = "Reset##" + actionId.toString() + ":" + context.toString();
    if (ImGui::Button(resetId.toRawUTF8()))
    {
        resetShortcutForContext(actionId, context);
    }
}

void HelpManagerComponent::renderShortcutCapturePanel()
{
    if (!shortcutCaptureState.isCapturing)
        return;

    // This renders as an overlay
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    
    if (ImGui::Begin("ShortcutCapture", nullptr, 
                     ImGuiWindowFlags_NoDecoration | 
                     ImGuiWindowFlags_NoMove | 
                     ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto& registry = shortcutManager.getRegistry();
        juce::String actionName = shortcutCaptureState.actionId.toString();
        if (auto it = registry.find(shortcutCaptureState.actionId); it != registry.end())
            actionName = it->second.name;

        ImGui::Text("Assigning: %s (%s)",
                    actionName.toRawUTF8(),
                    contextDisplayName(shortcutCaptureState.context).toRawUTF8());
        
        ImGui::Separator();
        ImGui::TextUnformatted("Press a key combination… (Esc to cancel)");
        
        ImGui::End();
    }
}

void HelpManagerComponent::beginShortcutCapture(const juce::Identifier& actionId, const juce::Identifier& context)
{
    shortcutCaptureState = {};
    shortcutCaptureState.isCapturing = true;
    shortcutCaptureState.actionId = actionId;
    shortcutCaptureState.context = context;
}

void HelpManagerComponent::updateShortcutCapture()
{
    if (!shortcutCaptureState.isCapturing)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        cancelShortcutCapture();
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    for (int keyIndex = ImGuiKey_NamedKey_BEGIN; keyIndex < ImGuiKey_NamedKey_END; ++keyIndex)
    {
        const ImGuiKey key = static_cast<ImGuiKey>(keyIndex);
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)
            continue;
        if (key >= ImGuiKey_ReservedForModCtrl)
            continue;
        const ImGuiKeyData* data = ImGui::GetKeyData(key);
        if (data == nullptr)
            continue;
        if (data->Down && data->DownDuration == 0.0f)
        {
            shortcutCaptureState.captured = collider::KeyChord::fromImGui(io, key);
            shortcutCaptureState.hasCaptured = shortcutCaptureState.captured.isValid();
            evaluateShortcutCaptureConflict();
            applyShortcutCapture(true);
            break;
        }
    }
}

void HelpManagerComponent::cancelShortcutCapture()
{
    shortcutCaptureState = {};
}

void HelpManagerComponent::applyShortcutCapture(bool forceReplace)
{
    if (!shortcutCaptureState.isCapturing || !shortcutCaptureState.hasCaptured || !shortcutCaptureState.captured.isValid())
        return;
    const auto& actionId = shortcutCaptureState.actionId;
    const auto& context = shortcutCaptureState.context;
    if (auto userBinding = shortcutManager.getUserBinding(actionId, context))
    {
        if (userBinding->isValid() && chordsEqual(*userBinding, shortcutCaptureState.captured))
        {
            cancelShortcutCapture();
            return;
        }
    }
    if (auto defaultBinding = shortcutManager.getDefaultBinding(actionId, context))
    {
        if (defaultBinding->isValid() && chordsEqual(*defaultBinding, shortcutCaptureState.captured))
        {
            if (shortcutManager.removeUserBinding(actionId, context))
                shortcutsDirty = true;
            cancelShortcutCapture();
            return;
        }
    }
    if (shortcutCaptureState.conflictActionId.isValid())
    {
        if (!forceReplace)
            return;
        clearShortcutForContext(shortcutCaptureState.conflictActionId, shortcutCaptureState.conflictContextId);
    }
    shortcutManager.setUserBinding(actionId, context, shortcutCaptureState.captured);
    shortcutsDirty = true;
    cancelShortcutCapture();
}

void HelpManagerComponent::evaluateShortcutCaptureConflict()
{
    shortcutCaptureState.conflictActionId = {};
    shortcutCaptureState.conflictContextId = {};
    shortcutCaptureState.conflictIsUserBinding = false;
    if (!shortcutCaptureState.hasCaptured || !shortcutCaptureState.captured.isValid())
        return;
    const auto& chord = shortcutCaptureState.captured;
    const auto& targetAction = shortcutCaptureState.actionId;
    const auto& targetContext = shortcutCaptureState.context;
    const auto& globalContext = collider::ShortcutManager::getGlobalContextIdentifier();
    const auto& registry = shortcutManager.getRegistry();
    const juce::Identifier contextsToCheck[] = { globalContext, ImGuiNodeEditorComponent::nodeEditorContextId };
    auto isSameChord = [&](const juce::Identifier& actionId, const juce::Identifier& contextId, const juce::Optional<collider::KeyChord>& chordOpt, bool isUser)
    {
        if (!chordOpt.hasValue() || !chordOpt->isValid())
            return false;
        if (!chordsEqual(*chordOpt, chord))
            return false;
        shortcutCaptureState.conflictActionId = actionId;
        shortcutCaptureState.conflictContextId = contextId;
        shortcutCaptureState.conflictIsUserBinding = isUser;
        return true;
    };
    // Ignore if chord matches current binding for this action/context
    auto currentBinding = shortcutManager.getBindingForContext(targetAction, targetContext);
    if (currentBinding.isValid() && chordsEqual(currentBinding, chord))
        return;
    for (const auto& [actionId, action] : registry)
    {
        for (const auto& ctx : contextsToCheck)
        {
            if (ctx != targetContext && targetContext == globalContext && ctx != globalContext)
                continue; // when editing global, only check global + other contexts once
            auto userBinding = shortcutManager.getUserBinding(actionId, ctx);
            if (isSameChord(actionId, ctx, userBinding, true))
                return;
            auto defaultBinding = shortcutManager.getDefaultBinding(actionId, ctx);
            // Only check default if no user override
            if (!userBinding.hasValue())
            {
                if (isSameChord(actionId, ctx, defaultBinding, false))
                    return;
            }
        }
    }
}

void HelpManagerComponent::clearShortcutForContext(const juce::Identifier& actionId, const juce::Identifier& context)
{
    collider::KeyChord cleared;
    shortcutManager.setUserBinding(actionId, context, cleared);
    shortcutsDirty = true;
    if (shortcutCaptureState.isCapturing &&
        shortcutCaptureState.actionId == actionId &&
        shortcutCaptureState.context == context)
    {
        cancelShortcutCapture();
    }
}

void HelpManagerComponent::resetShortcutForContext(const juce::Identifier& actionId, const juce::Identifier& context)
{
    if (shortcutManager.removeUserBinding(actionId, context))
    {
        shortcutsDirty = true;
    }
    if (shortcutCaptureState.isCapturing &&
        shortcutCaptureState.actionId == actionId &&
        shortcutCaptureState.context == context)
    {
        cancelShortcutCapture();
    }
}

void HelpManagerComponent::saveUserShortcutBindings()
{
    if (userShortcutFile.getFullPathName().isEmpty())
        return;
    auto parent = userShortcutFile.getParentDirectory();
    if (!parent.isDirectory())
        parent.createDirectory();
    shortcutManager.saveUserBindingsToFile(userShortcutFile);
    shortcutsDirty = false;
    NotificationManager::post(NotificationManager::Type::Success, "Shortcut settings saved");
}

juce::String HelpManagerComponent::getBindingLabelForContext(const juce::Identifier& actionId,
                                                             const juce::Identifier& context,
                                                             juce::String& sourceLabel) const
{
    const auto& globalContext = collider::ShortcutManager::getGlobalContextIdentifier();
    auto userBinding = shortcutManager.getUserBinding(actionId, context);
    if (userBinding.hasValue())
    {
        if (userBinding->isValid())
        {
            sourceLabel = "User";
            return userBinding->toString();
        }
        sourceLabel = "User (cleared)";
        return "Unassigned";
    }
    auto defaultBinding = shortcutManager.getDefaultBinding(actionId, context);
    if (defaultBinding.hasValue() && defaultBinding->isValid())
    {
        sourceLabel = "Default";
        return defaultBinding->toString();
    }
    if (context != globalContext)
    {
        auto userGlobal = shortcutManager.getUserBinding(actionId, globalContext);
        if (userGlobal.hasValue())
        {
            if (userGlobal->isValid())
            {
                sourceLabel = "Global (user)";
                return userGlobal->toString();
            }
            sourceLabel = "Global (user cleared)";
            return "Unassigned";
        }
        auto defaultGlobal = shortcutManager.getDefaultBinding(actionId, globalContext);
        if (defaultGlobal.hasValue() && defaultGlobal->isValid())
        {
            sourceLabel = "Global (default)";
            return defaultGlobal->toString();
        }
    }
    sourceLabel = "Unassigned";
    return "Unassigned";
}

// === END: CODE MOVED FROM ImGuiNodeEditorComponent.cpp ===

// === MARKDOWN PARSING & RENDERING ===

bool HelpManagerComponent::MarkdownSection::matchesSearch(const juce::String& searchTerm) const
{
    if (searchTerm.isEmpty())
        return true;
    
    if (title.containsIgnoreCase(searchTerm) || content.containsIgnoreCase(searchTerm))
        return true;
    
    for (const auto& child : children)
    {
        if (child.matchesSearch(searchTerm))
            return true;
    }
    
    return false;
}

bool HelpManagerComponent::MarkdownSection::containsAnchor(const juce::String& targetAnchor) const
{
    if (targetAnchor.isEmpty())
        return false;
    
    // Check if this section's anchor matches
    if (anchor == targetAnchor)
        return true;
    
    // Recursively check children
    for (const auto& child : children)
    {
        if (child.containsAnchor(targetAnchor))
            return true;
    }
    
    return false;
}

void HelpManagerComponent::loadNodeDictionary()
{
    if (nodeDictionaryLoaded || !nodeDictionaryFile.existsAsFile())
    {
        if (!nodeDictionaryFile.existsAsFile())
        {
            juce::Logger::writeToLog("[HelpManager] Node Dictionary file not found: " + nodeDictionaryFile.getFullPathName());
        }
        nodeDictionaryLoaded = true;
        return;
    }
    
    juce::String content = nodeDictionaryFile.loadFileAsString();
    if (content.isEmpty())
    {
        juce::Logger::writeToLog("[HelpManager] Failed to load Node Dictionary file or file is empty.");
        nodeDictionaryLoaded = true;
        return;
    }
    
    parseMarkdown(content, nodeDictionarySections);
    nodeDictionaryLoaded = true;
    juce::Logger::writeToLog("[HelpManager] Loaded Node Dictionary: " + juce::String(nodeDictionarySections.size()) + " top-level sections");
}

void HelpManagerComponent::loadGettingStarted()
{
    if (gettingStartedLoaded || !gettingStartedFile.existsAsFile())
    {
        if (!gettingStartedFile.existsAsFile())
        {
            juce::Logger::writeToLog("[HelpManager] Getting Started file not found: " + gettingStartedFile.getFullPathName());
        }
        gettingStartedLoaded = true;
        return;
    }
    
    juce::String content = gettingStartedFile.loadFileAsString();
    if (content.isEmpty())
    {
        juce::Logger::writeToLog("[HelpManager] Failed to load Getting Started file or file is empty.");
        gettingStartedLoaded = true;
        return;
    }
    
    parseMarkdown(content, gettingStartedSections);
    gettingStartedLoaded = true;
    juce::Logger::writeToLog("[HelpManager] Loaded Getting Started: " + juce::String(gettingStartedSections.size()) + " top-level sections");
}

void HelpManagerComponent::loadFaq()
{
    if (faqLoaded || !faqFile.existsAsFile())
    {
        if (!faqFile.existsAsFile())
        {
            juce::Logger::writeToLog("[HelpManager] FAQ file not found: " + faqFile.getFullPathName());
        }
        faqLoaded = true;
        return;
    }
    
    juce::String content = faqFile.loadFileAsString();
    if (content.isEmpty())
    {
        juce::Logger::writeToLog("[HelpManager] Failed to load FAQ file or file is empty.");
        faqLoaded = true;
        return;
    }
    
    parseMarkdown(content, faqSections);
    faqLoaded = true;
    juce::Logger::writeToLog("[HelpManager] Loaded FAQ: " + juce::String(faqSections.size()) + " top-level sections");
}

void HelpManagerComponent::parseMarkdown(const juce::String& content, std::vector<MarkdownSection>& sections)
{
    sections.clear();
    
    auto lines = juce::StringArray::fromLines(content);
    std::vector<MarkdownSection*> stack; // Stack to track current section hierarchy
    
    for (int i = 0; i < lines.size(); ++i)
    {
        juce::String line = lines[i].trimEnd();
        
        // Check for headers (##, ###, ####)
        if (line.startsWith("##"))
        {
            int level = 0;
            int startIdx = 0;
            while (startIdx < line.length() && line[startIdx] == '#')
            {
                level++;
                startIdx++;
            }
            
            // Skip if level is 1 (single #) - we only care about ## and below
            if (level < 2)
                continue;
            
            level -= 1; // Convert to 1-based: ## = 1, ### = 2, #### = 3
            
            juce::String title = line.substring(startIdx).trim();
            juce::String anchor = extractAnchor(line);
            
            // Pop stack until we find the right parent level
            while (!stack.empty() && stack.back()->level >= level)
            {
                stack.pop_back();
            }
            
            MarkdownSection newSection;
            newSection.title = title;
            newSection.level = level;
            newSection.anchor = anchor;
            
            // Add to appropriate parent
            if (stack.empty())
            {
                sections.push_back(newSection);
                stack.push_back(&sections.back());
            }
            else
            {
                stack.back()->children.push_back(newSection);
                stack.push_back(&stack.back()->children.back());
            }
        }
        else if (!stack.empty() && !line.isEmpty())
        {
            // Add content to current section
            if (stack.back()->content.isNotEmpty())
                stack.back()->content += "\n";
            stack.back()->content += line;
        }
    }
}

juce::String HelpManagerComponent::extractAnchor(const juce::String& headerLine)
{
    // Extract anchor from header line (e.g., "### vco" -> "vco")
    int startIdx = 0;
    while (startIdx < headerLine.length() && headerLine[startIdx] == '#')
        startIdx++;
    
    juce::String title = headerLine.substring(startIdx).trim();
    
    // Convert to lowercase anchor (simple version)
    juce::String anchor = title.toLowerCase();
    // Remove special characters and replace spaces with hyphens
    anchor = anchor.replaceCharacters(".,!?;:()[]{}", "");
    anchor = anchor.replace(" ", "-");
    
    return anchor;
}

void HelpManagerComponent::renderMarkdownSection(const MarkdownSection& section, const juce::String& searchTerm, bool parentMatches, bool forceExpand)
{
    bool sectionMatches = searchTerm.isEmpty() ? true : section.matchesSearch(searchTerm);
    bool shouldShow = searchTerm.isEmpty() || sectionMatches || parentMatches;
    
    if (!shouldShow)
        return;
    
    // Check if this section or any of its children contain the target anchor
    bool containsTargetAnchor = !scrollToAnchor.isEmpty() && section.containsAnchor(scrollToAnchor);
    
    // Check if this is the exact target section for scrolling
    bool isTargetSection = !scrollToAnchor.isEmpty() && section.anchor == scrollToAnchor;
    
    // Render based on level
    if (section.level == 1)
    {
        // Top-level sections use CollapsingHeader with category color
        ImU32 categoryColor = getCategoryColorForSection(section.title);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(categoryColor);
        
        ImGui::PushStyleColor(ImGuiCol_Header, categoryColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x*1.2f, c.y*1.2f, c.z*1.2f, 1.0f)));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x*1.4f, c.y*1.4f, c.z*1.4f, 1.0f)));
        
        // Use optimal text color for contrast
        ImU32 optimalTextColor = ThemeUtils::getOptimalTextColor(categoryColor);
        ImGui::PushStyleColor(ImGuiCol_Text, optimalTextColor);
        
        // Force expand if this section contains the target anchor
        if (forceExpand || containsTargetAnchor)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        
        // Determine if section should be expanded by default
        ImGuiTreeNodeFlags flags = 0;
        if (searchTerm.isEmpty() || sectionMatches || forceExpand || containsTargetAnchor)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        
        bool isOpen = ImGui::CollapsingHeader(section.title.toRawUTF8(), flags);
        ImGui::PopStyleColor(4);
        if (isOpen)
        {
            ImGui::Indent(10.0f);
            renderMarkdownText(section.content);
            
            for (const auto& child : section.children)
            {
                renderMarkdownSection(child, searchTerm, sectionMatches || containsTargetAnchor, containsTargetAnchor);
            }
            ImGui::Unindent(10.0f);
        }
    }
    else if (section.level == 2)
    {
        // Second-level sections (###) - node names with category colors
        
        // Check if this is the target section for scrolling - scroll BEFORE rendering header
        if (isTargetSection)
        {
            ImGui::SetScrollHereY(0.1f); // 10% from top
            scrollToAnchor = ""; // Clear after scrolling
        }
        
        ImGui::Spacing();
        ImGui::PushID(section.anchor.toRawUTF8()); // Unique ID for each node section
        
        // Use theme accent color for node names - bright and vibrant
        ImVec4 accentColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
        accentColor.w = 1.0f;
        // Make it slightly more vibrant
        accentColor.x = std::min(1.0f, accentColor.x * 1.1f);
        accentColor.y = std::min(1.0f, accentColor.y * 1.1f);
        accentColor.z = std::min(1.0f, accentColor.z * 1.15f);
        ImGui::PushStyleColor(ImGuiCol_Text, accentColor);
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextUnformatted(section.title.toRawUTF8());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PopID();
        
        ImGui::Spacing();
        
        renderMarkdownText(section.content);
        
        for (const auto& child : section.children)
        {
            renderMarkdownSection(child, searchTerm, sectionMatches, forceExpand && containsTargetAnchor);
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    else
    {
        // Level 3+ (####) - smaller styled header
        
        // Check if this is the target section for scrolling - scroll BEFORE rendering header
        if (isTargetSection)
        {
            ImGui::SetScrollHereY(0.1f); // 10% from top
            scrollToAnchor = ""; // Clear after scrolling
        }
        
        ImGui::Spacing();
        ImGui::PushID(section.anchor.toRawUTF8()); // Unique ID for each subsection
        
        // Use theme text color with slight emphasis
        ImVec4 subHeaderColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        subHeaderColor.w = 0.9f; // Slightly brighter than disabled
        ImGui::PushStyleColor(ImGuiCol_Text, subHeaderColor);
        ImGui::SetWindowFontScale(1.1f);
        ImGui::TextUnformatted(section.title.toRawUTF8());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PopID();
        
        ImGui::Spacing();
        
        renderMarkdownText(section.content);
        
        for (const auto& child : section.children)
        {
            renderMarkdownSection(child, searchTerm, sectionMatches, forceExpand && containsTargetAnchor);
        }
    }
}

void HelpManagerComponent::renderMarkdownText(const juce::String& text)
{
    if (text.isEmpty())
        return;
    
    auto lines = juce::StringArray::fromLines(text);
    bool inCodeBlock = false;
    
    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx)
    {
        juce::String line = lines[lineIdx];
        juce::String trimmed = line.trim();
        
        // Handle code blocks
        if (trimmed.startsWith("```"))
        {
            inCodeBlock = !inCodeBlock;
            if (inCodeBlock)
            {
                ImGui::Spacing();
                // Use a slightly brighter background for code blocks
                ImVec4 codeBg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
                codeBg.w = std::min(1.0f, codeBg.w * 1.3f); // Brighter background
                ImGui::PushStyleColor(ImGuiCol_ChildBg, codeBg);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
            }
            else
            {
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
            continue;
        }
        
        if (inCodeBlock)
        {
            // Render code block line with theme-aware color
            ImVec4 codeTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            // Make code text slightly brighter/cyan-tinted
            codeTextColor.x = std::min(1.0f, codeTextColor.x * 1.2f);
            codeTextColor.y = std::min(1.0f, codeTextColor.y * 1.1f);
            codeTextColor.z = std::min(1.0f, codeTextColor.z * 1.15f);
            ImGui::PushStyleColor(ImGuiCol_Text, codeTextColor);
            ImGui::TextUnformatted(line.toRawUTF8());
            ImGui::PopStyleColor();
            continue;
        }
        
        if (trimmed.isEmpty())
        {
            ImGui::Spacing();
            continue;
        }
        
        // Check for list items
        if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
        {
            ImGui::Indent(20.0f);
            renderFormattedText(trimmed.substring(2));
            ImGui::Unindent(20.0f);
        }
        // Regular text with formatting
        else
        {
            renderFormattedText(trimmed);
        }
    }
}

void HelpManagerComponent::renderFormattedText(const juce::String& text)
{
    // Parse formatting: bold (**text**), code (`text`), and links ([text](#anchor))
    juce::String remaining = text;
    std::vector<std::tuple<juce::String, int, juce::String>> segments; // text, type (0=plain, 1=bold, 2=code, 3=link), linkTarget
    
    // Parse the text into segments
    while (remaining.isNotEmpty())
    {
        int boldStart = remaining.indexOf("**");
        int codeStart = remaining.indexOf("`");
        int linkStart = remaining.indexOf("[");
        
        int nextMarker = -1;
        int formatType = 0;
        
        // Find the earliest marker
        if (boldStart >= 0 && (codeStart < 0 || boldStart < codeStart) && (linkStart < 0 || boldStart < linkStart))
        {
            nextMarker = boldStart;
            formatType = 1; // bold
        }
        else if (codeStart >= 0 && (linkStart < 0 || codeStart < linkStart))
        {
            nextMarker = codeStart;
            formatType = 2; // code
        }
        else if (linkStart >= 0)
        {
            nextMarker = linkStart;
            formatType = 3; // link
        }
        
        if (nextMarker < 0)
        {
            // No more formatting
            if (remaining.isNotEmpty())
                segments.emplace_back(remaining, 0, "");
            break;
        }
        
        // Add plain text before marker
        if (nextMarker > 0)
        {
            segments.emplace_back(remaining.substring(0, nextMarker), 0, "");
        }
        
        if (formatType == 1) // bold
        {
            int boldEnd = remaining.indexOf(boldStart + 2, "**");
            if (boldEnd > boldStart + 2)
            {
                segments.emplace_back(remaining.substring(boldStart + 2, boldEnd), 1, "");
                remaining = remaining.substring(boldEnd + 2);
            }
            else
            {
                // Malformed bold, treat as plain text
                segments.emplace_back(remaining.substring(boldStart), 0, "");
                remaining = "";
            }
        }
        else if (formatType == 2) // code
        {
            int codeEnd = remaining.indexOf(codeStart + 1, "`");
            if (codeEnd > codeStart)
            {
                segments.emplace_back(remaining.substring(codeStart + 1, codeEnd), 2, "");
                remaining = remaining.substring(codeEnd + 1);
            }
            else
            {
                segments.emplace_back(remaining.substring(codeStart), 0, "");
                remaining = "";
            }
        }
        else if (formatType == 3) // link [text](#anchor)
        {
            int linkTextEnd = remaining.indexOf(nextMarker + 1, "]");
            if (linkTextEnd > nextMarker)
            {
                juce::String linkText = remaining.substring(nextMarker + 1, linkTextEnd);
                int linkTargetStart = remaining.indexOf(linkTextEnd + 1, "(");
                if (linkTargetStart == linkTextEnd + 1)
                {
                    int linkTargetEnd = remaining.indexOf(linkTargetStart + 1, ")");
                    if (linkTargetEnd > linkTargetStart)
                    {
                        juce::String linkTarget = remaining.substring(linkTargetStart + 1, linkTargetEnd);
                        // Remove # from anchor if present
                        if (linkTarget.startsWith("#"))
                            linkTarget = linkTarget.substring(1);
                        segments.emplace_back(linkText, 3, linkTarget);
                        remaining = remaining.substring(linkTargetEnd + 1);
                    }
                    else
                    {
                        segments.emplace_back(remaining.substring(nextMarker), 0, "");
                        remaining = "";
                    }
                }
                else
                {
                    segments.emplace_back(remaining.substring(nextMarker), 0, "");
                    remaining = "";
                }
            }
            else
            {
                segments.emplace_back(remaining.substring(nextMarker), 0, "");
                remaining = "";
            }
        }
    }
    
    // Render segments
    for (const auto& [segmentText, formatType, linkTarget] : segments)
    {
        if (segmentText.isEmpty())
            continue;
            
        if (formatType == 1) // bold
        {
            // Bold text - use theme accent color
            ImVec4 boldColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
            boldColor.w = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, boldColor);
            ImGui::SetWindowFontScale(1.08f);
            ImGui::TextWrapped("%s", segmentText.toRawUTF8());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }
        else if (formatType == 2) // code
        {
            // Inline code - use theme colors with subtle background
            ImVec4 codeTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            codeTextColor.x = std::min(1.0f, codeTextColor.x * 1.15f);
            codeTextColor.y = std::min(1.0f, codeTextColor.y * 1.1f);
            codeTextColor.z = std::min(1.0f, codeTextColor.z * 1.2f);
            
            ImVec4 codeBgColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
            codeBgColor.w = std::min(1.0f, codeBgColor.w * 1.2f); // Slightly brighter background
            
            ImGui::PushStyleColor(ImGuiCol_Text, codeTextColor);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, codeBgColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::TextWrapped("%s", segmentText.toRawUTF8());
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
        else if (formatType == 3) // link
        {
            // Link - use theme accent color with hover effect
            ImVec4 linkColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
            linkColor.z = std::min(1.0f, linkColor.z * 1.2f); // Slightly more blue
            linkColor.w = 1.0f;
            
            juce::String linkId = "link_" + linkTarget;
            ImGui::PushID(linkId.toRawUTF8()); // Unique ID for each link
            
            ImGui::PushStyleColor(ImGuiCol_Text, linkColor);
            if (ImGui::Selectable(segmentText.toRawUTF8(), false, ImGuiSelectableFlags_None))
            {
                // Clicked! Scroll to anchor
                juce::String target = linkTarget;
                if (target.startsWith("#"))
                    target = target.substring(1);
                scrollToAnchor = target;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        else // plain
        {
            ImGui::TextWrapped("%s", segmentText.toRawUTF8());
        }
    }
}

ImU32 HelpManagerComponent::getCategoryColorForSection(const juce::String& sectionTitle) const
{
    juce::String titleUpper = sectionTitle.toUpperCase();
    
    // Map section titles to ModuleCategory
    if (titleUpper.contains("SOURCE") || titleUpper.contains("1. SOURCE"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Source);
    else if (titleUpper.contains("EFFECT") || titleUpper.contains("2. EFFECT"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Effect);
    else if (titleUpper.contains("MODULATOR") || titleUpper.contains("3. MODULATOR"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Modulator);
    else if (titleUpper.contains("UTILITY") || titleUpper.contains("4. UTILITY"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Utility);
    else if (titleUpper.contains("SEQUENCER") || titleUpper.contains("5. SEQUENCER"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Seq);
    else if (titleUpper.contains("MIDI") || titleUpper.contains("6. MIDI"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::MIDI);
    else if (titleUpper.contains("ANALYSIS") || titleUpper.contains("7. ANALYSIS"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Analysis);
    else if (titleUpper.contains("TTS") || titleUpper.contains("8. TTS"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::TTS_Voice);
    else if (titleUpper.contains("SPECIAL") || titleUpper.contains("9. SPECIAL"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Special_Exp);
    else if (titleUpper.contains("COMPUTER VISION") || titleUpper.contains("10. COMPUTER VISION"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::OpenCV);
    else if (titleUpper.contains("SYSTEM") || titleUpper.contains("11. SYSTEM"))
        return ThemeManager::getInstance().getCategoryColor(ModuleCategory::Sys);
    
    // Default color
    return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Header));
}

bool HelpManagerComponent::scrollToSectionIfNeeded(const juce::String& anchor)
{
    if (scrollToAnchor.isNotEmpty() && scrollToAnchor == anchor)
    {
        // Scroll to this section
        ImGui::SetScrollHereY(0.5f);
        scrollToAnchor = ""; // Clear after scrolling
        return true; // Indicate we scrolled
    }
    return false; // No scroll needed
}

void HelpManagerComponent::buildNavigationList(const std::vector<MarkdownSection>& sections, std::vector<NavigationItem>& navItems, int level)
{
    for (const auto& section : sections)
    {
        // Add all sections to navigation, but indent based on hierarchy
        NavigationItem item;
        item.title = section.title;
        item.anchor = section.anchor;
        item.level = level;
        item.isCategory = (section.level == 1); // Level 1 headers are categories like "1. SOURCE NODES"
        navItems.push_back(item);
        
        // Recursively add children with increased indentation
        if (!section.children.empty())
        {
            buildNavigationList(section.children, navItems, level + 1);
        }
    }
}

void HelpManagerComponent::renderNavigationSidebar(const std::vector<NavigationItem>& navItems, const juce::String& searchTerm)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::SetWindowFontScale(1.15f);
    ImGui::Text("Navigation");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    int visibleIndex = 0; // Counter for unique IDs
    for (size_t i = 0; i < navItems.size(); ++i)
    {
        const auto& item = navItems[i];
        
        // Filter by search term
        if (!searchTerm.isEmpty() && !item.title.containsIgnoreCase(searchTerm))
            continue;

        // Skip items without anchors (like "Table of Contents")
        if (item.anchor.isEmpty())
            continue;

        // Indentation for nested items
        if (item.level > 0)
        {
            ImGui::Indent(item.level * 15.0f);
        }

        // Create unique ID using index and anchor
        juce::String uniqueId = juce::String(visibleIndex) + "_" + item.anchor;
        ImGui::PushID(uniqueId.toRawUTF8());

        bool isSelected = scrollToAnchor == item.anchor;
        
        // Category headers get special styling with category colors
        if (item.isCategory)
        {
            ImU32 categoryColor = getCategoryColorForSection(item.title);
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(categoryColor);
            
            // Use category color for selected state
            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, categoryColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, categoryColor);
                ImVec4 selectedColor = ImVec4(c.x * 1.3f, c.y * 1.3f, c.z * 1.3f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertFloat4ToU32(selectedColor));
                ImGui::PushStyleColor(ImGuiCol_Text, ThemeUtils::getOptimalTextColor(categoryColor));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, c);
            }
            
            ImGui::SetWindowFontScale(1.1f);
        }
        else
        {
            // Node items - use theme accent color for selected
            if (isSelected)
            {
                ImVec4 accentColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
                accentColor.w = 1.0f;
                ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertFloat4ToU32(accentColor));
                ImGui::PushStyleColor(ImGuiCol_Text, accentColor);
                ImGui::SetWindowFontScale(1.05f);
            }
            else
            {
                // Use slightly brighter text for nodes
                ImVec4 nodeTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                nodeTextColor.w = 0.95f; // Slightly brighter
                ImGui::PushStyleColor(ImGuiCol_Text, nodeTextColor);
            }
        }

        // Clickable link with hover effect
        if (ImGui::Selectable(item.title.toRawUTF8(), isSelected, ImGuiSelectableFlags_None))
        {
            // Clicked! Scroll to this section in the content pane
            scrollToAnchor = item.anchor;
        }

        // Pop style colors
        if (item.isCategory)
        {
            ImGui::SetWindowFontScale(1.0f);
            if (isSelected)
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(1);
        }
        else
        {
            if (isSelected)
            {
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor(2);
            }
            else
            {
                ImGui::PopStyleColor(1);
            }
        }

        ImGui::PopID();

        if (item.level > 0)
        {
            ImGui::Unindent(item.level * 15.0f);
        }

        visibleIndex++;
    }
}

void HelpManagerComponent::renderNodeDictionaryContent(const std::vector<MarkdownSection>& sections, const juce::String& searchTerm)
{
    for (const auto& section : sections)
    {
        // Check if we need to scroll to this section or any of its children
        bool shouldExpand = scrollToAnchor.isNotEmpty() && section.containsAnchor(scrollToAnchor);
        
        // If scrolling is needed, ensure parent sections are expanded
        if (shouldExpand && section.level == 1)
        {
            // Force expand this section by using SetNextItemOpen
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        
        // Render the section (it will handle scrolling internally)
        renderMarkdownSection(section, searchTerm, true, shouldExpand);
    }
}

