# 🎵 SignalFlow Integration Development Plan

## **Project Overview**
This document outlines the complete development plan for integrating SignalFlow audio synthesis into the Collider PYO physics-audio game. The core mechanic involves continuous audio emission from sound bullets, spatial audio positioning, and an interactive audio listener system.

## **Core Mechanics**
- **Sound Bullets**: Continuously emit audio in 3D space
- **Amplitude Aura**: Defines hearing range of bullets (visualized as aura)
- **Audio Listener**: Follows mouse by default, can be dropped on grid
- **Spatial Audio**: 3D positioning with distance-based volume
- **Audio Effects Zones**: Painted zones that affect audio/physics
- **Collision Sounds**: Impact sounds based on material properties

## **Phase 1: SignalFlow Foundation & Research** 🔬

### **1.1 SignalFlow API Research**
- [ ] Research SignalFlow documentation and create comprehensive API reference
- [ ] Study spatial audio capabilities for 3D positioning
- [ ] Understand real-time synthesis for continuous bullet sounds
- [ ] Document audio effects chain architecture

### **1.2 Core Audio Architecture Design**
- [ ] Audio Engine: Central SignalFlow audio engine
- [ ] Spatial Audio System: 3D positioning and distance-based volume
- [ ] Audio Listener: Mouse-following listener with drop-to-grid capability
- [ ] Sound Bullet Integration: Continuous audio emission from bullets

## **Phase 2: Audio Listener System** 🎧

### **2.1 Create Audio Listener (`/audio/listener.py`)**
```python
class AudioListener:
    - position: (x, y) - follows mouse by default
    - listening_range: float - adjustable hearing range
    - is_dropped: bool - whether listener is fixed on grid
    - drop_position: (x, y) - fixed position when dropped
    - range_visualization: visual aura showing hearing range
```

### **2.2 Listener Controls**
- [ ] Mouse Following: Default behavior - listener follows mouse
- [ ] Drop to Grid: Click to drop listener at current position
- [ ] Range Adjustment: Mouse wheel to adjust listening range
- [ ] Visual Feedback: Aura visualization showing hearing range

## **Phase 3: Spatial Audio System** 🌐

### **3.1 3D Audio Positioning**
- [ ] Distance Calculation: Calculate distance from bullets to listener
- [ ] Volume Attenuation: Volume decreases with distance
- [ ] Stereo Panning: Left/right positioning based on bullet X position
- [ ] Doppler Effect: Frequency shift based on bullet velocity

### **3.2 Amplitude Aura Integration**
- [ ] Hearing Range: Connect amplitude parameter to listener range
- [ ] Visual Aura: Green/red aura showing hearing range
- [ ] Range Scaling: Amplitude parameter controls aura size
- [ ] Fade Zones: Gradual volume falloff at aura edges

## **Phase 4: Sound Bullet Audio Integration** 🔊

### **4.1 Continuous Audio Emission**
- [ ] SignalFlow Synthesis: Each bullet emits continuous audio
- [ ] Material-Based Sounds: Different materials = different audio characteristics
- [ ] Real-time Parameters: Audio changes based on bullet properties
- [ ] Performance Optimization: Efficient audio object management

### **4.2 Audio Properties Integration**
- [ ] Frequency: Based on bullet speed/velocity
- [ ] Amplitude: Controls both volume and hearing range
- [ ] Material Type: Affects audio timbre and characteristics
- [ ] Pitch on Grid: Musical pitch quantization

## **Phase 5: Audio Effects Zones** 🎨

### **5.1 Painted Zones System**
- [ ] Zone Types: Audio effects zones, physics effects zones
- [ ] Visual Representation: Painted areas on screen
- [ ] Zone Properties: Effect parameters, intensity, shape
- [ ] Zone Interaction: Bullets entering/leaving zones

### **5.2 Audio Effects Integration**
- [ ] Reverb Zones: Add reverb to bullets in zone
- [ ] Filter Zones: Apply frequency filtering
- [ ] Distortion Zones: Add distortion effects
- [ ] Delay Zones: Add echo/delay effects

## **Phase 6: Collision Sound System** 💥

### **6.1 Collision Detection**
- [ ] Physics Integration: Detect bullet collisions with objects
- [ ] Collision Properties: Material, velocity, angle
- [ ] Sound Generation: Generate impact sounds based on collision
- [ ] Temporal Effects: Short-duration collision sounds

