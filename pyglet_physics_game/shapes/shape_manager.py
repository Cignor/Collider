"""
Shape Manager - Loads and manages bullet shapes from JSON configuration
"""
import os
import json
import math
from typing import Dict, List, Tuple, Optional, Any
import pyglet
from pyglet import shapes
import pymunk

class ShapeManager:
    """Manages bullet shapes loaded from JSON configuration"""
    
    def __init__(self, shapes_file: str = None):
        if shapes_file is None:
            # Default to shapes.json in the same directory
            current_dir = os.path.dirname(__file__)
            shapes_file = os.path.join(current_dir, 'shapes.json')
        
        self.shapes_file = shapes_file
        self.shapes_data = {}
        self.load_shapes()
    
    def load_shapes(self):
        """Load shapes from JSON file"""
        try:
            with open(self.shapes_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
                self.shapes_data = data.get('shapes', {})
                print(f"DEBUG: Loaded {len(self.shapes_data)} shapes from {self.shapes_file}")
        except Exception as e:
            print(f"ERROR: Failed to load shapes from {self.shapes_file}: {e}")
            self.shapes_data = {}
    
    def get_shape_names(self) -> List[str]:
        """Get list of available shape names"""
        return list(self.shapes_data.keys())
    
    def get_shape_info(self, shape_name: str) -> Optional[Dict]:
        """Get shape information by name"""
        return self.shapes_data.get(shape_name)
    
    def create_visual_shape(self, shape_name: str, x: float, y: float, 
                          batch=None, group=None, color: Tuple[int, int, int] = (255, 255, 255), 
                          scale: float = 1.0) -> Optional[Any]:
        """Create visual shape for rendering"""
        shape_info = self.get_shape_info(shape_name)
        if not shape_info:
            print(f"ERROR: Unknown shape '{shape_name}'")
            return None
        
        visual_config = shape_info.get('visual', {})
        shape_type = visual_config.get('type')
        
        if shape_type == 'circle':
            size = visual_config.get('size', 8) * scale
            return shapes.Circle(x, y, size, color=color, batch=batch, group=group)
        
        elif shape_type == 'rectangle':
            width = visual_config.get('width', 16) * scale
            height = visual_config.get('height', 8) * scale
            return shapes.Rectangle(x - width/2, y - height/2, width, height, color=color, batch=batch, group=group)
        
        elif shape_type == 'polygon':
            points = visual_config.get('points', [])
            if points:
                # Calculate the actual center of the points
                center_x = sum(p[0] for p in points) / len(points)
                center_y = sum(p[1] for p in points) / len(points)
                
                # Create polygon with vertices centered around (0,0) and scaled
                # Then position it at x,y by translating the vertices
                scaled_points = []
                for point in points:
                    # Center the point around (0,0), scale it, then translate to x,y
                    # This ensures ALL scales center at the same x,y point
                    centered_x = (point[0] - center_x) * scale
                    centered_y = (point[1] - center_y) * scale
                    scaled_points.append((x + centered_x, y + centered_y))
                polygon = shapes.Polygon(*scaled_points, color=color, batch=batch, group=group)
                return polygon
        
        print(f"ERROR: Unknown visual shape type '{shape_type}' for shape '{shape_name}'")
        return None
    
    def create_physics_shape(self, shape_name: str, x: float, y: float) -> Optional[Tuple[pymunk.Body, pymunk.Shape]]:
        """Create physics collision shape"""
        shape_info = self.get_shape_info(shape_name)
        if not shape_info:
            print(f"ERROR: Unknown shape '{shape_name}'")
            return None
        
        physics_config = shape_info.get('physics', {})
        shape_type = physics_config.get('type')
        
        if shape_type == 'circle':
            size = physics_config.get('size', 8)
            body = pymunk.Body(1, pymunk.moment_for_circle(1, 0, size))
            body.position = x, y
            shape = pymunk.Circle(body, size)
            return body, shape
        
        elif shape_type == 'box':
            width = physics_config.get('width', 16)
            height = physics_config.get('height', 8)
            body = pymunk.Body(1, pymunk.moment_for_box(1, (width, height)))
            body.position = x, y
            shape = pymunk.Poly.create_box(body, (width, height))
            return body, shape
        
        elif shape_type == 'polygon':
            points = physics_config.get('points', [])
            if points:
                # print(f"DEBUG: Creating physics polygon for '{shape_name}' with points: {points}")
                try:
                    body = pymunk.Body(1, pymunk.moment_for_poly(1, points))
                    body.position = x, y
                    shape = pymunk.Poly(body, points)
                    # print(f"DEBUG: Successfully created physics polygon for '{shape_name}' at ({x}, {y})")
                    return body, shape
                except Exception as e:
                    print(f"ERROR: Failed to create physics polygon for '{shape_name}': {e}")
                    return None
        
        print(f"ERROR: Unknown physics shape type '{shape_type}' for shape '{shape_name}'")
        return None
    
    def create_debug_shape(self, shape_name: str, x: float, y: float, 
                          batch=None, group=None) -> Optional[Any]:
        """Create collision debug shape"""
        shape_info = self.get_shape_info(shape_name)
        if not shape_info:
            print(f"ERROR: Unknown shape '{shape_name}'")
            return None
        
        debug_config = shape_info.get('debug', {})
        shape_type = debug_config.get('type')
        debug_color = tuple(debug_config.get('color', [0, 255, 255]))
        
        print(f"DEBUG: Creating debug shape for '{shape_name}', type='{shape_type}', color={debug_color}")
        
        if shape_type == 'circle':
            size = debug_config.get('size', 8)
            return shapes.Circle(x, y, size, color=debug_color, batch=batch, group=group)
        
        elif shape_type == 'rectangle':
            width = debug_config.get('width', 16)
            height = debug_config.get('height', 8)
            return shapes.Rectangle(x - width/2, y - height/2, width, height, color=debug_color, batch=batch, group=group)
        
        elif shape_type == 'polygon':
            points = debug_config.get('points', [])
            if points:
                # Use the same approach as visual shapes - calculate actual center and position at x,y
                center_x = sum(p[0] for p in points) / len(points)
                center_y = sum(p[1] for p in points) / len(points)
                
                # Create polygon with vertices centered around (0,0) and positioned at x,y
                scaled_points = []
                for point in points:
                    # Center the point around (0,0), then translate to x,y
                    centered_x = (point[0] - center_x)
                    centered_y = (point[1] - center_y)
                    scaled_points.append((x + centered_x, y + centered_y))
                polygon = shapes.Polygon(*scaled_points, color=debug_color, batch=batch, group=group)
                return polygon
        
        print(f"ERROR: Unknown debug shape type '{shape_type}' for shape '{shape_name}'")
        return None
    
    def update_visual_shape_position(self, shape_obj: Any, shape_name: str, x: float, y: float, scale: float = 1.0):
        """Update visual shape position"""
        shape_info = self.get_shape_info(shape_name)
        if not shape_info:
            return
        
        visual_config = shape_info.get('visual', {})
        shape_type = visual_config.get('type')
        
        if shape_type == 'circle':
            shape_obj.x = x
            shape_obj.y = y
        
        elif shape_type == 'rectangle':
            # Rectangle positioning: x,y is the center, but Rectangle uses bottom-left corner
            width = visual_config.get('width', 16) * scale
            height = visual_config.get('height', 8) * scale
            shape_obj.x = x - width/2
            shape_obj.y = y - height/2
        
        elif shape_type == 'polygon':
            # For polygons, we need to recreate with new position since vertices are absolute
            # This is handled by the calling code recreating the shape
            pass
    
    def update_debug_shape_position(self, shape_obj: Any, shape_name: str, x: float, y: float):
        """Update debug shape position"""
        shape_info = self.get_shape_info(shape_name)
        if not shape_info:
            return
        
        debug_config = shape_info.get('debug', {})
        shape_type = debug_config.get('type')
        
        if shape_type == 'circle':
            shape_obj.x = x
            shape_obj.y = y
        
        elif shape_type == 'rectangle':
            # Rectangle positioning: x,y is the center, but Rectangle uses bottom-left corner
            width = debug_config.get('width', 16)
            height = debug_config.get('height', 8)
            shape_obj.x = x - width/2
            shape_obj.y = y - height/2
        
        elif shape_type == 'polygon':
            # For polygons, we need to recreate with new position since vertices are absolute
            # This is handled by the calling code recreating the shape
            pass

# Global shape manager instance
_shape_manager = None

def get_shape_manager() -> ShapeManager:
    """Get global shape manager instance"""
    global _shape_manager
    if _shape_manager is None:
        _shape_manager = ShapeManager()
    return _shape_manager
