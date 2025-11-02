import pyglet
import pymunk
import math
import random
import time
import traceback
from pyglet import shapes
from typing import List, Dict, Tuple

class ParticleSystem:
    """Manages wind particles and object trails"""
    
    def __init__(self, game):
        self.game = game
        
        # Wind particle system for visual feedback
        self.wind_particles = []
        self.particle_spawn_timer = 0
        self.particle_spawn_interval = 2  # Spawn particles every 2 frames
        self.max_wind_particles = 100  # Maximum number of wind particles
        
        print("DEBUG: Particle system initialized")
    
    def update(self, dt):
        """Update particle systems"""
        try:
            self._update_wind_particles(dt)
        except Exception as e:
            print(f"ERROR updating particle systems: {e}")
            traceback.print_exc()
    
    def draw(self):
        """Draw particle systems"""
        try:
            self._draw_wind_particles()
        except Exception as e:
            print(f"ERROR drawing particle systems: {e}")
            traceback.print_exc()
    
    def _update_wind_particles(self, dt):
        """Update wind particle positions and handle collisions"""
        try:
            # Spawn new particles
            self.particle_spawn_timer += 1
            if (self.particle_spawn_timer >= self.particle_spawn_interval and 
                self.game.current_wind_strength > 0 and 
                len(self.wind_particles) < self.max_wind_particles):
                self._spawn_wind_particle()
                self.particle_spawn_timer = 0
            
            # Update existing particles
            particles_to_remove = []
            for particle in self.wind_particles:
                # Update lifetime
                particle['lifetime'] -= dt
                if particle['lifetime'] <= 0:
                    particles_to_remove.append(particle)
                    continue
                
                # Store old position for collision detection
                old_x, old_y = particle['x'], particle['y']
                
                # Apply wind force
                wind_force = self.game.current_wind_strength * 0.1  # Scale down for visual effect
                new_x = old_x + math.cos(self.game.wind_direction) * wind_force
                new_y = old_y + math.sin(self.game.wind_direction) * wind_force
                
                # Add some random movement for natural look
                new_x += random.uniform(-0.5, 0.5)
                new_y += random.uniform(-0.5, 0.5)
                
                # Check collision with collision shapes
                if self._check_particle_collision(old_x, old_y, new_x, new_y):
                    # Collision detected - bounce the particle
                    particle['x'], particle['y'] = self._bounce_particle(old_x, old_y, new_x, new_y)
                    # Reduce lifetime on collision
                    particle['lifetime'] *= 0.8
                else:
                    # No collision - move normally
                    particle['x'], particle['y'] = new_x, new_y
                
                # Check screen boundaries
                if (particle['x'] < -100 or particle['x'] > self.game.width + 100 or 
                    particle['y'] < -100 or particle['y'] > self.game.height + 100):
                    particles_to_remove.append(particle)
            
            # Remove dead particles
            for particle in particles_to_remove:
                self.wind_particles.remove(particle)
                
        except Exception as e:
            print(f"ERROR updating wind particles: {e}")
            traceback.print_exc()
    
    def _spawn_wind_particle(self):
        """Spawn a new wind particle"""
        try:
            # Spawn particles along the left edge of the screen when wind is active
            if self.game.current_wind_strength > 0:
                particle = {
                    'x': random.uniform(-50, 50),  # Start slightly off-screen
                    'y': random.uniform(0, self.game.height),
                    'lifetime': random.uniform(2.0, 4.0),  # 2-4 seconds lifetime
                    'size': random.uniform(2, 6),  # Random size
                    'speed': random.uniform(0.5, 1.5)  # Random speed multiplier
                }
                self.wind_particles.append(particle)
                
        except Exception as e:
            print(f"ERROR spawning wind particle: {e}")
            traceback.print_exc()
    
    def _check_particle_collision(self, old_x, old_y, new_x, new_y):
        """Check if particle path intersects with any collision shapes"""
        try:
            for shape in self.game.collision_shapes:
                if hasattr(shape, 'a') and hasattr(shape, 'b') and hasattr(shape, 'radius'):
                    # This is a pymunk.Segment
                    if self._line_intersects_segment(old_x, old_y, new_x, new_y, shape):
                        return True
            return False
        except Exception as e:
            print(f"ERROR checking particle collision: {e}")
            return False
    
    def _line_intersects_segment(self, x1, y1, x2, y2, segment):
        """Check if line intersects with segment"""
        try:
            # Get segment endpoints
            seg_a = segment.a
            seg_b = segment.b
            
            # Simple bounding box check first
            min_x = min(seg_a.x, seg_b.x) - segment.radius
            max_x = max(seg_a.x, seg_b.x) + segment.radius
            min_y = min(seg_a.y, seg_b.y) - segment.radius
            max_y = max(seg_a.y, seg_b.y) + segment.radius
            
            # Check if particle path is completely outside bounding box
            if (x1 < min_x and x2 < min_x) or (x1 > max_x and x2 > max_x) or \
               (y1 < min_y and y2 < min_y) or (y1 > max_y and y2 > max_y):
                return False
            
            # More precise collision check (simplified)
            # For now, just check if particle gets very close to segment
            segment_length = math.sqrt((seg_b.x - seg_a.x)**2 + (seg_b.y - seg_a.y)**2)
            if segment_length == 0:
                return False
                
            # Calculate distance from particle path to segment
            # This is a simplified distance calculation
            t = max(0, min(1, ((x2 - seg_a.x) * (seg_b.x - seg_a.x) + (y2 - seg_a.y) * (seg_b.y - seg_a.y)) / (segment_length ** 2)))
            closest_x = seg_a.x + t * (seg_b.x - seg_a.x)
            closest_y = seg_a.y + t * (seg_b.y - seg_a.y)
            
            distance = math.sqrt((x2 - closest_x)**2 + (y2 - closest_y)**2)
            return distance < segment.radius + 5  # 5px buffer for particle size
            
        except Exception as e:
            print(f"ERROR checking line intersection: {e}")
            return False
    
    def _bounce_particle(self, old_x, old_y, new_x, new_y):
        """Bounce a particle off collision shapes"""
        try:
            # Simple bounce: reverse the movement direction
            dx = new_x - old_x
            dy = new_y - old_y
            
            # Bounce back with some randomness
            bounce_x = old_x - dx * 0.8 + random.uniform(-2, 2)
            bounce_y = old_y - dy * 0.8 + random.uniform(-2, 2)
            
            return bounce_x, bounce_y
            
        except Exception as e:
            print(f"ERROR bouncing particle: {e}")
            return old_x, old_y
    
    def _draw_wind_particles(self):
        """Draw wind particles for visual feedback"""
        try:
            if self.game.current_wind_strength > 0:
                for particle in self.wind_particles:
                    # Calculate opacity based on lifetime and wind strength
                    opacity = int(255 * (particle['lifetime'] / 4.0) * (self.game.current_wind_strength / 1000))
                    opacity = max(30, min(200, opacity))  # Clamp between 30-200
                    
                    # Draw particle as a small circle
                    from pyglet_physics_game.ui.color_manager import get_color_manager
                    color_mgr = get_color_manager()
                    circle = shapes.Circle(particle['x'], particle['y'], particle['size'], 
                                         color=color_mgr.particle_wind)
                    circle.opacity = opacity
                    circle.draw()
                    
        except Exception as e:
            print(f"ERROR drawing wind particles: {e}")
            traceback.print_exc()
    
    def clear_particles(self):
        """Clear all particles"""
        try:
            self.wind_particles.clear()
            print("DEBUG: All particles cleared")
        except Exception as e:
            print(f"ERROR clearing particles: {e}")
            traceback.print_exc()
    
    def get_particle_count(self) -> int:
        """Get current particle count"""
        return len(self.wind_particles)
    
    def set_max_particles(self, max_count: int):
        """Set maximum number of particles"""
        try:
            self.max_wind_particles = max(1, max_count)
            print(f"DEBUG: Max particles set to {self.max_wind_particles}")
        except Exception as e:
            print(f"ERROR setting max particles: {e}")
            traceback.print_exc()
