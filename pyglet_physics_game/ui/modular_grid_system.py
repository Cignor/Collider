"""
Modular Grid System - JSON-based Dynamic Grid Loading
Supports multiple grid configurations loaded from JSON files
Optimized for performance with caching and batch rendering
"""

import json
import os
import glob
from typing import Dict, List, Tuple, Optional, Any
from pyglet import shapes
from pyglet.graphics import Batch, Group
import math
from .color_manager import get_color_manager
from .palette_manager_v2 import get_palette_manager_v2
from typing import NamedTuple


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
    
    def __init__(self, screen_width: int, screen_height: int, config: Dict[str, Any] = None):
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Use config if provided, otherwise use defaults
        if config:
            calc_config = config.get("grid_calculator", {})
            self.edge_margin = calc_config.get("edge_margin", 20)
            self.corner_margin = calc_config.get("corner_margin", 40)
            self.macro_spacing = calc_config.get("macro_spacing", 100)
            self.layout_spacing = calc_config.get("layout_spacing", 20)
            self.micro_spacing = calc_config.get("micro_spacing", 4)
            self.columns = calc_config.get("columns", 12)
            self.gutter_width = calc_config.get("gutter_width", 16)
            self.margin_width = calc_config.get("margin_width", 80)
            self.baseline = calc_config.get("baseline", 4)
        else:
            # Default values
            self.edge_margin = 20
            self.corner_margin = 40
            self.macro_spacing = 100
            self.layout_spacing = 20
            self.micro_spacing = 4
            self.columns = 12
            self.gutter_width = 16
            self.margin_width = 80
            self.baseline = 4
    
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


class GridElement:
    """Base class for grid elements"""
    def __init__(self, config: Dict[str, Any], screen_width: int, screen_height: int):
        self.config = config
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.color_mgr = get_color_manager()
        self.palette_mgr = get_palette_manager_v2()
    
    def get_color(self, color_name: str) -> Tuple[int, int, int]:
        """Get color from palette manager"""
        return self.palette_mgr.get_color(color_name)
    
    def get_position(self, position_type: str) -> Tuple[float, float]:
        """Get position based on position type"""
        if position_type == "center_x":
            return (self.screen_width / 2, 0)
        elif position_type == "center_y":
            return (0, self.screen_height / 2)
        elif position_type == "screen_center":
            return (self.screen_width / 2, self.screen_height / 2)
        return (0, 0)


class CenterLinesElement(GridElement):
    """Renders center lines (vertical and horizontal)"""
    
    def create_shapes(self, batch: Batch, group: Group) -> List[shapes.Line]:
        """Create center line shapes"""
        shapes_list = []
        
        if not self.config.get("enabled", True):
            return shapes_list
        
        # Vertical line
        v_config = self.config.get("vertical_line", {})
        if v_config.get("enabled", True):
            x, _ = self.get_position("center_x")
            color = self.get_color(v_config.get("color", "accent_cyan"))
            opacity = v_config.get("opacity", 80)
            thickness = v_config.get("thickness", 2)
            
            line = shapes.Line(
                x, 0, x, self.screen_height,
                color=color, batch=batch, group=group
            )
            line.opacity = opacity
            line.width = thickness
            shapes_list.append(line)
        
        # Horizontal line
        h_config = self.config.get("horizontal_line", {})
        if h_config.get("enabled", True):
            _, y = self.get_position("center_y")
            color = self.get_color(h_config.get("color", "accent_cyan"))
            opacity = h_config.get("opacity", 80)
            thickness = h_config.get("thickness", 2)
            
            line = shapes.Line(
                0, y, self.screen_width, y,
                color=color, batch=batch, group=group
            )
            line.opacity = opacity
            line.width = thickness
            shapes_list.append(line)
        
        return shapes_list


