"""
Modular UI Components System
CSS-like design with proper margins, dynamic sizing, and spacing
"""

import pyglet
from pyglet import shapes, text
from typing import Dict, Any, List, Optional, Tuple
from dataclasses import dataclass

@dataclass
class UIMargin:
    """UI margin configuration"""
    top: int = 8
    right: int = 8
    bottom: int = 8
    left: int = 8

@dataclass
class UIPadding:
    """UI padding configuration"""
    top: int = 6
    right: int = 8
    bottom: int = 6
    left: int = 8

@dataclass
class UISpacing:
    """UI spacing configuration"""
    between_components: int = 8
    between_rows: int = 4
    between_columns: int = 8

class UIComponent:
    """Base UI component with modular design principles"""
    
    def __init__(self, theme=None):
        self.theme = theme
        self.margin = UIMargin()
        self.padding = UIPadding()
        self.spacing = UISpacing()
        
        # Component state
        self.visible = True
        self.width = 0
        self.height = 0
        self.x = 0
        self.y = 0
        
        # Colors will be retrieved dynamically from color manager
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
    
    def calculate_size(self) -> Tuple[int, int]:
        """Calculate component size based on content"""
        return (self.width, self.height)
    
    def set_position(self, x: int, y: int):
        """Set component position"""
        self.x = x
        self.y = y
    
    def draw(self):
        """Draw the component"""
        if not self.visible:
            return
        
        try:
            self._draw_background()
            self._draw_content()
        except Exception as e:
            print(f"ERROR drawing UI component: {e}")
    
    def _draw_background(self):
        """Draw component background"""
        bg_color = (*self.color_mgr.background_ui_panel, 200)
        if bg_color[3] > 0:  # Only draw if not transparent
            bg = shapes.Rectangle(self.x, self.y, self.width, self.height, color=bg_color[:3])
            bg.opacity = bg_color[3]
            bg.draw()
    
    def _draw_content(self):
        """Draw component content - override in subclasses"""
        pass
    
    def _label(self, value: str, font_size: int, x: int, y: int, color: tuple, 
               bold: bool = False, anchor_x: str = 'left', anchor_y: str = 'baseline'):
        """Helper to create and draw a themed label"""
        try:
            font_names = self.theme.ui_font_names if self.theme else ["Arial"]
            
            # Simulate bold by slightly increasing font size and adjusting color
            draw_size = font_size + (2 if bold else 0)
            col = tuple(min(255, c + 30) for c in color) if bold else color
            
            lbl = text.Label(value, font_size=draw_size, x=x, y=y, color=col, 
                           font_name=font_names, anchor_x=anchor_x, anchor_y=anchor_y)
            lbl.draw()
        except Exception:
            # Fallback to basic label
            text.Label(value, font_size=font_size, x=x, y=y, color=color,
                      anchor_x=anchor_x, anchor_y=anchor_y).draw()

class UITextBlock(UIComponent):
    """Simple text block component"""
    
    def __init__(self, text: str, font_size: int = 12, theme=None):
        super().__init__(theme)
        self.text = text
        self.font_size = font_size
        self.bold = False
        
        # Calculate size based on text
        self._calculate_size()
    
    def _calculate_size(self):
        """Calculate size based on text content"""
        # Estimate text width (rough calculation)
        char_width = self.font_size * 0.6
        self.width = int(len(self.text) * char_width) + self.padding.left + self.padding.right
        self.height = self.font_size + self.padding.top + self.padding.bottom
    
    def set_text(self, text: str):
        """Update text and recalculate size"""
        self.text = text
        self._calculate_size()
    
    def _draw_content(self):
        """Draw text content"""
        text_x = self.x + self.padding.left
        text_y = self.y + self.padding.bottom + self.font_size
        text_color = (*self.color_mgr.text_primary, 255)
        self._label(self.text, self.font_size, text_x, text_y, text_color, self.bold)

