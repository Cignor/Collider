"""
Audio HUD Panel
Clean, modular audio panel with proper grid alignment
"""

import pyglet
from pyglet import shapes, text
from .style import UIStyle
from typing import Optional, Dict, Any

class AudioHUD:
    """Modular audio panel with clean design"""
    
    def __init__(self, game):
        self.game = game
        self.theme = getattr(game, 'ui_theme', None)
        self.style = UIStyle(self.theme)
        self.coordinate_manager = None  # Will be set by modular HUD
        
        # HUD dimensions and positioning with proper margins
        self.margin = self.style.margin
        self.padding = self.style.padding
        self.gap = self.style.gap
        
        # Font sizes
        self.title_size = self.style.title_size
        self.info_size = self.style.info_size
        self.preset_size = self.style.preset_size
        
        # Box dimensions
        self.box_radius = 6  # Rounded corners
        self.box_padding = 8
        
    def draw(self):
        """Draw the audio panel"""
        try:
            # Get selections
            left_selection = self._get_shortened_selection('left')
            right_selection = self._get_shortened_selection('right')
            
            # Calculate text dimensions for independent auto-sizing
            left_text = f"L: {left_selection}"
            right_text = f"R: {right_selection}"
            
            # Proper width calculation using font metrics
            left_width = self.style.measure_text_width(left_text, self.info_size) + self.box_padding * 2
            right_width = self.style.measure_text_width(right_text, self.info_size) + self.box_padding * 2
            min_width = 120  # Minimum width for readability
            
            left_width = max(left_width, min_width)
            right_width = max(right_width, min_width)
            
            # Position elements using grid system for perfect alignment
            if hasattr(self.game, 'grid_system'):
                # Use grid system for positioning - snap to main grid (100px)
                # Position at exact grid intersection for perfect alignment
                start_x = 100  # Snap to main grid line
                start_y = self.game.height - 100  # Snap to main grid line from top
            else:
                # Fallback to old positioning
                start_x = self.style.col_x(self.game.width, 0)
                start_y = self.game.height - self.style.grid_margin
            
            # Title
            title_y = start_y - 25
            self._label("AUDIO", self.title_size, start_x, title_y, self.style.color_mgr.accent_cyan, bold=True)
            
            # Grid-aligned positions for perfect alignment
            if hasattr(self.game, 'grid_system'):
                # Use exact grid spacing for perfect alignment
                grid_spacing = 20  # Layout grid spacing
                l_y = title_y - grid_spacing - 10  # Align to grid
                r_y = l_y - grid_spacing - 4       # Align to grid  
                preset_y = r_y - grid_spacing - 4  # Align to grid
            else:
                # Fallback to old positioning
                l_y = self.style.snap_y(title_y - 35)
                r_y = self.style.snap_y(l_y - 32)
                preset_y = self.style.snap_y(start_y - 120)
            
            l_height = 24
            r_height = 24
            
            # L Selection Box (independent sizing)
            self.style.draw_box(start_x, l_y, left_width, l_height)
            # Simple text positioning - just add some padding from top
            self._label(left_text, self.info_size, start_x + self.box_padding, l_y + 6, self.style.color_mgr.text_primary)
            
            # R Selection Box (independent sizing)
            self.style.draw_box(start_x, r_y, right_width, r_height)
            # Simple text positioning - just add some padding from top
            self._label(right_text, self.info_size, start_x + self.box_padding, r_y + 6, self.style.color_mgr.text_primary)
            
            # Preset buttons row (ABSOLUTE position, use max width for centering)
            max_width = max(left_width, right_width)
            self._draw_preset_buttons(start_x, preset_y, max_width)
            
        except Exception as e:
            print(f"ERROR drawing audio panel: {e}")
            import traceback
            traceback.print_exc()
    
    def _get_shortened_selection(self, selector: str) -> str:
        """Return compact selector text:
        - Shorten long paths to just filename
        - Keep categories short
        """
        try:
            import os
            if hasattr(self.game, 'ui_manager') and hasattr(self.game.ui_manager, 'menu'):
                selection = getattr(self.game.ui_manager.menu, f'{selector}_selection', {})
                if selection:
                    sel_type = selection.get('type')
                    if sel_type == 'samples':
                        # For samples: folder is the subfolder (percussion, ambient, etc.)
                        # file is the full path to the .wav file
                        subfolder = selection.get('folder') or 'samples'
                        file_path = selection.get('file') or ''
                        filename = os.path.basename(file_path) if file_path else 'none'
                        return f"{subfolder}\\{filename}" if filename != 'none' else subfolder
                    elif sel_type in ('frequencies', 'noise'):
                        # For frequencies/noise: preset is the full path to the .json file
                        preset_path = selection.get('preset') or ''
                        name = os.path.splitext(os.path.basename(preset_path))[0] if preset_path else 'unknown'
                        return f"{sel_type}\\{name}"
            return "none"
        except Exception as e:
            print(f"ERROR in _get_shortened_selection: {e}")
            return "error"
    
    def _get_active_preset(self) -> Optional[int]:
        """Get currently active preset number"""
        try:
            if hasattr(self.game, 'ui_manager') and hasattr(self.game.ui_manager, 'menu'):
                preset = self.game.ui_manager.menu.active_preset
                return preset
        except Exception as e:
            print(f"ERROR getting active preset: {e}")
        return None
    
    def _draw_preset_buttons(self, x: int, y: int, max_width: int):
        """Draw preset buttons in a single row - FIXED POSITION, no centering"""
        try:
            # Button dimensions
            button_size = 24
            button_spacing = 4
            
            # FIXED POSITION - start at the same x as the L/R boxes above
            start_x = x
            
            # Get current preset
            current_preset = self._get_active_preset()
            if current_preset is None:
                current_preset = 0
            
            for i in range(10):
                button_x = start_x + i * (button_size + button_spacing)
                button_num = (i + 1) % 10  # 1-9, then 0
                
                # Highlight current preset
                if button_num == current_preset:
                    # Selected button - use accent color
                    self.style.draw_box(button_x, y, button_size, button_size, 
                                      bg_color=self.style.color_mgr.accent_cyan)
                    # Centered text positioning
                    self._label(str(button_num), self.preset_size, button_x + button_size//2, y + button_size//2 + 2, 
                              self.style.color_mgr.background_ui_panel, anchor_x='center', anchor_y='center')
                else:
                    # Unselected button
                    self.style.draw_box(button_x, y, button_size, button_size)
                    # Centered text positioning
                    self._label(str(button_num), self.preset_size, button_x + button_size//2, y + button_size//2 + 2, 
                              self.style.color_mgr.text_secondary, anchor_x='center', anchor_y='center')
                              
        except Exception as e:
            print(f"ERROR drawing preset buttons: {e}")
            import traceback
            traceback.print_exc()
    
    def _label(self, value: str, font_size: int, x: int, y: int, color: tuple, 
               bold: bool = False, anchor_x: str = 'left', anchor_y: str = 'baseline'):
        """Helper to create and draw a themed label with SpaceMono font"""
        try:
            # Ensure color is a valid RGB tuple
            if not isinstance(color, (tuple, list)) or len(color) < 3:
                color = self.style.color_mgr.text_primary  # Default to primary text color
            
            # Use theme font if available, otherwise fallback to SpaceMono
            if hasattr(self.game, 'ui_theme') and self.game.ui_theme and hasattr(self.game.ui_theme, 'ui_font_names'):
                font_names = self.game.ui_theme.ui_font_names
            else:
                font_names = ["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"]
            
            # Simulate bold by slightly increasing font size and adjusting color
            draw_size = font_size + (2 if bold else 0)
            if bold and len(color) >= 3:
                col = tuple(min(255, c + 30) for c in color[:3])
            else:
                col = color[:3] if len(color) >= 3 else self.style.color_mgr.text_primary
            
            # Create label with SpaceMono font
            lbl = text.Label(value, font_name=font_names, font_size=draw_size, x=x, y=y, color=col, 
                           anchor_x=anchor_x, anchor_y=anchor_y)
            lbl.draw()
        except Exception as e:
            print(f"ERROR in _label: {e}")
            # Fallback to basic label
            try:
                from .color_manager import get_color_manager
                color_mgr = get_color_manager()
                text.Label(value, font_size=font_size, x=x, y=y, color=color_mgr.text_primary,
                          anchor_x=anchor_x, anchor_y=anchor_y).draw()
            except Exception as e2:
                print(f"ERROR in fallback _label: {e2}")
