"""
Modular HUD System
Clean, modular HUD using separate components
"""

from .audio_hud import AudioHUD
from .tool_hud import ToolHUD
from .physics_hud import PhysicsHUD

class ModularHUD:
    """Main HUD coordinator using modular components"""
    
    def __init__(self, game):
        self.game = game
        self.coordinate_manager = None  # Will be set by game core
        
        # Initialize modular components
        self.audio_hud = AudioHUD(game)
        self.tool_hud = ToolHUD(game)
        self.physics_hud = PhysicsHUD(game)
    
    def set_coordinate_manager(self, coordinate_manager):
        """Set the coordinate manager for all HUD components"""
        self.coordinate_manager = coordinate_manager
        self.audio_hud.coordinate_manager = coordinate_manager
        self.tool_hud.coordinate_manager = coordinate_manager
        self.physics_hud.coordinate_manager = coordinate_manager
        
    def draw(self):
        """Draw all HUD components"""
        try:
            # Grid system is now handled by the renderer for proper depth sorting
            # No need to draw it here anymore
            
            # Draw modular components
            self.audio_hud.draw()
            self.tool_hud.draw()
            self.physics_hud.draw()
            
        except Exception as e:
            print(f"ERROR drawing modular HUD: {e}")
            import traceback
            traceback.print_exc()
