# Sample Variation Node - Detailed Implementation Plan

**Date**: 2025-01-XX  
**Feature**: Sample Variation Node for Repetitive Sound Variations  
**Status**: Planning Phase

---

## Executive Summary

Implement a new `SampleVariationModuleProcessor` node that helps audio artists create natural variations of repetitive sounds (footsteps, gun fires, etc.). The node loads samples from a selected folder and automatically switches between variations either sequentially or randomly after each playback. It includes small pitch variation capabilities to add subtle differences between samples.

---

## Feature Overview

### Core Functionality
1. **Folder-Based Sample Loading**: Select a folder containing variations of a sound
2. **Automatic Sample Switching**: After a sample finishes playing, automatically load the next variation
3. **Selection Mode**: Toggle between sequential (ordered) and random selection
4. **Pitch Variation**: Small pitch adjustments (±2 semitones max) to add subtle variation
5. **Trigger Input**: CV/gate input to start playback of current sample
6. **Drag & Drop Support**: Load samples via drag-and-drop (like SampleLoader)
7. **Button-Based Loading**: Load folder via file chooser button

### Use Cases
- **Game Audio**: Footstep variations for character movement
- **Sound Design**: Gun fire variations for weapon systems
- **Ambient Audio**: Environmental sound variations (wind, water, etc.)
- **Music Production**: Drum hit variations for more natural rhythms
- **Foley**: Repetitive sound variations for realistic audio

---

## Architecture Design

### 1. Module Structure

**Class Name**: `SampleVariationModuleProcessor`  
**Base Class**: `ModuleProcessor`  
**Location**: `juce/Source/audio/modules/SampleVariationModuleProcessor.h/cpp`

**Key Components**:
- Inherits from `ModuleProcessor` (like `SampleLoaderModuleProcessor`)
- Uses `SampleBank` for sample loading/caching
- Uses `SampleVoiceProcessor` for audio playback
- Uses `TimePitchProcessor` for pitch variation (via SampleVoiceProcessor)

### 2. Parameter Layout

**APVTS Parameters**:
```cpp
// Folder selection (stored as path string in extra state)
// No direct parameter - handled via UI button + extra state

// Selection mode
"selectionMode" - AudioParameterChoice: "Sequential", "Random" (default: Sequential)

// Pitch variation (small range: ±2 semitones)
"pitchVariation" - AudioParameterFloat: -2.0f to +2.0f, default 0.0f
"pitchVariation_mod" - AudioParameterFloat: -2.0f to +2.0f, default 0.0f (CV input)

// Gate/Volume
"gate" - AudioParameterFloat: 0.0f to 1.0f, default 0.8f
"gate_mod" - AudioParameterFloat: 0.0f to 1.0f, default 1.0f (CV input)

// Trigger (edge detection, no parameter needed - handled via CV input)

// Playback control
"loop" - AudioParameterBool: default false (play once, then switch)
```

**Input Buses** (Multi-bus architecture like SampleLoader):
- **Bus 0**: Pitch Variation Mod (1 channel)
- **Bus 1**: Gate Mod (1 channel)  
- **Bus 2**: Trigger (1 channel, edge detection)

**Output Buses**:
- **Bus 0**: Audio Output (stereo)

### 3. State Management

**Internal State**:
```cpp
// Folder management
juce::String currentFolderPath;  // Selected folder path
juce::Array<juce::File> folderSamples;  // Cached list of samples in folder
int currentSampleIndex;  // For sequential mode

// Sample playback
std::shared_ptr<SampleBank::Sample> currentSample;
std::unique_ptr<SampleVoiceProcessor> sampleProcessor;
std::atomic<SampleVoiceProcessor*> newSampleProcessor { nullptr };
juce::CriticalSection processorSwapLock;

// Playback state
bool isPlaying { false };
bool lastTriggerHigh { false };  // Edge detection

// Sample metadata
juce::String currentSampleName;
std::atomic<double> sampleDurationSeconds { 0.0 };
std::atomic<int> sampleSampleRate { 0 };
```

