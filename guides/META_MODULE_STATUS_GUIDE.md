# 🧩 Meta Module / Inlet / Outlet Status Guide

**Version**: 0.1  
**Last Updated**: 2025-11-09  
**Based on**: `ImGuiNodeEditorComponent`, `ModularSynthProcessor`, `MetaModuleProcessor`, `Inlet/OutletModuleProcessor`

---

## 🎯 Purpose

Document the intended behaviour, current implementation, and recovery plan for Meta modules and their Inlet/Outlet companions. Use this as the canonical reference while bringing sub-patching back online.

---

## 🧠 Expected Behaviour (Spec Recap)

- A **Meta** node should appear in the module browser, instantiate cleanly, and expose N audio inputs/outputs that mirror the number of `Inlet`/`Outlet` nodes inside its internal graph.
- `Inlet` nodes (inside the meta patch) act as audio entry points. They should be labelled, configurable (channel count), and automatically mapped to the parent Meta node inputs.
- `Outlet` nodes serve as exits from the internal graph back to the parent. Their channel counts and labels should determine the Meta node’s outputs.
- Collapsing a set of nodes should:
  - Generate a Meta module that contains a self-contained `ModularSynthProcessor` clone of the selected sub-graph.
  - Create `Inlet`/`Outlet` modules for every boundary cable and preserve all connections when rewired through the Meta shell.
  - Persist the internal patch and metadata so presets survive reload.

---

## 🔍 Current Implementation

### 3.1 UI + Factory Wiring

The module browser calls `addModule("meta")`, but the factory only recognises `"meta module"` and `"metamodule"`, so the button silently fails.

```1588:1601:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
auto addModuleButton = [this](const char* label, const char* type)
{
    if (ImGui::Selectable(label, false))
    {
        if (synth != nullptr)
        {
            auto nodeId = synth->addModule(type);
            const ImVec2 mouse = ImGui::GetMousePos();
            const int logicalId = (int) synth->getLogicalIdForNode (nodeId);
            pendingNodeScreenPositions[logicalId] = mouse;
            snapshotAfterEditor = true;
        }
    }
    ...
```

```1832:1843:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
pushCategoryColor(ModuleCategory::Sys);
bool systemExpanded = ImGui::CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen);
ImGui::PopStyleColor(3);
if (systemExpanded) {
    addModuleButton("Meta", "meta");
    addModuleButton("Inlet", "inlet");
    addModuleButton("Outlet", "outlet");
    ...
```

```845:848:juce/Source/audio/graph/ModularSynthProcessor.cpp
reg("meta module", []{ return std::make_unique<MetaModuleProcessor>(); });
reg("metamodule", []{ return std::make_unique<MetaModuleProcessor>(); });
reg("inlet", []{ return std::make_unique<InletModuleProcessor>(); });
reg("outlet", []{ return std::make_unique<OutletModuleProcessor>(); });
```

### 3.2 MetaModuleProcessor Runtime

- Internal graph bootstraps correctly and `getExtraStateTree()` persists the embedded patch.
- Audio bridging assumes **exactly two channels per inlet/outlet** and packs them sequentially (stereo-per-node). This ignores the `channelCount` parameter exposed by Inlet/Outlet modules and cannot represent mono/multi-channel patches.

```91:143:juce/Source/audio/modules/MetaModuleProcessor.cpp
auto inlets = getInletNodes();
for (size_t i = 0; i < inlets.size() && i < inletBuffers.size(); ++i)
{
    const int startChannel = (int)i * 2; // Each inlet gets 2 channels
    const int numChannels = juce::jmin(2, buffer.getNumChannels() - startChannel);
    ...
}
...
const int startChannel = (int)i * 2; // Each outlet provides 2 channels
const int numChannels = juce::jmin(outletBuffer.getNumChannels(), buffer.getNumChannels() - startChannel);
```

- `rebuildBusLayout()` is still a stub, so the outer AudioProcessor always exposes a fixed stereo IO regardless of how many inlet/outlet channels exist.
- When collapsing a patch, we load the generated blob via `setStateInformation`, but `MetaModuleProcessor` never overrides that function—the call is effectively a no-op.

```534:546:juce/Source/audio/modules/ModuleProcessor.h
void getStateInformation (juce::MemoryBlock&) override {}
void setStateInformation (const void*, int) override {}
```

### 3.3 InletModuleProcessor

- Exposes `channelCount` (1–16) and a user label, but the parent Meta module neither reads the count nor surfaces the label on its pins.
- The process loop simply copies from an incoming buffer supplied by Meta; without a working parent, standalone inlets output silence.

