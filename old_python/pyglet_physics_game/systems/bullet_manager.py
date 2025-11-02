import time
import traceback
from typing import Dict, List, Optional, Tuple, Any
from .bullet_data import BulletData, MaterialType, BulletState

class BulletManager:
    """Manages multiple bullets with efficient lifecycle management and trail integration"""
    
    def __init__(self, game):
        self.game = game
        
        # Bullet storage and management
        self.bullets: Dict[int, BulletData] = {}
        self.next_bullet_id: int = 1
        
        # Performance settings
        self.max_bullets: int = 1000
        self.cleanup_interval: float = 5.0  # seconds
        self.last_cleanup: float = time.time()
        
        # Statistics
        self.bullets_created: int = 0
        self.bullets_destroyed: int = 0
        self.active_bullets: int = 0
        
        # Trail integration
        self.trail_system = None  # Will be set by game core
        
        if getattr(game, 'debug_mode', False):
            print("DEBUG: Bullet manager initialized")
    
    def set_trail_system(self, trail_system):
        """Set reference to trail system for integration"""
        self.trail_system = trail_system
    
    def create_bullet(self, x: float, y: float, vx: float, vy: float, 
                     material: MaterialType = MaterialType.METAL, 
                     mass: float = 1.0) -> int:
        """Create a new bullet and return its ID"""
        try:
            # Check bullet limit
            if len(self.bullets) >= self.max_bullets:
                if getattr(self.game, 'debug_mode', False):
                    print(f"DEBUG: Bullet limit reached ({self.max_bullets}), cannot create more")
                return -1
            
            # Create bullet with unique ID
            bullet_id = self.next_bullet_id
            self.next_bullet_id += 1
            
            # Create bullet data
            bullet = BulletData(
                bullet_id=bullet_id,
                x=x, y=y,
                velocity_x=vx, velocity_y=vy,
                mass=mass,
                material_type=material
            )
            
            # Store bullet
            self.bullets[bullet_id] = bullet
            self.bullets_created += 1
            self.active_bullets += 1
            
            # Integrate with trail system if available
            if self.trail_system:
                self._integrate_bullet_with_trails(bullet)
            
            if getattr(self.game, 'debug_mode', False):
                print(f"DEBUG: Created bullet {bullet_id} at ({x:.1f}, {y:.1f}) with speed {bullet.speed:.1f}")
            
            return bullet_id
            
        except Exception as e:
            print(f"ERROR creating bullet: {e}")
            traceback.print_exc()
            return -1
    
    def _integrate_bullet_with_trails(self, bullet: BulletData):
        """Integrate bullet with the trail system"""
        try:
            if self.trail_system and bullet.trail_enabled:
                # Create trail entry for this bullet
                obj_id = bullet.bullet_id
                self.trail_system.trail_data[obj_id] = (
                    self.trail_system.x_array.copy(),
                    self.trail_system.y_array.copy(),
                    1
                )
                # Set initial position
                self.trail_system.trail_data[obj_id][0][0] = bullet.x
                self.trail_system.trail_data[obj_id][1][0] = bullet.y
                # Store position for tracking
                self.trail_system.object_positions[obj_id] = (bullet.x, bullet.y)
                
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR integrating bullet with trails: {e}")
    
    def update_bullet(self, bullet_id: int, new_x: float, new_y: float, dt: float) -> bool:
        """Update bullet position and all dependent properties"""
        try:
            if bullet_id not in self.bullets:
                return False
            
            bullet = self.bullets[bullet_id]
            if not bullet.is_active():
                return False
            
            # Update bullet data
            bullet.update_position(new_x, new_y, dt)
            
            # Update trail system if bullet has trails enabled
            if self.trail_system and bullet.trail_enabled:
                self._update_bullet_trail(bullet, new_x, new_y)
            
            return True
            
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR updating bullet {bullet_id}: {e}")
            return False
    
    def _update_bullet_trail(self, bullet: BulletData, new_x: float, new_y: float):
        """Update bullet trail in the trail system"""
        try:
            obj_id = bullet.bullet_id
            
            # Check if we should update trail this frame
            if not bullet.should_update_trail(self.game.frame_counter or 0):
                return
            
            # Update trail data
            if obj_id in self.trail_system.trail_data:
                x_array, y_array, count = self.trail_system.trail_data[obj_id]
                
                # Shift existing data (circular buffer)
                max_segments = min(bullet.trail_segments, len(x_array))
                for i in range(max_segments - 1, 0, -1):
                    x_array[i] = x_array[i-1]
                    y_array[i] = y_array[i-1]
                
                # Add new position
                x_array[0] = new_x
                y_array[0] = new_y
                
                # Update count
                new_count = min(count + 1, max_segments)
                self.trail_system.trail_data[obj_id] = (x_array, y_array, new_count)
                
                # Update stored position
                self.trail_system.object_positions[obj_id] = (new_x, new_y)
                
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR updating bullet trail: {e}")
    
    def destroy_bullet(self, bullet_id: int) -> bool:
        """Destroy a bullet and clean up its resources"""
        try:
            if bullet_id not in self.bullets:
                return False
            
            bullet = self.bullets[bullet_id]
            
            # Clean up trail system
            if self.trail_system and bullet_id in self.trail_system.trail_data:
                del self.trail_system.trail_data[bullet_id]
            if self.trail_system and bullet_id in self.trail_system.object_positions:
                del self.trail_system.object_positions[bullet_id]
            
            # Mark bullet as destroyed
            bullet.destroy()
            
            # Remove from active bullets
            if bullet.state == BulletState.ACTIVE:
                self.active_bullets -= 1
            
            # Remove from storage
            del self.bullets[bullet_id]
            self.bullets_destroyed += 1
            
            if getattr(self.game, 'debug_mode', False):
                print(f"DEBUG: Destroyed bullet {bullet_id}")
            
            return True
            
        except Exception as e:
            print(f"ERROR destroying bullet {bullet_id}: {e}")
            traceback.print_exc()
            return False
    
    def deactivate_bullet(self, bullet_id: int) -> bool:
        """Deactivate a bullet (pause processing)"""
        try:
            if bullet_id not in self.bullets:
                return False
            
            bullet = self.bullets[bullet_id]
            if bullet.state == BulletState.ACTIVE:
                bullet.deactivate()
                self.active_bullets -= 1
                return True
            
            return False
            
        except Exception as e:
            print(f"ERROR deactivating bullet {bullet_id}: {e}")
            return False
    
    def reactivate_bullet(self, bullet_id: int) -> bool:
        """Reactivate a deactivated bullet"""
        try:
            if bullet_id not in self.bullets:
                return False
            
            bullet = self.bullets[bullet_id]
            if bullet.state == BulletState.INACTIVE:
                bullet.reactivate()
                self.active_bullets += 1
                return True
            
            return False
            
        except Exception as e:
            print(f"ERROR reactivating bullet {bullet_id}: {e}")
            return False
    
    def get_bullet(self, bullet_id: int) -> Optional[BulletData]:
        """Get bullet data by ID"""
        return self.bullets.get(bullet_id)
    
    def get_active_bullets(self) -> List[BulletData]:
        """Get list of all active bullets"""
        return [bullet for bullet in self.bullets.values() if bullet.is_active()]
    
    def get_bullets_by_material(self, material: MaterialType) -> List[BulletData]:
        """Get all bullets of a specific material type"""
        return [bullet for bullet in self.bullets.values() 
                if bullet.material_type == material and bullet.is_active()]
    
    def get_bullets_in_radius(self, center_x: float, center_y: float, radius: float) -> List[BulletData]:
        """Get all bullets within a certain radius of a point"""
        bullets_in_radius = []
        radius_squared = radius * radius
        
        for bullet in self.bullets.values():
            if not bullet.is_active():
                continue
            
            dx = bullet.x - center_x
            dy = bullet.y - center_y
            distance_squared = dx*dx + dy*dy
            
            if distance_squared <= radius_squared:
                bullets_in_radius.append(bullet)
        
        return bullets_in_radius
    
    def apply_force_to_bullets(self, center_x: float, center_y: float, force_x: float, force_y: float, radius: float):
        """Apply force to all bullets within a radius (for explosions, etc.)"""
        bullets_in_radius = self.get_bullets_in_radius(center_x, center_y, radius)
        
        for bullet in bullets_in_radius:
            # Apply force based on distance (stronger at center)
            dx = bullet.x - center_x
            dy = bullet.y - center_y
            distance = (dx*dx + dy*dy)**0.5
            
            if distance > 0:
                # Force decreases with distance
                force_multiplier = max(0, 1.0 - distance / radius)
                bullet.apply_acceleration(
                    force_x * force_multiplier / bullet.mass,
                    force_y * force_multiplier / bullet.mass
                )
    
    def cleanup_destroyed_bullets(self):
        """Remove destroyed bullets and perform maintenance"""
        try:
            current_time = time.time()
            
            # Only cleanup periodically
            if current_time - self.last_cleanup < self.cleanup_interval:
                return
            
            # Remove destroyed bullets
            destroyed_ids = [bid for bid, bullet in self.bullets.items() 
                           if bullet.state == BulletState.DESTROYED]
            
            for bullet_id in destroyed_ids:
                self.destroy_bullet(bullet_id)
            
            # Update statistics
            self.last_cleanup = current_time
            
            if getattr(self.game, 'debug_mode', False) and destroyed_ids:
                print(f"DEBUG: Cleaned up {len(destroyed_ids)} destroyed bullets")
                
        except Exception as e:
            print(f"ERROR during bullet cleanup: {e}")
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get bullet system statistics"""
        return {
            "total_bullets": len(self.bullets),
            "active_bullets": self.active_bullets,
            "bullets_created": self.bullets_created,
            "bullets_destroyed": self.bullets_destroyed,
            "max_bullets": self.max_bullets,
            "trail_integration": self.trail_system is not None
        }
    
    def clear_all_bullets(self):
        """Clear all bullets from the system"""
        try:
            # Clean up trail system
            if self.trail_system:
                for bullet_id in list(self.bullets.keys()):
                    if bullet_id in self.trail_system.trail_data:
                        del self.trail_system.trail_data[bullet_id]
                    if bullet_id in self.trail_system.object_positions:
                        del self.trail_system.object_positions[bullet_id]
            
            # Clear all bullets
            self.bullets.clear()
            self.active_bullets = 0
            
            if getattr(self.game, 'debug_mode', False):
                print("DEBUG: All bullets cleared")
                
        except Exception as e:
            print(f"ERROR clearing all bullets: {e}")
    
    def update(self, dt: float):
        """Update bullet manager (called every frame)"""
        try:
            # Cleanup destroyed bullets periodically
            self.cleanup_destroyed_bullets()
            
            # Update bullet ages
            current_time = time.time()
            for bullet in self.bullets.values():
                bullet.age = current_time - bullet.creation_time
                
        except Exception as e:
            print(f"ERROR updating bullet manager: {e}")
            traceback.print_exc()
