# ASIO Setup Guide for Pikon Raditsz

## What is ASIO?
ASIO (Audio Stream Input/Output) is a computer sound card driver protocol for digital audio specified by Steinberg, providing a low-latency and high fidelity interface between a software application and a computer's sound card.

For Pikon Raditsz, ASIO is critical for:
- **Ultra-low latency** (down to 2-5ms)
- **Real-time video-to-audio synchronization**
- **Multi-channel audio input/output**
- **Professional audio interface support**

## Installing ASIO Drivers

### If you have an audio interface:
Most professional audio interfaces (Focusrite, Universal Audio, RME, PreSonus, Behringer, etc.) come with their own dedicated ASIO drivers.
1.  Go to your manufacturer's website.
2.  Download and install the latest drivers for your specific model.
3.  Restart your computer if prompted.

### If you DON'T have an audio interface:
If you are using your computer's built-in sound card (Realtek, etc.), you can use **ASIO4ALL**, a universal ASIO driver.
1.  Download ASIO4ALL from [www.asio4all.org](http://www.asio4all.org).
2.  Install it.
3.  It will appear as an ASIO device in Pikon Raditsz.

## Configuring ASIO in Pikon Raditsz

1.  Launch **Pikon Raditsz**.
2.  The application will attempt to initialize ASIO automatically on startup.
3.  If not automatically selected, go to **Options -> Audio Settings** (or similar menu).
4.  In the "Audio Device Type" dropdown, select **ASIO**.
5.  In the "Device" dropdown, select your specific ASIO driver (e.g., "Focusrite USB ASIO" or "ASIO4ALL v2").
6.  **Buffer Size**: We recommend setting this to **256 samples** or **128 samples** for the best balance between low latency and stability.
    - Lower = less latency, higher CPU usage.
    - Higher = more latency, safer against crackles/dropouts.

## Troubleshooting

### "No ASIO devices found"
- Ensure you have installed the ASIO drivers for your hardware.
- Restart the application.

### "Cannot open ASIO device"
- ASIO devices are often "exclusive mode". Make sure no other application (DAW, YouTube, Spotify) is using the ASIO driver at the same time.
- Check if the sample rate matches your system default (usually 44100 Hz or 48000 Hz).

### Crackling/Dropouts
- Increase the buffer size (e.g., from 128 to 256 or 512 samples).
- Close background applications.
- Ensure your laptop is plugged into power and set to "High Performance" mode.
