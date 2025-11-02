import pymunk
import traceback
import math
from typing import List, Tuple

class WindSystem:
    """Manages wind forces and effects"""
    
    def __init__(self, game):
        self.game = game
    
    def update(self, dt):
        """Update wind system"""
        try:
            # Apply global wind forces to all objects
            if self.game.current_wind_strength > 0:
                self._apply_global_wind()
            
            # Apply tool-specific wind effects
            self._apply_tool_wind()
            
        except Exception as e:
            print(f"ERROR updating wind system: {e}")
            traceback.print_exc()
    
    def _apply_global_wind(self):
        """Apply global wind forces to all physics objects"""
        try:
            if self.game.current_wind_strength > 0:
                # Apply wind to regular physics objects (X-axis only)
                for obj in self.game.physics_objects:
                    if obj.body.body_type == pymunk.Body.DYNAMIC:
                        # Apply wind force only in X direction
                        force_x = math.cos(self.game.wind_direction) * self.game.current_wind_strength
                        force_y = 0  # No Y component for wind
                        obj.body.apply_force_at_local_point((force_x, force_y), (0, 0))
                
                # Apply wind to sound bullets (they have their own physics bodies) - X-axis only
                for bullet in self.game.sound_bullets:
                    if bullet.is_active and bullet.physics_enabled and bullet.body:
                        if bullet.body.body_type == pymunk.Body.DYNAMIC:
                            # Apply wind force only in X direction
                            force_x = math.cos(self.game.wind_direction) * self.game.current_wind_strength
                            force_y = 0  # No Y component for wind
                            bullet.body.apply_force_at_local_point((force_x, force_y), (0, 0))
                    
        except Exception as e:
            print(f"ERROR applying global wind: {e}")
            traceback.print_exc()
    
    def _apply_tool_wind(self):
        """Apply wind tool effects"""
        try:
            # Find wind tool and apply its effects
            wind_tool = next((tool for tool in self.game.tools if hasattr(tool, 'apply_wind')), None)
            if wind_tool:
                wind_tool.apply_wind(self.game.space)
                
        except Exception as e:
            print(f"ERROR applying tool wind: {e}")
            traceback.print_exc()
    
    def set_wind_direction(self, direction: float):
        """Set wind direction in radians"""
        try:
            self.game.wind_direction = direction
            print(f"DEBUG: Wind direction set to {math.degrees(direction):.1f}°")
        except Exception as e:
            print(f"ERROR setting wind direction: {e}")
            traceback.print_exc()
    
    def set_wind_strength(self, strength: float):
        """Set wind strength"""
        try:
            self.game.current_wind_strength = max(0, strength)
            print(f"DEBUG: Wind strength set to {strength}")
        except Exception as e:
            print(f"ERROR setting wind strength: {e}")
            traceback.print_exc()
    
    def get_wind_direction(self) -> float:
        """Get current wind direction in radians"""
        return self.game.wind_direction
    
    def get_wind_strength(self) -> float:
        """Get current wind strength"""
        return self.game.current_wind_strength
    
    def reset_wind(self):
        """Reset wind to default state"""
        try:
            self.game.current_wind_strength = 0
            self.game.wind_direction = 0
            print("DEBUG: Wind reset to default")
        except Exception as e:
            print(f"ERROR resetting wind: {e}")
            traceback.print_exc()
