#!/usr/bin/env python3
"""
Pyglet Physics Game - Main Entry Point
A modular physics game with interactive tools and particle systems
"""

import traceback
from pyglet_physics_game.game.game_core import PhysicsGame

def main():
    """Main entry point"""
    try:
        print("Starting Pyglet Physics Game...")
        game = PhysicsGame()
        print("Game initialized successfully. Starting game loop...")
        
        # Start the game loop
        import pyglet
        pyglet.app.run()
        
    except Exception as e:
        print(f"FATAL ERROR in main: {e}")
        traceback.print_exc()
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())
