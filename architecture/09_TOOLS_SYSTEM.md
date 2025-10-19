# Tools Directory - Architecture Documentation

## Overview
The tools directory contains specialized tools for audio effects, melody generation, and physics effects. These tools integrate with the main game systems to provide advanced functionality for audio synthesis and physics simulation using SignalFlow for audio and Pymunk for physics.

## Tool Architecture

### Tool Categories
- **Audio Effects**: Real-time audio processing and effects
- **Melody Generation**: Musical pattern and melody creation
- **Physics Effects**: Advanced physics simulation effects

### Integration Points
- **SignalFlow Integration**: Audio tools use SignalFlow for synthesis
- **Pymunk Integration**: Physics tools use Pymunk for simulation
- **UI Integration**: Tools integrate with the user interface
- **Performance Optimization**: Tools are optimized for real-time use

## Directory Structure

### Audio Effects (`/audio_effects/`)
- **`chorus.py`**: Chorus effect using Pedalboard
- **`compressor.py`**: Audio compression effect
- **`delay.py`**: Delay and echo effects
- **`distortion.py`**: Distortion and overdrive effects
- **`noise_gate.py`**: Noise gate and filtering
- **`reverb.py`**: Reverb and spatial effects

### Melody Generation (`/melody/`)
- **`arpeggiator.py`**: Arpeggio pattern generation
- **`chord_progression.py`**: Chord progression creation
- **`rhythm_pattern.py`**: Rhythm pattern generation
- **`scale_generator.py`**: Musical scale generation

### Physics Effects (`/physics_effects/`)
- **`force_field.py`**: Force field physics effects
- **`gravity_well.py`**: Gravity well simulation
- **`spring_launcher.py`**: Spring-based launching system
- **`teleporter.py`**: Teleportation physics effects
- **`vortex.py`**: Vortex and swirling effects

## Audio Effects System

### Pedalboard Integration
**Purpose**: Professional audio effects using Pedalboard library

**Key Features**:
- **Real-time Processing**: Live audio effect processing
- **Professional Quality**: High-quality audio effects
- **Parameter Control**: Real-time parameter adjustment
- **SignalFlow Integration**: Seamless integration with SignalFlow

**Effect Categories**:
- **Time-based**: Delay, reverb, chorus
- **Dynamic**: Compressor, noise gate
- **Distortion**: Overdrive, distortion, fuzz
- **Spatial**: Reverb, stereo effects

### Reverb Effect (`reverb.py`)
**Purpose**: Spatial audio effects using Pedalboard's Reverb plugin

**Parameters**:
- **Room Size**: 0.0 to 1.0 (reverb space size)
- **Damping**: 0.0 to 1.0 (high-frequency damping)
- **Wet Level**: 0.0 to 1.0 (effect intensity)
- **Dry Level**: 0.0 to 1.0 (original signal level)
- **Width**: 0.0 to 1.0 (stereo width)
- **Freeze Mode**: 0.0 to 1.0 (infinite reverb)

**Integration**:
- **SignalFlow**: Real-time audio processing
- **UI Control**: Parameter adjustment interface
- **Performance**: Optimized for real-time use

### Chorus Effect (`chorus.py`)
**Purpose**: Chorus and modulation effects

**Features**:
- **Rate Control**: Modulation rate adjustment
- **Depth Control**: Effect depth control
- **Mix Control**: Dry/wet signal mixing
- **Real-time Processing**: Live audio processing

### Compressor Effect (`compressor.py`)
**Purpose**: Dynamic range compression

**Features**:
- **Threshold**: Compression threshold
- **Ratio**: Compression ratio
- **Attack**: Attack time
- **Release**: Release time
- **Makeup Gain**: Output gain compensation

## Melody Generation System

### Arpeggiator (`arpeggiator.py`)
**Purpose**: Arpeggio pattern generation for musical sequences