**Extra State Tree** (for folder path persistence):
```cpp
juce::ValueTree getExtraStateTree() const override
{
    juce::ValueTree extra("SampleVariationExtra");
    extra.setProperty("folderPath", currentFolderPath, nullptr);
    extra.setProperty("currentIndex", currentSampleIndex, nullptr);
    return extra;
}
```

### 4. Sample Switching Logic

**Sequential Mode**:
- Maintain `currentSampleIndex` (0 to `folderSamples.size() - 1`)
- After sample finishes: `currentSampleIndex = (currentSampleIndex + 1) % folderSamples.size()`
- Load next sample: `loadSample(folderSamples[currentSampleIndex])`

**Random Mode**:
- After sample finishes: Pick random index (excluding current if > 1 sample)
- Use `juce::Random` with time-based seed for variation
- Load random sample: `loadSample(folderSamples[randomIndex])`

**Detection of Playback End**:
- Monitor `sampleProcessor->isPlaying` in `processBlock()`
- When `isPlaying` transitions from `true` to `false`, trigger next sample load
- Use flag to prevent multiple triggers: `bool sampleEndDetected { false }`

### 5. Pitch Variation Implementation

**Small Range**: ±2 semitones maximum (vs ±24 in SampleLoader)

**Application**:
- Base pitch: 0.0 semitones (no change)
- Variation: `pitchVariation` parameter + `pitchVariation_mod` CV input
- Final pitch: `juce::jlimit(-2.0f, 2.0f, basePitch + variation)`
- Applied via `SampleVoiceProcessor::setBasePitchSemitones()`

**CV Input**:
- Maps 0.0-1.0 CV to -2.0 to +2.0 semitones
- Or relative mode: CV 0.5 = no change, ±1.0 = ±2 semitones

### 6. UI Implementation

**drawParametersInNode() Implementation**:

```cpp
void drawParametersInNode(float itemWidth, ...) override
{
    // 1. Folder selection button
    if (ImGui::Button("Select Folder", ImVec2(itemWidth * 0.48f, 0)))
    {
        // Open folder chooser (not file chooser)
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Sample Folder", 
            getLastFolder(), 
            "*"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | 
            juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto folder = fc.getResult();
                if (folder.isDirectory())
                    setSampleFolder(folder);
            }
        );
    }
    
    // 2. Selection mode (Sequential/Random)
    int mode = apvts.getRawParameterValue("selectionMode")->load() > 0.5f ? 1 : 0;
    const char* items[] = { "Sequential", "Random" };
    if (ImGui::Combo("Selection Mode", &mode, items, 2))
    {
        apvts.getParameter("selectionMode")->setValueNotifyingHost(mode > 0 ? 1.0f : 0.0f);
    }
    
    // 3. Pitch variation slider
    float pitchVar = apvts.getRawParameterValue("pitchVariation")->load();
    if (ImGui::SliderFloat("Pitch Variation", &pitchVar, -2.0f, 2.0f, "%.2f st"))
    {
        apvts.getParameter("pitchVariation")->setValueNotifyingHost(
            apvts.getParameterRange("pitchVariation").convertTo0to1(pitchVar)
        );
    }
    
    // 4. Gate slider
    // ... (similar to SampleLoader)
    
    // 5. Current sample info
    if (hasSampleLoaded())
    {
        ImGui::Text("Sample: %s", currentSampleName.toRawUTF8());
        ImGui::Text("Folder: %d samples", folderSamples.size());
        ImGui::Text("Index: %d/%d", currentSampleIndex + 1, folderSamples.size());
    }
    
    // 6. Drag & drop zone (like SampleLoader)
    // ... (handle DND_SAMPLE_PATH payload)
}
```

### 7. Audio Processing Flow

**processBlock() Logic**:

