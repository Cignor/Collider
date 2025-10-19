import pymunk
import traceback
import math
from typing import List, Tuple

class PhysicsManager:
    """Manages physics objects and screen wrapping"""
    
    def __init__(self, game):
        self.game = game
        print("DEBUG: Physics manager initialized")
    
    def update(self, dt):
        """Update physics systems"""
        try:
            # Apply screen wrapping
            self._apply_screen_wrapping()
            
            # Apply tool effects
            self._apply_tool_effects()
            
        except Exception as e:
            print(f"ERROR updating physics manager: {e}")
            traceback.print_exc()
    
    def _apply_screen_wrapping(self):
        """Apply classic arcade-style screen wrapping to physics objects"""
        try:
            for obj in self.game.physics_objects:
                if hasattr(obj, 'body') and obj.body:
                    pos = obj.body.position
                    new_x, new_y = pos.x, pos.y
                    wrapped = False
                    
                    # Horizontal wrapping (left/right)
                    if pos.x < -50:  # Off left edge
                        new_x = self.game.width + 50
                        wrapped = True
                    elif pos.x > self.game.width + 50:  # Off right edge
                        new_x = -50
                        wrapped = True
                    
                    # Vertical wrapping (top/bottom)
                    if pos.y < -50:  # Off bottom edge
                        new_y = self.game.height + 50
                        wrapped = True
                    elif pos.y > self.game.height + 50:  # Off top edge
                        new_y = -50
                        wrapped = True
                    
                    # Apply wrapping if needed
                    if wrapped:
                        obj.body.position = (new_x, new_y)
                        # Clear trail when wrapping to avoid visual artifacts
                        obj_id = id(obj)
                        self.game.trail_system.clear_trail(obj_id)
                        
        except Exception as e:
            print(f"ERROR applying screen wrapping: {e}")
            traceback.print_exc()
    
    def _apply_tool_effects(self):
        """Apply tool-specific physics effects"""
        try:
            # Apply magnet tool effects
            magnet_tool = next((tool for tool in self.game.tools if hasattr(tool, 'apply_magnetic_forces')), None)
            if magnet_tool:
                magnet_tool.apply_magnetic_forces(self.game.space)
                
            # Apply teleporter tool effects
            teleporter_tool = next((tool for tool in self.game.tools if hasattr(tool, 'check_teleportation')), None)
            if teleporter_tool:
                teleporter_tool.check_teleportation(self.game.space)
                
        except Exception as e:
            print(f"ERROR applying tool effects: {e}")
            traceback.print_exc()
    
    def add_collision_shape(self, shape):
        """Add a collision shape to the physics space"""
        try:
            if hasattr(shape, 'body') and shape.body:
                self.game.space.add(shape.body, shape)
            else:
                # Static shape without body
                self.game.space.add(shape)
            
            self.game.collision_shapes.append(shape)
            print(f"DEBUG: Added collision shape to physics space")
            
        except Exception as e:
            print(f"ERROR adding collision shape: {e}")
            traceback.print_exc()
    
    def remove_collision_shape(self, shape):
        """Remove a collision shape from the physics space"""
        try:
            if hasattr(shape, 'body') and shape.body:
                self.game.space.remove(shape.body, shape)
            else:
                # Static shape without body
                self.game.space.remove(shape)
            
            if shape in self.game.collision_shapes:
                self.game.collision_shapes.remove(shape)
            
            print(f"DEBUG: Removed collision shape from physics space")
            
        except Exception as e:
            print(f"ERROR removing collision shape: {e}")
            traceback.print_exc()
    
    def clear_all_shapes(self):
        """Clear all collision shapes"""
        try:
            # Remove collision shapes - properly remove from space
            for shape in self.game.collision_shapes[:]:  # Copy list to avoid modification during iteration
                try:
                    if hasattr(shape, 'body') and shape.body:
                        self.game.space.remove(shape.body, shape)
                    else:
                        # Try to remove just the shape if no body
                        self.game.space.remove(shape)
                except Exception as e:
                    print(f"DEBUG: Could not remove collision shape: {e}")
            
            self.game.collision_shapes.clear()
            print("DEBUG: All collision shapes cleared")
            
        except Exception as e:
            print(f"ERROR clearing collision shapes: {e}")
            traceback.print_exc()
    
    def get_collision_shapes(self) -> List:
        """Get all collision shapes"""
        return self.game.collision_shapes.copy()
    
    def get_collision_shape_count(self) -> int:
        """Get the count of collision shapes"""
        return len(self.game.collision_shapes)
    
    def set_gravity(self, gravity: Tuple[float, float]):
        """Set the physics space gravity"""
        try:
            self.game.space.gravity = gravity
            self.game.current_gravity = gravity
            print(f"DEBUG: Gravity set to {gravity}")
        except Exception as e:
            print(f"ERROR setting gravity: {e}")
            traceback.print_exc()
    
    def reset_gravity(self):
        """Reset gravity to base value"""
        try:
            self.game.space.gravity = self.game.base_gravity
            self.game.current_gravity = self.game.base_gravity
            print(f"DEBUG: Gravity reset to base: {self.game.base_gravity}")
        except Exception as e:
            print(f"ERROR resetting gravity: {e}")
            traceback.print_exc()
    
    def get_gravity(self) -> Tuple[float, float]:
        """Get current gravity"""
        return self.game.current_gravity
    
    def get_base_gravity(self) -> Tuple[float, float]:
        """Get base gravity value"""
        return self.game.base_gravity