**Key Features**:
- **Pattern Types**: Various arpeggio patterns
- **Tempo Control**: Adjustable tempo and timing
- **Scale Integration**: Integration with scale generator
- **Real-time Generation**: Live pattern generation

**Pattern Types**:
- **Up**: Ascending arpeggio
- **Down**: Descending arpeggio
- **Up-Down**: Alternating arpeggio
- **Random**: Random pattern generation

### Chord Progression (`chord_progression.py`)
**Purpose**: Musical chord progression creation

**Key Features**:
- **Chord Types**: Major, minor, diminished, augmented
- **Progressions**: Common chord progressions
- **Key Integration**: Integration with scale system
- **Real-time Generation**: Live progression generation

**Chord Types**:
- **Triads**: Major, minor, diminished, augmented
- **Sevenths**: Major 7th, minor 7th, dominant 7th
- **Extensions**: 9th, 11th, 13th chords
- **Alterations**: Sharp/flat alterations

### Rhythm Pattern (`rhythm_pattern.py`)
**Purpose**: Rhythm pattern generation

**Key Features**:
- **Pattern Types**: Various rhythm patterns
- **Tempo Control**: Adjustable tempo
- **Time Signatures**: Different time signatures
- **Real-time Generation**: Live pattern generation

**Pattern Types**:
- **Basic**: Simple rhythm patterns
- **Complex**: Advanced rhythm patterns
- **Polyrhythmic**: Multiple rhythm layers
- **Custom**: User-defined patterns

### Scale Generator (`scale_generator.py`)
**Purpose**: Musical scale generation and management

**Key Features**:
- **Scale Types**: Major, minor, modal scales
- **Key Signatures**: All major and minor keys
- **Mode Support**: Ionian, Dorian, Phrygian, etc.
- **Real-time Generation**: Live scale generation

**Scale Types**:
- **Major Scales**: Ionian mode
- **Minor Scales**: Natural, harmonic, melodic
- **Modal Scales**: Dorian, Phrygian, Lydian, etc.
- **Exotic Scales**: Pentatonic, blues, etc.

## Physics Effects System

### Force Field (`force_field.py`)
**Purpose**: Force field physics effects

**Key Features**:
- **Field Types**: Attraction, repulsion, vortex
- **Strength Control**: Adjustable field strength
- **Range Control**: Field range and falloff
- **Real-time Application**: Live force application

**Field Types**:
- **Attraction**: Pulling force field
- **Repulsion**: Pushing force field
- **Vortex**: Rotational force field
- **Gravity**: Gravitational force field

### Gravity Well (`gravity_well.py`)
**Purpose**: Gravitational well simulation

**Key Features**:
- **Gravity Strength**: Adjustable gravity strength
- **Well Size**: Gravitational well radius
- **Falloff**: Gravity falloff with distance
- **Real-time Simulation**: Live gravity simulation

**Physics Properties**:
- **Mass**: Well mass for gravity calculation
- **Radius**: Effective gravity radius
- **Strength**: Gravitational force strength
- **Falloff**: Distance-based force reduction

### Spring Launcher (`spring_launcher.py`)
**Purpose**: Spring-based launching system

**Key Features**:
- **Spring Force**: Adjustable spring strength
- **Launch Direction**: Configurable launch direction
- **Energy Transfer**: Kinetic energy transfer
- **Real-time Launching**: Live spring launching

**Spring Properties**:
- **Stiffness**: Spring constant
- **Damping**: Spring damping
- **Rest Length**: Natural spring length
- **Max Compression**: Maximum compression

### Teleporter (`teleporter.py`)
**Purpose**: Teleportation physics effects

**Key Features**:
- **Teleport Pairs**: Paired teleportation points
- **Instant Transport**: Immediate position change
- **Velocity Transfer**: Velocity preservation
- **Real-time Teleportation**: Live teleportation

**Teleportation Properties**:
- **Position**: Teleportation coordinates
- **Velocity**: Velocity transfer
- **Orientation**: Direction preservation
- **Energy**: Energy conservation

