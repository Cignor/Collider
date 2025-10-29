# Stroke Sequencer "Build Drum Kit" Button - Complete Guide

## Overview

The **"Build Drum Kit"** button in the Stroke Sequencer module provides a **one-click auto-wiring system** that creates a complete drum kit setup by:
1. Creating 3 Sample Loader modules (for the 3 trigger outputs)
2. Creating a Track Mixer module (configured for 6 channels - 3 stereo pairs)
3. Creating a Value module (set to 6.0 to tell the mixer how many tracks to enable)
4. Automatically wiring all connections between these modules
5. Positioning the new modules in an organized layout on the canvas

This guide explains the complete implementation so you can adapt this pattern to other modules.

---

## Architecture Overview

### The Flag-Based Trigger System

The system uses an **atomic boolean flag** as a communication bridge between the UI thread and the main editor logic:

```cpp
// In StrokeSequencerModuleProcessor.h
std::atomic<bool> autoBuildDrumKitTriggered { false };
```

**Why atomic?** Because the flag is set from the ImGui UI thread but read from the main editor thread, making it thread-safe.

### The Three-Part Flow

1. **UI Thread** (ImGui drawing) - Sets the flag when button is clicked
2. **Main Editor Thread** - Polls all module flags and dispatches to handlers
3. **Handler Function** - Creates modules, makes connections, positions nodes

---

## Part 1: The UI Button (Drawing the Button)

**Location:** `StrokeSequencerModuleProcessor.cpp` - `drawParametersInNode()` method

**Lines:** 706-716

```cpp
// Build Drum Kit Quick-Connect Button (80s blue style)
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.85f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.95f, 0.95f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.8f, 1.0f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
if (ImGui::Button("BUILD DRUM KIT", ImVec2(150, 0)))
{
    autoBuildDrumKitTriggered = true;  // ← THIS IS THE KEY LINE
}
ImGui::PopStyleColor(4);
HelpMarker("Auto-create 3 samplers + mixer, wire triggers to pads.");
```

### Key Points:
- The button is drawn in the module's custom UI (`drawParametersInNode`)
- When clicked, it simply sets the flag to `true`
- The button styling is optional (the important part is setting the flag)
- No module creation happens here - just flag setting

---

## Part 2: The Polling System (Checking for Triggered Flags)

**Location:** `ImGuiNodeEditorComponent.cpp` - `checkForAutoConnectionRequests()` method

**Lines:** 5899-5908

This function is called **every frame** from the main editor's update loop:

```cpp
// --- Check StrokeSequencer Flags ---
if (auto* strokeSeq = dynamic_cast<StrokeSequencerModuleProcessor*>(module))
{
    if (strokeSeq->autoBuildDrumKitTriggered.exchange(false))  // ← ATOMIC EXCHANGE
    {
        handleStrokeSeqBuildDrumKit(strokeSeq, modInfo.first);  // ← DISPATCH TO HANDLER
        pushSnapshot();                                          // ← SAVE UNDO STATE
        return;
    }
}
```

### Key Points:
- `exchange(false)` is an atomic operation that:
  - Returns the current value of the flag
  - Sets it to `false` immediately
  - This prevents the handler from being called multiple times
- The handler is passed:
  - A pointer to the stroke sequencer module
  - The logical ID (LID) of the module
- `pushSnapshot()` saves an undo state so the user can undo the operation
- The `return` statement ensures only one auto-connection happens per frame

### The Full Polling Loop Context

The `checkForAutoConnectionRequests()` function iterates through **all modules** looking for triggered flags:

```cpp
void ImGuiNodeEditorComponent::checkForAutoConnectionRequests()
{
    if (!synth) return;
    
    // Iterate through all modules
    for (const auto& modInfo : synth->getAllModules())
    {
        auto* module = synth->getModuleForLogical(modInfo.first);
        if (!module) continue;

        // Check MultiSequencer flags...
        // Check StrokeSequencer flags... (shown above)
        // Check MIDIPlayer flags...
        // etc.
    }
}
```

---

## Part 3: The Handler Function (Creating Modules and Connections)

**Location:** `ImGuiNodeEditorComponent.cpp` - `handleStrokeSeqBuildDrumKit()` method

**Lines:** 5476-5543

