## BPM-Synced Speed Integration Guide

### 0. Goal and Mental Model

We want all time-based playback nodes (Sample Loader first, others later) to support **BPM-synced speed** with this intuitive behavior:

- **120 BPM** is the **neutral reference**.
- **Higher BPM** → **faster playback** (shorter bar for the same audio).
- **Lower BPM** → **slower playback** (longer bar for the same audio).
- The **Speed knob** is a **multiplier** around that BPM-based speed:
  - `Speed = 1.0` → follow BPM exactly.
  - `Speed = 2.0` → double-time relative to BPM.
  - `Speed = 0.5` → half-time relative to BPM.

From the point of view of each node, there should be **ONE unified concept**:

- A **speed multiplier** \(s\) where:
  - \(s = 1.0\) → normal,
  - \(s > 1.0\) → faster,
  - \(s < 1.0\) → slower.

Internally, engines may still work with **time ratios** (duration multipliers), but that inversion must be hidden inside wrappers (like `TimePitchProcessor`), never leaked back out to module code.

---

### 1. Current State (Sample Loader as Reference)

#### 1.1 Where BPM Comes From

- Global transport (`TransportState`) carries:
  - `bpm`
  - `songPositionSeconds`
  - `songPositionBeats`
- `ModularSynthProcessor` broadcasts `TransportState` to each module by calling `setTimingInfo(const TransportState& state)`.
- Each module decides how to use `bpm` and the positions.

#### 1.2 Sample Loader Data Flow (Simplified)

- **Module**: `SampleLoaderModuleProcessor`
  - Tracks `currentBpm` (atomic).
  - In `processBlock`, it computes `speedNow`:
    - If synced: `speedNow = (bpm / 120.0f) * baseSpeed;`
    - Else: `speedNow = baseSpeed;`
  - Passes `speedNow` down to the voice:
    - `currentProcessor->setZoneTimeStretchRatio(speedNow);`

- **Voice**: `SampleVoiceProcessor`
  - Has `zoneTimeStretchRatio` (per-voice scalar).
  - Computes `effectiveTime = apTime * zoneTimeStretchRatio;`
  - For the **naive** engine:
    - Uses `step = pitchScale * effectiveTime;` ← now treated as SPEED.
  - For the **RubberBand** engine:
    - Passes `effectiveTime` to `TimePitchProcessor::setTimeStretchRatio`.

- **Wrapper**: `TimePitchProcessor`
  - Exposes `setTimeStretchRatio(double speed)`.
  - Internally:
    - For **RubberBand**:
      - `timeRatio = 1.0 / clampedSpeed;` (RubberBand expects duration ratio).
    - For **Fifo** (naive ST stub):
      - Uses `timeRatio = clampedSpeed;` as a speedish factor.

The key unification: **caller always passes SPEED**, underlying engines convert as needed.

---

### 2. What Was Going Wrong (The Inversion Bugs)

Symptoms observed:

- When synced:
  - **Higher BPM** made playback **slower**.
  - **Lower BPM** made playback **faster**.
  - Pops ("poc") at loop points.

Root causes:

- Treating the BPM-derived factor as **duration ratio** in some places and as **speed** in others.
- Dividing vs multiplying in inconsistent locations:
  - `step = pitchScale / ratio;` vs `step = pitchScale * speed;`
  - `timeRatio = ratio;` vs `timeRatio = 1.0 / speed;`
- Sync code treating every transport loop (bar wrap) as a **desync** and forcing a hard seek, which resets stretching buffers and causes pops.

Fixing Sample Loader gave us a **reference pattern** we can now apply to other nodes.

---

### 3. Reference Pattern: BPM-Synced Speed (Sample Loader)

#### 3.1 Module Level (`SampleLoaderModuleProcessor`)

**State:**

- `std::atomic<double> currentBpm { 120.0 };`
- `std::atomic<bool> syncToTransport { true };`

**In `setTimingInfo`:**

- Update BPM and mirror play state:
  - `currentBpm.store(state.bpm);`
  - `safeProcessor->isPlaying = state.isPlaying;`
- Compute `targetSamplePos` from transport:
  - Absolute mode: seconds-based.
  - Relative mode: loop-local, duration derived from speed:
    - `varispeedSpeed = (bpm / 120.0) * speedKnob;`
    - `loopDurationSec = loopLengthSamples / (sampleRate * varispeedSpeed);`
    - Map `songPositionSeconds` modulo `loopDurationSec` into `[0, 1]` progress.
- Circular sync:
  - Measure **circular difference** between engine cursor and `targetSamplePos` inside `[sSamp, sSamp + lSamp]`.
  - Only `setCurrentPosition` when:
    - transport actually jumped (`delta > 0.1` sec/beats), and
    - circular difference > ~4800 samples (~100ms).
- Always update **UI**:
  - `positionParam->store(targetSamplePos / totalSamples);`

**In `processBlock`:**

- Compute unified speed when synced:
  - `speedNow = baseSpeed;`
  - If `syncToTransport && currentSample && sampleSampleRate > 0`:
    - Read `bpm = currentBpm.load();` (default 120).
    - Clamp `bpm` to a sane minimum (avoid div-by-zero).
    - `speedNow = (float) (bpm / 120.0) * baseSpeed;`
- Pass `speedNow` to the voice:
  - `currentProcessor->setZoneTimeStretchRatio(speedNow);`

#### 3.2 Voice Level (`SampleVoiceProcessor`)

**Key idea:** `zoneTimeStretchRatio` is always interpreted as **speed**, not duration.

**In `renderBlock`:**

