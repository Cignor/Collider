# 🥁 MIDI Pads - Quick Start Guide

**Module Name**: `MIDI Pads`  
**Status**: ✅ Fully Implemented  
**Date**: 2025-10-25

---

## 🎯 What Is It?

The **MIDI Pads** module converts pad controller MIDI input into 16 independent gate/velocity CV outputs. Perfect for:
- Drum triggering
- Sample launching
- Rhythmic modulation
- Step sequencer input
- Live performance control

---

## 🚀 Quick Start

### 1. Add the Module

In the node editor or command line:
```
> midi pads
```
or
```
> midipads
```

### 2. Connect Your Pad Controller

The module automatically listens to all MIDI devices by default. If you have multiple MIDI controllers, use the **Channel** selector to filter specific MIDI channels.

### 3. Configure Note Mapping

**Start Note** (default: C1/#36):
- Defines which MIDI note corresponds to Pad 1
- Common values:
  - 36 (C1) - Standard drum pads
  - 60 (C4) - Keyboard middle C
  - 0-127 - Any MIDI note

**Layout** (default: Chromatic):
- **Chromatic**: Pads 1-16 = consecutive semitones
- **Row Octaves**: Each row +1 octave (same notes, different octaves)

### 4. Choose Trigger Mode

**Trigger** (default):
- Brief pulse on each hit
- Duration controlled by "Trigger Length" (1-100ms)
- ✅ Best for drum samples, one-shots

**Gate**:
- Gate high while pad held
- Gate low on note-off
- ✅ Best for sustained notes, envelopes

**Toggle**:
- Each hit toggles gate on/off
- ✅ Best for switches, step sequencer input

**Latch**:
- Gate high until another pad hit
- ✅ Best for sample selection, exclusive modes

### 5. Set Velocity Curve

**Linear** (default): 1:1 mapping  
**Exponential**: More dynamic, emphasizes loud hits  
**Logarithmic**: Compressed, evens out soft/loud  
**Fixed**: Ignores velocity, all hits = 1.0  

---

## 🔌 Outputs (33 Total)

### Channels 0-15: Pad Gates
- `Pad 1 Gate` ... `Pad 16 Gate`
- 0V = off, 1V = on
- Connect to sample trigger inputs

### Channels 16-31: Pad Velocities
- `Pad 1 Vel` ... `Pad 16 Vel`
- 0-1 range (normalized)
- Connect to sample volume, filter cutoff, etc.

### Channel 32: Global Velocity
- Last hit pad's velocity
- Useful for simple single-output routing

---

## 📝 Common Patch Examples

### Example 1: Simple Drum Kit (3 sounds)

```
MIDI Pads
├─ Pad 1 Gate → Sample Loader (kick.wav) → Mixer Ch 1
├─ Pad 2 Gate → Sample Loader (snare.wav) → Mixer Ch 2
└─ Pad 3 Gate → Sample Loader (hihat.wav) → Mixer Ch 3
                                             ↓
                                          Output
```

### Example 2: Velocity-Sensitive Drums

```
MIDI Pads
├─ Pad 1 Gate → Sample Loader Gate In
├─ Pad 1 Vel → Sample Loader Volume CV
└─ Sample Loader → Output
```

### Example 3: Rhythmic Filter Modulation

```
MIDI Pads
├─ Pad 5 Gate → VCO Gate
├─ Pad 5 Vel → VCF Cutoff CV  (velocity controls filter!)
└─ VCO → VCF → Output
```

### Example 4: 16-Pad Sample Grid

```
MIDI Pads (all 16 pads)
├─ Pad 1 Gate → Sample 1 → Mixer Ch 1
├─ Pad 2 Gate → Sample 2 → Mixer Ch 2
├─ ...
└─ Pad 16 Gate → Sample 16 → Mixer Ch 16
                               ↓
                            Output
```

### Example 5: Toggle Sequencer Steps

```
MIDI Pads (Toggle mode)
├─ Pad 1 Gate → Step Sequencer Step 1 Enable
├─ Pad 2 Gate → Step Sequencer Step 2 Enable
├─ ...
└─ Step Sequencer → VCO → Output
```

---

## 🎨 Visual Grid

The node displays a live 4x4 grid showing:
- **Active pads** (pulsing animation)
- **Velocity** (brightness)
- **Color coding** (row colors mode shows kick/snare/hats/perc)

```
┌────┬────┬────┬────┐
│ 1● │ 2  │ 3  │ 4● │  ← Kick drum row (red)
├────┼────┼────┼────┤
│ 5  │ 6● │ 7  │ 8  │  ← Snare row (blue)
├────┼────┼────┼────┤
│ 9  │ 10 │ 11●│ 12 │  ← Hi-hat row (green)
├────┼────┼────┼────┤
│ 13 │ 14 │ 15 │ 16●│  ← Percussion row (yellow)
└────┴────┴────┴────┘
```

**● = currently active (gate high or recently triggered)**

---

## 🎹 Hardware Compatibility

### Tested/Supported Controllers
- Akai MPD series (MPD218, MPD226, MPD232)
- Novation Launchpad (all models)
- Native Instruments Maschine pads
- Arturia BeatStep
- Any MIDI controller sending note-on/off messages

### Setup Tips

1. **Configure your pad controller**:
   - Set pads to send notes (not CCs)
   - Start note: C1 (MIDI 36) recommended
   - Chromatic layout works best

2. **Velocity curve**:
   - If pads feel too sensitive: use **Logarithmic**
   - If pads feel too dull: use **Exponential**
   - For consistent triggering: use **Fixed**

3. **Trigger length**:
   - Very short samples: 5-10ms
   - Normal drums: 10-20ms
   - Long samples: 20-50ms

---

## 🔧 Troubleshooting

### "No pads responding"
✅ Check MIDI device is connected and enabled  
✅ Verify Start Note matches your controller's note range  
✅ Try setting Channel to "All Channels"  

### "Wrong pads triggering"
✅ Adjust Start Note parameter  
✅ Switch between Chromatic/Row Octaves layout  

### "Pads too sensitive/not sensitive enough"
✅ Change Velocity Curve  
✅ Use exponential for more dynamic range  
✅ Use logarithmic to compress dynamics  

### "Gates not releasing"
✅ Check Trigger Mode (should be "Gate" if you want note-off response)  
✅ Trigger mode ignores note-off by design  

### "Multiple pads not working simultaneously"
✅ This is normal polyphonic operation!  
✅ All 16 pads can be active at once  

---

## 📊 Statistics Display

The node shows real-time stats:
- **Active Pads**: How many pads currently gate=high
- **Last Hit**: Which pad was most recently triggered and its velocity

---

## 💡 Pro Tips

1. **Use parallel pins** for compact patching:
   - Gate and Velocity outputs are aligned
   - Easy to route both signals

2. **Velocity as modulation source**:
   - Drive filters (Pad Vel → VCF Cutoff)
   - Control delay mix (Pad Vel → Delay Mix)
   - Modulate reverb (Pad Vel → Reverb Size)

3. **Toggle mode for step sequencer**:
   - Each pad = sequencer step on/off
   - Build patterns by hitting pads
   - Visual feedback in grid

4. **Latch mode for sample selection**:
   - 16 different bass samples
   - Hit pad to select, plays until you select another
   - Great for live performance variation

5. **Global Velocity shortcut**:
   - If you only need one pad at a time
   - Connect Global Vel instead of individual velocities
   - Simpler patching

---

## 🎵 Example Preset

See `Synth_presets/midi_pads_demo.xml` for a complete working example with:
- 3 sample loaders (kick, snare, hi-hat)
- Velocity-sensitive triggering
- Mixed to stereo output

Load it and hit pads 1, 2, 3 on your controller!

---

## 🛠️ Technical Details

### Audio Specifications
- **Channels**: 33 outputs
  - 0-15: Gates (0/1)
  - 16-31: Velocities (0-1)
  - 32: Global velocity (0-1)
- **Sample Rate**: Matches project
- **Latency**: < 1ms (real-time trigger response)
- **Polyphony**: 16 simultaneous pads

### MIDI Processing
- **Thread-safe** atomic operations
- **Zero-copy** MIDI handling
- **Device filtering** via MidiDeviceManager
- **Channel filtering** 1-16 or all

### UI Specifications
- **Grid rendering**: Custom ImDrawList (60fps)
- **Animation**: Pulsing active pads at 8Hz
- **Color modes**: 3 modes (velocity, row colors, fixed)
- **Tooltips**: Complete help on all parameters

---

## 📚 Related Modules

**MIDI Family**:
- `MIDI CV` - Monophonic keyboard → CV
- `MIDI Faders` - CC faders → CV
- `MIDI Knobs` - CC knobs → CV
- `MIDI Buttons` - CC buttons → Gates
- `MIDI Jog Wheel` - Rotary CC → CV
- `MIDI Pads` ← **You are here!**

**Sample Playback**:
- `Sample Loader` - Load and trigger samples
- `Granulator` - Granular sample playback

**Sequencing**:
- `Step Sequencer` - 16-step CV sequencer
- `Multi Sequencer` - 32-voice polyphonic sequencer

---

## 🔮 Future Enhancements (v2.0 Ideas)

- [ ] Choke groups (hi-hat open/closed)
- [ ] Aftertouch support (per-pad pressure)
- [ ] Pattern recording (loop pad hits)
- [ ] 8x8 grid option (64 pads)
- [ ] RGB feedback to hardware
- [ ] Pad banks (4 banks = 64 pads total)

---

**Happy Drumming! 🥁**

*For technical implementation details, see `MIDI_PAD_DESIGN_PROPOSAL.md`*
*For UI design patterns, see `IMGUI_NODE_DESIGN_GUIDE.md`*

