# Pyglet Physics Game - Architecture Documentation

## Overview
The main game engine built with Pyglet for rendering and Pymunk for physics simulation. This is the core of the Collider PYO project, featuring real-time physics, interactive tools, and audio integration.

## Core Architecture

### Main Entry Point
- **`main.py`**: Entry point that initializes and runs the PhysicsGame
- **`game/game_core.py`**: Core game class that orchestrates all systems

### System Architecture
The game follows a modular architecture with clear separation of concerns:

```
PhysicsGame (game_core.py)
├── Physics Engine (Pymunk)
├── Rendering System (Pyglet)
├── Input Handling
├── Audio Integration (PYO)
├── UI Systems
└── Performance Monitoring
```

## Directory Structure

### `/game/` - Core Game Logic
- **`game_core.py`**: Main PhysicsGame class
  - Initializes all subsystems
  - Manages game loop and timing
  - Handles physics simulation (60 FPS)
  - Coordinates rendering and input
  - Manages audio bullet system

### `/physics/` - Physics Engine
- **`physics_manager.py`**: Physics space management
  - Screen wrapping for objects
  - Tool effect application
  - Collision shape management
  - Gravity control
- **`physics_objects.py`**: Physics object definitions
- **`physics_tools.py`**: Interactive physics tools
  - Collision Tool (C)
  - Wind Tool (W)
  - Magnet Tool (M)
  - Teleporter Tool (T)
  - FreeDraw Tool (F)
- **`stroke_optimizer.py`**: Stroke optimization for performance

### `/rendering/` - Graphics System
- **`renderer.py`**: Main rendering engine
  - Pyglet batch rendering for performance
  - Layered rendering system
  - Background and grid rendering
  - Physics object rendering
  - UI overlay rendering

### `/input/` - Input Handling
- **`input_handler.py`**: Input event processing
  - Keyboard input (tool switching, physics control)
  - Mouse input (tool usage, object spawning)
  - Event delegation to tools and systems

### `/systems/` - Game Systems
- **`particle_system.py`**: Particle effects and visual feedback
- **`collision_system.py`**: Collision detection and response
- **`wind_system.py`**: Wind physics simulation
- **`trail_system.py`**: Object trail rendering
- **`bullet_manager.py`**: Bullet system management
- **`sound_bullet.py`**: Audio-integrated bullets
- **`bullet_data.py`**: Bullet data structures

### `/ui/` - User Interface
- **`manager.py`**: UI system manager
- **`modular_hud.py`**: Modular HUD system
- **`theme.py`**: UI theming system
- **`color_manager.py`**: Color management
- **`coordinate_system.py`**: Coordinate system management
- **`grid_system.py`**: Grid system for snapping
- **`mouse_system.py`**: Mouse interaction system
- **`visual_feedback.py`**: Visual feedback system
- **`debug_window.py`**: Debug information display

### `/tools/` - Audio and Physics Tools
- **`audio_effects/`**: Audio effect processors
- **`melody/`**: Melody generation tools
- **`physics_effects/`**: Physics effect tools

### `/shapes/` - Shape Management
- **`shape_manager.py`**: Shape creation and management
- **`shapes.json`**: Shape definitions

### `/utils/` - Utilities
- **`performance_monitor.py`**: Performance monitoring
- **`debug_utils.py`**: Debug utilities

## Key Features

### Physics System
- **Pymunk Integration**: 2D physics simulation
- **60 FPS Physics**: Fixed timestep for stability
- **Screen Wrapping**: Classic arcade-style object wrapping
- **Tool Integration**: Physics tools affect simulation

### Rendering System
- **Pyglet Graphics**: Hardware-accelerated rendering
- **Batch Rendering**: Single batch for maximum performance
- **Layered Rendering**: Proper depth ordering
- **Performance Optimization**: 1200+ FPS target

### Audio Integration
- **PYO Library**: Real-time audio synthesis
- **Sound Bullets**: Audio-integrated physics objects
- **Audio Presets**: Configurable audio properties
- **Real-time Processing**: Low-latency audio

### UI System
- **Modular Design**: Extensible UI components
- **Theme System**: Dynamic theming
- **Grid System**: Precise positioning and snapping
- **Debug Tools**: Comprehensive debugging interface

## Performance Characteristics

### Target Performance
- **Physics**: 60 FPS simulation
- **Rendering**: 1200+ FPS
- **Audio**: Real-time with minimal latency
- **Memory**: Efficient object pooling

### Optimization Strategies
- **Batch Rendering**: Single draw call for most elements
- **Object Pooling**: Reuse of physics objects
- **LOD System**: Level of detail for distant objects
- **Spatial Partitioning**: Efficient collision detection

## Integration Points

### Audio System
- Sound bullets integrate with physics
- Audio properties affect visual appearance
- Real-time audio synthesis based on physics

### UI System
- Grid system provides precise positioning
- Theme system affects all visual elements
- Debug system provides comprehensive monitoring

### Physics Tools
- Tools modify physics space
- Visual feedback for tool usage
- Real-time physics manipulation

## Development Guidelines

### Adding New Features
1. **Physics**: Add to `/physics/` directory
2. **Rendering**: Add to `/rendering/` directory
3. **UI**: Add to `/ui/` directory
4. **Systems**: Add to `/systems/` directory

### Performance Considerations
- Use batch rendering for multiple objects
- Implement object pooling for frequently created objects
- Minimize per-frame allocations
- Use efficient data structures

### Testing
- Test with high object counts
- Verify performance targets
- Test audio integration
- Validate physics accuracy

## Dependencies

### Core Dependencies
- **Pyglet 2.1.8+**: Graphics and window management
- **Pymunk 8.0.0+**: Physics simulation
- **SignalFlow**: Audio synthesis

### Additional Dependencies
- **NumPy**: Numerical computations
- **PyCairo**: Vector graphics

## Future Enhancements

### Planned Features
- 3D rendering capabilities
- Advanced audio effects
- Multiplayer support
- Scene save/load system
- Plugin architecture

### Performance Improvements
- GPU-accelerated physics
- Advanced culling techniques
- Memory optimization
- Multi-threading support