### **6.2 Material-Based Collision Sounds**
- [ ] Metal: Sharp, metallic sounds
- [ ] Wood: Dull, wooden thuds
- [ ] Glass: Sharp, shattering sounds
- [ ] Fabric: Soft, muffled sounds

## **Phase 7: Tools UI System** 🛠️

### **7.1 Audio Tools UI**
- [ ] Listener Controls: Position, range, drop/undrop
- [ ] Audio Effects: Reverb, filter, distortion controls
- [ ] Zone Painting: Tools for creating effect zones
- [ ] Parameter Adjustment: Real-time parameter tweaking

### **7.2 Physics Tools UI**
- [ ] Force Fields: Wind, gravity, magnetic fields
- [ ] Collision Tools: Bouncy surfaces, sticky surfaces
- [ ] Teleportation: Instant position changes
- [ ] Free Draw: Custom physics shapes

## **Phase 8: Performance Optimization** ⚡

### **8.1 Audio Performance**
- [ ] Object Pooling: Reuse audio objects efficiently
- [ ] LOD System: Reduce audio complexity for distant bullets
- [ ] Buffer Management: Optimize audio buffer sizes
- [ ] Threading: Separate audio processing thread

### **8.2 Visual Performance**
- [ ] Aura Optimization: Efficient aura rendering
- [ ] Zone Rendering: Optimized painted zone display
- [ ] UI Performance: Smooth tool interactions
- [ ] Memory Management: Proper cleanup of audio objects

## **🎯 Immediate Next Steps (This Week)**

### **Step 1: SignalFlow Research & Setup**
1. [ ] Install SignalFlow and create test environment
2. [ ] Study API documentation and create reference guide
3. [ ] Create basic audio test with simple synthesis
4. [ ] Document spatial audio capabilities

### **Step 2: Audio Listener Foundation**
1. [ ] Create `/audio/listener.py` with basic structure
2. [ ] Implement mouse following behavior
3. [ ] Add range visualization (aura)
4. [ ] Create drop-to-grid functionality

### **Step 3: Sound Bullet Audio Integration**
1. [ ] Modify `sound_bullet.py` to use SignalFlow
2. [ ] Implement continuous audio emission
3. [ ] Add spatial positioning based on bullet position
4. [ ] Connect amplitude to hearing range

## **🔧 Technical Architecture**

### **Audio Signal Flow**
```
Sound Bullet → SignalFlow Synthesis → Spatial Audio → Audio Listener → Output
     ↓                ↓                    ↓              ↓           ↓
  Properties → Frequency/Amplitude → 3D Positioning → Range Check → Speakers
```

### **Key Components**
- **`AudioEngine`**: Central SignalFlow audio engine
- **`AudioListener`**: Mouse-following listener with range
- **`SpatialAudio`**: 3D positioning and distance calculation
- **`SoundBullet`**: Enhanced with SignalFlow audio emission
- **`AudioZones`**: Painted effect zones
- **`CollisionAudio`**: Impact sound generation

## **📊 Success Metrics**

- **Audio Latency**: < 10ms response time
- **Spatial Accuracy**: Precise 3D positioning
- **Performance**: 60+ FPS with audio active
- **User Experience**: Intuitive listener controls
- **Audio Quality**: Clear, responsive audio feedback

## **🔗 Related Files**

- **`/audio/listener.py`**: Audio listener system (to be created)
- **`/audio/audio_engine.py`**: Central SignalFlow audio engine (to be created)
- **`/audio/spatial_audio.py`**: 3D audio positioning (to be created)
- **`/systems/sound_bullet.py`**: Enhanced with SignalFlow integration
- **`/ui/audio_tools.py`**: Audio tools UI (to be created)
- **`/tools/audio_effects/`**: Audio effect zones (to be created)

## **📝 Notes**

- SignalFlow replaces PYO for all audio synthesis
- Maintain existing physics integration with Pymunk
- Preserve current UI architecture while adding audio tools
- Focus on performance optimization for real-time audio
- Ensure cross-platform compatibility

---

**Last Updated**: September 2025  
**Status**: In Development  
**Priority**: High - Core Game Mechanic
