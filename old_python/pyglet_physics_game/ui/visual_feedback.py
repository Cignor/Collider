"""
Visual Feedback System
Time-sensitive visual cues for modifications with fade-out effects
"""

import time
import pyglet
from pyglet import shapes, text
from typing import List, Dict, Any, Optional

class VisualFeedback:
    """Manages time-sensitive visual feedback for user actions"""
    
    def __init__(self, game):
        self.game = game
        self.theme = getattr(game, 'ui_theme', None)
        
        # Feedback messages queue
        self.messages: List[Dict[str, Any]] = []
        
        # Visual settings
        self.feedback_duration = 2.0  # seconds
        self.fade_duration = 0.5      # fade out duration
        self.max_messages = 3         # max concurrent messages
        
        # Positioning (bottom-right corner)
        self.base_x = game.width - 300
        self.base_y = 100
        self.message_height = 60
        self.message_spacing = 10
        
        # Colors will be retrieved dynamically from color manager
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
        
        # Font sizes
        self.title_size = 14
        self.detail_size = 12
    
    def show_feedback(self, title: str, detail: str = "", 
                     feedback_type: str = "info", duration: float = None):
        """Show a visual feedback message"""
        if duration is None:
            duration = self.feedback_duration
            
        message = {
            'title': title,
            'detail': detail,
            'type': feedback_type,
            'start_time': time.time(),
            'duration': duration,
            'id': len(self.messages)  # Simple ID for uniqueness
        }
        
        # Add to messages list
        self.messages.append(message)
        
        # Limit concurrent messages
        if len(self.messages) > self.max_messages:
            self.messages.pop(0)
        
        print(f"DEBUG: Visual feedback - {title}: {detail}")
    
    def show_parameter_change(self, parameter_name: str, old_value: Any, new_value: Any, unit: str = ""):
        """Show parameter change feedback"""
        title = f"Parameter Changed"
        detail = f"{parameter_name}: {old_value} → {new_value}"
        if unit:
            detail += f" {unit}"
        
        self.show_feedback(title, detail, "info")
    
    def show_preset_action(self, action: str, preset_num: int, details: str = ""):
        """Show preset-related feedback"""
        title = f"Preset {action}"
        detail = f"Preset {preset_num}"
        if details:
            detail += f" - {details}"
        
        self.show_feedback(title, detail, "success")
    
    def show_audio_selection(self, selector: str, selection: str):
        """Show audio selection feedback"""
        title = f"{selector} Audio Selected"
        detail = selection
        
        self.show_feedback(title, detail, "success")
    
    def show_tool_change(self, old_tool: str, new_tool: str):
        """Show tool change feedback"""
        title = "Tool Changed"
        detail = f"{old_tool} → {new_tool}"
        
        self.show_feedback(title, detail, "info")
    
    def show_error(self, title: str, detail: str = ""):
        """Show error feedback"""
        self.show_feedback(title, detail, "error")
    
    def show_warning(self, title: str, detail: str = ""):
        """Show warning feedback"""
        self.show_feedback(title, detail, "warning")
    
    def update(self, dt: float):
        """Update feedback messages (remove expired ones)"""
        current_time = time.time()
        
        # Remove expired messages
        self.messages = [
            msg for msg in self.messages 
            if current_time - msg['start_time'] < msg['duration']
        ]
    
    def draw(self):
        """Draw all active feedback messages"""
        if not self.messages:
            return
        
        try:
            current_time = time.time()
            
            for i, message in enumerate(self.messages):
                # Calculate position (stacked from bottom)
                y_pos = self.base_y + (i * (self.message_height + self.message_spacing))
                
                # Calculate fade effect
                elapsed = current_time - message['start_time']
                fade_start = message['duration'] - self.fade_duration
                
                if elapsed < fade_start:
                    # Full opacity
                    opacity = 1.0
                else:
                    # Fade out
                    fade_progress = (elapsed - fade_start) / self.fade_duration
                    opacity = max(0.0, 1.0 - fade_progress)
                
                # Skip if fully transparent
                if opacity <= 0:
                    continue
                
                # Draw message
                self._draw_message(message, self.base_x, y_pos, opacity)
                
        except Exception as e:
            print(f"ERROR drawing visual feedback: {e}")
    
    def _draw_message(self, message: Dict[str, Any], x: int, y: int, opacity: float):
        """Draw a single feedback message"""
        # Get colors based on type
        border_color = self._get_type_color(message['type'])
        
        # Adjust colors for opacity
        bg_color_base = (*self.color_mgr.feedback_bg, 220)
        bg_color = tuple(int(c * opacity) for c in bg_color_base[:3]) + (int(bg_color_base[3] * opacity),)
        border_color_adj = tuple(int(c * opacity) for c in border_color[:3]) + (int(border_color[3] * opacity),)
        text_color_base = (*self.color_mgr.text_primary, 255)
        text_color_adj = tuple(int(c * opacity) for c in text_color_base[:3]) + (int(text_color_base[3] * opacity),)
        
        # Draw background
        bg = shapes.Rectangle(x, y, 280, self.message_height, color=bg_color[:3])
        bg.opacity = bg_color[3]
        bg.draw()
        
        # Draw border
        border = shapes.Rectangle(x, y, 280, self.message_height, color=border_color_adj[:3])
        border.opacity = border_color_adj[3]
        border.draw()
        
        # Draw title
        title_y = y + self.message_height - 20
        self._label(message['title'], self.title_size, x + 10, title_y, 
                   text_color_adj, bold=True)
        
        # Draw detail if present
        if message['detail']:
            detail_y = y + 10
            self._label(message['detail'], self.detail_size, x + 10, detail_y, 
                       text_color_adj)
    
    def _get_type_color(self, feedback_type: str) -> tuple:
        """Get color based on feedback type"""
        color_map = {
            'info': (*self.color_mgr.feedback_border, 255),
            'success': (*self.color_mgr.feedback_success, 255),
            'warning': (*self.color_mgr.feedback_warning, 255),
            'error': (*self.color_mgr.feedback_error, 255)
        }
        return color_map.get(feedback_type, (*self.color_mgr.feedback_border, 255))
    
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