### Vortex (`vortex.py`)
**Purpose**: Vortex and swirling effects

**Key Features**:
- **Vortex Strength**: Adjustable vortex strength
- **Rotation Speed**: Vortex rotation speed
- **Vortex Size**: Effective vortex radius
- **Real-time Simulation**: Live vortex simulation

**Vortex Properties**:
- **Angular Velocity**: Rotation speed
- **Radius**: Vortex effective radius
- **Strength**: Vortex force strength
- **Direction**: Rotation direction

## Integration Architecture

### SignalFlow Integration
- **Real-time Synthesis**: Live audio generation
- **Effect Processing**: Real-time effect application
- **Parameter Control**: Live parameter adjustment
- **Performance Optimization**: Optimized for real-time use

### Pymunk Integration
- **Physics Simulation**: Real-time physics effects
- **Force Application**: Live force application
- **Collision Detection**: Physics collision handling
- **Performance Optimization**: Optimized physics calculations

### UI Integration
- **Parameter Control**: Real-time parameter adjustment
- **Visual Feedback**: Effect visualization
- **User Interface**: Tool control interface
- **Performance Monitoring**: Real-time performance display

## Performance Characteristics

### Audio Performance
- **Latency**: Low-latency audio processing
- **CPU Usage**: Optimized CPU usage
- **Memory Usage**: Efficient memory management
- **Quality**: High-quality audio output

### Physics Performance
- **Simulation Speed**: Real-time physics simulation
- **Force Calculation**: Efficient force calculations
- **Collision Detection**: Optimized collision detection
- **Memory Usage**: Efficient memory usage

### System Performance
- **Integration**: Seamless system integration
- **Resource Usage**: Efficient resource usage
- **Scalability**: Scalable tool system
- **Maintainability**: Easy maintenance and updates

## Development Guidelines

### Adding New Tools
1. **Create Tool Class**: Implement tool with clear interface
2. **SignalFlow Integration**: Connect to SignalFlow for audio
3. **Pymunk Integration**: Connect to Pymunk for physics
4. **UI Integration**: Add user interface controls
5. **Performance Optimization**: Optimize for real-time use

### Tool Communication
- **Event-driven**: Tools communicate through events
- **Data Sharing**: Shared data structures for efficiency
- **Loose Coupling**: Tools are loosely coupled
- **Clear Interfaces**: Well-defined tool interfaces

## Future Enhancements

### Planned Features
- **Advanced Audio**: More sophisticated audio effects
- **Complex Physics**: More advanced physics effects
- **AI Integration**: AI-powered tool generation
- **Performance**: Further performance optimizations

### Tool Improvements
- **Modularity**: More modular tool design
- **Extensibility**: Easier tool extension
- **Testing**: Comprehensive tool testing
- **Documentation**: Enhanced tool documentation

## Troubleshooting

### Common Issues
- **Audio Latency**: Audio processing delays
- **Physics Instability**: Physics simulation issues
- **Performance**: Tool performance problems
- **Integration**: Tool integration issues

### Solutions
- **Audio Debugging**: Debug audio processing
- **Physics Debugging**: Debug physics simulation
- **Performance Profiling**: Profile tool performance
- **Integration Testing**: Test tool integration

## Best Practices

### Tool Design
- **Single Responsibility**: Each tool has one clear purpose
- **Loose Coupling**: Tools are loosely coupled
- **High Cohesion**: Related functionality grouped together
- **Clear Interfaces**: Well-defined tool interfaces

### Performance
- **Optimization**: Continuous performance optimization
- **Profiling**: Regular performance profiling
- **Memory Management**: Efficient memory usage
- **Resource Cleanup**: Proper resource cleanup

### Testing
- **Unit Testing**: Test individual tools
- **Integration Testing**: Test tool interactions
- **Performance Testing**: Test tool performance
- **Regression Testing**: Prevent performance regressions
