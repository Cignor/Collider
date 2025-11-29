# Frequently Asked Questions

## General

### What is Pikon Raditsz?

Pikon Raditsz is a modular synthesizer environment that combines audio processing, CV modulation, and real-time computer vision into a single, patchable interface.

### Is it free?

Yes, Pikon Raditsz is an open-source project.

## Updates

### How do I check for updates?

You can check for updates manually by going to **Help > Check for Updates** in the main menu. The application will connect to the update server and check if a newer version is available.

### What happens when I check for updates?

When you check for updates:

1. The application connects to the update server and downloads the latest version manifest
2. It compares your current version with the latest available version
3. If an update is available, a dialog will appear showing:
   - The new version number
   - Release notes and changelog
   - Download size
   - Option to download and install the update
4. If you're already on the latest version, you'll see a message confirming this

### Does the app check for updates automatically?

Yes, the application can automatically check for updates in the background. Automatic update checks are enabled by default and will run periodically (typically once per day) when the application is running. You can disable automatic checks in the settings if you prefer to check manually.

### How do I download and install an update?

When an update is available:

1. Click **"Download Update"** in the update dialog
2. The download progress will be shown in a progress dialog
3. Once the download completes, you'll be prompted to restart the application
4. After restarting, the new version will be installed and launched automatically

**Note:** You may need to close the application for the update to be applied. Make sure to save your work before updating.

### Can I skip an update?

Yes, if an update is available, you can choose to skip it. The update dialog provides options to:
- **Download and Install**: Proceed with the update
- **Skip This Version**: Skip this specific version (you'll be notified again about future updates)
- **Cancel**: Close the dialog without taking action

### What if the update check fails?

If the update check fails (e.g., due to network issues), you'll see an error message. You can:
- Check your internet connection
- Try again later
- Check manually again using **Help > Check for Updates**

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