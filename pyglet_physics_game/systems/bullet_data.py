import time
import math
from typing import Tuple, Optional, Dict, Any
from dataclasses import dataclass, field
from enum import Enum

class MaterialType(Enum):
    """Bullet material types for sound and visual effects"""
    METAL = "metal"
    ENERGY = "energy"
    PLASMA = "plasma"
    CRYSTAL = "crystal"
    ORGANIC = "organic"
    VOID = "void"

class BulletState(Enum):
    """Bullet lifecycle states"""
    ACTIVE = "active"
    INACTIVE = "inactive"
    DESTROYED = "destroyed"
    RICOCHET = "ricochet"

@dataclass
class BulletData:
    """Comprehensive bullet data tracking for sound design and trail management"""
    
    # Unique identification
    bullet_id: int
    creation_time: float = field(default_factory=time.time)
    
    # Core physics parameters
    x: float = 0.0
    y: float = 0.0
    velocity_x: float = 0.0
    velocity_y: float = 0.0
    acceleration_x: float = 0.0
    acceleration_y: float = 0.0
    mass: float = 1.0
    angle: float = 0.0
    
    # Trail management parameters
    trail_enabled: bool = False
    trail_segments: int = 0
    trail_opacity: int = 255
    trail_color: Tuple[int, int, int] = (255, 255, 0)
    trail_width: float = 1.0
    
    # Speed thresholds for trail management
    min_trail_speed: float = 50.0  # pixels/frame - below this, no trail
    max_trail_speed: float = 300.0  # pixels/frame - above this, full trail
    max_trail_segments: int = 10  # maximum trail segments allowed
    
    # Sound-relevant parameters
    material_type: MaterialType = MaterialType.METAL
    impact_force: float = 0.0
    air_resistance: float = 0.1
    angular_velocity: float = 0.0
    lifetime: float = 0.0
    age: float = 0.0
    
    # Advanced sound parameters
    relative_velocity: float = 0.0
    distance_from_listener: float = 0.0
    room_type: str = "default"
    reverb_level: float = 0.5
    bounce_count: int = 0
    ricochet_angle: float = 0.0
    kinetic_energy: float = 0.0
    potential_energy: float = 0.0
    
    # Performance parameters
    trail_update_rate: int = 1  # update every N frames
    detail_level: int = 1  # LOD level (1=high, 2=medium, 3=low)
    
    # State management
    state: BulletState = BulletState.ACTIVE
    last_update_time: float = field(default_factory=time.time)
    
    def __post_init__(self):
        """Initialize computed properties after creation"""
        self._update_computed_properties()
    
    def _update_computed_properties(self):
        """Update all computed properties based on current values"""
        # Calculate speed and direction
        self.speed = math.sqrt(self.velocity_x**2 + self.velocity_y**2)
        self.accel_magnitude = math.sqrt(self.acceleration_x**2 + self.acceleration_y**2)
        
        # Calculate angle from velocity
        if self.speed > 0.001:
            self.angle = math.atan2(self.velocity_y, self.velocity_x)
        
        # Calculate kinetic energy
        self.kinetic_energy = 0.5 * self.mass * (self.speed**2)
        
        # Calculate impact force (mass × velocity²)
        self.impact_force = self.mass * (self.speed**2)
        
        # Update age
        self.age = time.time() - self.creation_time
        
        # Update trail parameters based on speed
        self._update_trail_parameters()
    
    def _update_trail_parameters(self):
        """Dynamically update trail parameters based on bullet speed"""
        if self.speed < self.min_trail_speed:
            # Too slow - disable trail
            self.trail_enabled = False
            self.trail_segments = 0
            self.trail_opacity = 0
        elif self.speed > self.max_trail_speed:
            # Very fast - full trail
            self.trail_enabled = True
            self.trail_segments = self.max_trail_segments
            self.trail_opacity = 255
            self.trail_width = 2.0
        else:
            # Scale trail with speed
            speed_ratio = (self.speed - self.min_trail_speed) / (self.max_trail_speed - self.min_trail_speed)
            self.trail_enabled = True
            self.trail_segments = int(speed_ratio * self.max_trail_segments)
            self.trail_opacity = int(100 + speed_ratio * 155)  # 100-255 opacity
            self.trail_width = 1.0 + speed_ratio  # 1.0-2.0 width
        
        # Adjust trail color based on speed (yellow to orange to red)
        if self.trail_enabled:
            from pyglet_physics_game.ui.color_manager import get_color_manager
            color_mgr = get_color_manager()
            self.trail_color = color_mgr.get_trail_color(self.speed)
    
    def update_position(self, new_x: float, new_y: float, dt: float):
        """Update bullet position and recalculate all dependent properties"""
        # Calculate velocity from position change
        if dt > 0:
            self.velocity_x = (new_x - self.x) / dt
            self.velocity_y = (new_y - self.y) / dt
        
        # Update position
        self.x = new_x
        self.y = new_y
        
        # Update computed properties
        self._update_computed_properties()
        
        # Update last update time
        self.last_update_time = time.time()
    
    def update_velocity(self, new_vx: float, new_vy: float):
        """Update bullet velocity and recalculate all dependent properties"""
        self.velocity_x = new_vx
        self.velocity_y = new_vy
        self._update_computed_properties()
    
    def apply_acceleration(self, ax: float, ay: float):
        """Apply acceleration to bullet"""
        self.acceleration_x = ax
        self.acceleration_y = ay
        self._update_computed_properties()
    
    def set_material(self, material: MaterialType):
        """Set bullet material type"""
        self.material_type = material
        # Adjust properties based on material
        from pyglet_physics_game.ui.color_manager import get_color_manager
        color_mgr = get_color_manager()
        if material == MaterialType.ENERGY:
            self.air_resistance = 0.05
            self.trail_color = color_mgr.get_material_color('energy')
        elif material == MaterialType.PLASMA:
            self.air_resistance = 0.02
            self.trail_color = color_mgr.get_material_color('plasma')
        elif material == MaterialType.CRYSTAL:
            self.air_resistance = 0.15
            self.trail_color = color_mgr.get_material_color('crystal')
    
    def record_collision(self, collision_force: float, collision_angle: float):
        """Record collision data for sound effects"""
        self.bounce_count += 1
        self.ricochet_angle = collision_angle
        self.impact_force = max(self.impact_force, collision_force)
        
        # Reduce trail after collision
        if self.trail_segments > 0:
            self.trail_segments = max(0, self.trail_segments - 2)
    
    def get_trail_data(self) -> Dict[str, Any]:
        """Get trail data for rendering system"""
        if not self.trail_enabled:
            return {"enabled": False}
        
        return {
            "enabled": True,
            "segments": self.trail_segments,
            "opacity": self.trail_opacity,
            "color": self.trail_color,
            "width": self.trail_width,
            "update_rate": self.trail_update_rate
        }
    
    def get_sound_data(self) -> Dict[str, Any]:
        """Get sound-relevant data for audio system"""
        return {
            "material": self.material_type.value,
            "impact_force": self.impact_force,
            "speed": self.speed,
            "kinetic_energy": self.kinetic_energy,
            "bounce_count": self.bounce_count,
            "age": self.age,
            "distance": self.distance_from_listener,
            "room_type": self.room_type
        }
    
    def should_update_trail(self, frame_count: int) -> bool:
        """Check if trail should be updated this frame"""
        return frame_count % self.trail_update_rate == 0
    
    def is_active(self) -> bool:
        """Check if bullet is active and should be processed"""
        return self.state == BulletState.ACTIVE
    
    def destroy(self):
        """Mark bullet as destroyed"""
        self.state = BulletState.DESTROYED
        self.trail_enabled = False
        self.trail_segments = 0
    
    def deactivate(self):
        """Deactivate bullet (pause processing)"""
        self.state = BulletState.INACTIVE
        self.trail_enabled = False
    
    def reactivate(self):
        """Reactivate bullet"""
        self.state = BulletState.ACTIVE
        self._update_trail_parameters()
    
    def get_performance_stats(self) -> Dict[str, Any]:
        """Get performance statistics for debugging"""
        return {
            "bullet_id": self.bullet_id,
            "speed": self.speed,
            "trail_enabled": self.trail_enabled,
            "trail_segments": self.trail_segments,
            "update_rate": self.trail_update_rate,
            "detail_level": self.detail_level,
            "age": self.age
        }
