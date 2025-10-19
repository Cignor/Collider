"""
Theme Switching System
Allows users to cycle through different color themes using keyboard shortcuts
"""

from typing import List, Dict, Any
from .color_manager import get_color_manager

class ThemeSwitcher:
    """Handles theme switching and cycling"""
    
    def __init__(self):
        self.color_manager = get_color_manager()
        self.themes = self.color_manager.get_available_themes()
        self.current_index = 0
        self._find_current_theme_index()
    
    def _find_current_theme_index(self):
        """Find the index of the current theme"""
        current_theme = self.color_manager.get_current_theme()
        if current_theme in self.themes:
            self.current_index = self.themes.index(current_theme)
        else:
            self.current_index = 0
    
    def next_theme(self):
        """Switch to the next theme"""
        self.current_index = (self.current_index + 1) % len(self.themes)
        theme_name = self.themes[self.current_index]
        self.color_manager.set_theme(theme_name)
        return theme_name
    
    def previous_theme(self):
        """Switch to the previous theme"""
        self.current_index = (self.current_index - 1) % len(self.themes)
        theme_name = self.themes[self.current_index]
        self.color_manager.set_theme(theme_name)
        return theme_name
    
    def set_theme_by_name(self, theme_name: str):
        """Set theme by name"""
        if theme_name in self.themes:
            self.color_manager.set_theme(theme_name)
            self.current_index = self.themes.index(theme_name)
            return True
        return False
    
    def get_current_theme_info(self) -> Dict[str, str]:
        """Get information about the current theme"""
        current_theme = self.themes[self.current_index]
        return self.color_manager.get_theme_info(current_theme)
    
    def get_all_themes_info(self) -> List[Dict[str, str]]:
        """Get information about all available themes"""
        themes_info = []
        for theme_name in self.themes:
            info = self.color_manager.get_theme_info(theme_name)
            info['key'] = theme_name
            themes_info.append(info)
        return themes_info

# Global theme switcher instance
_theme_switcher = None

def get_theme_switcher() -> ThemeSwitcher:
    """Get the global theme switcher instance"""
    global _theme_switcher
    if _theme_switcher is None:
        _theme_switcher = ThemeSwitcher()
    return _theme_switcher
