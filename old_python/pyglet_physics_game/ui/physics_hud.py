"""
Physics HUD Panel
Clean, modular physics panel with gravity and wind data
Following the same template as audio_hud.py
"""

import pyglet
from pyglet import shapes, text
from .style import UIStyle
from typing import Optional, Dict, Any

class PhysicsHUD:
    """Modular physics panel with clean design"""
    
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
        """Draw the physics panel"""
        try:
            # Position using grid system for perfect alignment
            if hasattr(self.game, 'grid_system'):
                # Use grid system for positioning - snap to main grid (100px)
                # Position at exact grid intersection for perfect alignment
                start_x = 100  # Snap to main grid line - SAME as audio panel
                # Position UNDER the audio panel
                start_y = self.game.height - 200  # Below audio panel (100px down from audio)
            else:
                # Fallback to old positioning
                start_x = self.style.col_x(self.game.width, 0)
                start_y = self.game.height - self.style.grid_margin - 120
            
            # Title
            title_y = start_y - 25
            self._label("PHYSICS", self.title_size, start_x, title_y, self.style.color_mgr.feedback_success, bold=True)
            
            # Grid-aligned positions for perfect alignment
            if hasattr(self.game, 'grid_system'):
                # Use exact grid spacing for perfect alignment
                grid_spacing = 20  # Layout grid spacing
                gravity_y = title_y - grid_spacing - 10  # Align to grid
                wind_y = gravity_y - grid_spacing - 4    # Align to grid
            else:
                # Fallback to old positioning
                gravity_y = self.style.snap_y(title_y - 35)
                wind_y = self.style.snap_y(gravity_y - 32)
            
            # Get physics data
            gravity_data = self._get_gravity_data()
            wind_data = self._get_wind_data()
            
            # Gravity box (one-line, auto-sized)
            gravity_text = f"Gravity: {gravity_data}"
            gravity_width = max(120, self.style.measure_text_width(gravity_text, self.info_size) + self.box_padding * 2)
            self.style.draw_box(start_x, gravity_y, gravity_width, 24)
            # Simple text positioning
            self._label(gravity_text, self.info_size, start_x + self.box_padding, gravity_y + 6, self.style.color_mgr.text_primary)
            
            # Wind box (one-line, auto-sized, accent colored)
            if wind_data:
                wind_text = f"Wind: {wind_data}"
                wind_width = max(80, self.style.measure_text_width(wind_text, self.info_size) + self.box_padding * 2)
                self.style.draw_box(start_x, wind_y, wind_width, 24, bg_color=self.style.color_mgr.particle_wind)
                # Simple text positioning
                self._label(wind_text, self.info_size, start_x + self.box_padding, wind_y + 6, self.style.color_mgr.text_primary)
            
            # Physics Mode box (new line showing current bullet physics state)
            physics_mode_y = wind_y - 28  # Below wind box
            physics_mode = self._get_physics_mode()
            physics_text = f"Bullet Physics: {physics_mode}"
            physics_width = max(140, self.style.measure_text_width(physics_text, self.info_size) + self.box_padding * 2)
            # Use different colors based on mode
            if physics_mode == "ON":
                bg_color = self.style.color_mgr.feedback_success  # Green for ON
            else:
                bg_color = self.style.color_mgr.feedback_error  # Red for OFF
            self.style.draw_box(start_x, physics_mode_y, physics_width, 24, bg_color=bg_color)
            self._label(physics_text, self.info_size, start_x + self.box_padding, physics_mode_y + 6, self.style.color_mgr.text_primary)
                
        except Exception as e:
            print(f"ERROR drawing physics panel: {e}")
            import traceback
            traceback.print_exc()
    
    def _get_gravity_data(self) -> str:
        """Get current gravity information"""
        try:
            # Get gravity from the game's current_gravity attribute
            if hasattr(self.game, 'current_gravity'):
                gravity = self.game.current_gravity
                # Format gravity vector
                return f"({gravity[0]:.1f}, {gravity[1]:.1f})"
            # Fallback to space gravity
            elif hasattr(self.game, 'space') and hasattr(self.game.space, 'gravity'):
                gravity = self.game.space.gravity
                return f"({gravity.x:.1f}, {gravity.y:.1f})"
            return "Unknown"
        except Exception as e:
            print(f"ERROR getting gravity data: {e}")
            return "Error"
    
    def _get_wind_data(self) -> str:
        """Get current wind information"""
        try:
            # Get wind data from game attributes
            if hasattr(self.game, 'current_wind_strength') and hasattr(self.game, 'wind_direction'):
                strength = self.game.current_wind_strength
                direction = self.game.wind_direction
                
                if strength > 0:
                    # Convert direction to readable text
                    import math
                    degrees = math.degrees(direction)
                    # Normalize degrees to 0-360 range
                    degrees = degrees % 360
                    
                    # Convert to readable direction
                    if degrees < 45 or degrees > 315:
                        direction_text = "Right"
                    elif degrees > 135 and degrees < 225:
                        direction_text = "Left"
                    else:
                        direction_text = f"{degrees:.0f}°"
                    
                    return f"Strength: {strength:.1f}, Dir: {direction_text}"
                else:
                    return "Inactive"
            # Fallback to wind system
            elif hasattr(self.game, 'wind_system'):
                wind_system = self.game.wind_system
                if hasattr(wind_system, 'get_wind_strength') and hasattr(wind_system, 'get_wind_direction'):
                    strength = wind_system.get_wind_strength()
                    direction = wind_system.get_wind_direction()
                    if strength > 0:
                        import math
                        degrees = math.degrees(direction)
                        # Normalize degrees to 0-360 range
                        degrees = degrees % 360
                        
                        # Convert to readable direction
                        if degrees < 45 or degrees > 315:
                            direction_text = "Right"
                        elif degrees > 135 and degrees < 225:
                            direction_text = "Left"
                        else:
                            direction_text = f"{degrees:.0f}°"
                        
                        return f"Strength: {strength:.1f}, Dir: {direction_text}"
                    else:
                        return "Inactive"
            return "Inactive"
        except Exception as e:
            print(f"ERROR getting wind data: {e}")
            return "Error"
    
    def _get_physics_mode(self) -> str:
        """Get current global bullet physics mode (ON/OFF)"""
        try:
            # Check global physics state
            if hasattr(self.game, 'global_physics_enabled'):
                return "ON" if self.game.global_physics_enabled else "OFF"
            
            return "ON"  # Default to ON
        except Exception as e:
            print(f"ERROR getting global physics mode: {e}")
            return "ON"
    
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
