import pyglet
import pymunk
import traceback
from typing import Tuple
from pyglet import shapes
import math
from pyglet_physics_game.systems.bullet_data import BulletData, MaterialType

class PhysicsObject:
    """Base class for physics objects using Pyglet"""
    
    def __init__(self, body: pymunk.Body, shape: pymunk.Shape, color: Tuple[int, int, int], 
                 material: MaterialType = MaterialType.METAL, mass: float = 1.0):
        self.body = body
        self.shape = shape
        self.color = color
        self.original_color = color
        self.highlighted = False
        
        # Initialize bullet data for this physics object
        self.bullet_data = BulletData(
            bullet_id=id(self),  # Use object ID as bullet ID
            x=body.position.x,
            y=body.position.y,
            velocity_x=body.velocity.x,
            velocity_y=body.velocity.y,
            mass=mass,
            material_type=material
        )
        
        # Update color based on material
        self._update_material_color()
    
    def _update_material_color(self):
        """Update object color based on material type"""
        try:
            from pyglet_physics_game.ui.color_manager import get_color_manager
            color_mgr = get_color_manager()
            
            if self.bullet_data.material_type == MaterialType.ENERGY:
                self.color = color_mgr.get_material_color('energy')
            elif self.bullet_data.material_type == MaterialType.PLASMA:
                self.color = color_mgr.get_material_color('plasma')
            elif self.bullet_data.material_type == MaterialType.CRYSTAL:
                self.color = color_mgr.get_material_color('crystal')
            elif self.bullet_data.material_type == MaterialType.ORGANIC:
                self.color = color_mgr.get_material_color('organic')
            elif self.bullet_data.material_type == MaterialType.VOID:
                self.color = color_mgr.get_material_color('void')
            else:  # METAL
                self.color = color_mgr.get_material_color('metal')
        except Exception as e:
            print(f"ERROR updating material color: {e}")
            # Fallback to original color
            self.color = self.original_color
    
    def update_bullet_data(self):
        """Update bullet data with current physics state"""
        try:
            # Update position and velocity from physics body
            self.bullet_data.x = self.body.position.x
            self.bullet_data.y = self.body.position.y
            self.bullet_data.velocity_x = self.body.velocity.x
            self.bullet_data.velocity_y = self.body.velocity.y
            
            # Update computed properties (speed, trails, etc.)
            self.bullet_data._update_computed_properties()
            
        except Exception as e:
            print(f"ERROR updating bullet data: {e}")
    
    def set_material(self, material: MaterialType):
        """Change the material type of this object"""
        try:
            self.bullet_data.set_material(material)
            self._update_material_color()
        except Exception as e:
            print(f"ERROR setting material: {e}")
    
    def get_trail_data(self):
        """Get trail data for rendering"""
        return self.bullet_data.get_trail_data()
    
    def get_sound_data(self):
        """Get sound data for audio system"""
        return self.bullet_data.get_sound_data()
        
    def draw(self, batch):
        """Draw the physics object using batch"""
        try:
            if isinstance(self.shape, pymunk.Circle):
                self._draw_circle(batch)
            elif isinstance(self.shape, pymunk.Poly):
                self._draw_poly(batch)
            elif isinstance(self.shape, pymunk.Segment):
                self._draw_segment(batch)
        except Exception as e:
            print(f"ERROR in draw: {e}")
            
    def draw_direct(self):
        """Draw the physics object directly without batch"""
        try:
            if isinstance(self.shape, pymunk.Circle):
                self._draw_circle_direct()
            elif isinstance(self.shape, pymunk.Poly):
                self._draw_poly_direct()
            elif isinstance(self.shape, pymunk.Segment):
                self._draw_segment_direct()
        except Exception as e:
            print(f"ERROR in draw_direct: {e}")
            
    def _draw_circle(self, batch):
        """Draw circle using batch"""
        try:
            pos = self.body.position
            radius = self.shape.radius
            
            # Create circle using pyglet shapes and store reference to prevent garbage collection
            circle = pyglet.shapes.Circle(pos.x, pos.y, radius, color=self.color, batch=batch)
            circle.opacity = 255
            
            # Store reference to prevent garbage collection
            if not hasattr(self, '_batch_shapes'):
                self._batch_shapes = []
            self._batch_shapes.append(circle)
            
        except Exception as e:
            print(f"ERROR in _draw_circle: {e}")
            
    def _draw_circle_direct(self):
        """Draw circle directly"""
        try:
            pos = self.body.position
            radius = self.shape.radius
            
            # Create circle using pyglet shapes and draw immediately
            circle = pyglet.shapes.Circle(pos.x, pos.y, radius, color=self.color)
            circle.opacity = 255
            circle.draw()
            
        except Exception as e:
            print(f"ERROR in _draw_circle_direct: {e}")
            
    def _draw_poly(self, batch):
        """Draw polygon using batch"""
        try:
            pos = self.body.position
            angle = self.body.angle
            
            # Get polygon vertices
            vertices = self.shape.get_vertices()
            
            # Transform vertices to world coordinates
            world_vertices = []
            for vertex in vertices:
                # Rotate vertex
                cos_a = math.cos(angle)
                sin_a = math.sin(angle)
                rotated_x = vertex.x * cos_a - vertex.y * sin_a
                rotated_y = vertex.x * sin_a + vertex.y * cos_a
                
                # Translate to world position
                world_x = pos.x + rotated_x
                world_y = pos.y + rotated_y
                world_vertices.append((world_x, world_y))
            
            # Create polygon using pyglet shapes
            if len(world_vertices) >= 3:
                poly = pyglet.shapes.Polygon(*world_vertices, color=self.color, batch=batch)
                poly.opacity = 255
                
        except Exception as e:
            print(f"ERROR in _draw_poly: {e}")
            
    def _draw_poly_direct(self):
        """Draw polygon directly"""
        try:
            pos = self.body.position
            angle = self.body.angle
            
            # Get polygon vertices
            vertices = self.shape.get_vertices()
            
            # Transform vertices to world coordinates
            world_vertices = []
            for vertex in vertices:
                # Rotate vertex
                cos_a = math.cos(angle)
                sin_a = math.sin(angle)
                rotated_x = vertex.x * cos_a - vertex.y * sin_a
                rotated_y = vertex.x * sin_a + vertex.y * cos_a
                
                # Translate to world position
                world_x = pos.x + rotated_x
                world_y = pos.y + rotated_y
                world_vertices.append((world_x, world_y))
            
            # Create polygon using pyglet shapes and draw immediately
            if len(world_vertices) >= 3:
                poly = pyglet.shapes.Polygon(*world_vertices, color=self.color)
                poly.opacity = 255
                poly.draw()
                
        except Exception as e:
            print(f"ERROR in _draw_poly_direct: {e}")
            
    def _draw_segment(self, batch):
        """Draw segment using batch"""
        try:
            a = self.shape.a
            b = self.shape.b
            
            # Create line using pyglet shapes
            line = pyglet.shapes.Line(a.x, a.y, b.x, b.y, color=self.color, batch=batch)
            line.opacity = 255
            
        except Exception as e:
            print(f"ERROR in _draw_segment: {e}")
            
    def _draw_segment_direct(self):
        """Draw segment directly"""
        try:
            pos = self.body.position
            angle = self.body.angle
            
            # Get segment vertices
            a = self.shape.a
            b = self.shape.b
            
            # Transform vertices to world coordinates
            # Rotate vertex a
            cos_a = math.cos(angle)
            sin_a = math.sin(angle)
            a_rotated_x = a.x * cos_a - a.y * sin_a
            a_rotated_y = a.x * sin_a + a.y * cos_a
            
            # Rotate vertex b
            b_rotated_x = b.x * cos_a - b.y * sin_a
            b_rotated_y = b.x * sin_a + b.y * cos_a
            
            # Translate to world position
            a_world_x = pos.x + a_rotated_x
            a_world_y = pos.y + a_rotated_y
            b_world_x = pos.x + b_rotated_x
            b_world_y = pos.y + b_rotated_y
            
            # Create line using pyglet shapes and draw immediately
            line = pyglet.shapes.Line(a_world_x, a_world_y, b_world_x, b_world_y, color=self.color)
            line.opacity = 255
            line.draw()
            
        except Exception as e:
            print(f"ERROR in _draw_segment_direct: {e}")
    
    def set_highlight(self, highlighted: bool):
        """Set highlight state"""
        self.highlighted = highlighted
        
    def reset_color(self):
        """Reset to original color"""
        self.color = self.original_color