```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
{
    // 1. Get output bus
    auto outBus = getBusBuffer(buffer, false, 0);
    
    // 2. Handle processor swap (thread-safe)
    if (auto* pending = newSampleProcessor.exchange(nullptr))
    {
        const juce::ScopedLock lock(processorSwapLock);
        processorToDelete = std::move(sampleProcessor);
        sampleProcessor.reset(pending);
    }
    
    // 3. Safety check
    SampleVoiceProcessor* currentProcessor = nullptr;
    {
        const juce::ScopedLock lock(processorSwapLock);
        currentProcessor = sampleProcessor.get();
    }
    if (currentProcessor == nullptr || currentSample == nullptr)
    {
        outBus.clear();
        return;
    }
    
    // 4. Trigger detection (edge detection on trigger input)
    if (isParamInputConnected("trigger_mod") && triggerBus.getNumChannels() > 0)
    {
        const float* trigSignal = triggerBus.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            const bool trigHigh = trigSignal[i] > 0.5f;
            if (trigHigh && !lastTriggerHigh)
            {
                reset();  // Start playback
                break;
            }
            lastTriggerHigh = trigHigh;
        }
    }
    
    // 5. Check for playback end (detect transition from playing to stopped)
    bool wasPlaying = currentProcessor->isPlaying;
    
    // 6. Render audio if playing
    if (currentProcessor->isPlaying)
    {
        // Apply pitch variation
        float pitchVar = apvts.getRawParameterValue("pitchVariation")->load();
        if (isParamInputConnected("pitchVariation_mod") && pitchBus.getNumChannels() > 0)
        {
            float cv = juce::jlimit(0.0f, 1.0f, pitchBus.getReadPointer(0)[0]);
            pitchVar += juce::jmap(cv, -2.0f, 2.0f);  // CV maps to ±2 semitones
        }
        pitchVar = juce::jlimit(-2.0f, 2.0f, pitchVar);
        currentProcessor->setBasePitchSemitones(pitchVar);
        
        // Render audio
        juce::AudioBuffer<float> outputBuffer(...);
        currentProcessor->renderBlock(outputBuffer, midiMessages);
        
        // Apply gate
        // ... (similar to SampleLoader)
    }
    else
    {
        outBus.clear();
    }
    
    // 7. Detect playback end and switch samples
    if (wasPlaying && !currentProcessor->isPlaying && !sampleEndDetected)
    {
        sampleEndDetected = true;
        switchToNextSample();  // Load next variation
    }
    else if (!wasPlaying && currentProcessor->isPlaying)
    {
        sampleEndDetected = false;  // Reset flag when new sample starts
    }
}
```

### 8. Sample Folder Management

**setSampleFolder() Method**:

```cpp
void setSampleFolder(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;
    
    currentFolderPath = folder.getFullPathName();
    
    // Scan folder for audio files
    folderSamples.clear();
    folder.findChildFiles(folderSamples, juce::File::findFiles, false, 
                         "*.wav;*.mp3;*.flac;*.aiff;*.ogg");
    
    // Sort alphabetically for sequential mode
    folderSamples.sort();
    
    // Reset index
    currentSampleIndex = 0;
    
    // Load first sample if available
    if (folderSamples.size() > 0)
    {
        loadSample(folderSamples[0]);
    }
    
    // Save to extra state
    apvts.state.setProperty("folderPath", currentFolderPath, nullptr);
}
```

**switchToNextSample() Method**:

```cpp
void switchToNextSample()
{
    if (folderSamples.isEmpty())
        return;
    
    const bool randomMode = apvts.getRawParameterValue("selectionMode")->load() > 0.5f;
    
    if (randomMode)
    {
        // Random selection (exclude current if > 1 sample)
        juce::Random rng(juce::Time::getMillisecondCounterHiRes());
        int nextIndex = currentSampleIndex;
        if (folderSamples.size() > 1)
        {
            do {
                nextIndex = rng.nextInt(folderSamples.size());
            } while (nextIndex == currentSampleIndex && folderSamples.size() > 2);
        }
        currentSampleIndex = nextIndex;
    }
    else
    {
        // Sequential: wrap around
        currentSampleIndex = (currentSampleIndex + 1) % folderSamples.size();
    }
    
    // Load next sample
    loadSample(folderSamples[currentSampleIndex]);
}
```

