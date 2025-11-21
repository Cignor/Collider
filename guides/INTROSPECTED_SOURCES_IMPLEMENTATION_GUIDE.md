# Introspected Sources Implementation Guide

## Overview

The **BPM Monitor** node automatically discovers rhythm-producing modules in the graph through **introspection**—a mechanism where modules directly report their rhythm metadata via `getRhythmInfo()`. This provides instant, accurate BPM reporting without requiring audio analysis, making it the preferred "fast path" for tempo synchronization.

This guide explains how to correctly implement `getRhythmInfo()` in rhythm-capable modules, covering the data structure, implementation patterns, common pitfalls, and best practices for string handling and transport state access.

---

## What Are Introspected Sources?

**Introspected sources** are modules that implement `getRhythmInfo()` to directly report their rhythm information to the BPM Monitor. This includes:

- **Sequencers** (Step Sequencer, Multi Sequencer, Snapshot Sequencer)
- **Animations** (Animation Module with loaded clips)
- **Physics simulations** (Physics Module with rhythmic motion)
- **Any module** that produces time-based patterns

The BPM Monitor scans the graph periodically and queries each module. If a module returns a valid `RhythmInfo`, it appears in the "Introspected Sources" list and generates dynamic output pins (Raw BPM, normalized CV, active/confidence gate).

**Benefits:**
- **Instant discovery** - No need to wait for audio analysis
- **Accurate BPM** - Direct from the source, not estimated
- **Metadata rich** - Includes sync status, active state, source type
- **Low latency** - No tap tempo delay or confidence thresholds

---

## The RhythmInfo Structure

Located in `juce/Source/audio/modules/ModuleProcessor.h`:

```cpp
struct RhythmInfo
{
    juce::String displayName;    // e.g., "Sequencer #3", "Animation: Walk Cycle"
    float bpm;                    // Current BPM (can be modulated live value)
    bool isActive;                // Is this source currently producing rhythm?
    bool isSynced;                // Is it synced to global transport?
    juce::String sourceType;      // "sequencer", "animation", "physics", etc.
    
    RhythmInfo() : bpm(0.0f), isActive(false), isSynced(false) {}
    RhythmInfo(const juce::String& name, float bpmValue, bool active, bool synced, const juce::String& type = "")
        : displayName(name), bpm(bpmValue), isActive(active), isSynced(synced), sourceType(type) {}
};
```

**Field Requirements:**

1. **`displayName`** - Must be a valid, non-empty UTF-8 string. Used in UI lists and dynamic pin labels. Format: `"Type #ID"` (e.g., `"Sequencer #5"`).

2. **`bpm`** - Must be a finite float value. Use `std::isfinite()` before returning. Return `0.0f` if not applicable or inactive.

3. **`isActive`** - `true` if the module is currently producing rhythm, `false` if stopped/paused.

4. **`isSynced`** - `true` if synced to global transport, `false` if free-running.

5. **`sourceType`** - Short identifier like `"sequencer"`, `"animation"`, `"physics"`. Used for categorization.

---

## Implementation Pattern

### Basic Structure

```cpp
std::optional<RhythmInfo> YourModuleProcessor::getRhythmInfo() const
{
    // 1. Check if this module is rhythm-capable
    if (!isRhythmProducing())
        return std::nullopt; // Not a rhythm source
    
    RhythmInfo info;
    
    // 2. Set display name (MUST include logical ID for uniqueness)
    info.displayName = "YourModule #" + juce::String(getLogicalId());
    info.sourceType = "your_type";
    
    // 3. Determine sync status
    const bool syncEnabled = apvts.getRawParameterValue("sync")->load() > 0.5f;
    info.isSynced = syncEnabled;
    
    // 4. Read LIVE transport state (CRITICAL: not cached copy)
    TransportState transport;
    bool hasTransport = false;
    if (getParent())
    {
        transport = getParent()->getTransportState(); // Live state, not m_currentTransport
        hasTransport = true;
    }
    
    // 5. Determine active state
    if (syncEnabled)
    {
        info.isActive = hasTransport ? transport.isPlaying : false;
    }
    else
    {
        info.isActive = true; // Free-running is always active
    }
    
    // 6. Calculate BPM based on mode
    if (syncEnabled && info.isActive && hasTransport)
    {
        // Synced mode: calculate from transport BPM + division + steps
        // ... (see examples below)
    }
    else if (!syncEnabled)
    {
        // Free-running mode: calculate from internal rate
        // ... (see examples below)
    }
    else
    {
        info.bpm = 0.0f; // Synced but stopped
    }
    
    // 7. Validate BPM before returning
    if (!std::isfinite(info.bpm))
        info.bpm = 0.0f;
    
    return info;
}
```

---

## Critical Implementation Details

### 1. Always Read LIVE Transport State

