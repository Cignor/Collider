#!/usr/bin/env python3
"""
Game Runner Script
Always runs the Pyglet Physics Game from the correct location
"""

import os
import sys
import subprocess

def main():
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    # The game is in the pyglet_physics_game subdirectory
    game_dir = os.path.join(script_dir, 'pyglet_physics_game')
    
    # Check if the game directory exists
    if not os.path.exists(game_dir):
        print(f"ERROR: Game directory not found at {game_dir}")
        print("Please ensure you're running this script from the project root directory.")
        sys.exit(1)
    
    # Check if main.py exists in the game directory
    main_py = os.path.join(game_dir, 'main.py')
    if not os.path.exists(main_py):
        print(f"ERROR: main.py not found at {main_py}")
        sys.exit(1)
    
    # Run the game as a module from project root so package imports work
    print(f"Running game from: {script_dir}")
    print("Starting Pyglet Physics Game...")
    
    try:
        env = os.environ.copy()
        # Ensure project root is on PYTHONPATH
        env['PYTHONPATH'] = script_dir + (os.pathsep + env['PYTHONPATH'] if 'PYTHONPATH' in env and env['PYTHONPATH'] else '')
        subprocess.run([sys.executable, '-m', 'pyglet_physics_game.main'], check=True, cwd=script_dir, env=env)
    except subprocess.CalledProcessError as e:
        print(f"Game exited with error code: {e.returncode}")
        sys.exit(e.returncode)
    except KeyboardInterrupt:
        print("\nGame interrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"Error running game: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
