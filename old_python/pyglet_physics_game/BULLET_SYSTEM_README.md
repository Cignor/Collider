# Bullet Data System

A comprehensive bullet management system designed for sound design, trail management, and performance optimization.

## Features

### 🎯 **Unique Bullet Identification**
- Each bullet gets a unique ID for precise creation/deletion
- Efficient bullet lifecycle management
- Support for up to 1000 bullets simultaneously

### 🚀 **Speed-Based Trail Management**
- **No trails** for bullets moving < 50 pixels/frame
- **Scaled trails** for bullets moving 50-300 pixels/frame
- **Full trails** for bullets moving > 300 pixels/frame
- Trail length, opacity, and width automatically adjust with speed

### 🎨 **Dynamic Visual Effects**
- Trail colors change with speed (Yellow → Orange → Red)
- Material-based trail colors (Energy = Cyan, Plasma = Magenta, Crystal = Green)
- Automatic trail width scaling (1.0x to 2.0x)

### 🔊 **Sound-Ready Parameters**
- Impact force calculation (mass × velocity²)
- Material-specific properties (air resistance, sound characteristics)
- Collision history (bounce count, ricochet angles)
- Distance and environment data for 3D audio

### ⚡ **Performance Optimization**
- Automatic trail deactivation for slow bullets
- Configurable update frequencies
- LOD (Level of Detail) system
- Memory-efficient data structures

## Quick Start

### 1. Basic Bullet Creation

```python
from systems.bullet_data import BulletData, MaterialType
from systems.bullet_manager import BulletManager

# Create bullet manager
bullet_manager = BulletManager(game)

# Create a simple bullet
bullet_id = bullet_manager.create_bullet(
    x=100, y=100,           # Position
    vx=50, vy=30,           # Velocity
    material=MaterialType.METAL,  # Material type
    mass=1.0                 # Mass
)
```

### 2. Speed-Based Trail Effects

```python
# Get bullet data
bullet = bullet_manager.get_bullet(bullet_id)

# Update bullet position (trails update automatically)
bullet_manager.update_bullet(bullet_id, 150, 130, 0.016)

# Check trail status
if bullet.trail_enabled:
    print(f"Trail segments: {bullet.trail_segments}")
    print(f"Trail color: {bullet.trail_color}")
    print(f"Trail opacity: {bullet.trail_opacity}")
```

### 3. Material Types

```python
# Different materials have different properties
bullet.set_material(MaterialType.ENERGY)    # Low air resistance, cyan trails
bullet.set_material(MaterialType.PLASMA)    # Very low air resistance, magenta trails
bullet.set_material(MaterialType.CRYSTAL)   # High air resistance, green trails
bullet.set_material(MaterialType.METAL)     # Standard properties, yellow trails
```

## Advanced Usage

### Trail Integration

```python
# Set up trail system integration
bullet_manager.set_trail_system(game.trail_system)

# Bullets automatically integrate with trails
# Trails are managed based on bullet speed and material
```

### Collision Recording

```python
# Record collision for sound effects
bullet.record_collision(
    collision_force=500.0,
    collision_angle=math.pi/4
)

# Access collision data
print(f"Bounce count: {bullet.bounce_count}")
print(f"Impact force: {bullet.impact_force}")
```

### Bullet Queries

```python
# Get all active bullets
active_bullets = bullet_manager.get_active_bullets()

# Get bullets by material
energy_bullets = bullet_manager.get_bullets_by_material(MaterialType.ENERGY)

# Get bullets in radius (for explosions, etc.)
nearby_bullets = bullet_manager.get_bullets_in_radius(200, 200, 100)
```

### Force Application

```python
# Apply explosion force to nearby bullets
bullet_manager.apply_force_to_bullets(
    center_x=200, center_y=200,  # Explosion center
    force_x=1000, force_y=1000,  # Force vector
    radius=150                     # Affected radius
)
```

## Configuration

### Speed Thresholds

