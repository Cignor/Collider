import pymunk
import traceback
import math
from typing import List, Tuple

class CollisionSystem:
    """Manages collision shapes and boundary creation"""
    
    def __init__(self, game):
        self.game = game
    
    def update(self, dt):
        """Update collision system"""
        try:
            # Currently no per-frame updates needed
            pass
        except Exception as e:
            print(f"ERROR updating collision system: {e}")
            traceback.print_exc()
    
    def create_boundary_walls(self):
        """Create boundary walls around the game area"""
        try:
            # Bottom wall
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            shape = pymunk.Segment(body, (0, 50), (self.game.width, 50), 10)
            shape.friction = 0.7
            shape.elasticity = 0.5
            self.game.space.add(body, shape)
            self.game.collision_shapes.append(shape)
            
            # Left wall
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            shape = pymunk.Segment(body, (50, 0), (50, self.game.height), 10)
            shape.friction = 0.7
            shape.elasticity = 0.5
            self.game.space.add(body, shape)
            self.game.collision_shapes.append(shape)
            
            # Right wall
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            shape = pymunk.Segment(body, (self.game.width - 50, 0), (self.game.width - 50, self.game.height), 10)
            shape.friction = 0.7
            shape.elasticity = 0.5
            self.game.space.add(body, shape)
            self.game.collision_shapes.append(shape)
            
            # Top wall
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            shape = pymunk.Segment(body, (0, self.game.height - 50), (self.game.width, self.game.height - 50), 10)
            shape.friction = 0.7
            shape.elasticity = 0.5
            self.game.space.add(body, shape)
            self.game.collision_shapes.append(shape)
            
            print(f"DEBUG: Created boundary walls. Total collision shapes: {len(self.game.collision_shapes)}")
            
        except Exception as e:
            print(f"ERROR creating boundary walls: {e}")
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
        return self.game.collision_shapes
    
    def get_collision_shape_count(self) -> int:
        """Get the count of collision shapes"""
        return len(self.game.collision_shapes)
