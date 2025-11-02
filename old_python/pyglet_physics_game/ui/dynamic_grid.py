"""
Dynamic Grid System for UI Layout and Positioning

Based on web research and UI design best practices, this system provides:
- 12-column responsive grid layout
- Dynamic calculation based on screen resolution
- Grid-aware UI component positioning
- Multiple grid personalities for different use cases
"""

from typing import Dict, List, Tuple, Optional, NamedTuple
from pyglet import shapes
import math
from .color_manager import get_color_manager


class GridData(NamedTuple):
    """Grid configuration data"""
    column_width: float
    gutter_width: int
    margin_width: int
    baseline: int
    columns: int
    rows: int
    screen_width: int
    screen_height: int


class GridCalculator:
    """Calculates grid parameters based on screen resolution"""
    
    def __init__(self, screen_width: int, screen_height: int):
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.columns = 12  # Standard 12-column grid
        self.gutter_width = 20
        self.margin_width = 100
        self.baseline = 8  # 8px baseline for text alignment
        
    def calculate_grid(self) -> GridData:
        """Calculate grid parameters for current screen size"""
        # Calculate available width for columns
        available_width = self.screen_width - (2 * self.margin_width)
        total_gutter_space = (self.columns - 1) * self.gutter_width
        column_width = (available_width - total_gutter_space) / self.columns
        
        # Calculate rows based on baseline
        available_height = self.screen_height - (2 * self.margin_width)
        rows = int(available_height / self.baseline)
        
        return GridData(
            column_width=column_width,
            gutter_width=self.gutter_width,
            margin_width=self.margin_width,
            baseline=self.baseline,
            columns=self.columns,
            rows=rows,
            screen_width=self.screen_width,
            screen_height=self.screen_height
        )
    
    def get_breakpoint(self) -> str:
        """Determine grid configuration based on screen size"""
        if self.screen_width >= 1920:
            return 'desktop'  # 12 columns
        elif self.screen_width >= 1366:
            return 'laptop'   # 10 columns
        elif self.screen_width >= 1024:
            return 'tablet'   # 8 columns
        else:
            return 'mobile'   # 4 columns


