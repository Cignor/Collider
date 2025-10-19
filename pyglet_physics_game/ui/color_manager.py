"""
Enhanced color management system using Palettable and JSON configuration.
Provides professional color palettes and easy theme switching.
"""

import json
import os
from typing import Dict, List, Tuple, Any
from .palette_manager_v2 import get_palette_manager_v2

class ColorManager:
    """Enhanced color manager with Palettable integration"""
    
    def __init__(self, config_path: str = None):
        # Initialize palette manager
        self.palette_manager = get_palette_manager_v2()
        
        # Load JSON colors as fallback
        if config_path is None:
            current_dir = os.path.dirname(os.path.abspath(__file__))
            config_path = os.path.join(current_dir, '..', 'config', 'colors.json')
        
        self.colors = self._load_colors(config_path)
    
    def _load_colors(self, config_path: str) -> Dict[str, Any]:
        """Load colors from JSON file"""
        try:
            with open(config_path, 'r') as f:
                return json.load(f)
        except Exception as e:
            print(f"ERROR loading colors from {config_path}: {e}")
            return self._get_fallback_colors()
    
    def _get_fallback_colors(self) -> Dict[str, Any]:
        """Fallback colors if JSON loading fails"""
        return {
            "background": {"main_game": [45, 50, 60], "ui_panel": [25, 30, 40]},
            "text": {"primary": [220, 230, 240], "secondary": [180, 190, 200]},
            "categories": {"default": [0, 255, 255]},
            "presets": {"active": [0, 255, 255], "inactive": [40, 50, 65]},
            "grids": {"design": {"primary": [235, 220, 120], "secondary": [255, 245, 170], "spacing": 8}}
        }
    
    # Background colors - now using palette manager
    @property
    def background_main_game(self) -> Tuple[int, int, int]:
        """Main game background color"""
        return self.palette_manager.get_color('background_main_game')
    
    @property
    def background_ui_panel(self) -> Tuple[int, int, int]:
        """UI panel background color"""
        return self.palette_manager.get_color('background_ui_panel')
    
    @property
    def background_ui_opacity(self) -> int:
        """UI panel background opacity"""
        return self.colors.get("background", {}).get("ui_panel_opacity", 240)
    
    # Text colors - now using palette manager
    @property
    def text_primary(self) -> Tuple[int, int, int]:
        """Primary text color"""
        return self.palette_manager.get_color('text_primary')
    
    @property
    def text_secondary(self) -> Tuple[int, int, int]:
        """Secondary text color"""
        return self.palette_manager.get_color('text_secondary')
    
    @property
    def text_accent(self) -> Tuple[int, int, int]:
        """Accent text color (cyan)"""
        return self.palette_manager.get_color('accent_cyan')
    
    @property
    def accent_cyan(self) -> Tuple[int, int, int]:
        """Accent cyan color"""
        return self.palette_manager.get_color('accent_cyan')
    
    # Outline colors
    @property
    def outline_default(self) -> Tuple[int, int, int]:
        """Default outline color"""
        return tuple(self.colors["outlines"]["default"])
    
    @property
    def outline_opacity(self) -> int:
        """Default outline opacity"""
        return self.colors["outlines"]["opacity"]
    
    # Category colors - now using palette manager
    def category_color(self, category: str) -> Tuple[int, int, int]:
        """Get color for a specific category"""
        return self.palette_manager.get_category_color(category)
    
    # Preset colors - now using palette manager
    @property
    def preset_active(self) -> Tuple[int, int, int]:
        """Active preset background color"""
        return self.palette_manager.get_color('preset_active')
    
    @property
    def preset_inactive(self) -> Tuple[int, int, int]:
        """Inactive preset background color"""
        return self.palette_manager.get_color('preset_inactive')
    
    @property
    def preset_active_text(self) -> Tuple[int, int, int]:
        """Active preset text color"""
        return (0, 0, 0)  # Dark text for contrast with bright active preset
    
    @property
    def preset_inactive_text(self) -> Tuple[int, int, int]:
        """Inactive preset text color"""
        return self.palette_manager.get_color('text_primary')
    
    # Grid colors - now using palette manager
    def get_grid_colors(self, grid_name: str = None) -> Dict[str, Any]:
        """Get colors and settings for a specific grid"""
        return self.palette_manager.get_grid_colors()
    
    def get_all_grid_names(self) -> List[str]:
        """Get list of all available grid names"""
        return ['design', 'scientific', 'artistic']  # Basic grid types
    
    # Theme management methods
    def get_available_themes(self) -> List[str]:
        """Get list of available themes"""
        return self.palette_manager.get_available_themes()
    
    def set_theme(self, theme_name: str):
        """Switch to a different theme"""
        self.palette_manager.set_theme(theme_name)
    
    def get_current_theme(self) -> str:
        """Get current theme name"""
        return self.palette_manager.current_theme
    
    def get_theme_info(self, theme_name: str) -> Dict[str, str]:
        """Get theme information"""
        return self.palette_manager.get_theme_info(theme_name)
    
    # Material colors
    def get_material_color(self, material_type: str) -> Tuple[int, int, int]:
        """Get color for a specific material type"""
        return self.palette_manager.get_material_color(material_type)
    
    # Trail colors
    def get_trail_color(self, speed: float) -> Tuple[int, int, int]:
        """Get trail color based on speed"""
        return self.palette_manager.get_trail_color(speed)
    
    # Physics colors
    @property
    def collision_center(self) -> Tuple[int, int, int]:
        """Collision center line color"""
        return self.palette_manager.get_color('collision_center')
    
    @property
    def collision_outline(self) -> Tuple[int, int, int]:
        """Collision outline color"""
        return self.palette_manager.get_color('collision_outline')
    
    # Particle colors
    @property
    def particle_wind(self) -> Tuple[int, int, int]:
        """Wind particle color"""
        return self.palette_manager.get_color('particle_wind')
    
    # Gravity colors
    def get_gravity_color(self, is_positive: bool) -> Tuple[int, int, int]:
        """Get gravity color based on direction"""
        return self.palette_manager.get_color('gravity_positive' if is_positive else 'gravity_negative')
    
    # Wind colors
    def get_wind_color(self, is_positive: bool) -> Tuple[int, int, int]:
        """Get wind color based on direction"""
        return self.palette_manager.get_color('wind_positive' if is_positive else 'wind_negative')
    
    # Tool colors
    @property
    def erase_radius(self) -> Tuple[int, int, int]:
        """Erase radius indicator color"""
        return self.palette_manager.get_color('erase_radius')
    
    @property
    def brush_radius(self) -> Tuple[int, int, int]:
        """Brush radius indicator color"""
        return self.palette_manager.get_color('brush_radius')
    
    # Debug colors
    @property
    def debug_text(self) -> Tuple[int, int, int]:
        """Debug text color"""
        return self.palette_manager.get_color('debug_text')
    
    @property
    def debug_success(self) -> Tuple[int, int, int]:
        """Debug success color"""
        return self.palette_manager.get_color('debug_success')
    
    @property
    def debug_warning(self) -> Tuple[int, int, int]:
        """Debug warning color"""
        return self.palette_manager.get_color('debug_warning')
    
    @property
    def debug_info(self) -> Tuple[int, int, int]:
        """Debug info color"""
        return self.palette_manager.get_color('debug_info')
    
    # Feedback colors
    @property
    def feedback_bg(self) -> Tuple[int, int, int]:
        """Feedback background color"""
        return self.palette_manager.get_color('feedback_bg')
    
    @property
    def feedback_border(self) -> Tuple[int, int, int]:
        """Feedback border color"""
        return self.palette_manager.get_color('feedback_border')
    
    @property
    def feedback_success(self) -> Tuple[int, int, int]:
        """Feedback success color"""
        return self.palette_manager.get_color('feedback_success')
    
    @property
    def feedback_warning(self) -> Tuple[int, int, int]:
        """Feedback warning color"""
        return self.palette_manager.get_color('feedback_warning')
    
    @property
    def feedback_error(self) -> Tuple[int, int, int]:
        """Feedback error color"""
        return self.palette_manager.get_color('feedback_error')


# Global color manager instance
_color_manager = None

def get_color_manager() -> ColorManager:
    """Get the global color manager instance"""
    global _color_manager
    if _color_manager is None:
        _color_manager = ColorManager()
    return _color_manager
