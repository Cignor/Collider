# Music Tracker Node - Conceptualization

## 🎵 Vision: 90s Tracker + Modern Modular Twist

A pattern-based music tracker node that combines the classic vertical timeline workflow of 90s trackers (FastTracker II, Impulse Tracker) with modern modular synthesis capabilities.

---

## 📐 Core Concept

### Classic Tracker Elements:
- **Vertical Pattern Editor**: Time flows downward (rows = ticks/steps)
- **Multi-Channel Pattern Grid**: Each column = one instrument/track
- **Pattern-Based Sequencing**: Build patterns, arrange them into songs
- **Sample-Based Instruments**: Load samples, assign to tracks
- **Effect Commands**: Per-step effects (volume slides, pitch slides, etc.)

### Modern Modular Twist:
- **CV Integration**: Each track can output CV/Gate instead of just audio
- **Modular Routing**: Connect tracker outputs to any module (VCOs, filters, etc.)
- **Real-Time Modulation**: CV inputs can modulate pattern parameters
- **Hybrid Mode**: Mix sample playback with CV generation
- **Pattern Modulation**: CV can control pattern selection, speed, etc.

---

## 🏗️ Architecture Overview

### Data Structure:
```
TrackerNode
├── Pattern Bank (up to 64 patterns)
│   └── Pattern (64 rows × 8 channels)
│       └── Cell (Note, Instrument, Volume, Effect, Effect Value)
├── Song Sequence (pattern order list)
├── Instrument Bank (up to 32 instruments)
│   └── Instrument (Sample reference + settings)
└── Playback State (current pattern, row, tick)
```

### Key Components:

1. **Pattern Editor**
   - Grid: 64 rows × 8 channels
   - Each cell: Note (C-0 to B-9), Instrument (00-31), Volume (00-64), Effect (00-FF), Effect Value (00-FF)
   - Pattern length: Variable (1-64 rows)
   - Pattern speed: Ticks per row (1-32)

2. **Instrument System**
   - Each instrument references a sample (from SampleLoader or internal bank)
   - Per-instrument settings: Volume, Panning, Fine Tune, ADSR
   - Can also be "CV Instrument" - outputs CV/Gate instead of audio

3. **Effect System**
   - Classic tracker effects (volume slide, pitch slide, vibrato, etc.)
   - Modern effects (filter cutoff, resonance, delay send, etc.)
   - Per-step automation

4. **Song Sequencer**
   - Pattern order list (up to 256 patterns)
   - Loop points
   - Jump commands

---

## 🎹 UI Design Concepts

### Main View: Pattern Editor
```
┌─────────────────────────────────────────────────────────┐
│ Pattern: 01  Speed: 06  Tempo: 125 BPM                  │
├──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┤
│ Row │ Ch01 │ Ch02 │ Ch03 │ Ch04 │ Ch05 │ Ch06 │ Ch07 │
├──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┤
│ 00   │ C-3 01 40 │ ... │ ... │ ... │ ... │ ... │ ... │
│ 01   │ --- -- -- │ ... │ ... │ ... │ ... │ ... │ ... │
│ 02   │ D-3 01 40 │ ... │ ... │ ... │ ... │ ... │ ... │
│ 03   │ --- -- -- │ ... │ ... │ ... │ ... │ ... │ ... │
│ ...  │ ... │ ... │ ... │ ... │ ... │ ... │ ... │ ... │
│ 63   │ ... │ ... │ ... │ ... │ ... │ ... │ ... │ ... │
└──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
```

### Cell Format (per 90s tracker style):
- **Note**: C-0 to B-9 (or --- for no note)
- **Instrument**: 00-31 (instrument number)
- **Volume**: 00-64 (volume level)
- **Effect**: 00-FF (effect command)
- **Effect Value**: 00-FF (effect parameter)

### Alternative: Modern Compact View
- Smaller cells, more channels visible
- Color-coded by instrument type (sample vs CV)
- Visual waveform preview in cells

---

## 🎛️ Parameter Design

### Global Parameters:
- **Tempo** (BPM): 60-300
- **Pattern Speed** (ticks per row): 1-32
- **Current Pattern**: 0-63
- **Play Mode**: Pattern Loop / Song Play / Single Shot
- **Master Volume**: 0.0-1.0

### Per-Channel Parameters:
- **Channel Type**: Audio / CV / Gate / Hybrid
- **Volume**: 0.0-1.0
- **Panning**: -1.0 to 1.0
- **Mute/Solo**: Boolean