This is where the **actual work** happens. Let's break it down step-by-step:

### Step 1: Get the Stroke Sequencer's Position

```cpp
// 1. Get Stroke Sequencer position
auto seqNodeId = synth->getNodeIdForLogical(strokeSeqLid);
ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)strokeSeqLid);
```

**Why?** We need to know where the stroke sequencer is positioned so we can place the new modules relative to it.

- `seqNodeId` - The graph node ID (used for making connections)
- `seqPos` - The screen position (x, y coordinates on the canvas)

---

### Step 2: Create 3 Sample Loader Modules

```cpp
// 2. Create 3 Sample Loaders (for Floor, Mid, Ceiling triggers)
auto sampler1NodeId = synth->addModule("sample_loader");
auto sampler2NodeId = synth->addModule("sample_loader");
auto sampler3NodeId = synth->addModule("sample_loader");

auto sampler1Lid = synth->getLogicalIdForNode(sampler1NodeId);
auto sampler2Lid = synth->getLogicalIdForNode(sampler2NodeId);
auto sampler3Lid = synth->getLogicalIdForNode(sampler3NodeId);

// Position samplers in a vertical stack to the right
pendingNodePositions[(int)sampler1Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y);
pendingNodePositions[(int)sampler2Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y + 220.0f);
pendingNodePositions[(int)sampler3Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y + 440.0f);
```

**Key Concepts:**

