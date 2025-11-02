"""
Command Helper Window
Shows available commands and keybindings, toggleable with H key
"""

import pyglet
from pyglet import shapes, text
from typing import List, Dict, Any

class CommandHelper:
    """Command helper window showing available commands"""
    
    def __init__(self, game):
        self.game = game
        self.theme = getattr(game, 'ui_theme', None)
        
        # Window state
        self.is_visible = False
        
        # Window dimensions and positioning
        self.width = 350
        self.height = 400
        self.x = 0
        self.y = 0
        
        # Colors will be retrieved dynamically from color manager
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
        
        # Font sizes
        self.title_size = 16
        self.header_size = 12
        self.key_size = 11
        self.desc_size = 10
        
        # Command categories
        self.commands = self._get_commands()
    
    def _get_commands(self) -> Dict[str, List[Dict[str, str]]]:
        """Get organized command list"""
        return {
            'Movement & Interaction': [
                {'key': 'SPACE', 'desc': 'Spawn physics object'},
                {'key': 'TAB', 'desc': 'Switch tool'},
                {'key': 'C', 'desc': 'Clear all objects'},
                {'key': 'R', 'desc': 'Reset physics'},
                {'key': 'CTRL+Mouse', 'desc': 'Control objects'},
            ],
            'Audio System': [
                {'key': 'SHIFT+LMB', 'desc': 'Set left audio selector'},
                {'key': 'SHIFT+RMB', 'desc': 'Set right audio selector'},
                {'key': '0-9', 'desc': 'Recall preset'},
                {'key': 'SHIFT+0-9', 'desc': 'Store preset'},
            ],
            'Object Spawning': [
                {'key': 'B', 'desc': 'Spawn metal object'},
                {'key': 'N', 'desc': 'Spawn energy object'},
                {'key': 'M', 'desc': 'Spawn plasma object'},
            ],
            'Rendering': [
                {'key': 'F1', 'desc': 'Toggle simplified rendering'},
                {'key': 'F2', 'desc': 'Toggle particles'},
                {'key': 'F3', 'desc': 'Toggle trails'},
            ],
            'Tools': [
                {'key': 'Mouse Wheel', 'desc': 'Adjust brush width (FreeDraw)'},
                {'key': 'Mouse Drag', 'desc': 'Draw shapes (FreeDraw)'},
                {'key': 'Mouse Click', 'desc': 'Apply tool effect'},
            ],
            'Debug & Help': [
                {'key': 'F12', 'desc': 'Toggle debug window'},
                {'key': 'H', 'desc': 'Toggle this help window'},
            ]
        }
    
    def toggle(self):
        """Toggle command helper visibility"""
        self.is_visible = not self.is_visible
        if self.is_visible:
            # Center the window
            self.x = (self.game.width - self.width) // 2
            self.y = (self.game.height - self.height) // 2
        print(f"DEBUG: Command helper {'shown' if self.is_visible else 'hidden'}")
    
    def update(self, dt: float):
        """Update command helper (no-op for static content)"""
        pass
    
    def draw(self):
        """Draw the command helper window"""
        if not self.is_visible:
            return
        
        try:
                        # Draw background
            bg_color = (*self.color_mgr.background_ui_panel, 240)
            bg = shapes.Rectangle(self.x, self.y, self.width, self.height, color=bg_color[:3])
            bg.opacity = bg_color[3]
            bg.draw()

            # Draw border
            border_color = (*self.color_mgr.feedback_border, 255)
            border = shapes.Rectangle(self.x, self.y, self.width, self.height, color=border_color[:3])
            border.opacity = border_color[3]
            border.draw()
            
            # Draw title
            title_x = self.x + 20
            title_y = self.y + self.height - 30
            header_color = (*self.color_mgr.feedback_warning, 255)
            self._label("COMMAND REFERENCE (H)", self.title_size, title_x, title_y, 
                       header_color, bold=True)
            
            # Draw commands by category
            current_y = title_y - 40
            for category, commands in self.commands.items():
                # Category header
                self._label(f"{category}:", self.header_size, title_x, current_y, 
                           header_color, bold=True)
                current_y -= 25
                
                # Commands in category
                for cmd in commands:
                    if current_y < self.y + 20:  # Don't draw outside window
                        break
                    
                    # Key
                    key_x = title_x + 10
                    key_color = (*self.color_mgr.feedback_success, 255)
                    self._label(cmd['key'], self.key_size, key_x, current_y, key_color)
                    
                    # Description
                    desc_x = title_x + 120
                    desc_color = (*self.color_mgr.text_secondary, 255)
                    self._label(cmd['desc'], self.desc_size, desc_x, current_y, desc_color)
                    
                    current_y -= 18
                
                current_y -= 8  # Extra spacing between categories
            
        except Exception as e:
            print(f"ERROR drawing command helper: {e}")
    
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