---

## Implementation Steps

### Phase 1: Core Module Structure (Difficulty: Medium)
1. Create `SampleVariationModuleProcessor.h` and `.cpp` files
2. Inherit from `ModuleProcessor`
3. Implement basic APVTS parameter layout
4. Set up multi-bus I/O configuration
5. Implement `getAPVTS()` and basic `processBlock()` skeleton

**Risk**: Low  
**Estimated Time**: 2-3 hours

### Phase 2: Sample Loading & Management (Difficulty: Medium)
1. Implement `setSampleFolder()` method
2. Implement folder scanning and sample caching
3. Implement `loadSample()` using `SampleBank` (reuse from SampleLoader)
4. Implement `createSampleProcessor()` (similar to SampleLoader)
5. Add thread-safe processor swapping

**Risk**: Medium (thread safety concerns)  
**Estimated Time**: 3-4 hours

### Phase 3: Playback & Switching Logic (Difficulty: Medium-High)
1. Implement trigger detection (edge detection)
2. Implement playback end detection
3. Implement `switchToNextSample()` with sequential/random modes
4. Handle edge cases (empty folder, single sample, etc.)
5. Test sample switching timing

**Risk**: Medium (timing-sensitive logic)  
**Estimated Time**: 4-5 hours

### Phase 4: Pitch Variation (Difficulty: Low-Medium)
1. Implement pitch variation parameter handling
2. Implement CV input for pitch variation
3. Apply pitch via `SampleVoiceProcessor::setBasePitchSemitones()`
4. Test pitch range limits (±2 semitones)

**Risk**: Low  
**Estimated Time**: 2-3 hours

### Phase 5: UI Implementation (Difficulty: Medium)
1. Implement `drawParametersInNode()` with folder selection button
2. Add selection mode combo box
3. Add pitch variation slider
4. Add gate slider
5. Add current sample info display
6. Implement drag & drop support (reuse SampleLoader pattern)
7. Add folder path display

**Risk**: Low-Medium (UI complexity)  
**Estimated Time**: 4-5 hours

### Phase 6: State Persistence (Difficulty: Low)
1. Implement `getExtraStateTree()` for folder path
2. Implement `setExtraStateTree()` for folder path restoration
3. Implement `getStateInformation()` / `setStateInformation()`
4. Test preset save/load

**Risk**: Low  
**Estimated Time**: 1-2 hours

### Phase 7: Module Registration (Difficulty: Low)
1. Register module in module factory/registry
2. Add to PinDatabase for UI
3. Test node creation in Preset Creator
4. Verify I/O pins display correctly

**Risk**: Low  
**Estimated Time**: 1-2 hours

### Phase 8: Testing & Polish (Difficulty: Medium)
1. Test sequential mode with various folder sizes
2. Test random mode (verify no immediate repeats)
3. Test pitch variation range limits
4. Test trigger input responsiveness
5. Test drag & drop functionality
6. Test preset save/load with folder path
7. Performance testing (large folders, many samples)
8. Edge case testing (empty folder, invalid files, etc.)

**Risk**: Medium (edge cases)  
**Estimated Time**: 3-4 hours

---

## Risk Assessment

### High Risk Areas

1. **Thread Safety in Sample Switching**
   - **Risk**: Race condition when switching samples during playback
   - **Mitigation**: Use atomic processor swap pattern (like SampleLoader)
   - **Impact**: Audio glitches or crashes

2. **Playback End Detection Timing**
   - **Risk**: Missing playback end or detecting false positives
   - **Mitigation**: Use flag to prevent multiple triggers, check `isPlaying` transition
   - **Impact**: Samples not switching or switching too early