- Effective time and pitch:
  - `effectiveTime = jlimit(0.25, 4.0, apTime * zoneTimeStretchRatio);`
  - `effectivePitchSemis = basePitchSemitones + apPitch;`
- Naive engine step (fixed):
  - `step = pitchScale * effectiveTime;`
  - High speed ⇒ large step ⇒ faster playback.
- RubberBand engine:
  - `timePitch.setTimeStretchRatio(effectiveTime);`
  - `timePitch.setPitchSemitones(effectivePitchSemis);`

#### 3.3 Wrapper Level (`TimePitchProcessor`)

**Contract:** `setTimeStretchRatio(double speed)` where:

- `speed = 2.0` → 2× faster (half duration).
- `speed = 0.5` → 2× slower (double duration).

Internal RubberBand mapping:

- `clampedSpeed = jlimit(0.25, 4.0, speed);`
- `timeRatio = 1.0 / clampedSpeed;`
- `stretcher->setTimeRatio(timeRatio);`

Now both engines respond **in the same direction** to speed.

---

### 4. How to Roll This Out to Other Nodes

This is the pattern you should replicate across other modules that need BPM-synced speed:

#### 4.1 Identify Candidate Nodes

Look for modules that:

- Have a notion of **playback rate / time stretch**.
- Already have:
  - a `Speed` or similar parameter,
  - a `sync` or `syncToTransport` flag,
  - a transport-aware `setTimingInfo`.

Likely candidates:

- `SampleSfxModuleProcessor`
- Granular/looping samplers (if any).
- Any other nodes that feed into `TimePitchProcessor` or use `SampleVoiceProcessor`-like engines.

#### 4.2 Add / Reuse BPM State

In each module:

- Add:

```cpp
std::atomic<double> currentBpm { 120.0 };
```

- In `setTimingInfo(const TransportState& state)`:
  - Call `ModuleProcessor::setTimingInfo(state);`
  - If synced: `currentBpm.store(state.bpm);`
  - Mirror `isPlaying` if appropriate (like Video/Sample loader).

#### 4.3 Compute Unified Speed in `processBlock`

Inside `processBlock`:

1. Read the base speed knob:

```cpp
const float baseSpeed = apvts.getRawParameterValue("speed")->load();
float speedNow = baseSpeed;
```

2. If `syncToTransport` is **off**:

- Leave `speedNow = baseSpeed;` (manual control).

3. If `syncToTransport` is **on**:

- Read and clamp BPM:

```cpp
double bpm = currentBpm.load();
if (bpm < 1.0)
    bpm = 120.0;
```

- Apply unified varispeed:

```cpp
const float speedFactor = (float) (bpm / 120.0);
speedNow = speedFactor * baseSpeed;
```

4. Pass `speedNow` to the appropriate engine/voice:

- If using `SampleVoiceProcessor`:
  - `voice->setZoneTimeStretchRatio(speedNow);`
- If directly using `TimePitchProcessor`:
  - `timePitch.setTimeStretchRatio(speedNow);`

**Important:** never invert here. All inversion (if needed) belongs **inside** engine-specific wrappers.

#### 4.4 Update Position Sync (Optional but Recommended)

For nodes that sync position to transport (like Sample Loader and Video Loader):

- Use the same **circular sync + jump detection** pattern:
  - Compute `targetPos` from transport and speed (absolute or loop-relative).
  - Measure circular difference between engine cursor and `targetPos` in loop space.
  - Only hard-seek on:
    - actual transport jumps (bar wrap, user scrubs), **and**
    - circular error > ~100ms.
  - Always update **UI** from `targetPos` for smooth playhead.

This avoids both:

- **Stuttering** (engine constantly reseeking for tiny errors).
- **Pops** at loop boundaries (seek suppressed when engine and transport wrap together).

---

### 5. Strategy for Extending to New Nodes

1. **Audit** the node:
   - Does it already have a `speed`/`rate` knob?
   - Does it already have `sync` or use `setTimingInfo`?
   - Is it using `SampleVoiceProcessor` or a custom stretcher?

2. **Normalize the engine API**:
   - Decide on a single "speed" function:
     - `setSpeed(float speed)` or reuse `setTimeStretchRatio(float speed)`.
   - Ensure engine internals handle inversion:
     - e.g. `timeRatio = 1.0 / speed` inside the time-stretch lib wrapper.

3. **Wire BPM into the module**:
   - Add `currentBpm`.
   - Update it in `setTimingInfo`.

4. **Apply unified speed formula in `processBlock`**:

```cpp
float speedNow = baseSpeed;
if (syncToTransport && hasValidSampleOrClip)
{
    double bpm = currentBpm.load();
    if (bpm < 1.0) bpm = 120.0;
    speedNow = (float) (bpm / 120.0) * baseSpeed;
}
// use speedNow uniformly
```

5. **Test in both modes**:
   - **Naive** engine:
     - Expect pitch + speed changes.
   - **RubberBand** engine:
     - Expect speed changes with pitch preserved.
   - Scenarios:
     - BPM 60 → 120 → 240 with Speed = 1.0.
     - Speed knob sweeps at fixed BPM.
     - Sync on/off toggles mid-play.

---

### 6. Summary

- Every node that wants BPM-synced playback should:
  - Treat its **internal control** as a **speed multiplier**, never a duration ratio.
  - Derive that speed from:
    - `speed = (BPM / 120.0) * SpeedKnob;`
  - Pass that speed down to a **normalized engine API** that hides inversion details.
- Position sync should **use circular distance + jump detection** so loop wraps don't trigger pops or stutters.

Use `SampleLoaderModuleProcessor`, `SampleVoiceProcessor`, and `TimePitchProcessor` as the **reference implementation** when adding BPM-synced speed to other nodes.


