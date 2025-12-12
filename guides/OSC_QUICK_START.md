# OSC Quick Start Guide

## Overview

OSC (Open Sound Control) allows you to control your modular synth from external applications, mobile apps, or network devices.

## Step 1: Add an OSC Device (Receiver Port)

1. In the application, go to **Settings → OSC Device Manager...**
2. Click **"Add Device..."**
3. Enter:
   - **Device Name**: e.g., "TouchOSC" or "My Controller"
   - **Port**: e.g., `57120` (default) or any port 1024-65535
4. Click **"Add"** - the device will be automatically enabled
5. The device will appear in the list with a checkbox

## Step 2: Create an OSC CV Module

1. Add an **"OSC CV"** module to your patch (from MIDI → Controllers menu)
2. In the module UI:
   - **Source**: Select "All Sources" or a specific OSC device
   - **Pattern**: Enter the OSC address pattern you want to listen to (e.g., `/cv/pitch`)
3. Connect the outputs:
   - **Gate** → VCA Gate input
   - **Pitch CV** → VCO Pitch input
   - **Velocity** → VCA CV input (or anywhere you want velocity control)

## Step 3: Connect an External OSC Client

### Option A: Python Script (Test)

Create a file `test_osc.py`:

```python
import socket
import struct
import time

# OSC message format helper
def send_osc(ip, port, address, *args):
    # Build OSC message: address + type tags + arguments
    addr_bytes = address.encode('utf-8') + b'\x00'
    addr_bytes = addr_bytes + b'\x00' * ((4 - len(addr_bytes) % 4) % 4)
    
    type_tags = ',' + ''.join([
        'i' if isinstance(arg, int) else 'f'
        for arg in args
    ]) + '\x00'
    type_tags_bytes = type_tags.encode('utf-8')
    type_tags_bytes = type_tags_bytes + b'\x00' * ((4 - len(type_tags_bytes) % 4) % 4)
    
    args_bytes = b''
    for arg in args:
        if isinstance(arg, int):
            args_bytes += struct.pack('>i', arg)
        elif isinstance(arg, float):
            args_bytes += struct.pack('>f', arg)
    
    message = addr_bytes + type_tags_bytes + args_bytes
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(message, (ip, port))
    sock.close()

# Send to localhost on port 57120
IP = "127.0.0.1"
PORT = 57120

# Example 1: Send pitch CV (0.0 to 1.0 range)
send_osc(IP, PORT, "/cv/pitch", 0.6)  # Higher pitch
time.sleep(0.5)
send_osc(IP, PORT, "/cv/pitch", 0.4)  # Lower pitch

# Example 2: Send gate on/off
send_osc(IP, PORT, "/gate", 1.0)  # Gate ON
time.sleep(0.2)
send_osc(IP, PORT, "/gate", 0.0)  # Gate OFF

# Example 3: Send note on/off
send_osc(IP, PORT, "/synth/note/on", 60, 0.8)  # Note 60 (C4), velocity 0.8
time.sleep(0.5)
send_osc(IP, PORT, "/synth/note/off", 60)

# Example 4: Trigger (brief pulse)
send_osc(IP, PORT, "/trigger")

print("OSC messages sent!")
```

Run: `python test_osc.py`

### Option B: TouchOSC (Mobile App)

1. Install **TouchOSC** on your phone/tablet (iOS/Android)
2. In TouchOSC settings:
   - **Host**: Your computer's IP address (e.g., `192.168.1.100`)
   - **Port**: The port you configured (e.g., `57120`)
   - **Protocol**: OSC
3. Configure controls to send OSC messages:
   - **Slider** → `/cv/pitch` with range 0.0-1.0
   - **Button** → `/gate` with value 1.0 (press) / 0.0 (release)
   - **Toggle** → `/trigger` (sends trigger on toggle)

### Option C: Pure Data / Max/MSP

**Pure Data:**
```
[udpsend localhost 57120]

| 
[prepend /cv/pitch]
|
[f ]
|
[oscformat]
|
[udpsend]
```

**Max/MSP:**
- Use `udpsend` object: `udpsend localhost 57120`
- Send OSC messages like `/cv/pitch 0.5`

### Option D: Command Line (using `sendosc` or similar)

If you have `liblo-tools` installed (Linux):
```bash
sendosc 127.0.0.1 57120 /cv/pitch f 0.6
sendosc 127.0.0.1 57120 /gate f 1.0
sendosc 127.0.0.1 57120 /synth/note/on i 60 f 0.8
```

## Supported OSC Address Patterns

The **OSC CV** module supports these address patterns:

### Note Control
- `/synth/note/on {int32 note, float32 velocity}`
  - Example: `/synth/note/on 60 0.8` (Note 60 = C4, velocity 0.8)
  
- `/synth/note/off {int32 note}`
  - Example: `/synth/note/off 60`

### Direct CV Control
- `/cv/pitch {float32 value}`
  - Range: 0.0 to 1.0 (maps to pitch CV)
  - Example: `/cv/pitch 0.5`
  
- `/cv/velocity {float32 value}`
  - Range: 0.0 to 1.0
  - Example: `/cv/velocity 0.75`

### Gate/Trigger Control
- `/gate {float32 value}`
  - 1.0 = gate ON, 0.0 = gate OFF
  - Example: `/gate 1.0`
  
- `/trigger` (no arguments)
  - Sends a brief gate pulse (one audio block)
  - Example: `/trigger`

## Pattern Matching

The **Pattern** field in the OSC CV module supports wildcards:

- **Exact match**: `/cv/pitch` matches only `/cv/pitch`
- **Wildcard**: `/cv/*` matches `/cv/pitch`, `/cv/velocity`, `/cv/modwheel`, etc.
- **Single-level wildcard**: `/synth/note/*` matches `/synth/note/on`, `/synth/note/off`

## Monitoring Activity

1. **Status Bar**: Look at the top status bar - "OSC: [DeviceName]" appears in green when receiving messages
2. **Hover Tooltip**: Hover over the OSC indicator to see the last address received
3. **OSC Device Manager**: Open Settings → OSC Device Manager to see activity indicators for each device
4. **OSC CV Module**: The module displays current Gate, Pitch CV, and Velocity values

## Troubleshooting

### No activity showing?
1. Check the OSC Device Manager - is the device enabled (checkbox checked)?
2. Verify the port number matches (client sends to same port receiver listens on)
3. Check firewall settings (UDP port must be open)
4. For network devices, ensure they're on the same network or use `127.0.0.1` for localhost

### Messages not reaching the module?
1. Check the **Pattern** field matches the OSC address you're sending
2. Check the **Source** filter - try "All Sources" first
3. Look at the status bar OSC indicator - if it's showing activity, messages are arriving
4. Check the OSC CV module's Gate/Pitch/Velocity values - they should update if messages are matching

### Port already in use?
- The port number must be unique - try a different port (e.g., 57121, 8000, 9000)
- Common OSC ports: 57120 (SC default), 8000, 9000

## Example Patch

1. Add **OSC CV** module
2. Set Pattern to `/cv/pitch`
3. Connect **Pitch CV** output → **VCO** Pitch input
4. Connect **Gate** output → **VCA** Gate input
5. Send OSC message: `/cv/pitch 0.6` → VCO pitch should change
6. Send OSC message: `/gate 1.0` → VCA should open

## Next Steps

- Try different OSC controllers (TouchOSC, Lemur, Pure Data, etc.)
- Create custom OSC mappings for your workflow
- Use multiple OSC CV modules to receive different address patterns
- Combine with other modules (LFO, Envelope, etc.) for complex control


