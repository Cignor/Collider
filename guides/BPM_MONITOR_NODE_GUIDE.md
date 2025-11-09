# BPM Monitor Node Diagnostic Guide

## Overview

This document explains the role of the always-on `BPM Monitor` node, documents its current behaviour, highlights the gaps between design intent and implementation, and lays out a concrete recovery plan. The goal is to give an external JUCE/C++ expert enough context to fix the node without having to audit the full codebase.

---

## Intended Responsibilities

- Track **every rhythm-capable module** via `ModuleProcessor::getRhythmInfo()` and surface its live BPM, active state, and sync status.
- Provide a **fallback beat detector** (gate/CV tap tempo) for external or legacy sources that do not publish rhythm metadata.
- Expose **stable dynamic outputs** (Raw BPM, normalized CV, activity/confidence) so other modules can lock to any source without rewiring after graph changes.
- Stream **low-latency telemetry** to the UI for tooltips and list displays.
- Stay lightweight: scans and allocations must avoid the real-time audio thread hot path.

---

## Current Implementation Snapshot

Key logic lives in `juce/Source/audio/modules/BPMMonitorModuleProcessor.cpp`, with the process loop shown below:

```148:228:juce/Source/audio/modules/BPMMonitorModuleProcessor.cpp
    if (++m_scanCounter % 128 == 0)
    {
        // ... existing code ...
        scanGraphForRhythmSources();
    }

    if (mode == (int)OperationMode::Auto || mode == (int)OperationMode::DetectionOnly)
        processDetection(buffer);
    else
        m_detectedSources.clear();

    buffer.clear();
    int channelIndex = 0;

    // Introspected sources
    for (const auto& source : introspected)
    {
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), source.bpm, numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), normalizeBPM(source.bpm, minBPM, maxBPM), numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), source.isActive ? 1.0f : 0.0f, numSamples);
    }

    // Detected sources
    for (const auto& source : detected)
    {
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), source.detectedBPM, numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), normalizeBPM(source.detectedBPM, minBPM, maxBPM), numSamples);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), source.confidence, numSamples);
    }
```

The node allocates 96 output channels (32 sources × 3 metrics) and resynthesizes the dynamic pin list on every call to `getDynamicOutputPins()`.

---

## What It Is Doing Today

- **Introspection cadence** – `scanGraphForRhythmSources()` only runs every 128 audio blocks. With a typical 512-sample buffer at 44.1 kHz this is ~1.5 s latency (not the 2.9 ms comment claims), so new/removed rhythm sources take visible seconds to appear/disappear.
- **Beat detection** – `TapTempo` analyzers reset after a 3 s gap and require ≥3 consistent taps before publishing confidence > 0.3. Detection works, but the results are cleared and channels are reclaimed the instant the analyzer drops below the confidence threshold.
- **Dynamic outputs** – Channel indices are assigned sequentially per frame. When a source vanishes, later sources shift down; when a new source appears it reuses the lowest free channel. Any existing connections built for the previous occupant of that channel will now see the wrong BPM stream.
- **Data columns** – Introspected outputs provide Raw BPM / normalized CV / active gate; detected outputs provide Raw BPM / normalized CV / confidence. The `isSynced` flag from `RhythmInfo` is never published, so consumers cannot distinguish tempo-locked sources from free-running ones.
- **Normalization** – `normalizeBPM()` uses `juce::jmap` without clamping, so values outside the `[minBPM, maxBPM]` window will generate CV values <0 or >1.0, which is dangerous for downstream modulation targets.
- **UI feedback** – The Preset Creator UI lists sources but does not surface sync status, confidence thresholds, or channel indices, making it hard to correlate patch cables to list entries.

---

## Gaps vs. Expectations

- **Latency gap**: We expect near-instant discovery of new rhythm sources; current scanning delays exceed a second.
- **Channel stability gap**: Outputs should be stable per source identity; today they are positional and can silently reroute connections.
- **Metadata gap**: Downstream consumers need sync state and confidence gating but the node only exposes active/confidence as raw floats.
- **Safety gap**: CV values should be bounded; current mapping can push modules outside their modulation envelope.
- **Observability gap**: There is no instrumentation to debug which sources are mapped to which channels at runtime.

---

## Fix Strategy

1. **Stabilize channel assignments**
   - Introduce a persistent map keyed by `sourceKey` (`logicalId` for introspected modules, `inputChannel` for detectors) to reserve channel triplets.
   - Reuse existing channels when the same key reappears; zero-fill channels when the source is inactive but keep them allocated until the user disconnects cables.
   - Update `getDynamicOutputPins()` to read the stable map instead of rebuilding from scratch.

2. **Make graph scanning event-driven**
   - Expose a lightweight `markRhythmGraphDirty()` hook from `ModularSynthProcessor` (triggered on node add/remove) to flip an atomic flag inside the BPM monitor.
   - In `processBlock`, rescan only when the flag is set (and perhaps at a slower watchdog interval as a fallback). This removes the 1.5 s polling delay.

3. **Clamp and annotate outputs**
   - Replace `normalizeBPM()` with a clamped version and add guard rails for NaN/INF inputs.
   - Repurpose the third channel for introspected sources to encode both activity and sync (e.g., 0 = stopped, 0.5 = free-running, 1 = synced and active) or add a fourth channel when headroom allows.
   - For detected sources, gate outputs by confidence threshold to avoid jittering data when detection is noisy.

4. **Improve UI and telemetry**
   - Publish channel metadata via `setLiveParamValue()` (e.g., `source_key_n`, `source_state_n`) so the node UI can label which physical pin corresponds to each list entry.
   - Display sync/confidence states in `drawParametersInNode()` and add warnings when outputs are clamped or confidence is low.

5. **Add diagnostics and tests**
   - Implement `getConnectionDiagnostics()` override to print the stable channel table for debugging.
   - Add unit-style tests (or scripted scenarios) that add/remove rhythm modules rapidly and verify channel IDs remain consistent.
   - Verify detection accuracy with synthetic gate patterns at the edges of `detMinBPM`/`detMaxBPM` and ensure CV outputs stay within [0,1].

---

## Verification Checklist

- [ ] Connect a `StepSequencer` (sync on/off) and confirm BPM, sync state, and channel indices remain stable through repeated creation/deletion.
- [ ] Feed an external clock (MIDI gate or sample loop) into detection inputs; ensure confidence gating keeps outputs quiet until detection stabilises.
- [ ] Stress-test by toggling `numInputs` and module additions while cables remain connected—no rerouting or stale data should occur.
- [ ] Validate UI telemetry shows the same ordering as the physical pins and that min/max BPM clamps take effect.

---

## Related Files

- `juce/Source/audio/modules/BPMMonitorModuleProcessor.h/.cpp`
- `juce/Source/audio/modules/ModuleProcessor.h` (`RhythmInfo`, telemetry helpers)
- `juce/Source/audio/modules/TapTempo.h/.cpp`
- `juce/Source/audio/graph/ModularSynthProcessor.cpp` (`getModulesInfo`, graph notifications)

---

By addressing the stability, latency, and observability gaps outlined above, the BPM Monitor will become a reliable patching hub for tempo-aware modules and external rhythm sources.