**❌ WRONG - Using Cached Copy:**
```cpp
// DON'T DO THIS - m_currentTransport is stale!
if (syncEnabled && m_currentTransport.isPlaying)
{
    info.bpm = static_cast<float>(m_currentTransport.bpm * division);
}
```

**✅ CORRECT - Reading Live State:**
```cpp
// Read directly from parent's live transport state
TransportState transport;
bool hasTransport = false;
if (getParent())
{
    transport = getParent()->getTransportState(); // Live copy
    hasTransport = true;
}

if (syncEnabled && hasTransport && transport.isPlaying)
{
    info.bpm = static_cast<float>(transport.bpm * division);
}
```

**Why:** `m_currentTransport` is updated asynchronously in `processBlock()` and can be stale when `getRhythmInfo()` is called from the BPM Monitor's graph scan (which happens on a different thread or at a different time). Reading from `getParent()->getTransportState()` ensures you get the current, live state.

### 2. String Handling and Validation

**❌ WRONG - Direct Assignment Without Validation:**
```cpp
info.displayName = someString; // May be empty or corrupted
```

**✅ CORRECT - Validate and Sanitize:**
```cpp
juce::String rawName = "YourModule #" + juce::String(getLogicalId());
if (rawName.isEmpty())
{
    info.displayName = "Unknown #" + juce::String(getLogicalId());
}
else
{
    // Create a clean copy to avoid corruption
    info.displayName = juce::String(rawName.toUTF8().getAddress());
}
```

**Why:** The BPM Monitor converts strings to UTF-8 for ImGui rendering. Invalid or corrupted strings cause "?????????" characters in the UI. Always ensure:
- Strings are non-empty
- Use `toUTF8().getAddress()` when reconstructing to ensure clean UTF-8 encoding
- Include logical ID in display name for uniqueness

### 3. BPM Calculation Patterns

#### Pattern A: Synced Sequencer (Step Sequencer Example)

```cpp
if (syncEnabled && info.isActive && hasTransport)
{
    // Get division parameter
    int divisionIndex = (int)apvts.getRawParameterValue("rate_division")->load();
    
    // Check for global division override from Tempo Clock
    int globalDiv = transport.globalDivisionIndex.load();
    if (globalDiv >= 0)
        divisionIndex = globalDiv;
    
    // Division multipliers: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8
    static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0, 8.0 };
    const double beatDivision = divisions[juce::jlimit(0, 8, divisionIndex)];
    
    // Effective BPM = transport BPM * division * num_steps
    const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    info.bpm = static_cast<float>(transport.bpm * beatDivision * numSteps);
}
```

**Explanation:** In sync mode, the sequencer's effective BPM depends on:
- Transport BPM (from global tempo)
- Division (how many transport beats per sequencer step)
- Number of steps (full cycle length)

#### Pattern B: Free-Running Sequencer

```cpp
else if (!syncEnabled)
{
    // Free-running mode: convert Hz rate to BPM
    const float rate = rateParam ? rateParam->load() : 2.0f; // steps per second
    const int numSteps = numStepsParam ? (int)numStepsParam->load() : 8;
    
    // One full cycle through all steps = one "beat"
    // BPM = (cycles_per_second) * 60
    // cycles_per_second = rate_hz / num_steps
    info.bpm = (rate / static_cast<float>(numSteps)) * 60.0f;
}
```

**Explanation:** In free-running mode, calculate BPM from the internal step rate (Hz) and number of steps.

#### Pattern C: Animation Module

```cpp
// Animation BPM is based on clip duration
const auto* currentClip = currentAnimator->GetCurrentAnimation();
if (currentClip && currentClip->durationInTicks > 0.0 && currentClip->ticksPerSecond > 0.0)
{
    const double durationSeconds = currentClip->durationInTicks / currentClip->ticksPerSecond;
    info.bpm = static_cast<float>(60.0 / durationSeconds);
    
    // Apply animation speed multiplier
    const float speedMultiplier = currentAnimator->GetAnimationSpeed();
    info.bpm *= speedMultiplier;
}
else
{
    info.bpm = 0.0f; // No animation loaded
}
```

**Explanation:** Animation BPM is calculated from the clip's duration (one loop = one beat).

### 4. Always Validate BPM Values

```cpp
// Before returning, ensure BPM is finite
if (!std::isfinite(info.bpm))
    info.bpm = 0.0f;

return info;
```

**Why:** NaN or infinity values cause crashes in downstream modules and UI rendering. Always validate before returning.

---

## Common Pitfalls and Fixes

### Pitfall 1: Stale Transport State

**Symptom:** BPM Monitor shows incorrect BPM or "IDLE" when sequencer is actually playing.

**Cause:** Reading from `m_currentTransport` instead of live transport state.

**Fix:** Always use `getParent()->getTransportState()` to get a fresh copy.

### Pitfall 2: Garbage Characters in UI

**Symptom:** "?????????" appears in BPM Monitor's "Introspected Sources" list.

