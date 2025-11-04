#pragma once

#include <juce_core/juce_core.h>
#include "Theme.h"

class ThemeManager
{
public:
	static ThemeManager& getInstance();

	bool loadTheme(const juce::File& themeFile);
	bool saveTheme(const juce::File& themeFile);
	void applyTheme();
	void resetToDefault();
	
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
	ImU32 getGridColor() const;
	ImU32 getGridOriginColor() const;
	float getGridSize() const;
	ImU32 getScaleTextColor() const;
	float getScaleInterval() const;
	ImU32 getDropTargetOverlay() const;
	ImU32 getMousePositionText() const;

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
};


