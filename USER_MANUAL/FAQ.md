# Frequently Asked Questions

## General

### What is Pikon Raditsz?

Pikon Raditsz is a modular synthesizer environment that combines audio processing, CV modulation, and real-time computer vision into a single, patchable interface.

### Is it free?

Yes, Pikon Raditsz is an open-source project.

## Audio

### I can't hear anything!

This is the most common issue. Check this list:

- Is the global **Play** button (top menu) active?
- Is a module (like a **VCO** or **Sample Loader**) connected to the main **"Output"** node?
- If you are using a **VCA**, is its `Gain Mod` pin receiving a signal (e.g., from an **ADSR**)? If not, its gain is 0.

### My preset won't load.

This can happen if the preset was saved with a VST plugin that is no longer installed on your system.

## Shortcuts

### What is the shortcut for adding a module?

Right-click on the empty editor canvas to open the **"Add Module"** popup.

### How do I auto-connect nodes?

Select two or more nodes, then use the color-coded chaining keys:

- **C**: Chain Stereo Audio
- **G**: Chain Audio (Green pins)
- **B**: Chain CV (Blue pins)
- **Y**: Chain Gate (Yellow pins)

## Computer Vision

### My Webcam Loader is blank.

Ensure no other application (like Zoom, Teams, or OBS) is currently using your webcam. Only one application can access a webcam at a time.

### The Pose Estimator (or other AI modules) won't work.

The advanced vision modules require model files. Please see the **README.md** and the setup guides in the `guides/` directory of the project for instructions on where to download and place the required `.onnx` and `.weights` files.