```python
# Customize trail speed thresholds
bullet.min_trail_speed = 30.0    # Below this: no trails
bullet.max_trail_speed = 400.0   # Above this: full trails
bullet.max_trail_segments = 15   # Maximum trail length
```

### Performance Settings

```python
# Adjust update frequency (higher = better performance)
bullet.trail_update_rate = 2     # Update every 2 frames

# Set detail level
bullet.detail_level = 2          # 1=high, 2=medium, 3=low
```

### Material Properties

```python
# Customize material properties
if bullet.material_type == MaterialType.CUSTOM:
    bullet.air_resistance = 0.08
    bullet.trail_color = (255, 128, 0)  # Custom orange
```

## Sound Design Integration

### Impact Sounds

```python
# Get sound data for audio system
sound_data = bullet.get_sound_data()

# Use in audio engine
if sound_data["impact_force"] > 1000:
    play_sound("heavy_impact", volume=sound_data["impact_force"] / 10000)
```

### Material-Based Audio

```python
# Different materials = different sounds
if bullet.material_type == MaterialType.ENERGY:
    play_sound("energy_whine", pitch=bullet.speed / 100)
elif bullet.material_type == MaterialType.METAL:
    play_sound("metal_ricochet", volume=bullet.bounce_count * 0.2)
```

### 3D Audio Support

```python
# Position audio based on bullet location
audio_position = (bullet.x, bullet.y, bullet.distance_from_listener)
play_3d_sound("bullet_whoosh", audio_position, bullet.relative_velocity)
```

## Performance Tips

### 1. **Trail Management**
- Slow bullets automatically disable trails
- Use `trail_update_rate` to skip frames for distant bullets
- Set appropriate `detail_level` based on bullet importance

### 2. **Memory Efficiency**
- Destroy bullets when they're no longer needed
- Use `deactivate_bullet()` for temporary pauses
- Regular cleanup with `cleanup_destroyed_bullets()`

### 3. **Update Optimization**
- Only update bullets that have moved significantly
- Use `should_update_trail()` to check update frequency
- Batch bullet operations when possible

## Integration with Existing Systems

### Trail System
The bullet system automatically integrates with the existing trail system:
- Bullets create trail entries when trails are enabled
- Trail data is automatically cleaned up when bullets are destroyed
- Speed-based trail parameters are automatically applied

### Physics System
Bullets can be integrated with the physics system:
- Use bullet IDs to track physics objects
- Apply forces through the bullet manager
- Handle collisions and update bullet data

### Rendering System
Trail rendering automatically uses bullet trail data:
- Colors, opacity, and width from bullet properties
- Automatic trail length adjustment
- Material-based visual effects

## Example: Complete Bullet Lifecycle

```python
# 1. Create bullet
bullet_id = bullet_manager.create_bullet(100, 100, 200, 150, MaterialType.PLASMA)

# 2. Update during game loop
bullet_manager.update_bullet(bullet_id, 120, 115, dt)

# 3. Handle collision
bullet = bullet_manager.get_bullet(bullet_id)
bullet.record_collision(800.0, math.pi/6)

# 4. Apply force (explosion)
bullet_manager.apply_force_to_bullets(150, 120, 500, 500, 50)

# 5. Destroy when done
bullet_manager.destroy_bullet(bullet_id)
```

## Debugging

### Enable Debug Mode
```python
game.debug_mode = True
```

### Check Statistics
```python
stats = bullet_manager.get_statistics()
print(f"Active bullets: {stats['active_bullets']}")
print(f"Trail integration: {stats['trail_integration']}")
```

### Performance Monitoring
```python
# Get bullet performance data
bullet = bullet_manager.get_bullet(bullet_id)
perf_stats = bullet.get_performance_stats()
print(f"Update rate: {perf_stats['update_rate']}")
print(f"Detail level: {perf_stats['detail_level']}")
```

This bullet system provides a solid foundation for advanced game mechanics, sound design, and visual effects while maintaining excellent performance through intelligent trail management and optimization.