### Per-Instrument Parameters:
- **Sample File**: Reference to sample
- **Volume**: 0.0-1.0
- **Fine Tune**: -100 to +100 cents
- **ADSR Envelope**: Attack, Decay, Sustain, Release
- **Loop Mode**: Off / Forward / Ping-Pong
- **CV Output Mode**: If instrument is CV type

### Pattern Parameters:
- **Pattern Length**: 1-64 rows
- **Pattern Speed**: 1-32 ticks/row
- **Pattern Name**: String

---

## 🔌 I/O Design

### Inputs:
- **Clock In**: External clock sync (optional)
- **Reset**: Reset to pattern 0, row 0
- **Tempo Mod**: CV modulation of tempo
- **Pattern Select Mod**: CV to select pattern
- **Speed Mod**: CV to modulate pattern speed
- **Per-Channel CV Mods**: Volume, Pan, etc.

### Outputs:
- **Audio Out L/R**: Master audio output
- **Per-Channel Audio**: Individual channel outputs (8 channels)
- **Per-Channel CV**: If channel is CV type
- **Per-Channel Gate**: Gate outputs for CV channels
- **Pattern Position CV**: Current row position (0-1)
- **Pattern Index CV**: Current pattern number (0-63)
- **Tick CV**: Current tick within row (0-1)

---

## 🎨 Modern Features

### 1. **Hybrid Channels**
- Channel can be Audio, CV, or both
- Audio channels: Play samples
- CV channels: Output pitch CV + Gate
- Hybrid: Play sample AND output CV

### 2. **CV Pattern Recording**
- Record CV sequences into patterns
- Use any CV source as input
- Quantize to grid

### 3. **Pattern Modulation**
- CV can modulate pattern selection
- CV can modulate pattern speed
- CV can modulate tempo
- Real-time pattern switching

### 4. **Sample Integration**
- Reference samples from SampleLoader nodes
- Or load samples directly into tracker
- Sample preview in UI
- Sample slicing/trimming

### 5. **Effect Commands (Classic + Modern)**
- **00**: Arpeggio (classic)
- **01**: Portamento Up
- **02**: Portamento Down
- **03**: Tone Portamento
- **04**: Vibrato
- **05**: Volume Slide + Portamento
- **06**: Volume Slide + Vibrato
- **07**: Tremolo
- **08**: Set Panning
- **09**: Sample Offset
- **0A**: Volume Slide
- **0B**: Jump to Pattern
- **0C**: Set Volume
- **0D**: Pattern Break
- **0E**: Extended Effects
  - **E0**: Set Filter Cutoff
  - **E1**: Fine Portamento Up
  - **E2**: Fine Portamento Down
  - **E3**: Glissando
  - **E4**: Vibrato Waveform
  - **E5**: Set Finetune
  - **E6**: Pattern Loop
  - **E7**: Tremolo Waveform
  - **E8**: Set Panning (fine)
  - **E9**: Retrigger Note
  - **EA**: Fine Volume Slide Up
  - **EB**: Fine Volume Slide Down
  - **EC**: Note Cut
  - **ED**: Note Delay
  - **EE**: Pattern Delay
  - **EF**: Invert Loop

### 6. **Modern Effects**
- **F0-F9**: Filter Cutoff modulation
- **FA-FB**: Resonance modulation
- **FC-FD**: Delay Send
- **FE-FF**: Reverb Send

---

## 💾 State Management

### What Needs Saving:
- All patterns (64 patterns × 8 channels × 64 rows)
- Song sequence (pattern order list)
- All instruments (32 instruments × settings)
- Global settings (tempo, speed, etc.)
- Sample references (file paths or node IDs)

### Storage Strategy:
- Patterns: Store in APVTS as serialized strings (compact format)
- Instruments: Store in APVTS
- Samples: Reference by file path or node ID
- Song: Store in APVTS

### File Format:
- Use APVTS XML for state
- Consider MOD/XM import/export (future feature)

---

## 🎯 Implementation Questions

### 1. **Sample Management**
- **Q**: Should samples be loaded into the tracker, or reference external SampleLoader nodes?
- **A**: Both? Internal sample bank for classic tracker feel, but also allow referencing external nodes for modular flexibility.

### 2. **Pattern Size**
- **Q**: How many patterns? How many channels? How many rows?
- **A**: Start with 64 patterns, 8 channels, 64 rows (classic). Make configurable later.

