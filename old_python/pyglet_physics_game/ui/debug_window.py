"""
Debug Window System
Development information display toggleable with F12
"""

import pyglet
from pyglet import shapes, text
from typing import Dict, Any, List
import time

class DebugWindow:
    """Debug information window for development"""
    
    def __init__(self, game):
        self.game = game
        self.theme = getattr(game, 'ui_theme', None)
        
        # Window state
        self.is_visible = False
        
        # Window dimensions and positioning (bottom-right corner)
        self.width = 900  # Even wider to prevent any text overflow
        self.height = 500  # Much taller for more content
        self.x = self.game.width - self.width - 20  # Right side with margin
        self.y = 200  # Higher up to avoid covering HUD
        
        # Colors will be retrieved dynamically from color manager
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
        
        # Font sizes
        self.header_size = 12
        self.info_size = 10
        
        # Debug information categories
        self.debug_info = {}
        self.update_timer = 0
        self.update_interval = 0.5  # Update every 500ms (much less frequent)
        
        # PERFORMANCE: Cache labels to avoid recreating every frame
        self._cached_labels = []
        self._last_debug_info = None
    
    def toggle(self):
        """Toggle debug window visibility"""
        self.is_visible = not self.is_visible
        print(f"DEBUG: Debug window {'shown' if self.is_visible else 'hidden'}")
    
    def update(self, dt: float):
        """Update debug information"""
        if not self.is_visible:
            return
        
        self.update_timer += dt
        if self.update_timer >= self.update_interval:
            self._collect_debug_info()
            self.update_timer = 0
    
    def _collect_debug_info(self):
        """Collect current debug information - OPTIMIZED for performance"""
        try:
            # PERFORMANCE: Only collect essential debug info
            self.debug_info = {
                'Performance': {
                    'FPS': f"{self.game.performance_monitor.get_fps():.0f}" if hasattr(self.game, 'performance_monitor') else "N/A",
                    'Objects': len(getattr(self.game, 'physics_objects', [])),
                    'Collision Shapes': len(getattr(self.game.physics_manager, 'space', {}).shapes) if hasattr(self.game, 'physics_manager') and hasattr(self.game.physics_manager, 'space') else 0,
                },
                
                'Game State': {
                    'Current Tool': getattr(self.game.tools[self.game.current_tool_index], 'name', 'Unknown') if hasattr(self.game, 'tools') else 'None',
                    'Trails': getattr(self.game.trail_system, 'trails_enabled', False) if hasattr(self.game, 'trail_system') else False,
                    'Particles': len(getattr(self.game.particle_system, 'particles', [])) if hasattr(self.game, 'particle_system') else 0,
                },
                
                'Optimization': {
                    'Original Shapes': getattr(self.game.performance_monitor, 'original_shapes', 0) if hasattr(self.game, 'performance_monitor') else 0,
                    'Optimized Shapes': getattr(self.game.performance_monitor, 'optimized_shapes', 0) if hasattr(self.game, 'performance_monitor') else 0,
                    'Simplification Ratio': f"{getattr(self.game.performance_monitor, 'simplification_ratio', 0):.2f}%" if hasattr(self.game, 'performance_monitor') else "0.00%",
                },
                'Audio Clock': self._collect_audio_clock(),
            }
            
        except Exception as e:
            import traceback
            error_details = traceback.format_exc()
            self.debug_info = {
                'Error': {
                    'Debug Info Error': str(e),
                    'Error Type': type(e).__name__,
                    'Full Traceback': error_details[:200] + '...' if len(error_details) > 200 else error_details
                }
            }

    def _collect_audio_clock(self) -> Dict[str, Any]:
        try:
            eng = getattr(self.game, 'audio_engine', None)
            if not eng:
                return {'status': 'no engine'}
            ts = eng.get_clock_status() if hasattr(eng, 'get_clock_status') else {}
            return {
                'time_s': f"{float(ts.get('time_s', 0.0)):.3f}",
                'bpm': f"{float(ts.get('bpm', 0.0)):.1f}",
                'ppq': f"{float(ts.get('ppq', 0.0)):.0f}",
                'pulse': f"{float(ts.get('pulse', 0.0)):.1f}",
            }
        except Exception:
            return {'status': 'error'}
    
    def _get_selection_text(self, selector: str) -> str:
        """Get selection text for debug display"""
        try:
            if hasattr(self.game, 'ui_manager') and hasattr(self.game.ui_manager, 'menu'):
                selection = getattr(self.game.ui_manager.menu, f'{selector}_selection', {})
                if selection:
                    if selection.get('type') == 'samples':
                        folder = selection.get('folder', 'unknown')
                        file = selection.get('file', 'none')
                        if file and file != 'none':
                            filename = file.split('/')[-1] if '/' in file else file
                            return f"{folder}/{filename}"
                        else:
                            return f"{folder}/none"
                    else:
                        return f"{selection.get('type', 'none')}"
            return "none"
        except:
            return "error"
    
    def draw(self):
        """Draw the debug window - OPTIMIZED with caching"""
        if not self.is_visible:
            return
        
        try:
            # Draw background and border using the game's batch system to ensure proper layering
            # Get the game's batch and UI group for proper rendering order
            from pyglet_physics_game.game.game_core import PhysicsGame
            if hasattr(self.game, 'renderer') and hasattr(self.game.renderer, 'batch'):
                batch = self.game.renderer.batch
                ui_group = self.game.renderer.ui_group
            else:
                # Fallback to direct drawing if batch not available
                batch = None
                ui_group = None
            
            # Draw background using batch system for proper layering
            bg_color = self.color_mgr.background_ui_panel  # Use theme color
            if batch and ui_group:
                # Use batch system for proper layering
                bg = shapes.Rectangle(self.x, self.y, self.width, self.height, color=bg_color, batch=batch, group=ui_group)
                bg.opacity = 240  # Slightly transparent for better readability
            else:
                # Fallback to direct drawing
                bg = shapes.Rectangle(self.x, self.y, self.width, self.height, color=bg_color)
                bg.opacity = 240
                bg.draw()
            
            # Draw border
            border_color = self.color_mgr.outline_default
            if batch and ui_group:
                border = shapes.Rectangle(self.x, self.y, self.width, self.height, color=border_color, batch=batch, group=ui_group)
                border.opacity = 120
            else:
                border = shapes.Rectangle(self.x, self.y, self.width, self.height, color=border_color)
                border.opacity = 120
                border.draw()
            
            # PERFORMANCE: Only recreate labels if debug info changed
            if self.debug_info != self._last_debug_info:
                self._recreate_cached_labels()
                self._last_debug_info = self.debug_info.copy()
            
            # Draw all cached labels (ultra fast!)
            for label in self._cached_labels:
                label.draw()
            
        except Exception as e:
            print(f"ERROR drawing debug window: {e}")
    
    def _recreate_cached_labels(self):
        """Recreate cached labels when debug info changes"""
        try:
            # Clear old labels
            self._cached_labels = []
            
            # Draw title
            title_y = self.y + self.height - 25
            header_color = (*self.color_mgr.debug_info, 255)
            title_label = self._create_label("DEBUG WINDOW (F12)", self.header_size, 
                                           self.x + 10, title_y, header_color, bold=True)
            self._cached_labels.append(title_label)
            
            # Draw debug information
            current_y = title_y - 30
            for category, items in self.debug_info.items():
                # Category header
                header_label = self._create_label(f"{category}:", self.header_size, 
                                                self.x + 10, current_y, header_color, bold=True)
                self._cached_labels.append(header_label)
                current_y -= 20
                
                # Category items
                for key, value in items.items():
                    if current_y < self.y + 20:  # Don't draw outside window
                        break
                    
                    info_text = f"  {key}: {value}"
                    # Wrap long text to fit in the debug window
                    wrapped_lines = self._wrap_text(info_text, self.width - 30)
                    for line in wrapped_lines:
                        if current_y < self.y + 20:  # Don't draw outside window
                            break
                        value_color = (*self.color_mgr.debug_success, 255)
                        line_label = self._create_label(line, self.info_size, 
                                                      self.x + 15, current_y, value_color)
                        self._cached_labels.append(line_label)
                        current_y -= 15
                
                current_y -= 5  # Extra spacing between categories
                
        except Exception as e:
            print(f"ERROR recreating cached labels: {e}")
    
    def _create_label(self, value: str, font_size: int, x: int, y: int, color: tuple, bold: bool = False):
        """Create a label without drawing it (for caching)"""
        try:
            font_names = self.theme.ui_font_names if self.theme else ["Arial"]
            
            # Simulate bold by slightly increasing font size and adjusting color
            draw_size = font_size + (2 if bold else 0)
            col = tuple(min(255, c + 30) for c in color) if bold else color
            
            return text.Label(value, font_size=draw_size, x=x, y=y, color=col, 
                            font_name=font_names, anchor_x='left', anchor_y='baseline')
        except Exception:
            # Fallback to basic label
            return text.Label(value, font_size=font_size, x=x, y=y, color=color,
                            anchor_x='left', anchor_y='baseline')
    
    def _wrap_text(self, text: str, max_width: int) -> list:
        """Wrap text to fit within max_width characters"""
        if len(text) <= max_width:
            return [text]
        
        lines = []
        # Simple character-based wrapping for long error messages
        for i in range(0, len(text), max_width):
            lines.append(text[i:i + max_width])
        
        return lines
    
    def _label(self, value: str, font_size: int, x: int, y: int, color: tuple, bold: bool = False):
        """Helper to create and draw a themed label"""
        try:
            font_names = self.theme.ui_font_names if self.theme else ["Arial"]
            
            # Simulate bold by slightly increasing font size and adjusting color
            draw_size = font_size + (2 if bold else 0)
            col = tuple(min(255, c + 30) for c in color) if bold else color
            
            lbl = text.Label(value, font_size=draw_size, x=x, y=y, color=col, 
                           font_name=font_names, anchor_x='left', anchor_y='baseline')
            lbl.draw()
        except Exception:
            # Fallback to basic label
            text.Label(value, font_size=font_size, x=x, y=y, color=color,
                      anchor_x='left', anchor_y='baseline').draw()
