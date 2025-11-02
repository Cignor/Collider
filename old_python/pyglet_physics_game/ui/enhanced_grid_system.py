"""
Enhanced Multi-Level Grid System for Precise UI Layout

This system provides:
- Multiple grid levels (macro, layout, micro) for different use cases
- Forbidden zones at screen edges for safe UI placement
- Fine-grained sub-grids for precise UI element positioning
- Responsive breakpoints with appropriate grid scaling
"""

from typing import Dict, List, Tuple, Optional, NamedTuple
from pyglet import shapes
import math
from .color_manager import get_color_manager


class GridLevel(NamedTuple):
    """Grid level configuration"""
    name: str
    spacing: int
    opacity: int
    color_primary: Tuple[int, int, int]
    color_secondary: Tuple[int, int, int]
    visible: bool = True


class ForbiddenZone(NamedTuple):
    """Defines a forbidden area where UI elements cannot be placed"""
    name: str
    x: int
    y: int
    width: int
    height: int
    reason: str


class EnhancedGridData(NamedTuple):
    """Enhanced grid configuration data"""
    # Screen dimensions
    screen_width: int
    screen_height: int
    
    # Forbidden zones
    forbidden_zones: List[ForbiddenZone]
    safe_area_x: int
    safe_area_y: int
    safe_area_width: int
    safe_area_height: int
    
    # Grid levels
    macro_spacing: int      # Large structural grid (100px+)
    layout_spacing: int     # Layout grid (20-50px)
    micro_spacing: int      # Fine UI grid (4-8px)
    
    # Column system
    columns: int
    column_width: float
    gutter_width: int
    margin_width: int
    
    # Baseline system
    baseline: int
    rows: int


class EnhancedGridCalculator:
    """Enhanced grid calculator with multiple levels and forbidden zones"""
    
    def __init__(self, screen_width: int, screen_height: int):
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Forbidden zone configuration
        self.edge_margin = 20  # Minimum distance from screen edges
        self.corner_margin = 40  # Larger margin for corners
        
        # Grid level configurations
        self.macro_spacing = 100  # Large structural grid
        self.layout_spacing = 20  # Layout grid
        self.micro_spacing = 4   # Fine UI grid
        
        # Column system
        self.columns = 12
        self.gutter_width = 16
        self.margin_width = 80  # Reduced from 100 to account for forbidden zones
        
        # Baseline system
        self.baseline = 4  # Fine baseline for precise UI alignment
        
    def calculate_enhanced_grid(self) -> EnhancedGridData:
        """Calculate enhanced grid with forbidden zones and multiple levels"""
        
        # Create forbidden zones
        forbidden_zones = self._create_forbidden_zones()
        
        # Calculate safe area (usable space minus forbidden zones)
        safe_area_x = self.edge_margin
        safe_area_y = self.edge_margin
        safe_area_width = self.screen_width - (2 * self.edge_margin)
        safe_area_height = self.screen_height - (2 * self.edge_margin)
        
        # Calculate column system within safe area
        available_width = safe_area_width - (2 * self.margin_width)
        total_gutter_space = (self.columns - 1) * self.gutter_width
        column_width = (available_width - total_gutter_space) / self.columns
        
        # Calculate rows based on baseline
        available_height = safe_area_height - (2 * self.margin_width)
        rows = int(available_height / self.baseline)
        
        return EnhancedGridData(
            screen_width=self.screen_width,
            screen_height=self.screen_height,
            forbidden_zones=forbidden_zones,
            safe_area_x=safe_area_x,
            safe_area_y=safe_area_y,
            safe_area_width=safe_area_width,
            safe_area_height=safe_area_height,
            macro_spacing=self.macro_spacing,
            layout_spacing=self.layout_spacing,
            micro_spacing=self.micro_spacing,
            columns=self.columns,
            column_width=column_width,
            gutter_width=self.gutter_width,
            margin_width=self.margin_width,
            baseline=self.baseline,
            rows=rows
        )
    
    def _create_forbidden_zones(self) -> List[ForbiddenZone]:
        """Create forbidden zones at screen edges and corners"""
        zones = []
        
        # Top edge forbidden zone
        zones.append(ForbiddenZone(
            "top_edge", 0, self.screen_height - self.edge_margin,
            self.screen_width, self.edge_margin,
            "Top edge - reserved for system UI"
        ))
        
        # Bottom edge forbidden zone
        zones.append(ForbiddenZone(
            "bottom_edge", 0, 0,
            self.screen_width, self.edge_margin,
            "Bottom edge - reserved for system UI"
        ))
        
        # Left edge forbidden zone
        zones.append(ForbiddenZone(
            "left_edge", 0, 0,
            self.edge_margin, self.screen_height,
            "Left edge - reserved for system UI"
        ))
        
        # Right edge forbidden zone
        zones.append(ForbiddenZone(
            "right_edge", self.screen_width - self.edge_margin, 0,
            self.edge_margin, self.screen_height,
            "Right edge - reserved for system UI"
        ))
        
        # Corner forbidden zones (larger margins)
        # Top-left corner
        zones.append(ForbiddenZone(
            "top_left_corner", 0, self.screen_height - self.corner_margin,
            self.corner_margin, self.corner_margin,
            "Top-left corner - reserved for system UI"
        ))
        
        # Top-right corner
        zones.append(ForbiddenZone(
            "top_right_corner", self.screen_width - self.corner_margin, self.screen_height - self.corner_margin,
            self.corner_margin, self.corner_margin,
            "Top-right corner - reserved for system UI"
        ))
        
        # Bottom-left corner
        zones.append(ForbiddenZone(
            "bottom_left_corner", 0, 0,
            self.corner_margin, self.corner_margin,
            "Bottom-left corner - reserved for system UI"
        ))
        
        # Bottom-right corner
        zones.append(ForbiddenZone(
            "bottom_right_corner", self.screen_width - self.corner_margin, 0,
            self.corner_margin, self.corner_margin,
            "Bottom-right corner - reserved for system UI"
        ))
        
        return zones
    
    def get_breakpoint(self) -> str:
        """Determine grid configuration based on screen size"""
        if self.screen_width >= 1920:
            return 'desktop'  # 12 columns, full spacing
        elif self.screen_width >= 1366:
            return 'laptop'   # 10 columns, reduced spacing
        elif self.screen_width >= 1024:
            return 'tablet'   # 8 columns, compact spacing
        else:
            return 'mobile'   # 4 columns, minimal spacing


