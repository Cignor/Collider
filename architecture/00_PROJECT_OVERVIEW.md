# Collider PYO - Project Architecture

## Overview
This is a 2D physics simulation game built with **Pyglet** and **Pymunk**, featuring interactive tools, real-time physics, and audio integration using the **PYO** library. The project combines physics simulation with audio synthesis and effects.

## Project Structure

### Core Game Engine (`pyglet_physics_game/`)
The main game engine built with Pyglet for rendering and Pymunk for physics simulation.

### Audio System (`audio/`)
Audio generation and synthesis system using PYO library with frequency generators, noise generators, and sample libraries.

### Testing Framework (`tests/`)
Comprehensive test suite covering physics, audio, and rendering components.

### Documentation (`guide/`)
Technical guides and documentation for various components.

### Backup System (`BACKUPS/`)
Version control and backup system with timestamped snapshots of the project.

## Key Technologies

- **Pyglet**: 2D graphics and window management
- **Pymunk**: 2D physics simulation
- **SignalFlow**: Audio synthesis and processing
- **PyCairo**: Vector graphics rendering
- **NumPy**: Numerical computations

## Architecture Principles

1. **Modular Design**: Each system is self-contained with clear interfaces
2. **Performance First**: Optimized for high FPS and real-time physics
3. **Audio Integration**: Seamless integration between physics and audio
4. **Extensible Tools**: Easy to add new physics tools and audio effects
5. **Cross-Platform**: Designed to work on Windows, macOS, and Linux

## Entry Points

- `run_game.py`: Main entry point for the game
- `pyglet_physics_game/main.py`: Core game engine entry point
- `test_gemini.py`: Testing entry point

## Dependencies

See `requirements.txt` for complete dependency list including:
- Pyglet 2.1.8+
- Pymunk 8.0.0+
- SignalFlow (latest)
- PyCairo 1.25.0+
- NumPy 1.24.0+

## Development Workflow

1. **Core Development**: Work in `pyglet_physics_game/` directory
2. **Audio Development**: Work in `audio/` directory
3. **Testing**: Use `tests/` directory for comprehensive testing
4. **Backup**: Automatic backups in `BACKUPS/` directory
5. **Documentation**: Update relevant `.md` files in each directory

## Performance Targets

- **Physics**: 60 FPS physics simulation
- **Rendering**: 1200+ FPS target rendering
- **Audio**: Real-time audio synthesis with minimal latency
- **Memory**: Efficient object pooling and cleanup

## Future Enhancements

- 3D rendering capabilities
- Advanced audio effects
- Multiplayer support
- Scene save/load system
- Plugin architecture for custom tools
