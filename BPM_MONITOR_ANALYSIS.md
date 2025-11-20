# BPM Monitor - Why It Sucks: Full Report

## Executive Summary
The BPM monitor is **over-engineered by ~10x**. What should be ~100 lines of code is currently ~900+ lines with 5+ layers of abstraction, complex state machines, and unnecessary thread-safety overhead.

## Core Problem
**Detecting BPM from a signal is trivial:**
1. Detect rising edges (signal crosses threshold)
2. Measure time between edges
3. When you have 2+ intervals, calculate median BPM = 60 / median_interval
4. Done.

**Current implementation does:**
- Tap tempo algorithm with rolling 8-tap buffer
- Variance calculation for "confidence" metric
- Coefficient of variation calculations
- Exponential smoothing of BPM values
- Hold blocks for "stability" (24 blocks ≈ 0.5s)
- Signal hold blocks (12 blocks)
- Detection cache entries with multiple flags
- Separate visualization vs output snapshots
- Atomic char arrays for thread-safe string storage
- Complex validation and sanitization
- Graph introspection engine
- Debug logging infrastructure

## Specific Issues

### 1. Over-Complex Detection Algorithm
**Current:** TapTempo class with:
- Rolling buffer of 8 intervals
- Variance/standard deviation calculations
- Confidence metrics based on coefficient of variation
- Timeout logic (3 second reset)
- Multiple state flags

**Needed:** 
- Store last N intervals (N=2-4 is enough)
- Calculate median
- Done.

### 2. Unnecessary State Management
**Current:**
- `DetectionCacheEntry` with `bpmHold`, `signalHold`, `smoothedBPM`, `hasDetection`, `lastRms`, `lastEdgeCount`
- Exponential smoothing: `smoothedBpm += alpha * (newBpm - smoothedBpm)`
- Hold blocks to prevent flickering

**Needed:**
- Just store: last edge time, interval list, current BPM
- No smoothing needed if using median (already stable)
- No hold blocks needed

### 3. Thread Safety Overhead
**Current:**
- `VizSourceEntry` with `std::array<std::atomic<char>, 48>` for names
- Atomic floats for primary/secondary
- Atomic ints for flags
- Complex `createSnapshot()` that validates UTF-8, checks patterns, etc.

**Needed:**
- Simple struct with regular members
- Read once per frame in UI thread (no contention)
- No atomic overhead needed

### 4. Dual Snapshot System
**Current:**
- `vizSnapshot` for UI display
- `outputSnapshot` for audio outputs
- Different filtering logic for each
- Complex merge logic

**Needed:**
- One source of truth: detected BPMs
- UI reads from same data as audio outputs

### 5. Graph Introspection Complexity
**Current:**
- Scans entire graph every 128 blocks
- Queries modules for `getRhythmInfo()`
- Maintains separate `m_introspectedSources` vector
- Complex merging logic

**Needed:**
- This is actually useful, but can be simplified
- Just query modules, get BPM, output it
- No need for complex caching/merging

### 6. Visualization Data Structures
**Current:**
- `VizData` struct with atomic arrays
- `VizSourceEntry` with atomic char arrays
- Complex `setFromSource()` and `createSnapshot()` methods
- Validation at multiple levels

**Needed:**
- Simple struct: `{ name, bpm, confidence }`
- Read once per frame
- No atomic overhead

## What Should Happen (Minimal Implementation)

```cpp
// Per input channel:
struct SimpleBPMDetector {
    double lastEdgeTime = 0.0;
    std::vector<double> intervals;  // Last 4 intervals
    float currentBPM = 0.0f;
    
    void processSample(float sample, double time) {
        if (risingEdge(sample)) {
            if (lastEdgeTime > 0) {
                double interval = time - lastEdgeTime;
                intervals.push_back(interval);
                if (intervals.size() > 4) intervals.erase(intervals.begin());
                
                if (intervals.size() >= 2) {
                    // Calculate median BPM
                    auto sorted = intervals;
                    std::sort(sorted.begin(), sorted.end());
                    double median = sorted[sorted.size() / 2];
                    currentBPM = 60.0f / median;
                }
            }
            lastEdgeTime = time;
        }
    }
};
```

That's it. ~20 lines per channel. No smoothing, no confidence, no hold blocks, no atomic arrays, no complex state machines.

## Performance Impact
- Current: ~50+ atomic operations per sample, complex calculations
- Minimal: ~1 comparison per sample, simple median calculation every few beats

## Conclusion
The current implementation is a classic case of premature optimization and over-engineering. The core algorithm (detect edges, measure intervals, calculate BPM) is simple. All the "stability" and "confidence" metrics are solving problems that don't exist if you just use a median filter.

**Recommendation:** Strip it down to the bare minimum. Detect edges, store intervals, calculate median BPM. Everything else is unnecessary complexity.

