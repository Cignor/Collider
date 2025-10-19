"""
Grid System for UI Layout and Visual Design

Multiple grid personalities with different colors and spacing for various use cases.
Accessible via F2 key cycling.
"""

from typing import Dict, List, Tuple, Optional
from pyglet import shapes
import math
from .color_manager import get_color_manager


class GridPersonality:
    """Defines a grid's visual characteristics and behavior"""
    
    def __init__(self, name: str, description: str, 
                 primary_color: Tuple[int, int, int], 
                 secondary_color: Tuple[int, int, int],
                 spacing: int, opacity: int = 30):
        self.name = name
        self.description = description
        self.primary_color = primary_color
        self.secondary_color = secondary_color
        self.spacing = spacing
        self.opacity = opacity


class GridSystem:
    """Manages multiple grid personalities and rendering"""
    
    def __init__(self):
        self.color_mgr = get_color_manager()
        self.personalities = self._create_personalities()
        self.current_index = 0
        self.visible = True
        
        # PERFORMANCE: Cache grid batches to avoid recreating every frame
        self._grid_batches = {}  # Cache batches by (width, height, personality_index)
        self._last_screen_size = (0, 0)
        self._last_personality_index = -1
        
    def _create_personalities(self) -> List[GridPersonality]:
        """Create different grid personalities with distinct visual styles"""
        return [
            # Design Grid - Clean, professional
            GridPersonality(
                "Design Grid", 
                "Clean 8px baseline for UI alignment",
                tuple(self.color_mgr.get_grid_colors("design")["primary"]),
                tuple(self.color_mgr.get_grid_colors("design")["secondary"]),
                self.color_mgr.get_grid_colors("design")["spacing"],
                self.color_mgr.get_grid_colors("design")["opacity"]
            ),
            # Game Grid - Larger, more visible
            GridPersonality(
                "Game Grid",
                "100px spacing for game world reference", 
                tuple(self.color_mgr.get_grid_colors("game")["primary"]),
                tuple(self.color_mgr.get_grid_colors("game")["secondary"]),
                self.color_mgr.get_grid_colors("game")["spacing"],
                self.color_mgr.get_grid_colors("game")["opacity"]
            ),
            # Golden Ratio Grid - Aesthetic proportions
            GridPersonality(
                "Golden Grid",
                "Fibonacci-based spacing for natural proportions",
                tuple(self.color_mgr.get_grid_colors("golden")["primary"]),
                tuple(self.color_mgr.get_grid_colors("golden")["secondary"]),
                self.color_mgr.get_grid_colors("golden")["spacing"],
                self.color_mgr.get_grid_colors("golden")["opacity"]
            ),
            # Neon Grid - Futuristic
            GridPersonality(
                "Neon Grid",
                "Cyberpunk-style grid with electric colors",
                tuple(self.color_mgr.get_grid_colors("neon")["primary"]),
                tuple(self.color_mgr.get_grid_colors("neon")["secondary"]),
                self.color_mgr.get_grid_colors("neon")["spacing"],
                self.color_mgr.get_grid_colors("neon")["opacity"]
            ),
            # Minimal Grid - Subtle
            GridPersonality(
                "Minimal Grid",
                "Ultra-subtle grid for clean layouts",
                tuple(self.color_mgr.get_grid_colors("minimal")["primary"]),
                tuple(self.color_mgr.get_grid_colors("minimal")["secondary"]),
                self.color_mgr.get_grid_colors("minimal")["spacing"],
                self.color_mgr.get_grid_colors("minimal")["opacity"]
            )
        ]
    
    def cycle_grid(self):
        """Cycle to next grid personality"""
        self.current_index = (self.current_index + 1) % len(self.personalities)
        print(f"Switched to: {self.current_personality.name} - {self.current_personality.description}")
    
    def toggle_visibility(self):
        """Toggle grid visibility"""
        self.visible = not self.visible
        status = "ON" if self.visible else "OFF"
        print(f"Grid overlay: {status}")
    
    @property
    def current_personality(self) -> GridPersonality:
        return self.personalities[self.current_index]
    
    def draw(self, screen_width: int, screen_height: int):
        """Draw the current grid personality - ULTRA OPTIMIZED with caching"""
        if not self.visible:
            return
             
        grid = self.current_personality
        
        # PERFORMANCE: Check if we need to recreate the batch
        cache_key = (screen_width, screen_height, self.current_index)
        screen_size_changed = (screen_width, screen_height) != self._last_screen_size
        personality_changed = self.current_index != self._last_personality_index
        
        if cache_key not in self._grid_batches or screen_size_changed or personality_changed:
            # Create new cached batch
            self._create_cached_batch(screen_width, screen_height, grid)
            self._last_screen_size = (screen_width, screen_height)
            self._last_personality_index = self.current_index
        
        # Draw the cached batch (ultra fast!)
        batch = self._grid_batches[cache_key]
        batch.draw()
        
        # Optional: Draw grid info overlay
        self._draw_grid_info(screen_width, screen_height, grid)
    
    def _create_cached_batch(self, screen_width: int, screen_height: int, grid: GridPersonality):
        """Create and cache a grid batch for the given parameters"""
        from pyglet.graphics import Batch, Group
        
        cache_key = (screen_width, screen_height, self.current_index)
        
        # Create a single batch for all grid lines
        grid_batch = Batch()
        grid_group = Group(order=0)  # Behind everything else
        
        # Vertical lines - add to batch
        for x in range(0, screen_width + 1, grid.spacing):
            line = shapes.Line(x, 0, x, screen_height, color=grid.primary_color, 
                             batch=grid_batch, group=grid_group)
            line.opacity = grid.opacity
        
        # Horizontal lines - add to batch
        for y in range(0, screen_height + 1, grid.spacing):
            line = shapes.Line(0, y, screen_width, y, color=grid.secondary_color,
                             batch=grid_batch, group=grid_group)
            line.opacity = grid.opacity
        
        # Cache the batch
        self._grid_batches[cache_key] = grid_batch
    
    def _draw_grid_info(self, screen_width: int, screen_height: int, grid: GridPersonality):
        """Draw grid information overlay"""
        from pyglet import text
        
        # Grid name and spacing info
        info_text = f"{grid.name} ({grid.spacing}px)"
        from .color_manager import get_color_manager
        color_mgr = get_color_manager()
        label = text.Label(
            info_text,
            font_name=["Space Mono", "Arial"],
            font_size=12,
            x=screen_width - 200,
            y=screen_height - 30,
            color=color_mgr.text_primary
        )
        label.draw()