3. **Folder Scanning Performance**
   - **Risk**: Large folders (>100 files) cause UI freeze
   - **Mitigation**: Scan in background thread, cache results
   - **Impact**: UI responsiveness issues

### Medium Risk Areas

1. **Random Mode Avoiding Repeats**
   - **Risk**: Same sample plays twice in a row
   - **Mitigation**: Exclude current index when > 1 sample available
   - **Impact**: Less natural variation

2. **State Persistence of Folder Path**
   - **Risk**: Folder path becomes invalid after preset load
   - **Mitigation**: Validate folder exists, show error if missing
   - **Impact**: Broken presets

3. **CV Input Latency**
   - **Risk**: Pitch variation CV input causes audio artifacts
   - **Mitigation**: Smooth parameter changes, use existing CV handling patterns
   - **Impact**: Audio quality issues

### Low Risk Areas

1. **UI Layout**
   - **Risk**: UI elements don't fit in node
   - **Mitigation**: Use compact layout, test with different node sizes
   - **Impact**: Minor UI issues

2. **Parameter Range Validation**
   - **Risk**: Pitch variation exceeds ±2 semitones
   - **Mitigation**: Use `juce::jlimit()` in all pitch calculations
   - **Impact**: Minor parameter issues

---

## Difficulty Levels

### Beginner-Friendly Tasks
- Parameter layout definition
- Basic UI sliders and buttons
- State persistence (getStateInformation/setStateInformation)
- Module registration

### Intermediate Tasks
- Sample loading using SampleBank
- Trigger edge detection
- Pitch variation application
- Folder scanning and caching

### Advanced Tasks
- Thread-safe processor swapping
- Playback end detection timing
- Random selection algorithm (avoiding repeats)
- Background folder scanning (if needed)

---

## Confidence Rating

### Overall Confidence: **85%**

### Strong Points

1. **Well-Established Patterns**: Can reuse `SampleLoaderModuleProcessor` as a template
   - Sample loading via `SampleBank`
   - Processor swapping pattern
   - Multi-bus architecture
   - Drag & drop implementation

2. **Clear Requirements**: User requirements are well-defined
   - Folder-based loading
   - Sequential/random modes
   - Small pitch variation
   - Trigger input

3. **Existing Infrastructure**: All required components exist
   - `SampleBank` for sample management
   - `SampleVoiceProcessor` for playback
   - `TimePitchProcessor` for pitch variation
   - ModuleProcessor base class

4. **Similar Complexity**: Similar to existing modules
   - Less complex than SampleLoader (no range, position, sync)
   - More focused feature set

### Weak Points

1. **Playback End Detection**: Timing-sensitive logic
   - Need to detect exact moment sample finishes
   - Must handle edge cases (very short samples, loop mode conflicts)
   - **Mitigation**: Use proven pattern from SampleLoader, test thoroughly

2. **Random Selection Algorithm**: Avoiding immediate repeats
   - Need to ensure natural variation
   - Edge case: single sample in folder
   - **Mitigation**: Simple algorithm (exclude current if > 1 sample)

3. **Folder Management**: Performance with large folders
   - Scanning 100+ files could be slow
   - **Mitigation**: Cache results, scan once on folder selection
   - **Future Enhancement**: Background scanning if needed

4. **State Persistence**: Folder path validation
   - Folders may be moved/deleted between sessions
   - **Mitigation**: Validate on load, show error message

---

## Potential Problems & Solutions

### Problem 1: Sample Switching During Playback
**Issue**: Switching samples mid-playback causes audio glitches  
**Solution**: Only switch when `isPlaying` transitions to `false` (playback end)

### Problem 2: Rapid Trigger Inputs
**Issue**: Multiple triggers cause sample to restart repeatedly  
**Solution**: Edge detection only triggers on rising edge, ignore while playing

