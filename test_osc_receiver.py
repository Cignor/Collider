#!/usr/bin/env python3
"""
Simple OSC Receiver for Testing CV to OSC Sender Module
Listens on all interfaces and displays all incoming OSC messages
"""

from pythonosc import osc_server
from pythonosc import dispatcher
import socket
import sys

# Configuration - CHANGE THESE TO MATCH YOUR MODULE SETTINGS
LISTEN_PORT = 57123

# To listen on all interfaces (recommended), use "0.0.0.0"
# To listen only on a specific interface, use that IP (e.g., "192.168.0.165")
LISTEN_ADDRESS = "0.0.0.0"  # This will receive on all network interfaces

def print_osc_message(address, *args):
    """Print received OSC message"""
    # Format the message nicely
    if args:
        values_str = ", ".join([str(arg) for arg in args])
        print(f"[OSC] {address} -> {values_str}")
    else:
        print(f"[OSC] {address} (no arguments)")

def get_local_ip():
    """Get the local IP address"""
    try:
        # Connect to a remote address (doesn't actually connect)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def main():
    # Create dispatcher
    dispatcher_obj = dispatcher.Dispatcher()
    
    # Map all addresses to the print function (catch-all handler)
    dispatcher_obj.map("*", print_osc_message)
    
    # Create server
    try:
        server = osc_server.ThreadingOSCUDPServer(
            (LISTEN_ADDRESS, LISTEN_PORT), dispatcher_obj
        )
        
        local_ip = get_local_ip()
        
        print("=" * 60)
        print("OSC Receiver started!")
        print("=" * 60)
        print(f"Listening on: {LISTEN_ADDRESS}:{LISTEN_PORT}")
        if LISTEN_ADDRESS == "0.0.0.0":
            print(f"  (This means listening on ALL interfaces)")
            print(f"  Your local IP appears to be: {local_ip}")
        print("=" * 60)
        print("Waiting for OSC messages...")
        print("Press Ctrl+C to stop\n")
        print("-" * 60)
        
        # Start server
        server.serve_forever()
    except OSError as e:
        print(f"ERROR: Could not start OSC server on port {LISTEN_PORT}")
        print(f"       {e}")
        print(f"\nPossible causes:")
        print(f"  - Port {LISTEN_PORT} is already in use")
        print(f"  - Permission denied (try running as administrator)")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n" + "-" * 60)
        print("Stopping OSC receiver...")
        server.shutdown()
        print("Stopped.")

if __name__ == "__main__":
    main()