### 3. **Playback Engine**
- **Q**: Real-time sample playback or pre-rendered?
- **A**: Real-time for modular integration. Use existing SampleVoiceProcessor architecture.

### 4. **UI Complexity**
- **Q**: Full pattern editor in node, or separate window?
- **A**: Start with compact view in node, consider separate window for full editor later.

### 5. **CV Integration**
- **Q**: How deep should CV integration be?
- **A**: Deep! Every parameter should be CV-modulatable. Patterns can be CV sequences.

### 6. **Effect System**
- **Q**: Implement all classic effects or subset?
- **A**: Start with essential effects (volume slide, portamento, vibrato), add more iteratively.

### 7. **Performance**
- **Q**: How many simultaneous voices?
- **A**: 8 channels × polyphony per channel. Use voice stealing if needed.

---

## 🚀 Phased Implementation Plan

### Phase 1: Core Pattern Playback
- Pattern storage (64 rows × 8 channels)
- Basic note playback (C-0 to B-9)
- Instrument system (sample references)
- Simple pattern sequencer
- Basic UI (compact pattern view)

### Phase 2: Classic Effects
- Volume slide (0A)
- Portamento (01, 02, 03)
- Vibrato (04)
- Set Volume (0C)
- Pattern Break (0D)

### Phase 3: CV Integration
- CV channel type
- CV pattern recording
- CV outputs per channel
- Pattern modulation via CV

### Phase 4: Advanced Features
- Extended effects (E0-EF)
- Song sequencer
- Pattern loop points
- Sample slicing

### Phase 5: Polish
- MOD/XM import
- Pattern visualization
- Performance optimization
- Documentation

---

## 🎨 UI Mockup Ideas

### Compact Node View:
```
┌─────────────────────────────────────┐
│ 🎵 Music Tracker                    │
├─────────────────────────────────────┤
│ Pattern: [01]  Speed: [06]  BPM:125 │
│ ┌─────────────────────────────────┐ │
│ │ Row │ Ch1 │ Ch2 │ Ch3 │ Ch4 │...│ │
│ │ 00  │ C-3 │ --- │ D-4 │ --- │...│ │
│ │ 01  │ --- │ E-3 │ --- │ F-4 │...│ │
│ │ ... │ ... │ ... │ ... │ ... │...│ │
│ └─────────────────────────────────┘ │
│ [▶ Play] [⏸ Stop] [⏹ Reset]        │
│ Instruments: [1] [2] [3] ...        │
└─────────────────────────────────────┘
```

### Expanded View (when node is large):
- Full pattern grid visible
- Instrument list with sample previews
- Effect command reference
- Pattern order list

---

## 🔗 Integration with Existing Modules

### Can Connect To:
- **SampleLoader**: Reference samples from other nodes
- **VCO/PolyVCO**: Use CV outputs to control oscillators
- **Filters**: Use CV outputs for filter cutoff
- **Effects**: Route audio outputs to effects
- **Mixers**: Route channels to mixer inputs
- **TempoClock**: Sync to global tempo

### Can Be Controlled By:
- **TempoClock**: Master tempo sync
- **LFO**: Modulate pattern speed, tempo
- **Envelopes**: Modulate channel volume
- **CV Sources**: Pattern selection, speed, etc.

---

## 📚 Reference Implementation Ideas

### From Existing Codebase:
- **StepSequencer**: Pattern-based sequencing logic
- **MultiSequencer**: Multi-channel output pattern
- **SampleLoader**: Sample playback engine
- **SnapshotSequencer**: Pattern storage/recall

### From Classic Trackers:
- **FastTracker II**: Effect command set
- **Impulse Tracker**: Extended effects (E0-EF)
- **Renoise**: Modern UI patterns

---

## ❓ Open Questions for Discussion

1. **Pattern Editor UI**: 
   - Full editor in node (cramped but integrated)?
   - Separate floating window (better UX but disconnected)?
   - Hybrid: Compact view in node, expand to window on double-click?

2. **Sample Management**:
   - Internal sample bank (classic tracker feel)?
   - Reference external SampleLoader nodes (modular)?
   - Both (flexibility)?

3. **CV Depth**:
   - Just pattern selection and tempo?
   - Or full CV pattern recording/playback?
   - Per-step CV automation?

4. **Effect Complexity**:
   - Start simple (volume, portamento, vibrato)?
   - Or full effect set from day one?
   - Which effects are most important?

5. **Performance Targets**:
   - How many simultaneous voices?
   - Real-time or pre-rendered?
   - CPU budget?