### Problem 3: Empty or Invalid Folder
**Issue**: User selects folder with no valid audio files  
**Solution**: Validate folder contents, show error message, disable playback

### Problem 4: Folder Path Becomes Invalid
**Issue**: Preset saved with folder path, but folder moved/deleted  
**Solution**: Validate folder exists on preset load, show warning, allow re-selection

### Problem 5: Very Short Samples
**Issue**: Short samples (< 100ms) may cause rapid switching  
**Solution**: Add minimum playback duration check, or let user control via loop parameter

### Problem 6: Random Mode with Single Sample
**Issue**: Random mode with 1 sample always plays same file  
**Solution**: Handle gracefully (play the single sample), or disable random mode

### Problem 7: Pitch Variation Artifacts
**Issue**: Rapid pitch changes cause audio artifacts  
**Solution**: Use smoothing (already in SampleVoiceProcessor), limit CV update rate

### Problem 8: Thread Safety in Folder Scanning
**Issue**: UI thread scans folder while audio thread accesses folderSamples  
**Solution**: Use CriticalSection or atomic operations, scan once and cache

---

## Testing Strategy

### Unit Tests
- Folder scanning with various file types
- Sequential index wrapping
- Random selection (no immediate repeats)
- Pitch variation range limits

### Integration Tests
- Sample switching after playback end
- Trigger input edge detection
- State persistence (save/load preset)
- Drag & drop sample loading

### Performance Tests
- Large folders (100+ files)
- Rapid trigger inputs
- Continuous playback (1 hour+)
- Memory usage (many samples loaded)

### Edge Case Tests
- Empty folder
- Single sample in folder
- Invalid audio files in folder
- Very short samples (< 50ms)
- Very long samples (> 10 minutes)
- Folder path becomes invalid
- Rapid folder changes

---

## Future Enhancements (Out of Scope)

1. **Weighted Random Selection**: Assign weights to samples for more control
2. **Sample Preview**: Play sample preview before loading
3. **Folder Monitoring**: Auto-reload when folder contents change
4. **Sample Tagging**: Filter samples by tags/metadata
5. **Volume Normalization**: Auto-normalize samples to same level
6. **Crossfade Between Samples**: Smooth transitions between variations
7. **Pattern-Based Selection**: Define custom selection patterns (A-B-A-C, etc.)
8. **Sample Rate Conversion**: Handle samples with different sample rates

---

## Dependencies

### Required Components (Already Exist)
- `ModuleProcessor` base class
- `SampleBank` for sample loading/caching
- `SampleVoiceProcessor` for audio playback
- `TimePitchProcessor` for pitch variation
- `ModularSynthProcessor` for graph management
- ImGui for UI rendering

### No New Dependencies Required
- All functionality can be built using existing infrastructure

---

## Estimated Total Time

**Conservative Estimate**: 20-25 hours  
**Optimistic Estimate**: 15-20 hours  
**Realistic Estimate**: 18-22 hours

**Breakdown**:
- Core structure: 2-3 hours
- Sample loading: 3-4 hours
- Playback & switching: 4-5 hours
- Pitch variation: 2-3 hours
- UI implementation: 4-5 hours
- State persistence: 1-2 hours
- Module registration: 1-2 hours
- Testing & polish: 3-4 hours

---

## Conclusion

This feature is **highly feasible** with **strong confidence** (85%). The implementation follows well-established patterns from `SampleLoaderModuleProcessor`, reducing risk significantly. The main challenges are timing-sensitive playback end detection and thread-safe sample switching, both of which have proven solutions in the existing codebase.

The feature provides clear value for audio artists working with repetitive sounds, and the implementation complexity is manageable within the existing architecture.

---

## Approval & Sign-off

**Status**: Ready for Implementation  
**Risk Level**: Medium (manageable with proper testing)  
**Confidence**: High (85%)  
**Recommended Approach**: Phased implementation with thorough testing at each phase

