#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include <atomic>
#include "Theme.h"

class ThemeManager
{
public:
	static ThemeManager& getInstance();

	bool loadTheme(const juce::File& themeFile);
	bool saveTheme(const juce::File& themeFile);
	void applyTheme();
	void resetToDefault();
	void requestFontReload();
	bool consumeFontReloadRequest();
	void rebuildFontsNow();
	
	// Persistence
	void saveUserThemePreference(const juce::String& themeFilename);
	bool loadUserThemePreference();

	const Theme& getCurrentTheme() const;
	Theme& getEditableTheme();  // For theme editor - returns mutable reference

	// Colors
	ImU32 getCategoryColor(ModuleCategory cat, bool hovered = false);
	ImU32 getPinColor(PinDataType type);
	ImU32 getPinConnectedColor();
	ImU32 getPinDisconnectedColor();

	// Layout
	float getSidebarWidth() const;
	float getNodeDefaultWidth() const;
	float getWindowPadding() const;
	
	// Canvas
	ImU32 getCanvasBackground() const;
	ImU32 getGridColor() const;
	ImU32 getGridOriginColor() const;
	float getGridSize() const;
	ImU32 getScaleTextColor() const;
	float getScaleInterval() const;
	ImU32 getDropTargetOverlay() const;
	ImU32 getMousePositionText() const;
	
	// Node styling
	ImU32 getNodeBackground() const;
	ImU32 getNodeFrame() const;
	ImU32 getNodeFrameHovered() const;
	ImU32 getNodeFrameSelected() const;
	float getNodeRounding() const;
	float getNodeBorderWidth() const;

private:
	ThemeManager();
	~ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;
	ThemeManager(ThemeManager&&) = delete;
	ThemeManager& operator=(ThemeManager&&) = delete;

	void loadDefaultTheme();
	void applyImGuiStyle();
	void applyFonts();

	// JSON helpers
	static juce::var colorToVar(ImU32 c);
	static ImU32 varToColor(const juce::var& v, ImU32 fallback);
	static juce::var vec4ToVar(const ImVec4& v);
	static ImVec4 varToVec4(const juce::var& v, const ImVec4& fallback);

	static juce::String moduleCategoryToString(ModuleCategory c);
	static bool stringToModuleCategory(const juce::String& s, ModuleCategory& out);
	static juce::String pinTypeToString(PinDataType t);
	static bool stringToPinType(const juce::String& s, PinDataType& out);

	Theme currentTheme;
	Theme defaultTheme;

	std::atomic<bool> fontReloadPending { false };
};

/**
 * A theme-aware replacement for ImGui::TextColored.
 * It automatically applies a text glow/shadow if enabled in the theme.
 */
inline void ThemeText(const char* text, ImVec4 color)
{
	const auto& theme = ThemeManager::getInstance().getCurrentTheme();

	if (theme.text.enable_text_glow)
	{
		// --- DRAW GLOW/SHADOW ---
		// Get the current cursor, draw the shadows offset,
		// then reset the cursor to draw the main text on top.
		
		ImVec2 pos = ImGui::GetCursorPos();
		ImVec4 glowColor = theme.text.text_glow_color;

		ImGui::PushStyleColor(ImGuiCol_Text, glowColor);
		
		// Draw 4 shadow layers for a soft "glow" effect.
		// We must use ImGui::TextUnformatted and handle newlines
		// manually to prevent formatting issues with ImGui::Text.
		ImGui::SetCursorPos(ImVec2(pos.x - 1, pos.y)); ImGui::TextUnformatted(text);
		ImGui::SetCursorPos(ImVec2(pos.x + 1, pos.y)); ImGui::TextUnformatted(text);
		ImGui::SetCursorPos(ImVec2(pos.x, pos.y - 1)); ImGui::TextUnformatted(text);
		ImGui::SetCursorPos(ImVec2(pos.x, pos.y + 1)); ImGui::TextUnformatted(text);

		ImGui::PopStyleColor();

		// Reset cursor to the original position
		ImGui::SetCursorPos(pos);
	}
	
	// --- DRAW MAIN TEXT ---
	// This draws the main text and advances the cursor normally.
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::TextUnformatted(text); // Use TextUnformatted for consistency
	ImGui::PopStyleColor();
}

/**
 * Overload for non-colored text (uses default theme text color)
 */
inline void ThemeText(const char* text)
{
	// Get the default text color from ImGui style settings
	ImVec4 defaultColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
	ThemeText(text, defaultColor);
}