6. **File Format**:
   - Custom format only?
   - MOD/XM import/export?
   - MIDI export?

7. **Polyphony**:
   - Monophonic per channel (classic)?
   - Polyphonic per channel (modern)?
   - Configurable?

---

## 🎯 Next Steps

1. **Decide on core feature set** (Phase 1 scope)
2. **Design data structures** (Pattern, Cell, Instrument)
3. **Prototype pattern playback** (proof of concept)
4. **Design UI mockup** (get visual feedback)
5. **Plan state management** (APVTS structure)
6. **Estimate complexity** (development time)

---

## 🔧 Technical Implementation Details

### Based on Existing Codebase Patterns:

#### 1. **ModuleProcessor Inheritance**
- Inherit from `ModuleProcessor` (like `StepSequencerModuleProcessor`)
- Implement `processBlock()`, `prepareToPlay()`, `setTimingInfo()`
- Use `AudioProcessorValueTreeState` (APVTS) for all parameters
- Support `getParamRouting()` for CV modulation inputs

#### 2. **Transport Sync**
- Use `setTimingInfo(const TransportState& state)` to receive transport commands
- Track `TransportCommand::Play`, `Pause`, `Stop` separately (don't reset on pause!)
- Store current pattern/row/tick position for resume after pause
- Only reset to pattern 0, row 0 on `TransportCommand::Stop`
- Reference: `PLAN/sample_pause_rollout.md` for pause-robustness patterns

#### 3. **Pattern Storage in APVTS**
- **Challenge**: 64 patterns × 8 channels × 64 rows = 32,768 cells
- **Solution**: Serialize patterns as compact strings
  - Format: `"C-3,01,40,0A,10|D-4,02,50,04,08|..."` (pipe-separated rows, comma-separated fields)
  - Store in APVTS as `"pattern_00"`, `"pattern_01"`, etc.
  - Parse on load, serialize on save
- **Alternative**: Use `getExtraStateTree()` / `setExtraStateTree()` for complex data

#### 4. **Sample Playback Architecture**
- Reuse `SampleVoiceProcessor` from `SampleLoaderModuleProcessor`
- Each channel maintains a voice pool (polyphony per channel)
- Use voice stealing when polyphony exceeded
- Support sample references:
  - **Internal**: Load samples into tracker's sample bank
  - **External**: Reference `SampleLoader` node IDs (future: node connection system)

#### 5. **Real-Time Pattern Playback**
- Maintain tick counter (0 to `patternSpeed - 1`)
- On tick 0: Process row (trigger notes, apply effects)
- On tick > 0: Continue effect processing (volume slides, vibrato, etc.)
- Calculate samples per tick: `samplesPerTick = (sampleRate * 60.0) / (tempo * patternSpeed)`
- Use phase accumulator for sub-sample accuracy

#### 6. **Effect Processing**
- Each channel maintains effect state:
  ```cpp
  struct ChannelEffectState {
      float volumeSlide = 0.0f;      // 0A effect
      float portamentoTarget = 0.0f; // 03 effect
      float portamentoSpeed = 0.0f;
      float vibratoDepth = 0.0f;     // 04 effect
      float vibratoSpeed = 0.0f;
      float vibratoPhase = 0.0f;
      // ... more effects
  };
  ```
- Process effects every sample (for smooth slides)
- Reset effect state on new note trigger

#### 7. **CV Output Generation**
- For CV channels: Output pitch CV (0.0-1.0 mapped to note range)
- Gate output: High on note trigger, low on note off
- Use `juce::AudioBuffer<float>` for CV outputs
- Support per-channel CV outputs (8 channels × 2 outputs = 16 CV outputs)

#### 8. **Thread Safety**
- Pattern data: Read-only during playback (safe from audio thread)
- Pattern editing: Use lock-free queues or message thread only
- Sample loading: Background thread (like `PoseEstimatorModule`)
- State updates: Use `std::atomic` for playback position

#### 9. **UI Rendering (ImGui)**
- Pattern grid: Use `ImGui::Table` or custom drawing
- Cell editing: Click to edit, keyboard navigation
- Color coding: Different colors for note types, instruments
- Scroll view: Virtual scrolling for large patterns
- Reference: `StepSequencerModuleProcessor::drawParametersInNode()`

#### 10. **I/O Bus Configuration**
```cpp
BusesProperties()
    .withInput("CV In", juce::AudioChannelSet::discreteChannels(8), true)  // Clock, Reset, Tempo Mod, Pattern Mod, Speed Mod, etc.
    .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)         // Master L/R
    .withOutput("Channel Out", juce::AudioChannelSet::discreteChannels(8), true)  // Per-channel audio
    .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(16), true)     // Per-channel CV (8 channels × 2 outputs)
```

#### 11. **Modulation Support (Modulation Trinity)**
- **Tempo Mod**: `paramIdTempoMod` → CV input channel 2
- **Pattern Select Mod**: `paramIdPatternMod` → CV input channel 3
- **Speed Mod**: `paramIdSpeedMod` → CV input channel 4
- Implement `getParamRouting()` for all modulation inputs
- Read CV in `processBlock()` before clearing buffer (buffer aliasing safety!)
- Update live telemetry with `setLiveParamValue()`

#### 12. **State Persistence**
- Patterns: Store in APVTS (serialized strings)
- Instruments: Store in APVTS (sample paths, settings)
- Song sequence: Store in APVTS (pattern order list)
- Playback state: Don't persist (reset on load)
- Use `getStateInformation()` / `setStateInformation()` for binary data (samples?)

---

## 🎯 Key Design Decisions Needed

### Decision 1: Pattern Editor UI Location
**Options:**
- **A) In-Node Compact View**: Shows 8-16 rows, scrollable, cramped but integrated
- **B) Separate Window**: Full editor, better UX, but disconnected from node graph
- **C) Hybrid**: Compact view in node, double-click opens full editor window