- `synth->addModule("sample_loader")` - Creates a new module instance
  - Returns a `NodeID` (the audio graph's internal ID)
- `synth->getLogicalIdForNode(nodeId)` - Converts to a Logical ID (LID)
  - LIDs are used for UI positioning and user-facing identification
- `pendingNodePositions[lid]` - A map that stores where to draw nodes
  - The actual positioning happens later in the render loop
  - This allows batch positioning of multiple nodes

**Positioning Strategy:**
- X-axis: All samplers at `seqPos.x + 400` (400 pixels to the right)
- Y-axis: Stacked vertically with 220-pixel spacing
  - Sampler 1: `seqPos.y` (same height as sequencer)
  - Sampler 2: `seqPos.y + 220`
  - Sampler 3: `seqPos.y + 440`

---

### Step 3: Create Track Mixer Module

```cpp
// 3. Create Track Mixer (will be set to 6 tracks by Value node)
auto mixerNodeId = synth->addModule("track_mixer");
auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y + 200.0f);
```

**Why Track Mixer?**
- Handles multiple stereo inputs (up to 32 tracks = 64 channels)
- Each sampler has 2 outputs (stereo: L + R)
- We need to mix 3 samplers = 6 channels total

**Positioning:**
- X-axis: `seqPos.x + 800` (far right, after samplers)
- Y-axis: `seqPos.y + 200` (centered vertically relative to the 3 samplers)

---

### Step 4: Create Value Node (Set to 6.0)

```cpp
// 4. Create Value node set to 6.0 (for 3 stereo tracks = 6 channels)
auto valueNodeId = synth->addModule("value");
auto valueLid = synth->getLogicalIdForNode(valueNodeId);
pendingNodePositions[(int)valueLid] = ImVec2(seqPos.x + 600.0f, seqPos.y + 550.0f);

if (auto* valueNode = dynamic_cast<ValueModuleProcessor*>(synth->getModuleForLogical(valueLid)))
{
    *dynamic_cast<juce::AudioParameterFloat*>(valueNode->getAPVTS().getParameter("value")) = 6.0f;
}
```

**Why a Value Node?**

The Track Mixer has a parameter called "Num Tracks" that determines how many stereo pairs are active. By connecting a Value node set to 6.0, we're telling the mixer to enable:
- Tracks 1-2 (Sampler 1 stereo pair)
- Tracks 3-4 (Sampler 2 stereo pair)
- Tracks 5-6 (Sampler 3 stereo pair)

**Setting the Parameter:**
```cpp
// Get the module instance
valueNode = synth->getModuleForLogical(valueLid)

// Get its APVTS (Audio Processor Value Tree State)
auto& apvts = valueNode->getAPVTS()

// Get the specific parameter
auto* param = apvts.getParameter("value")

// Cast to the correct parameter type and set the value
*dynamic_cast<juce::AudioParameterFloat*>(param) = 6.0f
```

---

### Step 5: Connect Triggers (Stroke Sequencer → Sample Loaders)

```cpp
// 5. Connect Stroke Sequencer TRIGGERS to Sample Loader TRIGGER MOD inputs (channel 3)
synth->connect(seqNodeId, 0, sampler1NodeId, 3); // Floor Trig   -> Sampler 1 Trigger Mod
synth->connect(seqNodeId, 1, sampler2NodeId, 3); // Mid Trig     -> Sampler 2 Trigger Mod
synth->connect(seqNodeId, 2, sampler3NodeId, 3); // Ceiling Trig -> Sampler 3 Trigger Mod
```

**Understanding the Connection API:**

```cpp
synth->connect(
    sourceNodeId,      // The module sending the signal
    sourceChannel,     // Which output channel (0-based)
    destNodeId,        // The module receiving the signal
    destChannel        // Which input channel (0-based)
);
```

**Stroke Sequencer Output Channels:**
- Channel 0: Floor Trigger (gate output)
- Channel 1: Mid Trigger (gate output)
- Channel 2: Ceiling Trigger (gate output)
- Channel 3: Value CV (the current Y position on the stroke)

**Sample Loader Input Channels:**
- Channel 0: Pitch Mod In (CV)
- Channel 1: Speed Mod In (CV)
- Channel 2: Gate Mod In (gate)
- Channel 3: **Trigger Mod In** (gate) ← This is what we're connecting to

**Why Channel 3?**
The Sample Loader's "Trigger Mod" input is specifically designed to retrigger the sample from the beginning when it receives a gate signal, making it perfect for drum triggers.

---

### Step 6: Connect Audio (Sample Loaders → Track Mixer)

```cpp
// 6. Connect Sample Loader AUDIO OUTPUTS to Track Mixer AUDIO INPUTS (stereo pairs)
// Sampler 1 (L+R) -> Mixer Audio 1+2
synth->connect(sampler1NodeId, 0, mixerNodeId, 0); // Sampler 1 L -> Mixer Audio 1
synth->connect(sampler1NodeId, 1, mixerNodeId, 1); // Sampler 1 R -> Mixer Audio 2

// Sampler 2 (L+R) -> Mixer Audio 3+4
synth->connect(sampler2NodeId, 0, mixerNodeId, 2); // Sampler 2 L -> Mixer Audio 3
synth->connect(sampler2NodeId, 1, mixerNodeId, 3); // Sampler 2 R -> Mixer Audio 4

// Sampler 3 (L+R) -> Mixer Audio 5+6
synth->connect(sampler3NodeId, 0, mixerNodeId, 4); // Sampler 3 L -> Mixer Audio 5
synth->connect(sampler3NodeId, 1, mixerNodeId, 5); // Sampler 3 R -> Mixer Audio 6
```

**Sample Loader Output Channels:**
- Channel 0: Audio Output Left
- Channel 1: Audio Output Right

**Track Mixer Input Channels:**
- Channels 0-63: Audio inputs (32 stereo pairs)
- We're using channels 0-5 (the first 3 stereo pairs)

**Why This Mapping?**
```
Sampler 1 Stereo Pair → Mixer Channels 0-1 (Track 1)
Sampler 2 Stereo Pair → Mixer Channels 2-3 (Track 2)
Sampler 3 Stereo Pair → Mixer Channels 4-5 (Track 3)
```

---

### Step 7: Connect Track Count (Value → Track Mixer)

```cpp
// 7. Connect Value node (6.0) to Track Mixer's "Num Tracks" input
synth->connect(valueNodeId, 0, mixerNodeId, 64); // Value (6) -> Num Tracks Mod
```

**Track Mixer Special Input Channels:**
- Channels 0-63: Audio inputs
- **Channel 64**: "Num Tracks Mod" input ← This is the track count control

By connecting the Value node (which outputs a constant 6.0) to channel 64, we're telling the mixer to enable 6 tracks (3 stereo pairs).

**Why Not Just Set a Parameter?**

In modular synthesis systems, it's often more flexible to use CV (Control Voltage) connections rather than hardcoded parameters. This allows the user to:
- See the connection visually on the canvas
- Modify the track count by changing the Value node
- Potentially modulate the track count dynamically (though not needed here)

---

### Step 8: Connect Output (Track Mixer → Global Output)

```cpp
// 8. Connect Track Mixer output to global output
auto outputNodeId = synth->getOutputNodeID();
synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Global Out L
synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Global Out R
```

**Track Mixer Output Channels:**
- Channel 0: Master Output Left
- Channel 1: Master Output Right

**Global Output:**
- Channel 0: Left speaker
- Channel 1: Right speaker

This final step makes the drum kit audible by routing the mixed audio to the system output.

---

### Step 9: Commit Changes

```cpp
synth->commitChanges();
graphNeedsRebuild = true;
```

**Why Commit?**

- `synth->commitChanges()` - Tells the audio graph to:
  - Validate all connections
  - Update the internal audio routing
  - Prepare the graph for audio processing

- `graphNeedsRebuild = true` - Tells the UI to:
  - Redraw the node editor
  - Update cable positions
  - Refresh the visual representation

**Important:** Without calling `commitChanges()`, the connections exist in memory but won't actually process audio!

---

## Visual Diagram

Here's what the "Build Drum Kit" button creates:

```
                     [Stroke Sequencer]
                           |
                           | (400px right)
          +----------------+----------------+
          |                |                |
          v                v                v
    Floor Trig       Mid Trig       Ceiling Trig
          |                |                |
          v                v                v
   [Sample Loader 1] [Sample Loader 2] [Sample Loader 3]
       (y+0)            (y+220)           (y+440)
          |                |                |
      L   |   R        L   |   R        L   |   R
          |                |                |
          +----------------+----------------+
                           |
                           | (400px right)
                           v
                    [Track Mixer]
                    (y+200, x+800)
                      ^         |
                      |         |
                [Value=6.0]     |
                (y+550)         |
                                v
                        [Global Output]
                        (speakers)
```

**Positioning Summary:**
- Stroke Sequencer: (x, y) ← original position
- Sample Loaders: (x+400, y+0/220/440) ← 400px right, stacked vertically
- Track Mixer: (x+800, y+200) ← 800px right, centered vertically
- Value Node: (x+600, y+550) ← 600px right, below everything

---

## How to Adapt This Pattern for Your Own Module

### Step-by-Step Adaptation Guide

#### 1. Add the Flag to Your Module Header

```cpp
// In YourModuleProcessor.h
class YourModuleProcessor : public ModuleProcessor
{
public:
#if defined(PRESET_CREATOR_UI)
    std::atomic<bool> autoYourFeatureTriggered { false };
#endif
    // ... rest of your class
};
```

**Important:** Wrap it in `#if defined(PRESET_CREATOR_UI)` so it only exists in the editor build, not the plugin build.

---

#### 2. Add the Button to Your Module's UI

```cpp
// In YourModuleProcessor.cpp - drawParametersInNode() method
void YourModuleProcessor::drawParametersInNode(float itemWidth, 
    const std::function<bool(const juce::String&)>& isParamModulated, 
    const std::function<void()>& onModificationEnded)
{
    // ... your existing UI code ...
    
    if (ImGui::Button("YOUR BUTTON TEXT", ImVec2(150, 0)))
    {
        autoYourFeatureTriggered = true;
    }
}
```

---

#### 3. Add the Polling Check

```cpp
// In ImGuiNodeEditorComponent.cpp - checkForAutoConnectionRequests() method

// Add this block after the existing checks for other modules:
if (auto* yourModule = dynamic_cast<YourModuleProcessor*>(module))
{
    if (yourModule->autoYourFeatureTriggered.exchange(false))
    {
        handleYourModuleAutoFeature(yourModule, modInfo.first);
        pushSnapshot();
        return;
    }
}
```

---

#### 4. Declare Your Handler Function

```cpp
// In ImGuiNodeEditorComponent.h
class ImGuiNodeEditorComponent : public juce::Component
{
private:
    // Add this with the other handler declarations:
    void handleYourModuleAutoFeature(YourModuleProcessor* module, juce::uint32 moduleLid);
};
```

---

#### 5. Implement Your Handler Function

```cpp
// In ImGuiNodeEditorComponent.cpp
void ImGuiNodeEditorComponent::handleYourModuleAutoFeature(YourModuleProcessor* module, juce::uint32 moduleLid)
{
    if (!synth || !module) return;

    // 1. Get module position
    auto moduleNodeId = synth->getNodeIdForLogical(moduleLid);
    ImVec2 modulePos = ImNodes::GetNodeGridSpacePos((int)moduleLid);

    // 2. Create new modules
    auto newModule1NodeId = synth->addModule("module_type_name");
    auto newModule1Lid = synth->getLogicalIdForNode(newModule1NodeId);
    pendingNodePositions[(int)newModule1Lid] = ImVec2(modulePos.x + 400.0f, modulePos.y);

    // 3. Configure modules (if needed)
    if (auto* newMod = dynamic_cast<SomeModuleType*>(synth->getModuleForLogical(newModule1Lid)))
    {
        auto& apvts = newMod->getAPVTS();
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("paramName")) = 1.0f;
    }

    // 4. Make connections
    synth->connect(moduleNodeId, outputChannel, newModule1NodeId, inputChannel);

    // 5. Commit changes
    synth->commitChanges();
    graphNeedsRebuild = true;
}
```

---

## Common Patterns and Best Practices

### Pattern 1: Positioning Modules

**Horizontal Spacing:**
```cpp
// Modules in a chain (left to right)
pendingNodePositions[lid1] = ImVec2(baseX + 0,    baseY);
pendingNodePositions[lid2] = ImVec2(baseX + 400,  baseY);
pendingNodePositions[lid3] = ImVec2(baseX + 800,  baseY);
```

**Vertical Spacing:**
```cpp
// Modules in a stack (top to bottom)
pendingNodePositions[lid1] = ImVec2(baseX, baseY + 0);
pendingNodePositions[lid2] = ImVec2(baseX, baseY + 220);
pendingNodePositions[lid3] = ImVec2(baseX, baseY + 440);
```

**Grid Layout:**
```cpp
// Modules in a grid
for (int row = 0; row < rows; ++row)
{
    for (int col = 0; col < cols; ++col)
    {
        float x = baseX + (col * 400.0f);
        float y = baseY + (row * 220.0f);
        pendingNodePositions[lids[row][col]] = ImVec2(x, y);
    }
}
```

---

### Pattern 2: Bulk Module Creation

```cpp
// Create N identical modules
std::vector<juce::AudioProcessor::NodeID> moduleNodeIds;
std::vector<juce::uint32> moduleLids;

for (int i = 0; i < numModules; ++i)
{
    auto nodeId = synth->addModule("module_type");
    auto lid = synth->getLogicalIdForNode(nodeId);
    
    moduleNodeIds.push_back(nodeId);
    moduleLids.push_back(lid);
    
    // Position each module
    pendingNodePositions[(int)lid] = ImVec2(
        baseX + (i * 400.0f),
        baseY + (i * 220.0f)
    );
}
```

---

### Pattern 3: Fan-Out Connections (1 → Many)

```cpp
// Connect one source to multiple destinations
auto sourceNodeId = synth->getNodeIdForLogical(sourceLid);

for (size_t i = 0; i < destNodeIds.size(); ++i)
{
    synth->connect(
        sourceNodeId, i,           // Use different output channels
        destNodeIds[i], 0          // All connect to input channel 0
    );
}
```

---

### Pattern 4: Fan-In Connections (Many → 1)

```cpp
// Connect multiple sources to one destination (like a mixer)
auto destNodeId = synth->getNodeIdForLogical(destLid);

for (size_t i = 0; i < sourceNodeIds.size(); ++i)
{
    synth->connect(
        sourceNodeIds[i], 0,       // All use output channel 0
        destNodeId, i              // Each goes to a different input channel
    );
}
```

---

### Pattern 5: Setting Module Parameters Programmatically

```cpp
// Float parameter
if (auto* module = dynamic_cast<ModuleType*>(synth->getModuleForLogical(lid)))
{
    auto& apvts = module->getAPVTS();
    auto* param = apvts.getParameter("paramName");
    *dynamic_cast<juce::AudioParameterFloat*>(param) = 1.5f;
}

// Int parameter
*dynamic_cast<juce::AudioParameterInt*>(param) = 8;

// Bool parameter
*dynamic_cast<juce::AudioParameterBool*>(param) = true;

// Choice parameter
*dynamic_cast<juce::AudioParameterChoice*>(param) = 2; // Index of choice
```

---

### Pattern 6: Getting Module Properties

```cpp
// Get the number of input/output channels
int numInputs = module->getTotalNumInputChannels();
int numOutputs = module->getTotalNumOutputChannels();

// Get a parameter value
float value = apvts.getRawParameterValue("paramName")->load();

// Check if a module is a specific type
if (auto* sampler = dynamic_cast<SampleLoaderModuleProcessor*>(module))
{
    // Work with sampler-specific methods
}
```

---

## Common Module Types for Auto-Connection

### Sample Loaders
```cpp
auto nodeId = synth->addModule("sample_loader");
```
- **Inputs:** Pitch Mod (0), Speed Mod (1), Gate Mod (2), Trigger Mod (3)
- **Outputs:** Audio L (0), Audio R (1)
- **Use Case:** Drum pads, one-shot samples

### Track Mixer
```cpp
auto nodeId = synth->addModule("track_mixer");
```
- **Inputs:** Audio 0-63 (32 stereo pairs), Num Tracks Mod (64)
- **Outputs:** Master L (0), Master R (1)
- **Use Case:** Mixing multiple audio sources

### Value Generator
```cpp
auto nodeId = synth->addModule("value");
```
- **Inputs:** None (or modulation input)
- **Outputs:** Value CV (0)
- **Use Case:** Constant CV source, parameter control

### VCO (Voltage Controlled Oscillator)
```cpp
auto nodeId = synth->addModule("vco");
```
- **Inputs:** Frequency Mod (0), PWM Mod (1), etc.
- **Outputs:** Audio (0)
- **Use Case:** Synthesizer voices

### Poly VCO
```cpp
auto nodeId = synth->addModule("polyvco");
```
- **Inputs:** 32 Frequency inputs, 32 Gate inputs, 32 Waveform inputs, Num Voices Mod (0)
- **Outputs:** 32 Audio outputs
- **Use Case:** Polyphonic synthesis

---

## Debugging Tips

### 1. Log Everything
```cpp
juce::Logger::writeToLog("🔧 Creating modules...");
juce::Logger::writeToLog("Module LID: " + juce::String((int)lid));
juce::Logger::writeToLog("Position: (" + juce::String(x) + ", " + juce::String(y) + ")");
```

### 2. Check for Null Pointers
```cpp
if (!synth) {
    juce::Logger::writeToLog("ERROR: synth is null!");
    return;
}
if (!module) {
    juce::Logger::writeToLog("ERROR: module is null!");
    return;
}
```

### 3. Verify Node IDs
```cpp
auto nodeId = synth->getNodeIdForLogical(lid);
if (!nodeId.isValid()) {
    juce::Logger::writeToLog("ERROR: Invalid node ID for LID " + juce::String((int)lid));
    return;
}
```

### 4. Test with Simple Cases First
- Start by creating just one module
- Add positioning
- Add one connection
- Then scale up to the full system

### 5. Check Connection Validity
```cpp
// The audio graph will log errors if connections fail
// Check the console for messages like:
// "Connection failed: invalid source/destination"
// "Connection failed: channel out of range"
```

---

## Advanced: Context-Aware Auto-Connection

You can make your auto-connection smarter by checking the module's current state:

```cpp
void ImGuiNodeEditorComponent::handleSmartAutoConnect(YourModule* module, juce::uint32 lid)
{
    // Check how many outputs are already connected
    int connectedOutputs = countConnectedOutputs(lid);
    
    if (connectedOutputs == 0)
    {
        // No connections yet - create a full setup
        createFullSetup(module, lid);
    }
    else
    {
        // Already connected - just add one more module
        addOneMoreModule(module, lid);
    }
}

int ImGuiNodeEditorComponent::countConnectedOutputs(juce::uint32 lid)
{
    auto nodeId = synth->getNodeIdForLogical(lid);
    int count = 0;
    
    for (const auto& conn : synth->getAllConnections())
    {
        if (conn.sourceNodeID == nodeId)
            count++;
    }
    
    return count;
}
```

---

## Summary Checklist

When implementing an auto-connection feature, make sure you:

- [ ] Declare an `std::atomic<bool>` flag in your module header (wrapped in `#if defined(PRESET_CREATOR_UI)`)
- [ ] Add a button in `drawParametersInNode()` that sets the flag to `true`
- [ ] Add a check in `checkForAutoConnectionRequests()` that uses `.exchange(false)`
- [ ] Declare your handler function in `ImGuiNodeEditorComponent.h`
- [ ] Implement your handler function following this structure:
  1. Get module position
  2. Create new modules
  3. Configure module parameters (if needed)
  4. Make all connections
  5. Call `synth->commitChanges()` and set `graphNeedsRebuild = true`
- [ ] Call `pushSnapshot()` in the polling check to enable undo
- [ ] Add debug logging to track execution
- [ ] Test thoroughly with different scenarios

---

## Real-World Use Cases

### Example 1: Auto-Connect Reverb Chain
```cpp
void handleAutoReverbChain(YourModule* module, juce::uint32 lid)
{
    // Create: EQ → Compressor → Reverb → Output
    auto eqId = synth->addModule("eq");
    auto compId = synth->addModule("compressor");
    auto reverbId = synth->addModule("reverb");
    
    // Chain them together
    synth->connect(moduleNodeId, 0, eqId, 0);
    synth->connect(eqId, 0, compId, 0);
    synth->connect(compId, 0, reverbId, 0);
    synth->connect(reverbId, 0, outputNodeId, 0);
    
    synth->commitChanges();
}
```

### Example 2: Auto-Create Multi-Effect Sends
```cpp
void handleAutoEffectSends(MixerModule* mixer, juce::uint32 mixerLid)
{
    // Create 4 send effects in parallel
    std::vector<std::string> effects = {"reverb", "delay", "chorus", "flanger"};
    
    for (size_t i = 0; i < effects.size(); ++i)
    {
        auto fxId = synth->addModule(effects[i]);
        
        // Connect mixer send to effect input
        synth->connect(mixerNodeId, 2 + i, fxId, 0);
        
        // Connect effect return to mixer return
        synth->connect(fxId, 0, mixerNodeId, 10 + i);
    }
    
    synth->commitChanges();
}
```

### Example 3: Auto-Create Voice Architecture
```cpp
void handleAutoVoiceChain(SequencerModule* seq, juce::uint32 seqLid)
{
    // For each sequence step, create VCO → VCF → VCA → Envelope
    for (int i = 0; i < numSteps; ++i)
    {
        auto vcoId = synth->addModule("vco");
        auto vcfId = synth->addModule("vcf");
        auto vcaId = synth->addModule("vca");
        auto envId = synth->addModule("adsr");
        
        // Audio chain
        synth->connect(vcoId, 0, vcfId, 0);
        synth->connect(vcfId, 0, vcaId, 0);
        
        // Modulation
        synth->connect(envId, 0, vcaId, 1);   // Env → VCA amplitude
        synth->connect(envId, 0, vcfId, 1);   // Env → VCF cutoff
        synth->connect(seqNodeId, i, envId, 0); // Seq gate → Env trigger
    }
    
    synth->commitChanges();
}
```

---

## Conclusion

The "Build Drum Kit" button demonstrates a powerful pattern for creating complex module setups with a single click. The key principles are:

1. **Separation of Concerns:** UI sets flags, polling checks flags, handlers do work
2. **Thread Safety:** Use atomic flags for cross-thread communication
3. **Undo Support:** Always call `pushSnapshot()` after making changes
4. **Predictable Layout:** Use consistent spacing formulas for positioning
5. **Complete Wiring:** Don't forget to call `commitChanges()` at the end

By following this pattern, you can create intuitive "quick-start" workflows for your users that transform complex patching tasks into single-button operations.

---

## Related Files

- `juce/Source/audio/modules/StrokeSequencerModuleProcessor.h` - Flag declaration
- `juce/Source/audio/modules/StrokeSequencerModuleProcessor.cpp` - Button implementation
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.h` - Handler declaration
- `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp` - Polling and handler implementation

---

**Document Version:** 1.0  
**Last Updated:** October 29, 2025  
**Author:** AI Assistant  
**Purpose:** Guide for implementing auto-connection features in modular synthesizer modules

