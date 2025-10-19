# Audio System - Architecture Documentation

## Overview
The audio system provides real-time audio synthesis and processing using the SignalFlow library. It integrates with the physics simulation to create audio-visual experiences where physics objects generate sound based on their properties and interactions.

## System Architecture

### Core Components
- **Frequency Generators**: Pure tone generation (sine, square, triangle waves)
- **Noise Generators**: Various noise types (white, pink, brown)
- **Sample Library**: Pre-recorded audio samples for effects and instruments
- **Audio Integration**: Real-time audio synthesis based on physics

### Directory Structure

#### `/frequencies/` - Pure Tone Generation
- **`sine.py` & `sine.json`**: Sine wave generator
  - Frequency range: 20Hz - 20kHz
  - Gain control: -24dB to +12dB
- **`square.py` & `square.json`**: Square wave generator
  - Same parameter structure as sine
- **`triangle.py` & `triangle.json`**: Triangle wave generator
  - Same parameter structure as sine

#### `/noise/` - Noise Generation
- **`white_noise.py` & `white_noise.json`**: White noise generator
  - Gain control: -24dB to +12dB
  - Fade control: 0-2000ms
- **`pink_noise.py` & `pink_noise.json`**: Pink noise generator
- **`brown_noise.py` & `brown_noise.json`**: Brown noise generator

#### `/samples/` - Sample Library
- **`ambient/`**: Atmospheric and ambient sounds
  - Bass slides, duduk, joombush samples
  - Various musical keys and scales
- **`effects/`**: Sound effects and transitions
  - Animal sounds, psy FX
  - Various tempos and keys
- **`melodic/`**: Melodic instruments and fills
  - Saxophone, kemenche, bansuri, koto
  - Steel drums, orchestra, synth fills
- **`percussion/`**: Rhythmic elements
  - Acoustic crashes, kicks, rides, toms
  - Various timbres and dynamics
- **`new/`**: New sample additions

## Audio Integration

### Physics-Audio Connection
- **Sound Bullets**: Physics objects that generate audio
- **Material Properties**: Different materials produce different sounds
- **Real-time Synthesis**: Audio parameters change based on physics
- **Spatial Audio**: Position-based audio effects

### Audio Properties
Each audio generator has configurable parameters:
- **Frequency**: Pitch control for tone generators
- **Gain**: Volume control (-24dB to +12dB)
- **Fade**: Envelope control for noise generators
- **Material Type**: Affects audio characteristics

## Technical Implementation

### SignalFlow Integration
- **Real-time Processing**: Low-latency audio synthesis
- **Parameter Control**: Dynamic parameter adjustment
- **Audio Threading**: Separate audio processing thread
- **Buffer Management**: Efficient audio buffer handling

### Parameter System
- **JSON Configuration**: Human-readable parameter files
- **Type Safety**: Strongly typed parameters
- **Range Validation**: Min/max value constraints
- **Step Control**: Precise parameter adjustment

### Sample Management
- **WAV Format**: High-quality audio samples
- **Memory Management**: Efficient sample loading
- **Caching**: Sample preloading for performance
- **Streaming**: Large sample streaming support

## Audio Categories

### Frequency Generators
- **Pure Tones**: Sine, square, triangle waves
- **Harmonic Content**: Different wave shapes
- **Frequency Range**: Full audible spectrum
- **Real-time Control**: Dynamic frequency adjustment

### Noise Generators
- **White Noise**: Equal energy across all frequencies
- **Pink Noise**: Equal energy per octave
- **Brown Noise**: More energy in lower frequencies
- **Fade Control**: Envelope shaping

### Sample Library
- **Ambient**: Atmospheric and background sounds
- **Effects**: Transitions and special effects
- **Melodic**: Musical instruments and melodies
- **Percussion**: Rhythmic and percussive elements

## Performance Characteristics

### Audio Quality
- **Sample Rate**: 44.1kHz standard
- **Bit Depth**: 16-bit minimum, 24-bit preferred
- **Latency**: <10ms target
- **Dynamic Range**: 96dB theoretical

### Memory Usage
- **Sample Caching**: Efficient memory management
- **Streaming**: Large sample support
- **Compression**: Lossless compression where possible
- **Garbage Collection**: Automatic cleanup

## Integration with Physics

### Sound Bullet System
- **Physics Objects**: Generate audio based on properties
- **Material Types**: Different materials, different sounds
- **Collision Audio**: Sound on impact
- **Movement Audio**: Continuous sound based on motion

### Real-time Parameters
- **Frequency**: Based on object velocity
- **Gain**: Based on object mass
- **Effects**: Based on object material
- **Spatial**: Based on object position

## Development Guidelines

### Adding New Generators
1. Create `.py` file with generator class
2. Create `.json` file with parameters
3. Implement PYO integration
4. Add to audio system registry

### Adding New Samples
1. Place WAV files in appropriate category
2. Use descriptive naming convention
3. Ensure proper audio quality
4. Update sample index

### Performance Optimization
- Use efficient audio processing
- Minimize memory allocations
- Implement proper caching
- Use appropriate sample rates

## Future Enhancements

### Planned Features
- **Advanced Effects**: Reverb, delay, distortion
- **Synthesis**: More complex synthesis methods
- **Spatial Audio**: 3D audio positioning
- **MIDI Integration**: MIDI input/output support

### Audio Quality Improvements
- **Higher Sample Rates**: 48kHz, 96kHz support
- **Better Compression**: Advanced audio compression
- **Real-time Effects**: More audio effects
- **Audio Analysis**: FFT analysis and visualization

## Dependencies

### Core Dependencies
- **SignalFlow**: Audio synthesis library
- **NumPy**: Numerical computations
- **Pydub**: Audio file processing
- **Librosa**: Audio analysis

### Additional Dependencies
- **Mido**: MIDI file handling
- **Midiutil**: MIDI creation
- **Midi2audio**: MIDI to audio conversion
- **Espeakng**: Text-to-speech

## Troubleshooting

### Common Issues
- **Audio Latency**: Check buffer size settings
- **Memory Usage**: Monitor sample caching
- **Audio Quality**: Verify sample rate settings
- **Performance**: Check CPU usage

### Debug Tools
- **Audio Monitor**: Real-time audio level monitoring
- **Parameter Display**: Current parameter values
- **Performance Metrics**: Audio processing statistics
- **Error Logging**: Detailed error information