**Recommendation**: Start with **A**, add **C** later if needed.

### Decision 2: Sample Management Strategy
**Options:**
- **A) Internal Only**: Tracker has its own sample bank (classic tracker feel)
- **B) External References**: Reference `SampleLoader` nodes (modular, but complex)
- **C) Both**: Internal bank + ability to reference external nodes

**Recommendation**: Start with **A**, add **B** as future feature.

### Decision 3: Polyphony Model
**Options:**
- **A) Monophonic Per Channel**: Classic tracker (one note at a time per channel)
- **B) Polyphonic Per Channel**: Modern (multiple notes per channel)
- **C) Configurable**: User chooses per channel

**Recommendation**: Start with **A** (simpler), add **B** later.

### Decision 4: Effect Set Scope
**Options:**
- **A) Minimal**: Volume slide, portamento, vibrato only (Phase 1)
- **B) Classic Set**: All FastTracker II effects (Phase 2)
- **C) Extended Set**: Impulse Tracker extended effects (Phase 3+)

**Recommendation**: **A** for MVP, iterate to **B** and **C**.

### Decision 5: CV Integration Depth
**Options:**
- **A) Basic**: Pattern selection, tempo, speed only
- **B) Medium**: + CV pattern recording, per-channel CV outputs
- **C) Deep**: + Per-step CV automation, CV effect modulation

**Recommendation**: **A** for MVP, expand to **B** and **C** iteratively.

---

## 📊 Complexity Estimates

### Phase 1 (Core Pattern Playback):
- **Pattern Storage**: ⭐⭐ (APVTS serialization)
- **Pattern Playback**: ⭐⭐⭐ (tick-based timing, row processing)
- **Note Triggering**: ⭐⭐ (sample voice management)
- **Basic UI**: ⭐⭐⭐ (ImGui table/grid)
- **Total**: ~2-3 weeks

### Phase 2 (Classic Effects):
- **Effect System**: ⭐⭐⭐⭐ (multiple effects, state management)
- **Effect Processing**: ⭐⭐⭐ (per-sample updates)
- **Total**: ~1-2 weeks

### Phase 3 (CV Integration):
- **CV Outputs**: ⭐⭐ (straightforward)
- **CV Pattern Recording**: ⭐⭐⭐ (UI + state management)
- **Pattern Modulation**: ⭐⭐ (CV input handling)
- **Total**: ~1 week

### Phase 4 (Advanced Features):
- **Extended Effects**: ⭐⭐⭐⭐ (complex effect logic)
- **Song Sequencer**: ⭐⭐⭐ (pattern order management)
- **Total**: ~2 weeks

### Phase 5 (Polish):
- **MOD/XM Import**: ⭐⭐⭐⭐⭐ (file format parsing)
- **Performance**: ⭐⭐⭐ (optimization)
- **Total**: ~1-2 weeks

**Total Estimated Time**: 7-10 weeks for full implementation

---

*This is a living document - update as we refine the concept!*