```87:107:juce/Source/audio/modules/InletModuleProcessor.cpp
if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
    channelCount = p->get();

if (ImGui::SliderInt("Channels", &channelCount, 1, 16))
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(paramIdChannelCount)))
        *p = channelCount;
}
```

### 3.4 OutletModuleProcessor

- Mirrors the inlet design (label + channel count) and only caches its incoming buffer for the parent to read. No additional issues beyond the missing parent wiring.

### 3.5 Collapse Workflow

- Boundary detection and internal graph serialization run, but rewiring the new Meta node ignores inlet/outlet indices—every cable is remapped to channel 0.
- Because `metaModule->setStateInformation(...)` is a stub, the generated internal patch never loads, so newly collapsed meta nodes end up empty even if they were created.

```9044:9065:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
for (const auto& bc : boundaries)
{
    if (bc.isInput)
    {
        auto extNodeId = synth->getNodeIdForLogical(bc.externalLogicalId);
        synth->connect(extNodeId, bc.externalChannel, metaNodeId, 0);
    }
    else if (bc.externalLogicalId != 0)
    {
        auto extNodeId = synth->getNodeIdForLogical(bc.externalLogicalId);
        synth->connect(metaNodeId, 0, extNodeId, bc.externalChannel);
    }
    else
    {
        auto outputNodeId = synth->getOutputNodeID();
        synth->connect(metaNodeId, 0, outputNodeId, bc.externalChannel);
    }
}
```

---

## 🚨 Failure Summary

- **UI button dead**: `addModule("meta")` does nothing because `"meta"` is not registered in the factory.
- **Collapsed meta shells are empty**: `setStateInformation` does not load the generated internal graph, so no inlets/outlets appear and the module produces silence.
- **Channel layout mismatch**: Meta hardcodes stereo-per-inlet/outlet, ignoring the channel counts configured inside the patch; bus layout never expands.
- **Rewire logic incomplete**: Collapse reconnects every cable to channel 0, so even after we load the internal graph the outer pins would be wrong.
- **Labels dropped**: Inlet/Outlet labels and counts are not reflected on the parent Meta node pins, reducing usability once basic wiring works.

---

## 🛠️ Strategy to Make Meta Modules Work

1. **Unblock creation**  
   - Register `"meta"` (and optionally `"meta_module"`) as aliases in `getModuleFactory()`.

2. **Implement state plumbing**  
   - Override `MetaModuleProcessor::getStateInformation`/`setStateInformation` to serialise `MetaModuleState` or call through to `getExtraStateTree()`/`setExtraStateTree()` so the collapse workflow and preset loading share the same code path.

3. **Channel mapping overhaul**  
   - Read `channelCount` from each `Inlet`/`Outlet` when building caches.  
   - Resize `inletBuffers`, `outletBuffers`, `lastOutputValues`, and bus layouts to match summed channel counts.  
   - Update `drawIoPins` to emit one pin per channel (or annotate multi-channel pins) and surface custom labels.

4. **Collapse rewire fix**  
   - While generating boundary metadata, capture both the inlet index and channel indices.  
   - Reconnect using cumulative channel offsets so meta inputs/outputs align with internal nodes.  
   - Persist that ordering so manual meta creation matches the collapse results.

5. **Polish & validation**  
   - Expose Meta node labels in the UI (pin names, node title).  
   - Add undo/redo coverage for collapse.  
   - Create smoke tests: simple oscillator → filter sub-patch collapsed into meta, mono & stereo variants, preset save/load.

---

## ✅ Testing & Verification Plan

- Instantiate a Meta node from the browser, confirm pins appear and respond to `channelCount` edits.  
- Collapse a three-node chain with one external input and two outputs; validate the internal graph loads, outer pins map correctly, and audio flows.  
- Save & reload a preset containing nested meta nodes. No connections should be lost, and labels must survive.  
- Regression test standalone Inlet/Outlet nodes to ensure they fail gracefully (silence) when not inside a Meta module.

---

## 📎 Reference Notes

- `MetaModuleProcessor::getExtraStateTree()` already stores the internal graph base64 payload, so the preferred fix is to reuse that infrastructure rather than invent a new format.
- Once wiring works, consider limiting standalone Inlet/Outlet availability in the global module list—they are only meaningful inside Meta graphs.
- Future extensions: expose parameter forwarding (the TODO in `MetaModuleProcessor::createParameterLayout`) and UI affordances for editing the internal patch in-place.

---

**End of Guide** | Version 0.1 | 2025-11-09


