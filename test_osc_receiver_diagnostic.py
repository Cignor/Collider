#!/usr/bin/env python3
"""
Diagnostic OSC Receiver - Enhanced version with connection testing
"""

from pythonosc import osc_server
from pythonosc import dispatcher
import socket
import sys
import time
from datetime import datetime

LISTEN_PORT = 57123
LISTEN_ADDRESS = "0.0.0.0"  # Listen on all interfaces

message_count = 0
last_message_time = None

def print_osc_message(address, *args):
    """Print received OSC message with timestamp"""
    global message_count, last_message_time
    message_count += 1
    last_message_time = datetime.now()
    
    timestamp = last_message_time.strftime("%H:%M:%S.%f")[:-3]
    
    # Format the message nicely
    if args:
        values_str = ", ".join([f"{arg:.6f}" if isinstance(arg, float) else str(arg) for arg in args])
        print(f"[{timestamp}] #{message_count} | {address:30s} -> {values_str}")
    else:
        print(f"[{timestamp}] #{message_count} | {address:30s} (no arguments)")

def get_local_ip():
    """Get the local IP address"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def test_port_available():
    """Test if the port is available"""
    try:
        test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        test_socket.bind((LISTEN_ADDRESS, LISTEN_PORT))
        test_socket.close()
        return True
    except OSError:
        return False

def main():
    print("=" * 70)
    print("OSC Receiver - Diagnostic Mode")
    print("=" * 70)
    
    # Test port availability
    if not test_port_available():
        print(f"ERROR: Port {LISTEN_PORT} is not available!")
        print("       It may be in use by another application.")
        print("\nTroubleshooting:")
        print(f"  1. Check if another instance of this script is running")
        print(f"  2. Check if another OSC receiver is using port {LISTEN_PORT}")
        print(f"  3. Try changing LISTEN_PORT in this script")
        sys.exit(1)
    
    local_ip = get_local_ip()
    
    print(f"Configuration:")
    print(f"  Listen Address: {LISTEN_ADDRESS}")
    print(f"  Listen Port: {LISTEN_PORT}")
    if LISTEN_ADDRESS == "0.0.0.0":
        print(f"  Listening on ALL network interfaces")
        print(f"  Your local IP appears to be: {local_ip}")
    print("=" * 70)
    
    # Create dispatcher
    dispatcher_obj = dispatcher.Dispatcher()
    dispatcher_obj.map("*", print_osc_message)
    
    # Create and start server
    try:
        server = osc_server.ThreadingOSCUDPServer(
            (LISTEN_ADDRESS, LISTEN_PORT), dispatcher_obj
        )
        
        print("Server started successfully!")
        print("-" * 70)
        print(f"In your CV to OSC module, configure:")
        print(f"  Host: {local_ip}")
        print(f"  Port: {LISTEN_PORT}")
        print("-" * 70)
        print("Waiting for OSC messages...")
        print("(Press Ctrl+C to stop)\n")
        print("=" * 70)
        
        # Start server
        server.serve_forever()
    except OSError as e:
        print(f"ERROR: Could not start OSC server!")
        print(f"       {e}")
        print(f"\nPossible causes:")
        print(f"  - Port {LISTEN_PORT} is already in use")
        print(f"  - Permission denied (try running as administrator)")
        print(f"  - Firewall blocking UDP port {LISTEN_PORT}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n" + "=" * 70)
        print(f"Statistics:")
        print(f"  Total messages received: {message_count}")
        if last_message_time:
            print(f"  Last message at: {last_message_time.strftime('%H:%M:%S')}")
        print("=" * 70)
        print("Stopping OSC receiver...")
        server.shutdown()
        print("Stopped.")

if __name__ == "__main__":
    main()

