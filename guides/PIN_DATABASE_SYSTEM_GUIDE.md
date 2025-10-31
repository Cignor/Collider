### Pin Database System: Static and Dynamic Pins

This guide explains how the node pin system is declared in `juce/Source/preset_creator/PinDatabase.cpp`, how nodes specify their inputs/outputs, and how dynamic I/O is handled. It uses the `drive` node (static pins) and the `physics` node (dynamic pins) as examples.

### What the Pin Database Does

- **Central registry**: Maps a lowercase node name (e.g., `"drive"`, `"physics"`) to a `ModulePinInfo` structure that describes the node’s default width, inputs, outputs, and parameter modulation pins.
- **Editor contract**: The ImGui node editor queries this database to draw sockets, color codes by type, and to know which pins are connectable.
- **Tooling metadata**: The same structure also supports module descriptions (tooltips/help) and aliases.

### Core Types

- **`ModulePinInfo`**: Holds
  - `defaultWidth`: a `NodeWidth` enum for the node’s default UI width (`Small`, `Medium`, `Big`, `ExtraWide`, `Exception` for fully custom layout nodes).
  - `audioIns`: vector of `AudioPin` describing inputs.
  - `audioOuts`: vector of `AudioPin` describing outputs.
  - `modPins`: vector of `ModPin` describing named parameters that can be modulated (used for UI disable/enable of modulators).

- **`AudioPin`**: A connectable port with fields `(displayName, absoluteChannel, PinDataType)`.
  - `PinDataType::Audio` = red (audio), `PinDataType::CV` = blue (control voltage), `PinDataType::Gate` = yellow (gates/triggers), `PinDataType::Raw` = grey (non-audio numeric/control).
  - `absoluteChannel` indexes into the module’s internal multi-bus layout. Conventionally:
    - Low indices are used for primary inputs/outputs.
    - Larger indices may target "Mod" or extended buses as documented in each node.

- **`ModPin`**: A named parameter modulation descriptor `(displayName, parameterId, PinDataType)`. This is not a connectable socket; it’s metadata the UI uses to enable/disable modulation widgets when a cable is attached on the corresponding input channel.

### Naming and Lookup Rules

- Keys in the database are lowercased node names (e.g., `"drive"`, `"physics"`, `"pose_estimator"`). A few aliases are added to normalize underscores and capitalization (e.g., `"clock_divider"` → `"ClockDivider"`).
- The editor uses the database to render sockets and their labels. The backing audio/graph engine is responsible for mapping `absoluteChannel` to the actual processing busses.

### Module Descriptions

`populateModuleDescriptions()` seeds human-readable descriptions used in tooltips and palettes. Add/update descriptions there when introducing new nodes or renaming existing ones.

### Static Pins Example: `drive`

The `drive` node declares two audio inputs and two audio outputs with no modulation pins. Its entry in `PinDatabase.cpp` looks like this:

```20:30:juce/Source/preset_creator/PinDatabase.cpp
    db["drive"] = ModulePinInfo(
        NodeWidth::Small,
        { AudioPin("In L", 0, PinDataType::Audio), AudioPin("In R", 1, PinDataType::Audio) },
        { AudioPin("Out L", 0, PinDataType::Audio), AudioPin("Out R", 1, PinDataType::Audio) },
        {}
    );
```

Key takeaways:
- **Width**: `Small` node in the editor.
- **Inputs**: Two red audio inputs labeled `In L` and `In R` at channels 0 and 1.
- **Outputs**: Two red audio outputs labeled `Out L` and `Out R` at channels 0 and 1.
- **Modulation**: None specified (`{}`), so no mod widgets are tied to cable state.

This is a good template for simple effect nodes that do not expose per-parameter CV inputs.

### Dynamic Pins Example: `physics`

Some nodes manage their I/O dynamically at runtime (number of bodies, contacts, or sources can change). For such cases, the database records only a placeholder and defers the exact pin list to the module:

```963:969:juce/Source/preset_creator/PinDatabase.cpp
    db["physics"] = ModulePinInfo(
        NodeWidth::Exception,
        {}, // Dynamic inputs defined by module
        {}, // Dynamic outputs defined by module
        {}
    );
```

Key takeaways:
- **Width**: `Exception` signals the editor to expect custom sizing/zoom handling.
- **Inputs/Outputs**: Empty vectors in the database. The module is responsible for publishing its current pins (e.g., per-collider gates, positions) to the UI/engine.
- **Usage pattern**: The runtime implementation (in the module code) updates pin counts and labels as the scene changes. The editor queries this live state to show accurate sockets.

Other dynamic examples follow the same pattern:
- `color_tracker`: fixed input (`Source In`), dynamic outputs added per tracked color (3 pins each: X, Y, Gate).
- `pose_estimator`, `hand_tracker`, `face_tracker`: programmatically generate many outputs from enumerations or counts.

