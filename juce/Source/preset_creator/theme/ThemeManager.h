#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include <atomic>
#include <cmath>
#include "Theme.h"

class ThemeManager
{
public:
	static ThemeManager& getInstance();

	bool loadTheme(const juce::File& themeFile);
	bool saveTheme(const juce::File& themeFile);
	void applyTheme();
	void resetToDefault();
	void applyFonts(ImGuiIO& io);
	void requestFontReload();
	bool consumeFontReloadRequest();
	void rebuildFontsNow();
	
	// Persistence
	void saveUserThemePreference(const juce::String& themeFilename);
	bool loadUserThemePreference();

	const Theme& getCurrentTheme() const;
	Theme& getEditableTheme();  // For theme editor - returns mutable reference
	juce::String getCurrentThemeFilename() const { return m_currentThemeFilename; }  // Get filename of currently loaded theme

	// Colors
	ImU32 getCategoryColor(ModuleCategory cat, bool hovered = false);
	ImU32 getPinColor(PinDataType type);
	ImU32 getPinConnectedColor();
	ImU32 getPinDisconnectedColor();
	ImU32 getCategoryColor(ModuleCategory category) const;

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
	ImU32 getSelectionRect() const;
	ImU32 getSelectionRectOutline() const;
	
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
	juce::String m_currentThemeFilename;  // Filename of currently loaded theme (empty if default)

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

/**
 * Utility functions for automatic text color adjustment based on background luminance.
 * Implements WCAG 2.1 relative luminance calculation for accessibility.
 */
namespace ThemeUtils
{
	/**
	 * Calculate relative luminance of a color using WCAG 2.1 formula.
	 * Returns a value between 0.0 (black) and 1.0 (white).
	 * 
	 * @param color ImU32 color in format IM_COL32(R, G, B, A)
	 * @return Relative luminance (0.0 = darkest, 1.0 = lightest)
	 */
	inline float calculateRelativeLuminance(ImU32 color)
	{
		// Extract RGB components (ImU32 is typically ARGB or RGBA depending on endianness)
		// ImGui uses IM_COL32(R, G, B, A) which creates RGBA format
		ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
		
		// Convert sRGB to linear RGB (gamma correction)
		auto toLinear = [](float c) -> float
		{
			if (c <= 0.04045f)
				return c / 12.92f;
			return std::pow((c + 0.055f) / 1.055f, 2.4f);
		};
		
		float r = toLinear(rgba.x);
		float g = toLinear(rgba.y);
		float b = toLinear(rgba.z);
		
		// Calculate relative luminance using WCAG 2.1 coefficients
		// These weights account for human eye sensitivity to different colors
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	}
	
	/**
	 * Calculate contrast ratio between two colors using WCAG 2.1 formula.
	 * Returns a value >= 1.0, where higher values indicate better contrast.
	 * WCAG AA requires 4.5:1 for normal text, 3:1 for large text.
	 * 
	 * @param color1 First color (lighter should be first for accurate ratio)
	 * @param color2 Second color (darker should be second)
	 * @return Contrast ratio (1.0 = no contrast, 21.0 = maximum contrast)
	 */
	inline float calculateContrastRatio(ImU32 color1, ImU32 color2)
	{
		float l1 = calculateRelativeLuminance(color1);
		float l2 = calculateRelativeLuminance(color2);
		
		// Ensure lighter color is first
		if (l1 < l2)
		{
			float temp = l1;
			l1 = l2;
			l2 = temp;
		}
		
		// WCAG contrast ratio formula: (L1 + 0.05) / (L2 + 0.05)
		return (l1 + 0.05f) / (l2 + 0.05f);
	}
	
	/**
	 * Get optimal text color (black or white) for a given background color.
	 * Uses contrast ratio calculation to ensure WCAG-compliant legibility.
	 * Chooses the text color (black or white) that provides the best contrast.
	 * 
	 * @param backgroundColor Background color in ImU32 format
	 * @param minContrast Minimum contrast ratio required (default 4.5 for WCAG AA)
	 * @return IM_COL32(0, 0, 0, 255) for black text, IM_COL32(255, 255, 255, 255) for white text
	 */
	inline ImU32 getOptimalTextColor(ImU32 backgroundColor, float minContrast = 4.5f)
	{
		// Define pure black and white
		const ImU32 blackText = IM_COL32(0, 0, 0, 255);
		const ImU32 whiteText = IM_COL32(255, 255, 255, 255);
		
		// Calculate contrast ratios for both text color options
		// calculateContrastRatio ensures lighter color is first, so order doesn't matter
		float contrastWithBlack = calculateContrastRatio(backgroundColor, blackText);
		float contrastWithWhite = calculateContrastRatio(whiteText, backgroundColor);
		
		// Choose the option with better contrast
		// This handles edge cases like bright yellow where white text has poor contrast
		if (contrastWithBlack >= contrastWithWhite)
		{
			// Black text provides better or equal contrast
			// This will be chosen for light backgrounds (yellow, light green, etc.)
			return blackText;
		}
		else
		{
			// White text provides better contrast
			// This will be chosen for dark backgrounds
			return whiteText;
		}
	}
}


