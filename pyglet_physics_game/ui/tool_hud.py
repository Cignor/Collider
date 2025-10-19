"""
Tool HUD Panel
Clean, modular tool panel with proper grid alignment
"""

import pyglet
from pyglet import shapes, text
from .style import UIStyle
from typing import Optional, Dict, Any

class ToolHUD:
    """Modular tool panel with clean design"""
    
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
        
        # Box dimensions
        self.box_radius = 6  # Rounded corners
        self.box_padding = 8
        
    def draw(self):
        """Draw the tool panel"""
        try:
            # Position using grid system for perfect alignment - SAME LEVEL AS AUDIO PANEL
            if hasattr(self.game, 'grid_system'):
                # Snap to main grid (100px) for perfect alignment
                # Position at EXACT SAME Y level as audio panel for horizontal alignment
                x = self.game.width - 300  # Leave room for panel width
                y = self.game.height - 100  # SAME as audio panel: self.game.height - 100
            else:
                # Fallback to old positioning
                x = self.game.width - self.margin - 300
                y = self.game.height - self.margin - 110

            # Calculate positions using grid spacing - MATCH AUDIO PANEL SPACING
            if hasattr(self.game, 'grid_system'):
                # Use EXACT SAME grid spacing as audio panel for perfect alignment
                grid_spacing = 20  # Layout grid spacing - SAME as audio panel
                title_y = y - 25  # SAME as audio panel: title_y = start_y - 25
                name_y = title_y - grid_spacing - 10  # SAME as audio panel: l_y = title_y - grid_spacing - 10
                info_y = name_y - grid_spacing - 4    # SAME as audio panel: r_y = l_y - grid_spacing - 4
            else:
                title_y = y - 25
                name_y = y - 35
                info_y = name_y - 32
            
            # Clean styling matching AUDIO panel - no big box, just title and one-line boxes
            tool_category = self._get_current_tool_category()
            title_color = self.style.category_color(tool_category)
            
            # Title (no background box, just text) - use calculated title_y
            self._label("TOOL", self.title_size, x, title_y, title_color, bold=True)
            
            # Get tool information
            tool_name = self._get_current_tool_name()
            current_tool = self._get_current_tool()
            tool_info = self._get_tool_info(current_tool)
            
            # Name box (one-line, auto-sized)
            name_text = f"Name: {tool_name}"
            name_width = max(120, self.style.measure_text_width(name_text, self.info_size) + self.box_padding * 2)
            self.style.draw_box(x, name_y, name_width, 24)
            # Simple text positioning
            self._label(name_text, self.info_size, x + self.box_padding, name_y + 6, self.style.color_mgr.text_primary)
            
            # Info box (one-line, auto-sized, accent colored)
            if tool_info:
                info_width = max(80, self.style.measure_text_width(tool_info, self.info_size) + self.box_padding * 2)
                accent_color = self.style.category_color(tool_category)
                self.style.draw_box(x, info_y, info_width, 24, bg_color=accent_color)
                # Simple text positioning
                self._label(tool_info, self.info_size, x + self.box_padding, info_y + 6, self.style.color_mgr.text_primary)
                
        except Exception as e:
            print(f"ERROR drawing tool panel: {e}")
            import traceback
            traceback.print_exc()
    
    def _get_current_tool_name(self) -> str:
        """Get current tool name"""
        if hasattr(self.game, 'tools') and hasattr(self.game, 'current_tool_index'):
            if 0 <= self.game.current_tool_index < len(self.game.tools):
                tool = self.game.tools[self.game.current_tool_index]
                return getattr(tool, 'name', tool.__class__.__name__)
        return "Unknown"
    
    def _get_current_tool_category(self) -> str:
        """Get current tool category"""
        if hasattr(self.game, 'tools') and hasattr(self.game, 'current_tool_index'):
            if 0 <= self.game.current_tool_index < len(self.game.tools):
                tool = self.game.tools[self.game.current_tool_index]
                if hasattr(tool, 'category'):
                    return tool.category
        return "Unknown"
    
    def _get_current_tool(self):
        """Get current tool object"""
        if hasattr(self.game, 'tools') and hasattr(self.game, 'current_tool_index'):
            if 0 <= self.game.current_tool_index < len(self.game.tools):
                return self.game.tools[self.game.current_tool_index]
        return None
    
    def _get_tool_info(self, tool) -> str:
        """Get tool-specific information for display"""
        if not tool:
            return None
            
        # Get tool-specific properties
        if hasattr(tool, 'thickness'):
            return f"Size: {tool.thickness}px"
        elif hasattr(tool, 'wind_strength'):
            return f"Force: {tool.wind_strength}"
        elif hasattr(tool, 'magnet_strength'):
            return f"Force: {tool.magnet_strength}"
        elif hasattr(tool, 'teleport_radius'):
            return f"Range: {tool.teleport_radius}px"
        else:
            return "Active"
    
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