class GridPosition:
    """Helper class for positioning UI elements on the grid"""
    
    def __init__(self, grid_calc: GridCalculator):
        self.grid_calc = grid_calc
        self._grid_data = None
        
    def _get_grid_data(self) -> GridData:
        """Get cached grid data"""
        if self._grid_data is None:
            self._grid_data = self.grid_calc.calculate_grid()
        return self._grid_data
    
    def get_column_position(self, start_col: int, span: int = 1) -> float:
        """Get X position for a column span (0-based indexing)"""
        grid_data = self._get_grid_data()
        x = grid_data.margin_width
        x += start_col * (grid_data.column_width + grid_data.gutter_width)
        return x
        
    def get_column_width(self, span: int = 1) -> float:
        """Get width for a column span"""
        grid_data = self._get_grid_data()
        return (span * grid_data.column_width) + ((span - 1) * grid_data.gutter_width)
    
    def get_row_position(self, start_row: int, span: int = 1) -> float:
        """Get Y position for a row span (0-based indexing, top-down)"""
        grid_data = self._get_grid_data()
        y = grid_data.screen_height - grid_data.margin_width
        y -= start_row * grid_data.baseline
        y -= span * grid_data.baseline
        return y
        
    def get_row_height(self, span: int = 1) -> float:
        """Get height for a row span"""
        grid_data = self._get_grid_data()
        return span * grid_data.baseline
    
    def get_grid_rect(self, start_col: int, start_row: int, 
                     col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Get complete rectangle (x, y, width, height) for grid position"""
        x = self.get_column_position(start_col, col_span)
        y = self.get_row_position(start_row, row_span)
        width = self.get_column_width(col_span)
        height = self.get_row_height(row_span)
        return x, y, width, height
    
    def snap_to_grid(self, x: float, y: float) -> Tuple[float, float]:
        """Snap coordinates to nearest grid intersection"""
        grid_data = self._get_grid_data()
        snapped_x = round(x / grid_data.baseline) * grid_data.baseline
        snapped_y = round(y / grid_data.baseline) * grid_data.baseline
        return snapped_x, snapped_y


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


class DynamicGridSystem:
    """Main grid system that manages layout and visualization"""
    
    def __init__(self, screen_width: int, screen_height: int):
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Initialize grid calculator and position helper
        self.grid_calc = GridCalculator(screen_width, screen_height)
        self.grid_pos = GridPosition(self.grid_calc)
        
        # Grid visualization
        self.color_mgr = get_color_manager()
        self.personalities = self._create_personalities()
        self.current_index = 0
        self.visible = True
        
        # Performance optimization
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
            # Layout Grid - 12-column structure
            GridPersonality(
                "Layout Grid",
                "12-column responsive layout structure", 
                tuple(self.color_mgr.get_grid_colors("layout")["primary"]),
                tuple(self.color_mgr.get_grid_colors("layout")["secondary"]),
                self.color_mgr.get_grid_colors("layout")["spacing"],
                self.color_mgr.get_grid_colors("layout")["opacity"]
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
            # Game Grid - Larger, more visible
            GridPersonality(
                "Game Grid",
                "100px spacing for game world reference", 
                tuple(self.color_mgr.get_grid_colors("game")["primary"]),
                tuple(self.color_mgr.get_grid_colors("game")["secondary"]),
                self.color_mgr.get_grid_colors("game")["spacing"],
                self.color_mgr.get_grid_colors("game")["opacity"]
            ),
            # Neon Grid - Futuristic
            GridPersonality(
                "Neon Grid",
                "Cyberpunk-style grid with electric colors",
                tuple(self.color_mgr.get_grid_colors("neon")["primary"]),
                tuple(self.color_mgr.get_grid_colors("neon")["secondary"]),
                self.color_mgr.get_grid_colors("neon")["spacing"],
                self.color_mgr.get_grid_colors("neon")["opacity"]
            )
        ]
    
    def update_resolution(self, screen_width: int, screen_height: int):
        """Update grid system for new screen resolution"""
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.grid_calc = GridCalculator(screen_width, screen_height)
        self.grid_pos = GridPosition(self.grid_calc)
        
        # Clear cached batches for new resolution
        self._grid_batches.clear()
        self._last_screen_size = (0, 0)
    
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
    
    def get_grid_data(self) -> GridData:
        """Get current grid configuration"""
        return self.grid_calc.calculate_grid()
    
    def position_component(self, start_col: int, start_row: int, 
                          col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Position a UI component on the grid"""
        return self.grid_pos.get_grid_rect(start_col, start_row, col_span, row_span)
    
    def snap_to_grid(self, x: float, y: float) -> Tuple[float, float]:
        """Snap coordinates to nearest grid intersection"""
        return self.grid_pos.snap_to_grid(x, y)
    
    def draw(self):
        """Draw the current grid personality - ULTRA OPTIMIZED with caching"""
        if not self.visible:
            return
             
        grid = self.current_personality
        
        # PERFORMANCE: Check if we need to recreate the batch
        cache_key = (self.screen_width, self.screen_height, self.current_index)
        screen_size_changed = (self.screen_width, self.screen_height) != self._last_screen_size
        personality_changed = self.current_index != self._last_personality_index
        
        if cache_key not in self._grid_batches or screen_size_changed or personality_changed:
            # Create new cached batch
            self._create_cached_batch(grid)
            self._last_screen_size = (self.screen_width, self.screen_height)
            self._last_personality_index = self.current_index
        
        # Draw the cached batch (ultra fast!)
        batch = self._grid_batches[cache_key]
        batch.draw()
        
        # Optional: Draw grid info overlay
        self._draw_grid_info(grid)
    
    def _create_cached_batch(self, grid: GridPersonality):
        """Create and cache a grid batch for the given parameters"""
        from pyglet.graphics import Batch, Group
        
        cache_key = (self.screen_width, self.screen_height, self.current_index)
        
        # Create a single batch for all grid lines
        grid_batch = Batch()
        grid_group = Group(order=0)  # Behind everything else
        
        # Vertical lines - add to batch
        for x in range(0, self.screen_width + 1, grid.spacing):
            line = shapes.Line(x, 0, x, self.screen_height, color=grid.primary_color, 
                             batch=grid_batch, group=grid_group)
            line.opacity = grid.opacity
        
        # Horizontal lines - add to batch
        for y in range(0, self.screen_height + 1, grid.spacing):
            line = shapes.Line(0, y, self.screen_width, y, color=grid.secondary_color,
                             batch=grid_batch, group=grid_group)
            line.opacity = grid.opacity
        
        # Cache the batch
        self._grid_batches[cache_key] = grid_batch
    
    def _draw_grid_info(self, grid: GridPersonality):
        """Draw grid information overlay"""
        from pyglet import text
        
        # Grid name and spacing info
        info_text = f"{grid.name} ({grid.spacing}px)"
        label = text.Label(
            info_text,
            font_name=["Space Mono", "Arial"],
            font_size=12,
            x=self.screen_width - 200,
            y=self.screen_height - 30,
            color=self.color_mgr.text_primary
        )
        label.draw()


class GridAwareComponent:
    """Base class for UI components that use the grid system"""
    
    def __init__(self, grid_system: DynamicGridSystem):
        self.grid_system = grid_system
        
    def position_on_grid(self, start_col: int, start_row: int, 
                        col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Position component on grid"""
        return self.grid_system.position_component(start_col, start_row, col_span, row_span)
    
    def snap_to_grid(self, x: float, y: float) -> Tuple[float, float]:
        """Snap component position to grid"""
        return self.grid_system.snap_to_grid(x, y)


class GridValidator:
    """Validates UI component placement within grid bounds"""
    
    def __init__(self, grid_system: DynamicGridSystem):
        self.grid_system = grid_system
        
    def validate_placement(self, x: float, y: float, width: float, height: float) -> bool:
        """Validate that component fits within grid bounds"""
        grid_data = self.grid_system.get_grid_data()
        return (x >= 0 and 
                y >= 0 and
                x + width <= grid_data.screen_width and
                y + height <= grid_data.screen_height)
    
    def get_valid_position(self, x: float, y: float, width: float, height: float) -> Tuple[float, float]:
        """Get valid position within grid bounds"""
        grid_data = self.grid_system.get_grid_data()
        
        # Clamp to screen bounds
        x = max(0, min(x, grid_data.screen_width - width))
        y = max(0, min(y, grid_data.screen_height - height))
        
        return x, y
