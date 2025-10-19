"""
Global Coordinate System Manager for Pyglet Physics Game

This module provides a centralized, consistent coordinate system that handles
all coordinate transformations across the entire application.

COORDINATE SYSTEM STANDARD:
- Origin: Bottom-left (0,0) - Pyglet native
- Y-axis: Increases upward
- All coordinates are in pixels
- All transformations go through this system
- Integrates with the existing ModularGridSystem for UI positioning
"""

from typing import Tuple, Dict, Any, Optional
from dataclasses import dataclass
import math


@dataclass
class CoordinateSystem:
    """Global coordinate system configuration"""
    screen_width: int
    screen_height: int
    
    def __post_init__(self):
        """Validate coordinate system"""
        if self.screen_width <= 0 or self.screen_height <= 0:
            raise ValueError("Screen dimensions must be positive")


class CoordinateManager:
    """
    Centralized coordinate system manager.
    
    Provides consistent coordinate transformations and utilities
    for all UI components, physics objects, and rendering systems.
    Integrates with the existing ModularGridSystem for consistent UI positioning.
    """
    
    def __init__(self, screen_width: int, screen_height: int, grid_system=None):
        self.screen_width = screen_width
        self.screen_height = screen_height
        self._coordinate_system = CoordinateSystem(screen_width, screen_height)
        self.grid_system = grid_system  # Reference to ModularGridSystem
    
    def update_resolution(self, screen_width: int, screen_height: int):
        """Update screen resolution and recalculate all coordinates"""
        self.screen_width = screen_width
        self.screen_height = screen_height
        self._coordinate_system = CoordinateSystem(screen_width, screen_height)
        
        # Update grid system if available
        if self.grid_system:
            self.grid_system.update_resolution(screen_width, screen_height)
    
    # ===== CORE COORDINATE TRANSFORMATIONS =====
    
    def to_pyglet(self, x: float, y: float, from_system: str = "screen") -> Tuple[float, float]:
        """
        Convert coordinates to Pyglet's bottom-left origin system.
        
        Args:
            x, y: Input coordinates
            from_system: Source coordinate system ("screen", "menu", "ui")
        
        Returns:
            Tuple of (x, y) in Pyglet coordinates
        """
        if from_system == "screen":
            # Screen coordinates (top-left origin) to Pyglet (bottom-left origin)
            return x, self.screen_height - y
        elif from_system == "menu":
            # Menu coordinates (top-left of menu) to Pyglet (bottom-left origin)
            return x, self.screen_height - y
        elif from_system == "ui":
            # UI coordinates (top-left origin) to Pyglet (bottom-left origin)
            return x, self.screen_height - y
        else:
            # Already in Pyglet coordinates
            return x, y
    
    def from_pyglet(self, x: float, y: float, to_system: str = "screen") -> Tuple[float, float]:
        """
        Convert coordinates from Pyglet's bottom-left origin system.
        
        Args:
            x, y: Pyglet coordinates
            to_system: Target coordinate system ("screen", "menu", "ui")
        
        Returns:
            Tuple of (x, y) in target coordinate system
        """
        if to_system == "screen":
            # Pyglet (bottom-left origin) to screen coordinates (top-left origin)
            return x, self.screen_height - y
        elif to_system == "menu":
            # Pyglet (bottom-left origin) to menu coordinates (top-left of menu)
            return x, self.screen_height - y
        elif to_system == "ui":
            # Pyglet (bottom-left origin) to UI coordinates (top-left origin)
            return x, self.screen_height - y
        else:
            # Already in Pyglet coordinates
            return x, y
    
    def from_ui_menu(self, y: float) -> float:
        """
        Convert Y coordinate from UI menu system (top-down) to Pyglet coordinates (bottom-up).
        
        Args:
            y: Y coordinate in UI menu system (top-down, origin at top)
        
        Returns:
            Y coordinate in Pyglet system (bottom-up, origin at bottom)
        """
        return self.screen_height - y
    
    # ===== SCREEN POSITIONING UTILITIES =====
    
    def screen_center(self) -> Tuple[float, float]:
        """Get screen center in Pyglet coordinates"""
        return self.screen_width / 2, self.screen_height / 2
    
    def screen_center_screen_coords(self) -> Tuple[float, float]:
        """Get screen center in screen coordinates (top-left origin)"""
        return self.screen_width / 2, self.screen_height / 2
    
    def clamp_to_screen(self, x: float, y: float, margin: float = 0) -> Tuple[float, float]:
        """Clamp coordinates to screen bounds with optional margin"""
        clamped_x = max(margin, min(x, self.screen_width - margin))
        clamped_y = max(margin, min(y, self.screen_height - margin))
        return clamped_x, clamped_y
    
    def clamp_to_screen_pyglet(self, x: float, y: float, margin: float = 0) -> Tuple[float, float]:
        """Clamp Pyglet coordinates to screen bounds with optional margin"""
        clamped_x = max(margin, min(x, self.screen_width - margin))
        clamped_y = max(margin, min(y, self.screen_height - margin))
        return clamped_x, clamped_y
    
    # ===== UI COMPONENT POSITIONING =====
    
    def position_ui_component(self, 
                            component_width: float, 
                            component_height: float,
                            position: str = "top_left",
                            offset_x: float = 0,
                            offset_y: float = 0) -> Tuple[float, float]:
        """
        Position UI components consistently.
        
        Args:
            component_width, component_height: Size of the component
            position: Position type ("top_left", "top_right", "bottom_left", "bottom_right", "center")
            offset_x, offset_y: Additional offset
        
        Returns:
            Tuple of (x, y) in Pyglet coordinates
        """
        if position == "top_left":
            x = offset_x
            y = self.screen_height - component_height - offset_y
        elif position == "top_right":
            x = self.screen_width - component_width - offset_x
            y = self.screen_height - component_height - offset_y
        elif position == "bottom_left":
            x = offset_x
            y = offset_y
        elif position == "bottom_right":
            x = self.screen_width - component_width - offset_x
            y = offset_y
        elif position == "center":
            x = (self.screen_width - component_width) / 2 + offset_x
            y = (self.screen_height - component_height) / 2 + offset_y
        else:
            raise ValueError(f"Unknown position: {position}")
        
        return self.clamp_to_screen_pyglet(x, y)
    
    # ===== MENU POSITIONING =====
    
    def position_menu(self, 
                     menu_width: float, 
                     menu_height: float,
                     anchor_x: float, 
                     anchor_y: float,
                     padding: float = 10) -> Tuple[float, float]:
        """
        Position menu with proper clamping and coordinate conversion.
        
        Args:
            menu_width, menu_height: Menu dimensions
            anchor_x, anchor_y: Anchor point (in screen coordinates)
            padding: Minimum distance from screen edges
        
        Returns:
            Tuple of (x, y) in Pyglet coordinates for menu position
        """
        # Convert anchor to Pyglet coordinates
        pyglet_anchor_x, pyglet_anchor_y = self.to_pyglet(anchor_x, anchor_y, "screen")
        
        # Clamp to screen bounds
        clamped_x = max(padding, min(pyglet_anchor_x, self.screen_width - menu_width - padding))
        clamped_y = max(padding, min(pyglet_anchor_y, self.screen_height - menu_height - padding))
        
        return clamped_x, clamped_y
    
    # ===== SLIDER POSITIONING =====
    
    def calculate_slider_knob_position(self,
                                     slider_x: float,
                                     slider_y: float,
                                     slider_width: float,
                                     value: float,
                                     min_value: float,
                                     max_value: float,
                                     knob_size: float = 6) -> Tuple[float, float]:
        """
        Calculate slider knob position with proper coordinate handling.
        
        Args:
            slider_x, slider_y: Slider track position (in Pyglet coordinates)
            slider_width: Width of slider track
            value: Current value
            min_value, max_value: Value range
            knob_size: Size of knob (for centering)
        
        Returns:
            Tuple of (knob_x, knob_y) in Pyglet coordinates
        """
        # Normalize value to 0-1 range
        if max_value == min_value:
            normalized = 0.0
        else:
            normalized = (value - min_value) / (max_value - min_value)
        
        # Calculate knob position
        knob_x = slider_x + normalized * slider_width - knob_size / 2
        knob_y = slider_y - knob_size / 2
        
        return knob_x, knob_y
    
    def calculate_slider_value_from_position(self,
                                           mouse_x: float,
                                           slider_x: float,
                                           slider_width: float,
                                           min_value: float,
                                           max_value: float) -> float:
        """
        Calculate slider value from mouse position.
        
        Args:
            mouse_x: Mouse X position (in Pyglet coordinates)
            slider_x: Slider track X position (in Pyglet coordinates)
            slider_width: Width of slider track
            min_value, max_value: Value range
        
        Returns:
            Calculated value
        """
        # Clamp mouse position to slider bounds
        clamped_x = max(slider_x, min(mouse_x, slider_x + slider_width))
        
        # Calculate normalized position
        if slider_width == 0:
            normalized = 0.0
        else:
            normalized = (clamped_x - slider_x) / slider_width
        
        # Convert to value
        return min_value + normalized * (max_value - min_value)
    
    # ===== GRID POSITIONING =====
    
    def snap_to_grid(self, x: float, y: float, grid_size: float) -> Tuple[float, float]:
        """Snap coordinates to grid"""
        snapped_x = round(x / grid_size) * grid_size
        snapped_y = round(y / grid_size) * grid_size
        return snapped_x, snapped_y
    
    def grid_position(self, col: int, row: int, grid_size: float, offset_x: float = 0, offset_y: float = 0) -> Tuple[float, float]:
        """Get grid position from column/row indices"""
        x = col * grid_size + offset_x
        y = row * grid_size + offset_y
        return x, y
    
    # ===== GRID SYSTEM INTEGRATION =====
    
    def get_grid_calculator(self):
        """Get the current grid calculator from the grid system"""
        if self.grid_system and hasattr(self.grid_system, 'grid_calc'):
            return self.grid_system.grid_calc
        return None
    
    def position_ui_on_grid(self, 
                           component_width: float, 
                           component_height: float,
                           position: str = "top_left",
                           grid_col: int = 0,
                           grid_row: int = 0,
                           offset_x: float = 0,
                           offset_y: float = 0) -> Tuple[float, float]:
        """
        Position UI components using the grid system.
        
        Args:
            component_width, component_height: Size of the component
            position: Position type ("top_left", "top_right", "bottom_left", "bottom_right", "center")
            grid_col, grid_row: Grid column and row indices
            offset_x, offset_y: Additional offset from grid position
        
        Returns:
            Tuple of (x, y) in Pyglet coordinates
        """
        grid_calc = self.get_grid_calculator()
        if not grid_calc:
            # Fallback to basic positioning
            return self.position_ui_component(component_width, component_height, position, offset_x, offset_y)
        
        # Calculate base position using grid system
        if position == "top_left":
            # Use grid system's column calculation
            x = grid_calc.edge_margin + grid_col * (grid_calc.column_width + grid_calc.gutter_width) + offset_x
            y = self.screen_height - component_height - grid_calc.edge_margin - grid_row * grid_calc.layout_spacing - offset_y
        elif position == "top_right":
            x = self.screen_width - component_width - grid_calc.edge_margin - grid_col * (grid_calc.column_width + grid_calc.gutter_width) - offset_x
            y = self.screen_height - component_height - grid_calc.edge_margin - grid_row * grid_calc.layout_spacing - offset_y
        elif position == "bottom_left":
            x = grid_calc.edge_margin + grid_col * (grid_calc.column_width + grid_calc.gutter_width) + offset_x
            y = grid_calc.edge_margin + grid_row * grid_calc.layout_spacing + offset_y
        elif position == "bottom_right":
            x = self.screen_width - component_width - grid_calc.edge_margin - grid_col * (grid_calc.column_width + grid_calc.gutter_width) - offset_x
            y = grid_calc.edge_margin + grid_row * grid_calc.layout_spacing + offset_y
        elif position == "center":
            # Center on screen
            x = (self.screen_width - component_width) / 2 + offset_x
            y = (self.screen_height - component_height) / 2 + offset_y
        else:
            raise ValueError(f"Unknown position: {position}")
        
        return self.clamp_to_screen_pyglet(x, y)
    
    def snap_to_grid_system(self, x: float, y: float, grid_level: str = "layout") -> Tuple[float, float]:
        """
        Snap coordinates to the current grid system.
        
        Args:
            x, y: Input coordinates
            grid_level: Grid level ("macro", "layout", "micro")
        
        Returns:
            Tuple of (x, y) snapped to grid
        """
        grid_calc = self.get_grid_calculator()
        if not grid_calc:
            return x, y
        
        if grid_level == "macro":
            grid_size = grid_calc.macro_spacing
        elif grid_level == "layout":
            grid_size = grid_calc.layout_spacing
        elif grid_level == "micro":
            grid_size = grid_calc.micro_spacing
        else:
            return x, y
        
        return self.snap_to_grid(x, y, grid_size)
    
    def get_safe_area_bounds(self) -> Tuple[float, float, float, float]:
        """
        Get the safe area bounds from the grid system.
        
        Returns:
            Tuple of (x, y, width, height) in Pyglet coordinates
        """
        grid_calc = self.get_grid_calculator()
        if not grid_calc:
            # Fallback to screen bounds with margin
            margin = 20
            return margin, margin, self.screen_width - 2 * margin, self.screen_height - 2 * margin
        
        # Get safe area from grid calculator
        safe_x = grid_calc.edge_margin
        safe_y = grid_calc.edge_margin
        safe_width = self.screen_width - 2 * grid_calc.edge_margin
        safe_height = self.screen_height - 2 * grid_calc.edge_margin
        
        return safe_x, safe_y, safe_width, safe_height
    
    def is_in_safe_area(self, x: float, y: float) -> bool:
        """Check if coordinates are within the safe area"""
        safe_x, safe_y, safe_width, safe_height = self.get_safe_area_bounds()
        return (safe_x <= x <= safe_x + safe_width and 
                safe_y <= y <= safe_y + safe_height)
    
    # ===== DISTANCE AND COLLISION =====
    
    def distance(self, x1: float, y1: float, x2: float, y2: float) -> float:
        """Calculate distance between two points"""
        dx = x2 - x1
        dy = y2 - y1
        return math.sqrt(dx * dx + dy * dy)
    
    def point_in_rectangle(self, 
                          point_x: float, 
                          point_y: float,
                          rect_x: float, 
                          rect_y: float, 
                          rect_width: float, 
                          rect_height: float) -> bool:
        """Check if point is inside rectangle (Pyglet coordinates)"""
        return (rect_x <= point_x <= rect_x + rect_width and 
                rect_y <= point_y <= rect_y + rect_height)
    
    def point_in_circle(self, 
                       point_x: float, 
                       point_y: float,
                       circle_x: float, 
                       circle_y: float, 
                       radius: float) -> bool:
        """Check if point is inside circle"""
        return self.distance(point_x, point_y, circle_x, circle_y) <= radius
    
    # ===== DEBUGGING AND VALIDATION =====
    
    def validate_coordinates(self, x: float, y: float, system: str = "pyglet") -> bool:
        """Validate that coordinates are within screen bounds"""
        if system == "pyglet":
            return 0 <= x <= self.screen_width and 0 <= y <= self.screen_height
        else:
            # Convert to Pyglet first
            pyglet_x, pyglet_y = self.to_pyglet(x, y, system)
            return self.validate_coordinates(pyglet_x, pyglet_y, "pyglet")
    
    def debug_coordinate_info(self, x: float, y: float, system: str = "pyglet") -> Dict[str, Any]:
        """Get debug information about coordinates"""
        pyglet_x, pyglet_y = self.to_pyglet(x, y, system) if system != "pyglet" else (x, y)
        screen_x, screen_y = self.from_pyglet(pyglet_x, pyglet_y, "screen")
        
        return {
            "original": {"x": x, "y": y, "system": system},
            "pyglet": {"x": pyglet_x, "y": pyglet_y},
            "screen": {"x": screen_x, "y": screen_y},
            "valid": self.validate_coordinates(pyglet_x, pyglet_y, "pyglet"),
            "screen_center": self.screen_center(),
            "screen_dimensions": {"width": self.screen_width, "height": self.screen_height}
        }


# Global coordinate manager instance
_coordinate_manager: Optional[CoordinateManager] = None


def get_coordinate_manager() -> CoordinateManager:
    """Get the global coordinate manager instance"""
    global _coordinate_manager
    if _coordinate_manager is None:
        raise RuntimeError("Coordinate manager not initialized. Call initialize_coordinate_system() first.")
    return _coordinate_manager


def initialize_coordinate_system(screen_width: int, screen_height: int, grid_system=None) -> CoordinateManager:
    """Initialize the global coordinate system"""
    global _coordinate_manager
    _coordinate_manager = CoordinateManager(screen_width, screen_height, grid_system)
    return _coordinate_manager


def update_coordinate_system(screen_width: int, screen_height: int, grid_system=None):
    """Update the global coordinate system with new screen dimensions"""
    global _coordinate_manager
    if _coordinate_manager is None:
        _coordinate_manager = CoordinateManager(screen_width, screen_height, grid_system)
    else:
        _coordinate_manager.update_resolution(screen_width, screen_height)
        if grid_system:
            _coordinate_manager.grid_system = grid_system
