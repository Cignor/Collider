"""
Enhanced Color Management System using Palettable and JSON themes
Professional color palettes for the Pyglet Physics Game
"""

import json
import os
import glob
from typing import Dict, List, Tuple, Any
from palettable.colorbrewer.qualitative import Dark2_8, Set1_8, Set2_8, Set3_8
from palettable.colorbrewer.sequential import Blues_8, Greens_8, Oranges_8, Purples_8
from palettable.tableau import Tableau_10, Tableau_20
from palettable.wesanderson import Darjeeling2_5, FantasticFox1_5, GrandBudapest1_4
from palettable.scientific.diverging import Roma_10, Vik_10
from palettable.cartocolors.qualitative import Bold_7, Pastel_7

class PaletteManagerV2:
    """Enhanced color management using professional palettes from Palettable and JSON themes"""
    
    def __init__(self):
        self.current_theme = "sci_fi_blue"
        self.palettes = self._initialize_palettes()
        self.themes = {}
        self._load_all_themes()
        self._load_theme(self.current_theme)
    
    def _initialize_palettes(self) -> Dict[str, Any]:
        """Initialize all available palettes"""
        return {
            # Professional UI Palettes
            'tableau_10': Tableau_10,
            'tableau_20': Tableau_20,
            
            # Scientific/Physics Palettes
            'roma_10': Roma_10,
            'vik_10': Vik_10,
            
            # Colorbrewer Qualitative (categorical)
            'dark2_8': Dark2_8,
            'set1_8': Set1_8,
            'set2_8': Set2_8,
            'set3_8': Set3_8,
            
            # Colorbrewer Sequential (gradients)
            'blues_8': Blues_8,
            'greens_8': Greens_8,
            'oranges_8': Oranges_8,
            'purples_8': Purples_8,
            
            # Artistic Palettes
            'darjeeling2_5': Darjeeling2_5,
            'fantastic_fox_5': FantasticFox1_5,
            'grand_budapest_4': GrandBudapest1_4,
            
            # CartoColors
            'bold_7': Bold_7,
            'pastel_7': Pastel_7,
        }
    
    def _load_all_themes(self):
        """Load all theme files from the palettes directory"""
        try:
            # Get the directory where this file is located
            current_dir = os.path.dirname(os.path.abspath(__file__))
            palettes_dir = os.path.join(current_dir, 'palettes')
            
            # Find all JSON files in the palettes directory
            theme_files = glob.glob(os.path.join(palettes_dir, '*.json'))
            
            for theme_file in theme_files:
                try:
                    with open(theme_file, 'r') as f:
                        theme_data = json.load(f)
                    
                    # Extract theme name from filename
                    theme_name = os.path.splitext(os.path.basename(theme_file))[0]
                    
                    # Convert color lists to tuples for consistency
                    if 'colors' in theme_data:
                        for color_key, color_value in theme_data['colors'].items():
                            if isinstance(color_value, list) and len(color_value) >= 3:
                                theme_data['colors'][color_key] = tuple(color_value[:3])
                    
                    self.themes[theme_name] = theme_data
                    print(f"Loaded theme: {theme_data.get('name', theme_name)}")
                    
                except Exception as e:
                    print(f"ERROR loading theme file {theme_file}: {e}")
            
            if not self.themes:
                print("WARNING: No themes loaded, creating fallback theme")
                self._create_fallback_theme()
                
        except Exception as e:
            print(f"ERROR loading themes: {e}")
            self._create_fallback_theme()
    
    def _create_fallback_theme(self):
        """Create a fallback theme if no themes are loaded"""
        self.themes['fallback'] = {
            'name': 'Fallback',
            'description': 'Fallback theme when no themes are available',
            'palette': 'blues_8',
            'colors': {
                'background_main_game': (45, 55, 70),
                'background_ui_panel': (25, 35, 50),
                'text_primary': (200, 220, 240),
                'text_secondary': (150, 170, 190),
                'accent_cyan': (0, 200, 255),
                'accent_blue': (50, 150, 255),
                'accent_purple': (100, 50, 200),
                'category_tools': (255, 100, 200),
                'category_audio': (0, 200, 255),
                'category_physics': (100, 255, 100),
                'preset_active': (0, 200, 255),
                'preset_inactive': (80, 100, 120),
                'grid_primary': (60, 80, 100),
                'grid_secondary': (40, 60, 80),
                'collision_center': (255, 100, 100),
                'collision_outline': (255, 255, 100),
                'material_energy': (0, 255, 255),
                'material_plasma': (255, 0, 255),
                'material_crystal': (0, 255, 0),
                'material_organic': (255, 165, 0),
                'material_void': (128, 0, 128),
                'material_metal': (150, 150, 150),
                'trail_fast': (255, 255, 0),
                'trail_medium': (255, 200, 0),
                'trail_slow': (255, 100, 0),
                'particle_wind': (100, 200, 255),
                'gravity_positive': (0, 255, 0),
                'gravity_negative': (255, 0, 0),
                'wind_positive': (0, 255, 255),
                'wind_negative': (150, 150, 150),
                'erase_radius': (255, 0, 0),
                'brush_radius': (255, 255, 0),
                'debug_text': (255, 255, 255),
                'debug_success': (150, 255, 150),
                'debug_warning': (255, 255, 150),
                'debug_info': (255, 150, 255),
                'feedback_bg': (15, 18, 25),
                'feedback_border': (0, 200, 255),
                'feedback_success': (0, 255, 0),
                'feedback_warning': (255, 255, 0),
                'feedback_error': (255, 0, 0)
            }
        }
    
    def _load_theme(self, theme_name: str):
        """Load a specific theme"""
        if theme_name in self.themes:
            self.current_theme = theme_name
            self.current_colors = self.themes[theme_name]['colors'].copy()
        else:
            print(f"WARNING: Theme '{theme_name}' not found, using fallback")
            self._load_theme('fallback')
    
    def get_color(self, color_name: str) -> Tuple[int, int, int]:
        """Get a color from the current theme"""
        return self.current_colors.get(color_name, (128, 128, 128))
    
    def get_palette_colors(self, palette_name: str, num_colors: int = None) -> List[Tuple[int, int, int]]:
        """Get colors from a specific palette"""
        if palette_name in self.palettes:
            palette = self.palettes[palette_name]
            colors = palette.colors  # RGB tuples 0-255
            if num_colors:
                return colors[:num_colors]
            return colors
        return [(128, 128, 128)]  # Default gray
    
    def get_available_themes(self) -> List[str]:
        """Get list of available theme names"""
        return list(self.themes.keys())
    
    def get_theme_info(self, theme_name: str) -> Dict[str, str]:
        """Get theme information"""
        if theme_name in self.themes:
            theme = self.themes[theme_name]
            return {
                'name': theme['name'],
                'description': theme['description'],
                'palette': theme['palette']
            }
        return {}
    
    def set_theme(self, theme_name: str):
        """Switch to a different theme"""
        self._load_theme(theme_name)
        print(f"Switched to theme: {self.themes[theme_name]['name']}")
    
    def get_category_color(self, category: str) -> Tuple[int, int, int]:
        """Get color for a specific category"""
        category_map = {
            'tools': 'category_tools',
            'audio': 'category_audio', 
            'physics': 'category_physics',
            'unknown': 'category_tools'
        }
        color_key = category_map.get(category.lower(), 'category_tools')
        return self.get_color(color_key)
    
    def get_grid_colors(self, grid_type: str = 'design') -> Dict[str, Any]:
        """Get grid colors for the current theme and grid type"""
        grid_configs = {
            'design': {
                'primary': self.get_color('grid_primary'),
                'secondary': self.get_color('grid_secondary'),
                'spacing': 8,
                'opacity': 30
            },
            'layout': {
                'primary': self.get_color('grid_layout_primary'),
                'secondary': self.get_color('grid_layout_secondary'),
                'spacing': 20,
                'opacity': 50
            },
            'golden': {
                'primary': self.get_color('grid_golden_primary'),
                'secondary': self.get_color('grid_golden_secondary'),
                'spacing': 13,  # Fibonacci-based
                'opacity': 40
            },
            'game': {
                'primary': self.get_color('grid_game_primary'),
                'secondary': self.get_color('grid_game_secondary'),
                'spacing': 100,
                'opacity': 100
            },
            'neon': {
                'primary': self.get_color('grid_neon_primary'),
                'secondary': self.get_color('grid_neon_secondary'),
                'spacing': 25,
                'opacity': 80
            }
        }
        
        return grid_configs.get(grid_type, grid_configs['design'])
    
    def get_material_color(self, material_type: str) -> Tuple[int, int, int]:
        """Get color for a specific material type"""
        material_map = {
            'energy': 'material_energy',
            'plasma': 'material_plasma',
            'crystal': 'material_crystal',
            'organic': 'material_organic',
            'void': 'material_void',
            'metal': 'material_metal'
        }
        color_key = material_map.get(material_type.lower(), 'material_metal')
        return self.get_color(color_key)
    
    def get_trail_color(self, speed: float) -> Tuple[int, int, int]:
        """Get trail color based on speed"""
        if speed > 200:
            return self.get_color('trail_fast')
        elif speed > 100:
            return self.get_color('trail_medium')
        else:
            return self.get_color('trail_slow')
    
    def save_theme_to_file(self, theme_name: str, filename: str = None):
        """Save current theme to JSON file"""
        if not filename:
            filename = f"theme_{theme_name}.json"
        
        theme_data = {
            'name': self.themes[theme_name]['name'],
            'description': self.themes[theme_name]['description'],
            'palette': self.themes[theme_name]['palette'],
            'colors': self.themes[theme_name]['colors']
        }
        
        with open(filename, 'w') as f:
            json.dump(theme_data, f, indent=2)
        print(f"Theme saved to {filename}")
    
    def load_theme_from_file(self, filename: str):
        """Load theme from JSON file"""
        try:
            with open(filename, 'r') as f:
                theme_data = json.load(f)
            
            theme_name = theme_data.get('name', 'custom').lower().replace(' ', '_')
            self.themes[theme_name] = theme_data
            self._load_theme(theme_name)
            print(f"Theme loaded from {filename}")
        except Exception as e:
            print(f"Error loading theme from {filename}: {e}")

# Global instance
_palette_manager_v2 = None

def get_palette_manager_v2() -> PaletteManagerV2:
    """Get the global palette manager instance"""
    global _palette_manager_v2
    if _palette_manager_v2 is None:
        _palette_manager_v2 = PaletteManagerV2()
    return _palette_manager_v2
