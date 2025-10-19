"""
Always-On HUD System
Clean HUD with proper spacing and shortened paths
"""

import pyglet
from pyglet import shapes, text
from .style import UIStyle
from typing import Optional, Dict, Any

class AlwaysOnHUD:
    """Always-visible HUD with clean design"""
    
    def __init__(self, game):
        self.game = game
        self.theme = getattr(game, 'ui_theme', None)
        self.style = UIStyle(self.theme)
        
        # HUD dimensions and positioning with proper margins
        self.margin = self.style.margin
        self.padding = self.style.padding
        self.gap = self.style.gap
        
        # Colors will be retrieved dynamically from color manager
        
        # Font sizes
        self.title_size = self.style.title_size
        self.info_size = self.style.info_size
        self.preset_size = self.style.preset_size
        
        # Box dimensions (will be calculated dynamically)
        self.box_radius = 6  # Rounded corners
        self.box_padding = 8

        # Tool panel fixed size (top-right)
        self.tool_panel_width = 300
        self.tool_panel_height = 110
        
    def draw(self):
        """Draw the always-on HUD"""
        try:
            # Draw grid system overlay (behind UI)
            if hasattr(self.game, 'grid_system'):
                self.game.grid_system.draw()
            
            # Draw left panel (Audio)
            self._draw_audio_panel()
            
            # Draw right panel (Tool)
            self._draw_tool_panel()
            
        except Exception as e:
            print(f"ERROR drawing HUD: {e}")
            import traceback
            traceback.print_exc()
    
    # Background is drawn by the renderer; HUD stays transparent over the scene
    
    def _draw_audio_panel(self):
        """Draw modular audio panel with separate L/R boxes"""
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
                grid_calc = self.game.grid_system.grid_calc
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
            self._label(left_text, self.info_size, start_x + self.box_padding, l_y + 6, self.style.color_mgr.text_primary)
            
            # R Selection Box (independent sizing)
            self.style.draw_box(start_x, r_y, right_width, r_height)
            self._label(right_text, self.info_size, start_x + self.box_padding, r_y + 6, self.style.color_mgr.text_primary)
            
            # Preset buttons row (ABSOLUTE position, use max width for centering)
            max_width = max(left_width, right_width)
            self._draw_preset_buttons(start_x, preset_y, max_width)
            
        except Exception as e:
            print(f"ERROR drawing audio panel: {e}")
            import traceback
            traceback.print_exc()
    
    def _draw_tool_panel(self):
        """Draw a compact tool panel on the top-right with the same sci-fi style"""
        try:
            # Position using grid system for perfect alignment
            if hasattr(self.game, 'grid_system'):
                # Snap to main grid (100px) for perfect alignment
                # Position at exact grid intersection (right side, top)
                x = self.game.width - 100 - self.tool_panel_width  # Snap to grid from right
                y = self.game.height - 100 - self.tool_panel_height  # Snap to grid from top
            else:
                # Fallback to old positioning
                x = self.game.width - self.margin - self.tool_panel_width
                y = self.game.height - self.margin - self.tool_panel_height

            # Clean styling matching AUDIO panel - no big box, just title and one-line boxes
            tool_category = self._get_current_tool_category()
            title_color = self.style.category_color(tool_category)
            
            # Title (no background box, just text)
            self._label("TOOL", self.title_size, x, y, title_color, bold=True)
            
            # Get tool information
            tool_name = self._get_current_tool_name()
            current_tool = self._get_current_tool()
            tool_info = self._get_tool_info(current_tool)
            
            # Calculate positions using grid spacing
            if hasattr(self.game, 'grid_system'):
                grid_spacing = 20  # Layout grid spacing
                name_y = y - grid_spacing - 10
                info_y = name_y - grid_spacing - 4
            else:
                name_y = y - 35
                info_y = name_y - 32
            
            # Name box (one-line, auto-sized)
            name_text = f"Name: {tool_name}"
            name_width = max(120, self.style.measure_text_width(name_text, self.info_size) + self.box_padding * 2)
            self.style.draw_box(x, name_y, name_width, 24)
            self._label(name_text, self.info_size, x + self.box_padding, name_y + 6, self.style.color_mgr.text_primary)
            
            # Info box (one-line, auto-sized, accent colored)
            if tool_info:
                info_width = max(80, self.style.measure_text_width(tool_info, self.info_size) + self.box_padding * 2)
                accent_color = self.style.category_color(tool_category)
                self.style.draw_box(x, info_y, info_width, 24, bg_color=accent_color)
                self._label(tool_info, self.info_size, x + self.box_padding, info_y + 6, self.style.color_mgr.text_primary)
        except Exception as e:
            print(f"ERROR drawing tool panel: {e}")
            import traceback
            traceback.print_exc()
    
    def _get_shortened_selection(self, selector: str) -> str:
        """Return compact selector text:
        - samples => "<subfolder>\\<filename>"
        - frequencies => "frequencies\\<name>"
        - noise => "noise\\<name>"
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
    
    def _draw_rounded_box(self, x: int, y: int, width: int, height: int, bg_color: tuple, outline_color: tuple):
        """Draw a rounded rectangle box with subtle outline"""
        try:
            # For now, use regular rectangles with subtle outline
            # TODO: Implement proper rounded rectangles when pyglet supports it better
            
            # Background
            bg = shapes.Rectangle(x, y, width, height, color=bg_color)
            bg.opacity = 200  # Fixed opacity for UI panels
            bg.draw()
            
            # Subtle outline (1px border)
            outline = shapes.Rectangle(x, y, width, height, color=outline_color)
            outline.opacity = 80
            outline.draw()
            
            # Inner highlight for sci-fi effect
            from .color_manager import get_color_manager
            color_mgr = get_color_manager()
            highlight = shapes.Rectangle(x + 1, y + 1, width - 2, height - 2, color=color_mgr.text_primary)
            highlight.opacity = 20
            highlight.draw()
            
        except Exception as e:
            print(f"ERROR drawing rounded box: {e}")
    
    def _draw_preset_buttons(self, start_x: int, start_y: int, max_width: int):
        """Draw preset buttons in keyboard layout: 1-9, 0
        The row is LEFT-ANCHORED; it never recenters based on content.
        """
        try:
            active_preset = self._get_active_preset()
            order = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0]
            btn_size = 22
            btn_spacing = 4
            # Left-anchored layout (no horizontal movement)
            offset_x = 0

            for idx, val in enumerate(order):
                btn_x = start_x + offset_x + idx * (btn_size + btn_spacing)
                btn_y = start_y
                
                # Button colors
                if active_preset == val:
                    bg_color = self.style.color_mgr.accent_cyan
                    text_color = self.style.color_mgr.preset_active_text
                else:
                    bg_color = self.style.color_mgr.preset_inactive
                    text_color = self.style.color_mgr.text_secondary
                
                # Draw button
                self.style.draw_box(btn_x, btn_y, btn_size, btn_size, bg_color=bg_color)
                
                # Button text
                self._label(str(val), self.preset_size, btn_x + btn_size//2, btn_y + btn_size//2 + 2, 
                           text_color, anchor_x='center', anchor_y='center')
        except Exception as e:
            print(f"ERROR drawing preset buttons: {e}")
    
    def _calculate_text_width(self, value: str, font_size: int) -> int:
        """Calculate actual text width using font metrics"""
        try:
            # Use theme font if available, otherwise fallback to SpaceMono
            if self.theme and hasattr(self.theme, 'ui_font_names'):
                font_names = self.theme.ui_font_names
            else:
                font_names = ["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"]
            
            # Create a temporary label to measure text width
            temp_label = text.Label(
                value,
                font_name=font_names,
                font_size=font_size,
                x=0, y=0
            )
            # Add some extra padding for safety
            return int(temp_label.content_width) + 10
        except Exception as e:
            print(f"ERROR calculating text width: {e}")
            # Fallback to character-based estimation for monospace
            return int(len(value) * (font_size * 0.6) + 10)
    
    def _truncate_text(self, text: str, max_width: int) -> str:
        """Truncate text to fit within max_width characters"""
        # More aggressive truncation for UI display
        if len(text) <= max_width:
            return text
        
        # If text is too long, truncate and add ellipsis
        if max_width > 3:
            return text[:max_width-3] + "..."
        else:
            return text[:max_width]
    
    def _label(self, value: str, font_size: int, x: int, y: int, color: tuple, 
               bold: bool = False, anchor_x: str = 'left', anchor_y: str = 'baseline'):
        """Helper to create and draw a themed label with SpaceMono font"""
        try:
            # Ensure color is a valid RGB tuple
            if not isinstance(color, (tuple, list)) or len(color) < 3:
                color = self.style.color_mgr.text_primary  # Default to primary text color
            
            # Use theme font if available, otherwise fallback to SpaceMono
            if self.theme and hasattr(self.theme, 'ui_font_names'):
                font_names = self.theme.ui_font_names
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