### Choosing Between Static and Dynamic Declarations

- Use a **static** declaration when the pin count and labels are known and fixed at compile time (e.g., `drive`, `vcf`, `chorus`).
- Use a **dynamic** declaration when the pin count depends on runtime factors (sources, tracks, detections), or when the node defines a custom UI footprint (`NodeWidth::Exception`).

### Channel Indexing Conventions

- Keep left/right audio as adjacent channels starting at 0 when possible.
- For parameter CV inputs, prefer grouping by function and keep numbering contiguous for clarity.
- For bulk/generated pins (e.g., 32 voices, 70 landmarks), keep indices contiguous and derive names from arrays to prevent mistakes.
- For extended or modulation busses, document the absolute channel ranges clearly in the initializer list (see `polyvco`, `track mixer`, and `function_generator` examples in `PinDatabase.cpp`).

### Modulation Pins (`modPins`) and UI Behavior

- `modPins` do not create physical sockets. They are metadata binding parameter IDs (e.g., `"drive_mod"`, `"rate_mod"`) to types. The editor can disable a parameter knob when a cable occupies its corresponding modulation input channel.
- If your node exposes per-parameter CV inputs as `AudioPin`s, also add a matching `ModPin` entry so the UI can reflect cable-driven modulation correctly.

### Adding a New Node

1) Add a description in `populateModuleDescriptions()` for user-facing help.
2) In `populatePinDatabase()`, insert a new `db["your_node_name"] = ModulePinInfo(...)` entry:
- Pick `NodeWidth` based on UI complexity; use `Exception` for custom/zoomed nodes.
- Define `audioIns` and `audioOuts` with clear labels, contiguous channels, and correct `PinDataType`.
- Optionally define `modPins` to bind parameter IDs to modulation state in the UI.
3) If the node is dynamic, keep `audioIns`/`audioOuts` minimal (or empty) and let the module code publish and update pins at runtime.
4) If needed, add lowercase/underscore aliases mapping to the primary key to improve findability.

### Validating Your Entry

- Ensure the node name matches what the module requests at creation time (lowercase key).
- Confirm colors align with `PinDataType` (Audio=red, CV=blue, Gate=yellow, Raw=grey).
- Verify channel indices don’t collide within a node’s inputs or outputs.
- For dynamic nodes, confirm the runtime module correctly reports changing pin lists to the editor.

### Quick References in `PinDatabase.cpp`

- Descriptions seeded in `populateModuleDescriptions()` include many audio, MIDI, and vision modules.
- Examples worth scanning for patterns:
  - Simple static: `drive`, `gate`, `limiter`.
  - Static with many CVs: `graphic_eq`, `function_generator`.
  - Programmatic bulk outputs: `pose_estimator`, `hand_tracker`, `face_tracker`.
  - Dynamic footprint: `physics`, `color_tracker`.

With these conventions, the editor can render consistent, color-coded pins for simple processors like `drive`, while still supporting sophisticated modules like `physics` that grow and adapt their I/O dynamically at runtime.

### Auto-Connect and Keyboard Shortcuts

The node editor provides quick auto-connection tools that leverage pin types defined in the pin database. When one or more nodes are selected, you can trigger chaining with single-letter shortcuts:

```3549:3573:juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp
        // C: Standard stereo chaining (channels 0->0, 1->1)
        if (ImGui::IsKeyPressed(ImGuiKey_C))
        {
            handleNodeChaining();
        }
        // G: Audio type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_G))
        {
            handleColorCodedChaining(PinDataType::Audio);
        }
        // B: CV type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_B))
        {
            handleColorCodedChaining(PinDataType::CV);
        }
        // R: Raw type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            handleColorCodedChaining(PinDataType::Raw);
        }
        // Y: Gate type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            handleColorCodedChaining(PinDataType::Gate);
        }
```

- **C (Chain)**: Connects standard stereo paths in order between selected nodes, mapping output 0→input 0 and output 1→input 1.
- **G (Audio)**: Color-coded chaining for `PinDataType::Audio` (red). Connects compatible audio outputs to audio inputs across the selection.
- **B (CV)**: Color-coded chaining for `PinDataType::CV` (blue).
- **Y (Gate)**: Color-coded chaining for `PinDataType::Gate` (yellow).
- **R (Raw)**: Color-coded chaining for `PinDataType::Raw` (grey).

How it uses the pin database:
- The chaining routines look up pin layouts and types per node to find compatible pairs. Static nodes like `drive` are straightforward because their pins are fixed. Dynamic nodes like `physics` may expose changing pins at runtime; chaining will use whatever the module currently publishes.
- When a suitable input/output isn’t available for the chosen type, the routine skips that connection gracefully.

Tip: You can toggle a shortcuts overlay with F1 in the editor to discover other useful bindings.