class UIButton(UIComponent):
    """Button component with hover effects"""
    
    def __init__(self, text: str, width: int = 60, height: int = 25, theme=None):
        super().__init__(theme)
        self.text = text
        self.width = width
        self.height = height
        self.active = False
        self.hovered = False
        
        # Button colors
        self.active_color = (100, 255, 100, 180)
        self.inactive_color = (80, 80, 100, 180)
        self.hover_color = (120, 120, 140, 180)
    
    def _draw_content(self):
        """Draw button content"""
        # Choose color based on state
        if self.active:
            color = self.active_color
        elif self.hovered:
            color = self.hover_color
        else:
            color = self.inactive_color
        
        # Draw button background
        bg = shapes.Rectangle(self.x, self.y, self.width, self.height, color=color[:3])
        bg.opacity = color[3]
        bg.draw()
        
        # Draw button text
        text_x = self.x + self.width // 2
        text_y = self.y + self.height // 2
        self._label(self.text, 10, text_x, text_y, (255, 255, 255, 255), 
                   anchor_x='center', anchor_y='center')

class UIPanel(UIComponent):
    """Panel component that can contain other components"""
    
    def __init__(self, title: str = "", width: int = 200, height: int = 100, theme=None):
        super().__init__(theme)
        self.title = title
        self.width = width
        self.height = height
        self.components: List[UIComponent] = []
        
        # Colors will be retrieved dynamically from color manager
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
    
    def add_component(self, component: UIComponent, x: int = None, y: int = None):
        """Add a component to the panel"""
        if x is None:
            x = self.padding.left
        if y is None:
            y = self.padding.bottom
        
        # Position relative to panel
        component.set_position(self.x + x, self.y + y)
        self.components.append(component)
    
    def _draw_content(self):
        """Draw panel content"""
        # Draw border
        border_color = (*self.color_mgr.outline_default, 255)
        border = shapes.Rectangle(self.x, self.y, self.width, self.height, color=border_color[:3])
        border.opacity = border_color[3]
        border.draw()
        
        # Draw title if present
        if self.title:
            title_x = self.x + self.padding.left
            title_y = self.y + self.height - self.padding.top - 14
            self._label(self.title, 14, title_x, title_y, self.title_color, bold=True)
        
        # Draw components
        for component in self.components:
            component.draw()

class UILayout:
    """Layout manager for positioning components"""
    
    def __init__(self, screen_width: int, screen_height: int, margin: UIMargin = None):
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.margin = margin or UIMargin()
        self.spacing = UISpacing()
        
        # Layout zones
        self.top_left = (self.margin.left, screen_height - self.margin.top)
        self.top_right = (screen_width - self.margin.right, screen_height - self.margin.top)
        self.bottom_left = (self.margin.left, self.margin.bottom)
        self.bottom_right = (screen_width - self.margin.right, self.margin.bottom)
    
    def position_top_left(self, component: UIComponent, offset_x: int = 0, offset_y: int = 0):
        """Position component in top-left area"""
        x = self.top_left[0] + offset_x
        y = self.top_left[1] - component.height - offset_y
        component.set_position(x, y)
    
    def position_top_right(self, component: UIComponent, offset_x: int = 0, offset_y: int = 0):
        """Position component in top-right area"""
        x = self.top_right[0] - component.width - offset_x
        y = self.top_right[1] - component.height - offset_y
        component.set_position(x, y)
    
    def position_bottom_left(self, component: UIComponent, offset_x: int = 0, offset_y: int = 0):
        """Position component in bottom-left area"""
        x = self.bottom_left[0] + offset_x
        y = self.bottom_left[1] + offset_y
        component.set_position(x, y)
    
    def position_bottom_right(self, component: UIComponent, offset_x: int = 0, offset_y: int = 0):
        """Position component in bottom-right area"""
        x = self.bottom_right[0] - component.width - offset_x
        y = self.bottom_right[1] + offset_y
        component.set_position(x, y)
