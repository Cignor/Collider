"""
Vortex Physics Effect
Creates a rotational force field that spins objects around a center point
"""

import pymunk
import math

class VortexEffect:
    """Vortex that creates rotational force around a center point"""
    
    def __init__(self):
        self.name = "Vortex"
        self.category = "physics_effects"
        
        # Default parameters
        self.position = (300, 300)        # Vortex center
        self.radius = 150.0               # Effect radius
        self.strength = 800.0             # Rotational force strength
        self.direction = 1.0              # 1.0 = clockwise, -1.0 = counter-clockwise
        self.active = True                # Whether effect is active
        
    def apply_to_space(self, space, position=None):
        """Apply vortex to physics space"""
        if position:
            self.position = position
            
        if not self.active:
            return
            
        # Get all bodies in the space
        for body in space.bodies:
            if body.body_type == pymunk.Body.DYNAMIC:
                # Calculate distance to vortex center
                dx = body.position.x - self.position[0]
                dy = body.position.y - self.position[1]
                distance = math.sqrt(dx*dx + dy*dy)
                
                # Apply force if within radius
                if distance < self.radius and distance > 0:
                    # Calculate tangential force (perpendicular to radius)
                    # For clockwise rotation: force is perpendicular to radius vector
                    tangent_x = -dy / distance  # Perpendicular to radius
                    tangent_y = dx / distance
                    
                    # Apply direction (clockwise/counter-clockwise)
                    tangent_x *= self.direction
                    tangent_y *= self.direction
                    
                    # Calculate force magnitude (stronger closer to center)
                    force_magnitude = self.strength * (1.0 - distance / self.radius)
                    
                    # Apply tangential force
                    force_x = tangent_x * force_magnitude
                    force_y = tangent_y * force_magnitude
                    
                    body.apply_force_at_world_point((force_x, force_y), body.position)
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Vortex",
            "type": "physics_effect",
            "params": [
                {
                    "id": "position_x",
                    "label": "Position X",
                    "type": "float",
                    "min": 0.0,
                    "max": 1920.0,
                    "step": 10.0,
                    "value": self.position[0],
                    "unit": "px"
                },
                {
                    "id": "position_y",
                    "label": "Position Y",
                    "type": "float",
                    "min": 0.0,
                    "max": 1080.0,
                    "step": 10.0,
                    "value": self.position[1],
                    "unit": "px"
                },
                {
                    "id": "radius",
                    "label": "Radius",
                    "type": "float",
                    "min": 50.0,
                    "max": 400.0,
                    "step": 10.0,
                    "value": self.radius,
                    "unit": "px"
                },
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
                    "min": -1.0,
                    "max": 1.0,
                    "step": 0.1,
                    "value": self.direction,
                    "unit": ""
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