class CenterCircleElement(GridElement):
    """Renders center circle with gradient"""
    
    def create_shapes(self, batch: Batch, group: Group) -> List[shapes.Circle]:
        """Create center circle shapes"""
        shapes_list = []
        
        if not self.config.get("enabled", True):
            return shapes_list
        
        # Get center position
        center_x, center_y = self.get_position("screen_center")
        
        # Calculate radius
        radius = self.config.get("radius", 50)
        radius_percentage = self.config.get("radius_percentage", 0.05)
        if radius_percentage > 0:
            # Use percentage of screen size
            radius = min(self.screen_width, self.screen_height) * radius_percentage
        
        # Create gradient circle
        gradient_config = self.config.get("gradient", {})
        if gradient_config.get("enabled", True):
            center_color = self.get_color(gradient_config.get("center_color", "material_energy"))
            edge_color = self.get_color(gradient_config.get("edge_color", "material_energy"))
            center_opacity = gradient_config.get("center_opacity", 100)
            edge_opacity = gradient_config.get("edge_opacity", 0)
            gradient_steps = gradient_config.get("gradient_steps", 20)
            
            # Create concentric circles for gradient effect
            for i in range(gradient_steps):
                step_radius = radius * (i / gradient_steps)
                if step_radius < 1:
                    continue
                
                # Interpolate color and opacity
                t = i / (gradient_steps - 1)
                color = self._interpolate_color(center_color, edge_color, t)
                opacity = int(center_opacity * (1 - t) + edge_opacity * t)
                
                circle = shapes.Circle(
                    center_x, center_y, step_radius,
                    color=color, batch=batch, group=group
                )
                circle.opacity = opacity
                shapes_list.append(circle)
        
        # Add outline if enabled
        outline_config = self.config.get("outline", {})
        if outline_config.get("enabled", True):
            outline_color = self.get_color(outline_config.get("color", "accent_cyan"))
            outline_opacity = outline_config.get("opacity", 60)
            outline_thickness = outline_config.get("thickness", 1)
            
            # Create outline circle (hollow)
            outline = shapes.Circle(
                center_x, center_y, radius,
                color=outline_color, batch=batch, group=group
            )
            outline.opacity = outline_opacity
            # Note: Pyglet shapes.Circle doesn't support thickness directly
            # We'll create multiple circles for thickness effect
            for i in range(outline_thickness):
                thick_circle = shapes.Circle(
                    center_x, center_y, radius + i,
                    color=outline_color, batch=batch, group=group
                )
                thick_circle.opacity = outline_opacity
                shapes_list.append(thick_circle)
        
        return shapes_list
    
    def _interpolate_color(self, color1: Tuple[int, int, int], color2: Tuple[int, int, int], t: float) -> Tuple[int, int, int]:
        """Interpolate between two colors"""
        return (
            int(color1[0] * (1 - t) + color2[0] * t),
            int(color1[1] * (1 - t) + color2[1] * t),
            int(color1[2] * (1 - t) + color2[2] * t)
        )


