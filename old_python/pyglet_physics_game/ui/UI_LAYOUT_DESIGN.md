# UI Layout Design for 1920x1080 Resolution

## Overview
Complete UI architecture for the physics game with always-on HUD elements and tool system integration.

## Layout Zones

### 1. Top HUD (Always Visible)
```
┌─────────────────────────────────────────────────────────────────────────────────┐
│ Audio Selectors (Left)                    Tool Info (Right)                    │
│ L: [samples/percussion/kick.wav]          Current Tool: [FreeDraw]             │
│ R: [noise/pink_noise]                     Category: [Physics Effects]          │
│                                            Brush Width: 12px                   │
│ Preset Bank: [0][1][2][3][4][5]           Material: Metal                      │
│            [6][7][8][9] [ACTIVE: 3]                                            │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 2. Bottom HUD (Always Visible)
```
┌─────────────────────────────────────────────────────────────────────────────────┐
│ Tool Categories: [Audio Effects] [Physics Effects] [Melody]                    │
│                                                                                 │
│ Tool Options:                                                                   │
│ • Distortion: Drive=0.5, Gain=0.0dB                                            │
│ • Reverb: Room=0.5, Wet=0.3                                                    │
│ • Gravity Well: Strength=1000N, Radius=200px                                   │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 3. Side Panels (Expandable)
```
┌─────────────┐                    Game Area                    ┌─────────────┐
│             │                                                 │             │
│   Tool      │                 1920x1080                       │  Parameter  │
│  Browser    │                Physics Game                     │   Editor    │
│             │                                                 │             │
│ • Audio     │                                                 │ • Sliders   │
│ • Physics   │                                                 │ • Values    │
│ • Melody    │                                                 │ • Presets   │
│             │                                                 │             │
└─────────────┘                                                 └─────────────┘
```

## Tool System Integration

### Audio Effects (Pedalboard API)
- **Distortion**: Drive, Output Gain
- **Reverb**: Room Size, Damping, Wet/Dry Levels
- **Chorus**: Rate, Depth, Centre Delay, Feedback
- **Delay**: Delay Time, Feedback, Mix
- **Compressor**: Threshold, Ratio, Attack, Release
- **Noise Gate**: Threshold, Ratio, Attack, Release

### Physics Effects (Pymunk)
- **Gravity Well**: Strength, Radius, Position
- **Force Field**: Strength, Direction, Width, Height
- **Teleporter**: Entrance/Exit Positions, Radius, Cooldown
- **Spring Launcher**: Position, Radius, Force, Direction
- **Vortex**: Position, Radius, Strength, Direction

### Melody Effects (Custom)
- **Scale Generator**: Root Note, Scale Type, Octave Range
- **Chord Progression**: Root Note, Progression Type, Voicing
- **Rhythm Pattern**: Pattern Type, Tempo, Swing
- **Arpeggiator**: Root Note, Chord Type, Pattern, Octave Range

## Implementation Plan

1. **HUD Components**: Create always-visible status displays
2. **Tool Browser**: Expandable panel for tool selection
3. **Parameter Editor**: Real-time parameter adjustment
4. **Preset System**: Save/load tool configurations
5. **Integration**: Connect tools to game physics and audio systems