class EnhancedGridPosition:
    """Enhanced grid positioning with forbidden zone awareness"""
    
    def __init__(self, grid_calc: EnhancedGridCalculator):
        self.grid_calc = grid_calc
        self._grid_data = None
        
    def _get_grid_data(self) -> EnhancedGridData:
        """Get cached grid data"""
        if self._grid_data is None:
            self._grid_data = self.grid_calc.calculate_enhanced_grid()
        return self._grid_data
    
    def get_safe_position(self, start_col: int, start_row: int, 
                         col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Get position within safe area, avoiding forbidden zones"""
        grid_data = self._get_grid_data()
        
        # Calculate position within safe area
        x = grid_data.safe_area_x + grid_data.margin_width
        x += start_col * (grid_data.column_width + grid_data.gutter_width)
        
        y = grid_data.safe_area_y + grid_data.margin_width
        y += start_row * grid_data.baseline
        
        width = (col_span * grid_data.column_width) + ((col_span - 1) * grid_data.gutter_width)
        height = row_span * grid_data.baseline
        
        # Validate against forbidden zones
        if not self._validate_placement(x, y, width, height):
            # Adjust position to avoid forbidden zones
            x, y = self._adjust_for_forbidden_zones(x, y, width, height)
        
        return x, y, width, height
    
    def get_micro_grid_position(self, x: float, y: float, 
                               width: float, height: float) -> Tuple[float, float, float, float]:
        """Get position snapped to micro grid within safe area"""
        grid_data = self._get_grid_data()
        
        # Snap to micro grid
        snapped_x = round(x / grid_data.micro_spacing) * grid_data.micro_spacing
        snapped_y = round(y / grid_data.micro_spacing) * grid_data.micro_spacing
        snapped_width = round(width / grid_data.micro_spacing) * grid_data.micro_spacing
        snapped_height = round(height / grid_data.micro_spacing) * grid_data.micro_spacing
        
        # Ensure within safe area
        snapped_x = max(grid_data.safe_area_x, min(snapped_x, 
                                                   grid_data.safe_area_x + grid_data.safe_area_width - snapped_width))
        snapped_y = max(grid_data.safe_area_y, min(snapped_y, 
                                                   grid_data.safe_area_y + grid_data.safe_area_height - snapped_height))
        
        return snapped_x, snapped_y, snapped_width, snapped_height
    
    def _validate_placement(self, x: float, y: float, width: float, height: float) -> bool:
        """Check if placement conflicts with forbidden zones"""
        grid_data = self._get_grid_data()
        
        for zone in grid_data.forbidden_zones:
            # Check for overlap with forbidden zone
            if (x < zone.x + zone.width and x + width > zone.x and
                y < zone.y + zone.height and y + height > zone.y):
                return False
        
        return True
    
    def _adjust_for_forbidden_zones(self, x: float, y: float, width: float, height: float) -> Tuple[float, float]:
        """Adjust position to avoid forbidden zones"""
        grid_data = self._get_grid_data()
        
        # Try to move within safe area
        if x < grid_data.safe_area_x:
            x = grid_data.safe_area_x
        elif x + width > grid_data.safe_area_x + grid_data.safe_area_width:
            x = grid_data.safe_area_x + grid_data.safe_area_width - width
        
        if y < grid_data.safe_area_y:
            y = grid_data.safe_area_y
        elif y + height > grid_data.safe_area_y + grid_data.safe_area_height:
            y = grid_data.safe_area_y + grid_data.safe_area_height - height
        
        return x, y


class EnhancedGridSystem:
    """Enhanced grid system with multiple levels and forbidden zones"""
    
    def __init__(self, screen_width: int, screen_height: int):
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Initialize enhanced grid calculator and position helper
        self.grid_calc = EnhancedGridCalculator(screen_width, screen_height)
        self.grid_pos = EnhancedGridPosition(self.grid_calc)
        
        # Grid visualization
        self.color_mgr = get_color_manager()
        self.grid_levels = self._create_grid_levels()
        self.current_level_index = 0
        self.visible = True
        
        # Performance optimization
        self._grid_batches = {}  # Cache batches by (width, height, level_index)
        self._last_screen_size = (0, 0)
        self._last_level_index = -1
        
    def _create_grid_levels(self) -> List[GridLevel]:
        """Create different grid levels for different use cases"""
        return [
            # Macro Grid - Large structural grid
            GridLevel(
                "Macro Grid",
                self.grid_calc.macro_spacing,
                20,
                tuple(self.color_mgr.get_grid_colors("game")["primary"]),
                tuple(self.color_mgr.get_grid_colors("game")["secondary"]),
                True
            ),
            # Layout Grid - Layout structure
            GridLevel(
                "Layout Grid",
                self.grid_calc.layout_spacing,
                30,
                tuple(self.color_mgr.get_grid_colors("layout")["primary"]),
                tuple(self.color_mgr.get_grid_colors("layout")["secondary"]),
                True
            ),
            # Micro Grid - Fine UI alignment
            GridLevel(
                "Micro Grid",
                self.grid_calc.micro_spacing,
                40,
                tuple(self.color_mgr.get_grid_colors("design")["primary"]),
                tuple(self.color_mgr.get_grid_colors("design")["secondary"]),
                True
            ),
            # All Grids - Show all levels
            GridLevel(
                "All Grids",
                0,  # Special case - will draw all levels
                50,
                tuple(self.color_mgr.get_grid_colors("neon")["primary"]),
                tuple(self.color_mgr.get_grid_colors("neon")["secondary"]),
                True
            )
        ]
    
    def update_resolution(self, screen_width: int, screen_height: int):
        """Update grid system for new screen resolution"""
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.grid_calc = EnhancedGridCalculator(screen_width, screen_height)
        self.grid_pos = EnhancedGridPosition(self.grid_calc)
        
        # Clear cached batches for new resolution
        self._grid_batches.clear()
        self._last_screen_size = (0, 0)
    
    def cycle_grid_level(self):
        """Toggle grid visibility (no cycling needed for UI design)"""
        self.visible = not self.visible
        status = "ON" if self.visible else "OFF"
        print(f"Grid system: {status}")
    
    def toggle_visibility(self):
        """Toggle grid visibility"""
        self.visible = not self.visible
        status = "ON" if self.visible else "OFF"
        print(f"Enhanced grid overlay: {status}")
    
    @property
    def current_level(self) -> GridLevel:
        return self.grid_levels[self.current_level_index]
    
    def get_grid_data(self) -> EnhancedGridData:
        """Get current enhanced grid configuration"""
        return self.grid_calc.calculate_enhanced_grid()
    
    def position_component_safe(self, start_col: int, start_row: int, 
                               col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Position a UI component safely within the grid, avoiding forbidden zones"""
        return self.grid_pos.get_safe_position(start_col, start_row, col_span, row_span)
    
    def snap_to_micro_grid(self, x: float, y: float, width: float, height: float) -> Tuple[float, float, float, float]:
        """Snap coordinates to micro grid within safe area"""
        return self.grid_pos.get_micro_grid_position(x, y, width, height)
    
    def draw(self):
        """Draw UI design grid system - optimized for performance"""
        if not self.visible:
            return
        
        # PERFORMANCE: Check if we need to recreate the batch
        cache_key = (self.screen_width, self.screen_height)
        screen_size_changed = (self.screen_width, self.screen_height) != self._last_screen_size
        
        if cache_key not in self._grid_batches or screen_size_changed:
            self._create_ui_grid_batch()
            self._last_screen_size = (self.screen_width, self.screen_height)
        
        # Draw the cached batch (ultra fast!)
        batch_data = self._grid_batches[cache_key]
        batch = batch_data['batch']
        batch.draw()
    
    def _create_ui_grid_batch(self):
        """Create optimized batch for UI design grid"""
        from pyglet.graphics import Batch, Group
        from pyglet import shapes
        
        cache_key = (self.screen_width, self.screen_height)
        
        # Create batch and groups
        grid_batch = Batch()
        main_grid_group = Group(order=1)  # Main grid
        sub_grid_group = Group(order=2)   # Sub grid (more transparent)
        
        # Store lines to prevent garbage collection
        grid_lines = []
        
        # Get colors from palettable theme - use brighter colors for visibility
        color_mgr = get_color_manager()
        grid_colors = color_mgr.get_grid_colors()
        
        # Use brighter colors for better visibility
        main_color = (200, 220, 240)  # Light blue-gray
        sub_color = (150, 170, 190)   # Slightly darker but still visible
        
        # Main grid (100px spacing) - more visible
        main_spacing = 100
        main_opacity = 80
        
        # Vertical main grid lines
        for x in range(0, self.screen_width + 1, main_spacing):
            line = shapes.Line(x, 0, x, self.screen_height, color=main_color, 
                             batch=grid_batch, group=main_grid_group)
            line.opacity = main_opacity
            grid_lines.append(line)
        
        # Horizontal main grid lines
        for y in range(0, self.screen_height + 1, main_spacing):
            line = shapes.Line(0, y, self.screen_width, y, color=main_color,
                             batch=grid_batch, group=main_grid_group)
            line.opacity = main_opacity
            grid_lines.append(line)
        
        # Sub grid (8px spacing) - less visible
        sub_spacing = 8
        sub_opacity = 40  # Much more transparent
        
        # Vertical sub grid lines
        for x in range(0, self.screen_width + 1, sub_spacing):
            line = shapes.Line(x, 0, x, self.screen_height, color=sub_color, 
                             batch=grid_batch, group=sub_grid_group)
            line.opacity = sub_opacity
            grid_lines.append(line)
        
        # Horizontal sub grid lines
        for y in range(0, self.screen_height + 1, sub_spacing):
            line = shapes.Line(0, y, self.screen_width, y, color=sub_color,
                             batch=grid_batch, group=sub_grid_group)
            line.opacity = sub_opacity
            grid_lines.append(line)
        
        # Cache the batch and lines
        self._grid_batches[cache_key] = {
            'batch': grid_batch,
            'lines': grid_lines
        }
        print(f"DEBUG: Created UI grid batch - Main: {main_spacing}px, Sub: {sub_spacing}px, Lines: {len(grid_lines)}")
    

    
    def _draw_forbidden_zones(self):
        """Draw forbidden zones as semi-transparent overlays"""
        grid_data = self.grid_calc.calculate_enhanced_grid()
        
        for zone in grid_data.forbidden_zones:
            # Draw forbidden zone as semi-transparent red overlay
            overlay = shapes.Rectangle(zone.x, zone.y, zone.width, zone.height, 
                                     color=(255, 0, 0))
            overlay.opacity = 30
            overlay.draw()
    



class EnhancedGridAwareComponent:
    """Enhanced base class for UI components that use the grid system"""
    
    def __init__(self, grid_system: EnhancedGridSystem):
        self.grid_system = grid_system
        
    def position_on_grid_safe(self, start_col: int, start_row: int, 
                             col_span: int = 1, row_span: int = 1) -> Tuple[float, float, float, float]:
        """Position component safely on grid, avoiding forbidden zones"""
        return self.grid_system.position_component_safe(start_col, start_row, col_span, row_span)
    
    def snap_to_micro_grid(self, x: float, y: float, width: float, height: float) -> Tuple[float, float, float, float]:
        """Snap component to micro grid within safe area"""
        return self.grid_system.snap_to_micro_grid(x, y, width, height)