class GridLinesElement(GridElement):
    """Renders grid lines (macro and micro)"""
    
    def create_shapes(self, batch: Batch, group: Group) -> List[shapes.Line]:
        """Create grid line shapes"""
        shapes_list = []
        
        # Macro grid
        macro_config = self.config.get("macro_grid", {})
        if macro_config.get("enabled", True):
            spacing = macro_config.get("spacing", 100)
            spacing_percentage = macro_config.get("spacing_percentage", 0.052)
            if spacing_percentage > 0:
                spacing = min(self.screen_width, self.screen_height) * spacing_percentage
            
            color = self.get_color(macro_config.get("color", "grid_primary"))
            opacity = macro_config.get("opacity", 80)
            thickness = macro_config.get("thickness", 1)
            
            # Vertical lines
            for x in range(0, self.screen_width + 1, int(spacing)):
                line = shapes.Line(x, 0, x, self.screen_height, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
            
            # Horizontal lines
            for y in range(0, self.screen_height + 1, int(spacing)):
                line = shapes.Line(0, y, self.screen_width, y, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
        
        # Micro grid
        micro_config = self.config.get("micro_grid", {})
        if micro_config.get("enabled", True):
            spacing = micro_config.get("spacing", 8)
            spacing_percentage = micro_config.get("spacing_percentage", 0.004)
            if spacing_percentage > 0:
                spacing = min(self.screen_width, self.screen_height) * spacing_percentage
            
            color = self.get_color(micro_config.get("color", "grid_secondary"))
            opacity = micro_config.get("opacity", 40)
            thickness = micro_config.get("thickness", 1)
            
            # Vertical lines
            for x in range(0, self.screen_width + 1, int(spacing)):
                line = shapes.Line(x, 0, x, self.screen_height, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
            
            # Horizontal lines
            for y in range(0, self.screen_height + 1, int(spacing)):
                line = shapes.Line(0, y, self.screen_width, y, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
        
        return shapes_list


class EnhancedDesignGridElement(GridElement):
    """Renders enhanced design grid with macro, layout, and micro levels"""
    
    def __init__(self, config: Dict[str, Any], screen_width: int, screen_height: int):
        super().__init__(config, screen_width, screen_height)
        # Create enhanced grid calculator
        self.grid_calc = EnhancedGridCalculator(screen_width, screen_height, config)
        self.grid_data = self.grid_calc.calculate_enhanced_grid()
    
    def create_shapes(self, batch: Batch, group: Group) -> List[shapes.Line]:
        """Create enhanced design grid shapes"""
        shapes_list = []
        
        # Macro grid
        macro_config = self.config.get("macro_grid", {})
        if macro_config.get("enabled", True):
            spacing = self.grid_data.macro_spacing
            color = self.get_color(macro_config.get("color", "grid_primary"))
            opacity = macro_config.get("opacity", 80)
            thickness = macro_config.get("thickness", 1)
            
            # Vertical lines
            for x in range(0, self.screen_width + 1, spacing):
                line = shapes.Line(x, 0, x, self.screen_height, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
            
            # Horizontal lines
            for y in range(0, self.screen_height + 1, spacing):
                line = shapes.Line(0, y, self.screen_width, y, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
        
        # Layout grid
        layout_config = self.config.get("layout_grid", {})
        if layout_config.get("enabled", True):
            spacing = self.grid_data.layout_spacing
            color = self.get_color(layout_config.get("color", "grid_layout_primary"))
            opacity = layout_config.get("opacity", 50)
            thickness = layout_config.get("thickness", 1)
            
            # Vertical lines
            for x in range(0, self.screen_width + 1, spacing):
                line = shapes.Line(x, 0, x, self.screen_height, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
            
            # Horizontal lines
            for y in range(0, self.screen_height + 1, spacing):
                line = shapes.Line(0, y, self.screen_width, y, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
        
        # Micro grid
        micro_config = self.config.get("micro_grid", {})
        if micro_config.get("enabled", True):
            spacing = self.grid_data.micro_spacing
            color = self.get_color(micro_config.get("color", "grid_secondary"))
            opacity = micro_config.get("opacity", 40)
            thickness = micro_config.get("thickness", 1)
            
            # Vertical lines
            for x in range(0, self.screen_width + 1, spacing):
                line = shapes.Line(x, 0, x, self.screen_height, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
            
            # Horizontal lines
            for y in range(0, self.screen_height + 1, spacing):
                line = shapes.Line(0, y, self.screen_width, y, color=color, batch=batch, group=group)
                line.opacity = opacity
                line.width = thickness
                shapes_list.append(line)
        
        return shapes_list


class ModularGridSystem:
    """Modular grid system that loads configurations from JSON files"""
    
    def __init__(self, screen_width: int, screen_height: int):
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Grid configurations
        self.grid_configs = {}
        self.current_grid = "design_grid"  # Default to design grid
        self.visible = True
        
        # Enhanced grid calculator (for compatibility with old HUD code)
        self.grid_calc = None
        
        # Performance optimization - caching
        self._grid_batches = {}  # Cache by (grid_name, width, height)
        self._last_screen_size = (0, 0)
        self._last_grid_name = None
        
        # Load all grid configurations
        self._load_grid_configurations()
        
        # Initialize grid calculator for current grid
        self._update_grid_calculator()
    
    def _load_grid_configurations(self):
        """Load all grid configurations from JSON files"""
        try:
            # Get the directory where this file is located
            current_dir = os.path.dirname(os.path.abspath(__file__))
            grids_dir = os.path.join(current_dir, 'grids')
            
            # Find all JSON files in the grids directory
            grid_files = glob.glob(os.path.join(grids_dir, '*.json'))
            
            for grid_file in grid_files:
                try:
                    with open(grid_file, 'r') as f:
                        grid_config = json.load(f)
                    
                    # Extract grid name from filename
                    grid_name = os.path.splitext(os.path.basename(grid_file))[0]
                    
                    self.grid_configs[grid_name] = grid_config
                    print(f"Loaded grid configuration: {grid_config.get('name', grid_name)}")
                    
                except Exception as e:
                    print(f"ERROR loading grid file {grid_file}: {e}")
            
            if not self.grid_configs:
                print("WARNING: No grid configurations loaded")
                
        except Exception as e:
            print(f"ERROR loading grid configurations: {e}")
    
    def _update_grid_calculator(self):
        """Update the grid calculator for the current grid"""
        if self.current_grid in self.grid_configs:
            grid_config = self.grid_configs[self.current_grid]
            self.grid_calc = EnhancedGridCalculator(self.screen_width, self.screen_height, grid_config)
        else:
            # Fallback to default calculator
            self.grid_calc = EnhancedGridCalculator(self.screen_width, self.screen_height)
    
    def set_grid(self, grid_name: str):
        """Switch to a different grid configuration"""
        if grid_name in self.grid_configs:
            self.current_grid = grid_name
            self._update_grid_calculator()  # Update grid calculator
            # Clear the batch cache when switching grids to prevent old shapes from persisting
            self._grid_batches.clear()
            print(f"Switched to grid: {self.grid_configs[grid_name].get('name', grid_name)}")
        else:
            print(f"WARNING: Grid '{grid_name}' not found")
    
    def update_resolution(self, screen_width: int, screen_height: int):
        """Update grid system for new screen resolution"""
        self.screen_width = screen_width
        self.screen_height = screen_height
        
        # Update grid calculator for new resolution
        self._update_grid_calculator()
        
        # Clear cached batches for new resolution
        self._grid_batches.clear()
        self._last_screen_size = (0, 0)
    
    def toggle_visibility(self):
        """Toggle grid visibility"""
        self.visible = not self.visible
        status = "ON" if self.visible else "OFF"
        print(f"Modular grid system: {status}")
    
    def draw(self):
        """Draw the current grid configuration"""
        if not self.visible:
            return
        
        # PERFORMANCE: Check if we need to recreate the batch
        cache_key = (self.current_grid, self.screen_width, self.screen_height)
        screen_size_changed = (self.screen_width, self.screen_height) != self._last_screen_size
        grid_changed = self.current_grid != self._last_grid_name
        
        if cache_key not in self._grid_batches or screen_size_changed or grid_changed:
            self._create_grid_batch()
            self._last_screen_size = (self.screen_width, self.screen_height)
            self._last_grid_name = self.current_grid
        
        # Draw the cached batch (ultra fast!)
        batch_data = self._grid_batches[cache_key]
        batch = batch_data['batch']
        batch.draw()
    
    def draw_with_renderer_batch(self, renderer_batch, grid_group):
        """Draw the current grid configuration using the renderer's batch system"""
        if not self.visible:
            return
        
        # PERFORMANCE: Check if we need to recreate the batch
        cache_key = (self.current_grid, self.screen_width, self.screen_height)
        screen_size_changed = (self.screen_width, self.screen_height) != self._last_screen_size
        grid_changed = self.current_grid != self._last_grid_name
        
        if cache_key not in self._grid_batches or screen_size_changed or grid_changed:
            self._create_grid_batch_with_renderer(renderer_batch, grid_group)
            self._last_screen_size = (self.screen_width, self.screen_height)
            self._last_grid_name = self.current_grid
    
    def _create_grid_batch(self):
        """Create optimized batch for current grid configuration"""
        cache_key = (self.current_grid, self.screen_width, self.screen_height)
        
        # Create batch and group
        grid_batch = Batch()
        # Use lower order than bullets to ensure grid appears under bullets
        grid_group = Group(order=0.5)  # Between background (0) and bullets (2)
        
        # Store shapes to prevent garbage collection
        grid_shapes = []
        
        # Get current grid configuration
        grid_config = self.grid_configs.get(self.current_grid, {})
        elements_config = grid_config.get("elements", {})
        
        # Create elements based on configuration
        if "center_lines" in elements_config:
            center_lines = CenterLinesElement(
                elements_config["center_lines"], 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(center_lines.create_shapes(grid_batch, grid_group))
        
        if "center_circle" in elements_config:
            center_circle = CenterCircleElement(
                elements_config["center_circle"], 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(center_circle.create_shapes(grid_batch, grid_group))
        
        # Enhanced design grid (with grid_calc support)
        if grid_config.get("type") == "ui_design_enhanced":
            enhanced_design = EnhancedDesignGridElement(
                grid_config, 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(enhanced_design.create_shapes(grid_batch, grid_group))
        
        # Regular grid lines
        elif "macro_grid" in elements_config or "micro_grid" in elements_config:
            grid_lines = GridLinesElement(
                elements_config, 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(grid_lines.create_shapes(grid_batch, grid_group))
        
        # Cache the batch and shapes
        self._grid_batches[cache_key] = {
            'batch': grid_batch,
            'shapes': grid_shapes
        }
        
        print(f"DEBUG: Created grid batch for '{self.current_grid}' - Shapes: {len(grid_shapes)}")
    
    def _create_grid_batch_with_renderer(self, renderer_batch, grid_group):
        """Create grid shapes using the renderer's batch system for proper depth sorting"""
        cache_key = (self.current_grid, self.screen_width, self.screen_height)
        
        # Store shapes to prevent garbage collection
        grid_shapes = []
        
        # Get current grid configuration
        grid_config = self.grid_configs.get(self.current_grid, {})
        elements_config = grid_config.get("elements", {})
        
        # Create elements based on configuration using renderer's batch
        if "center_lines" in elements_config:
            center_lines = CenterLinesElement(
                elements_config["center_lines"], 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(center_lines.create_shapes(renderer_batch, grid_group))
        
        if "center_circle" in elements_config:
            center_circle = CenterCircleElement(
                elements_config["center_circle"], 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(center_circle.create_shapes(renderer_batch, grid_group))
        
        # Enhanced design grid (with grid_calc support)
        if grid_config.get("type") == "ui_design_enhanced":
            enhanced_design = EnhancedDesignGridElement(
                grid_config, 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(enhanced_design.create_shapes(renderer_batch, grid_group))
        
        # Regular grid lines
        elif "macro_grid" in elements_config or "micro_grid" in elements_config:
            grid_lines = GridLinesElement(
                elements_config, 
                self.screen_width, 
                self.screen_height
            )
            grid_shapes.extend(grid_lines.create_shapes(renderer_batch, grid_group))
        
        # Cache the shapes (no separate batch needed since we're using renderer's batch)
        self._grid_batches[cache_key] = {
            'batch': None,  # No separate batch
            'shapes': grid_shapes
        }
        
        print(f"DEBUG: Created grid shapes for '{self.current_grid}' using renderer batch - Shapes: {len(grid_shapes)}")
    
    def get_available_grids(self) -> List[str]:
        """Get list of available grid names"""
        return list(self.grid_configs.keys())
    
    def get_current_grid_info(self) -> Dict[str, str]:
        """Get current grid information"""
        if self.current_grid in self.grid_configs:
            grid = self.grid_configs[self.current_grid]
            return {
                'name': grid.get('name', self.current_grid),
                'description': grid.get('description', ''),
                'type': grid.get('type', 'unknown')
            }
        return {}
