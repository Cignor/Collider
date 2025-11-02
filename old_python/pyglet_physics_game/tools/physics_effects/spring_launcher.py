"""
Spring Launcher Physics Effect
Applies impulse force to objects when they enter the launcher area
"""

import pymunk
import math

class SpringLauncherEffect:
    """Spring launcher that applies impulse force to objects"""
    
    def __init__(self):
        self.name = "Spring Launcher"
        self.category = "physics_effects"
        
        # Default parameters
        self.position = (200, 200)        # Launcher position
        self.radius = 80.0                # Activation radius
        self.force = 1000.0               # Impulse force strength
        self.direction = 1.57             # Direction in radians (π/2 = up)
        self.cooldown = 0.5               # Cooldown time in seconds
        self.active = True                # Whether effect is active
        
        # Internal state
        self.launched_objects = {}        # Track cooldowns
        
    def apply_to_space(self, space, position=None):
        """Apply spring launcher to physics space"""
        if position:
            self.position = position
            
        if not self.active:
            return
            
        current_time = pymunk.space.get_current_time_step()
        
        # Get all bodies in the space
        for body in space.bodies:
            if body.body_type == pymunk.Body.DYNAMIC:
                # Check if object is near launcher
                dx = body.position.x - self.position[0]
                dy = body.position.y - self.position[1]
                distance = math.sqrt(dx*dx + dy*dy)
                
                if distance < self.radius:
                    # Check cooldown
                    obj_id = id(body)
                    last_launch = self.launched_objects.get(obj_id, 0)
                    
                    if current_time - last_launch > self.cooldown:
                        # Calculate impulse vector
                        impulse_x = math.cos(self.direction) * self.force
                        impulse_y = math.sin(self.direction) * self.force
                        
                        # Apply impulse to body
                        body.apply_impulse_at_world_point((impulse_x, impulse_y), body.position)
                        
                        # Update cooldown
                        self.launched_objects[obj_id] = current_time
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Spring Launcher",
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
                    "min": 20.0,
                    "max": 200.0,
                    "step": 5.0,
                    "value": self.radius,
                    "unit": "px"
                },
                {
                    "id": "force",
                    "label": "Force",
                    "type": "float",
                    "min": 100.0,
                    "max": 5000.0,
                    "step": 50.0,
                    "value": self.force,
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
                    "id": "cooldown",
                    "label": "Cooldown",
                    "type": "float",
                    "min": 0.1,
                    "max": 3.0,
                    "step": 0.1,
                    "value": self.cooldown,
                    "unit": "s"
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
