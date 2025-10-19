"""
Teleporter Physics Effect
Instantly moves objects from one location to another
"""

import pymunk
import math

class TeleporterEffect:
    """Teleporter that moves objects between two points"""
    
    def __init__(self):
        self.name = "Teleporter"
        self.category = "physics_effects"
        
        # Default parameters
        self.entrance_pos = (100, 100)    # Entrance position
        self.exit_pos = (500, 500)        # Exit position
        self.radius = 50.0                # Teleporter radius
        self.cooldown = 1.0               # Cooldown time in seconds
        self.active = True                # Whether effect is active
        
        # Internal state
        self.teleported_objects = {}      # Track cooldowns
        
    def apply_to_space(self, space, position=None):
        """Apply teleporter to physics space"""
        if not self.active:
            return
            
        current_time = pymunk.space.get_current_time_step()
        
        # Check entrance teleporter
        for body in space.bodies:
            if body.body_type == pymunk.Body.DYNAMIC:
                # Check if object is near entrance
                dx = body.position.x - self.entrance_pos[0]
                dy = body.position.y - self.entrance_pos[1]
                distance = math.sqrt(dx*dx + dy*dy)
                
                if distance < self.radius:
                    # Check cooldown
                    obj_id = id(body)
                    last_teleport = self.teleported_objects.get(obj_id, 0)
                    
                    if current_time - last_teleport > self.cooldown:
                        # Teleport object
                        body.position = self.exit_pos
                        # Reset velocity to prevent physics glitches
                        body.velocity = (0, 0)
                        body.angular_velocity = 0
                        
                        # Update cooldown
                        self.teleported_objects[obj_id] = current_time
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Teleporter",
            "type": "physics_effect",
            "params": [
                {
                    "id": "entrance_x",
                    "label": "Entrance X",
                    "type": "float",
                    "min": 0.0,
                    "max": 1920.0,
                    "step": 10.0,
                    "value": self.entrance_pos[0],
                    "unit": "px"
                },
                {
                    "id": "entrance_y",
                    "label": "Entrance Y",
                    "type": "float",
                    "min": 0.0,
                    "max": 1080.0,
                    "step": 10.0,
                    "value": self.entrance_pos[1],
                    "unit": "px"
                },
                {
                    "id": "exit_x",
                    "label": "Exit X",
                    "type": "float",
                    "min": 0.0,
                    "max": 1920.0,
                    "step": 10.0,
                    "value": self.exit_pos[0],
                    "unit": "px"
                },
                {
                    "id": "exit_y",
                    "label": "Exit Y",
                    "type": "float",
                    "min": 0.0,
                    "max": 1080.0,
                    "step": 10.0,
                    "value": self.exit_pos[1],
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
                    "id": "cooldown",
                    "label": "Cooldown",
                    "type": "float",
                    "min": 0.1,
                    "max": 5.0,
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
