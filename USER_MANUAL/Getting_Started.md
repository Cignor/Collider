# Getting Started with Collider

Welcome to Collider! This guide will walk you through creating your first sound.

## Your First Patch: "Hello, World!"

Let's make a simple synthesizer voice.

### 1. Add an Oscillator

In the **"Modules"** list on the left, find the **"Sources"** category.

Click on **"VCO"** (Voltage-Controlled Oscillator). It will appear in the main editor.

The VCO is now generating a constant tone, but it's not connected to the output.

### 2. Add an Amplifier

In the **"Modules"** list, find the **"Utilities"** category.

Click on **"VCA"** (Voltage-Controlled Amplifier).

The VCA controls the volume. By default, its gain is 0, so no sound passes through.

### 3. Add an Envelope

In the **"Modules"** list, find the **"Modulators"** category.

Click on **"ADSR"** (Attack, Decay, Sustain, Release).

This will shape our sound's volume over time.

### 4. Add a Trigger

In the **"Modules"** list, find the **"Sequencers"** category.

Click on **"Tempo Clock"**. This provides a global beat.

We will use its output to trigger our envelope.

### 5. Connect Everything

Now, let's connect the modules by clicking and dragging from their pins:

- **VCO → VCA**: Click the `Out` pin on the VCO and drag it to the `In L` pin on the VCA.
- **Tempo Clock → ADSR**: Click the `Beat Trig` pin on the Tempo Clock and drag it to the `Gate In` pin on the ADSR.
- **ADSR → VCA**: Click the `Env Out` pin on the ADSR and drag it to the `Gain Mod` pin on the VCA.
- **VCA → Output**: Click the `Out L` pin on the VCA and drag it to the `In L` pin on the main **"Output"** node.

### 6. Make Sound!

Click the **"Play"** button in the main menu bar (or press **Spacebar**).

You should now hear a rhythmic synth beep!

- Try adjusting the **Attack**, **Decay**, and **Release** knobs on the ADSR module to change the shape of the sound.
- Try changing the **"Frequency"** on the VCO to change the pitch.

**Congratulations, you've made your first patch!**