#pragma once

#include "Theme.h"
#include "ThemeManager.h"
#include <imgui.h>

/**
 * Theme Editor Component
 * 
 * Provides a visual editor for modifying theme properties in real-time.
 * Changes are applied immediately and can be saved as custom themes.
 */
class ThemeEditorComponent
{
public:
    ThemeEditorComponent();
    ~ThemeEditorComponent() = default;

    // Render the theme editor window
    void render();

    // Window state
    void open();
    void close();
    bool isOpen() const { return m_isOpen; }

private:
    // Tab rendering functions
    void renderTabs();
    void renderImGuiStyleTab();
    void renderImGuiColorsTab();
    void renderAccentTab();
    void renderTextColorsTab();
    void renderStatusColorsTab();
    void renderHeaderColorsTab();
    void renderImNodesTab();
    void renderLinksTab();
    void renderCanvasTab();
    void renderLayoutTab();
    void renderFontsTab();
    void renderWindowsTab();
    void renderModulationTab();
    void renderMetersTab();
    void renderTimelineTab();
    void renderModulesTab();

    // Helper UI components
    bool colorEdit4(const char* label, ImVec4& color, ImGuiColorEditFlags flags = 0);
    bool colorEditU32(const char* label, ImU32& color, ImGuiColorEditFlags flags = 0);
    bool dragFloat(const char* label, float& value, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char* format = "%.2f");
    bool dragFloat2(const char* label, ImVec2& value, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char* format = "%.2f");
    bool triStateColorEdit(const char* label, TriStateColor& tsc);
    
    // Eyedropper support
    void renderPickerOverlay();
    void beginPickColor(ImU32* target);
    void beginPickColor(ImVec4* target);
    static bool sampleScreenPixel(int x, int y, unsigned char outRGBA[4]);
    
    // Save/Load
    void renderSaveDialog();
    void saveTheme();
    void resetCurrentTab();
    void applyChanges();

    // State
    bool m_isOpen = false;
    Theme m_workingCopy;  // Working copy of theme (modifications applied here)
    bool m_hasChanges = false;
    int m_currentTab = 0;
    
    // Save dialog state
    bool m_showSaveDialog = false;
    char m_saveThemeName[256] = {0};
    
    // Eyedropper state
    bool m_pickerActive = false;
    ImU32* m_pickTargetU32 = nullptr;
    ImVec4* m_pickTargetVec4 = nullptr;
    
    // Tab names
    static constexpr const char* s_tabNames[] = {
        "ImGui Style",
        "ImGui Colors",
        "Accent",
        "Text Colors",
        "Status",
        "Headers",
        "ImNodes",
        "Links",
        "Canvas",
        "Layout",
        "Fonts",
        "Windows",
        "Modulation",
        "Meters",
        "Timeline",
        "Modules"
    };
    
    static constexpr int s_numTabs = 16;
};