**Cause:** Invalid or corrupted string data, or improper UTF-8 conversion.

**Fix:**
1. Ensure `displayName` is always non-empty
2. Reconstruct strings using `juce::String(rawName.toUTF8().getAddress())`
3. Include logical ID in display name: `"Type #" + juce::String(getLogicalId())`

### Pitfall 3: Returning Invalid BPM

**Symptom:** Downstream modules crash or produce NaN values.

**Cause:** Returning NaN or infinity from BPM calculation.

**Fix:** Always validate with `std::isfinite()` and return `0.0f` for invalid values.

### Pitfall 4: Missing Active State Logic

**Symptom:** Module shows as active when transport is stopped.

**Fix:** Properly check transport state:
```cpp
if (syncEnabled)
{
    info.isActive = hasTransport ? transport.isPlaying : false;
}
else
{
    info.isActive = true; // Free-running is always active
}
```

### Pitfall 5: Not Handling Missing Parent

**Symptom:** Crash when `getParent()` returns `nullptr`.

**Fix:** Always check for parent before accessing:
```cpp
TransportState transport;
bool hasTransport = false;
if (getParent())
{
    transport = getParent()->getTransportState();
    hasTransport = true;
}
```

---

## Reference Implementations

### StepSequencerModuleProcessor

**Location:** `juce/Source/audio/modules/StepSequencerModuleProcessor.cpp:942`

**Key Features:**
- Reads live transport state correctly
- Handles sync and free-running modes
- Calculates effective BPM from transport + division + steps
- Validates all values before returning

### MultiSequencerModuleProcessor

**Location:** `juce/Source/audio/modules/MultiSequencerModuleProcessor.cpp:742`

**Key Features:**
- Same pattern as StepSequencer
- Handles multiple sequencer tracks
- Uses logical ID for unique display names

### AnimationModuleProcessor

**Location:** `juce/Source/audio/modules/AnimationModuleProcessor.cpp:1239`

**Key Features:**
- Free-running (not synced)
- Calculates BPM from animation clip duration
- Applies speed multiplier
- Returns `std::nullopt` when no animation is loaded

---

## Testing Checklist

When implementing `getRhythmInfo()` for a new module:

1. **✅ Display Name**
   - [ ] Includes logical ID: `"Type #" + juce::String(getLogicalId())`
   - [ ] Non-empty and valid UTF-8
   - [ ] Appears correctly in BPM Monitor UI (no "?????????")

2. **✅ Transport State**
   - [ ] Reads from `getParent()->getTransportState()` (not cached copy)
   - [ ] Handles missing parent gracefully
   - [ ] Active state reflects transport playing state in sync mode

3. **✅ BPM Calculation**
   - [ ] Synced mode: calculates from transport BPM + division + steps
   - [ ] Free-running mode: calculates from internal rate
   - [ ] Returns `0.0f` when inactive or invalid
   - [ ] Validates with `std::isfinite()` before returning

4. **✅ Sync Status**
   - [ ] `isSynced` reflects parameter state
   - [ ] Active state logic handles both modes correctly

5. **✅ Source Type**
   - [ ] Set to appropriate identifier (`"sequencer"`, `"animation"`, etc.)
   - [ ] Non-empty string

6. **✅ Edge Cases**
   - [ ] Handles transport stopped in sync mode
   - [ ] Handles missing parent
   - [ ] Handles invalid parameters (returns safe defaults)
   - [ ] Returns `std::nullopt` when module is not rhythm-capable

---

## How the BPM Monitor Uses This Data

The BPM Monitor's `scanGraphForRhythmSources()` function:

1. **Iterates through all modules** in the graph
2. **Calls `getRhythmInfo()`** on each module
3. **If valid**, creates an `IntrospectedSource` entry
4. **Validates and sanitizes** the display name to prevent UI corruption
5. **Generates dynamic output pins** for each source:
   - `[Name] BPM` - Raw BPM value
   - `[Name] CV` - Normalized 0-1 for modulation
   - `[Name] Active` - Gate signal (1.0 = active, 0.0 = inactive)

The scan runs periodically (every 128 audio blocks) to discover new sources and update existing ones.

---

## Summary

Implementing `getRhythmInfo()` correctly requires:

1. **Live transport state** - Always read from `getParent()->getTransportState()`, never cached copies
2. **Valid strings** - Ensure display names are non-empty, include logical ID, and use clean UTF-8 encoding
3. **Finite BPM values** - Always validate with `std::isfinite()` and return `0.0f` for invalid values
4. **Proper active state** - Check transport playing state in sync mode, always true in free-running mode
5. **Correct BPM calculation** - Use transport BPM + division + steps for synced sequencers, internal rate for free-running

Following these patterns ensures your module appears correctly in the BPM Monitor and provides accurate, stable rhythm information to the rest of the system.

