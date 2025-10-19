"""
Force Field Physics Effect
Creates a directional force field that pushes objects in a specific direction
"""

import pymunk
import math

class ForceFieldEffect:
    """Directional force field that pushes objects"""
    
    def __init__(self):
        self.name = "Force Field"
        self.category = "physics_effects"
        
        # Default parameters
        self.strength = 500.0      # Force strength
        self.direction = 0.0       # Direction in radians (0 = right, π/2 = up)
        self.width = 300.0         # Field width
        self.height = 100.0        # Field height
        self.position = (0, 0)     # Center position
        self.active = True         # Whether effect is active
        
    def apply_to_space(self, space, position=None):
        """Apply force field to physics space"""
        if position:
            self.position = position
            
        if not self.active:
            return
            
        # Calculate field bounds
        half_width = self.width / 2
        half_height = self.height / 2
        left = self.position[0] - half_width
        right = self.position[0] + half_width
        bottom = self.position[1] - half_height
        top = self.position[1] + half_height
        
        # Calculate force vector
        force_x = math.cos(self.direction) * self.strength
        force_y = math.sin(self.direction) * self.strength
        
        # Get all bodies in the space
        for body in space.bodies:
            if body.body_type == pymunk.Body.DYNAMIC:
                # Check if body is within field bounds
                if (left <= body.position.x <= right and 
                    bottom <= body.position.y <= top):
                    
                    # Apply force to body
                    body.apply_force_at_world_point((force_x, force_y), body.position)
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Force Field",
            "type": "physics_effect",
            "params": [
                {
                    "id": "strength",
                    "label": "Strength",
                    "type": "float",
                    "min": 100.0,
                    "max": 2000.0,
                    "step": 50.0,
                    "value": self.strength,
                    "unit": "N"
                },
                {
                    "id": "direction",
                    "label": "Direction",
                    "type": "float",
                    "min": 0.0,
                    "max": 6.28,
                    "step": 0.1,
                    "value": self.direction,
                    "unit": "rad"
                },
                {
                    "id": "width",
                    "label": "Width",
                    "type": "float",
                    "min": 50.0,
                    "max": 600.0,
                    "step": 10.0,
                    "value": self.width,
                    "unit": "px"
                },
                {
                    "id": "height",
                    "label": "Height",
                    "type": "float",
                    "min": 50.0,
                    "max": 300.0,
                    "step": 10.0,
                    "value": self.height,
                    "unit": "px"
                },
                {
                    "id": "active",
                    "label": "Active",
                    "type": "bool",
                    "value": self.active,
                    "unit": ""
                }
            ]
        }
