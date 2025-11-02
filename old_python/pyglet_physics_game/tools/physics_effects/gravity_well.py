"""
Gravity Well Physics Effect
Creates a localized gravity field that attracts objects
"""

import pymunk
import math

class GravityWellEffect:
    """Gravity well that attracts nearby objects"""
    
    def __init__(self):
        self.name = "Gravity Well"
        self.category = "physics_effects"
        
        # Default parameters
        self.strength = 1000.0     # Force strength
        self.radius = 200.0        # Effect radius in pixels
        self.position = (0, 0)     # Center position
        self.active = True         # Whether effect is active
        
    def apply_to_space(self, space, position=None):
        """Apply gravity well to physics space"""
        if position:
            self.position = position
            
        if not self.active:
            return
            
        # Get all bodies in the space
        for body in space.bodies:
            if body.body_type == pymunk.Body.DYNAMIC:
                # Calculate distance to gravity well
                dx = self.position[0] - body.position.x
                dy = self.position[1] - body.position.y
                distance = math.sqrt(dx*dx + dy*dy)
                
                # Apply force if within radius
                if distance < self.radius and distance > 0:
                    # Calculate force magnitude (inverse square law)
                    force_magnitude = self.strength / (distance * distance)
                    
                    # Normalize direction vector
                    force_x = (dx / distance) * force_magnitude
                    force_y = (dy / distance) * force_magnitude
                    
                    # Apply force to body
                    body.apply_force_at_world_point((force_x, force_y), body.position)
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Gravity Well",
            "type": "physics_effect",
            "params": [
                {
                    "id": "strength",
                    "label": "Strength",
                    "type": "float",
                    "min": 100.0,
                    "max": 5000.0,
                    "step": 50.0,
                    "value": self.strength,
                    "unit": "N"
                },
                {
                    "id": "radius",
                    "label": "Radius",
                    "type": "float",
                    "min": 50.0,
                    "max": 500.0,
                    "step": 10.0,
                    "value": self.radius,
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
