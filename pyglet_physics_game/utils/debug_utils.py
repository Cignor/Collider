import traceback
import time
from typing import Any, Dict, List

class DebugUtils:
    """Utility functions for debugging and error reporting"""
    
    def __init__(self, game):
        self.game = game
        self.debug_enabled = True
        self.error_log = []
        self.max_error_log_size = 100
        
        print("DEBUG: Debug utilities initialized")
    
    def log_error(self, error: Exception, context: str = ""):
        """Log an error with context"""
        try:
            if self.debug_enabled:
                error_entry = {
                    'timestamp': time.time(),
                    'error_type': type(error).__name__,
                    'error_message': str(error),
                    'context': context,
                    'traceback': traceback.format_exc()
                }
                
                self.error_log.append(error_entry)
                
                # Keep log size manageable
                if len(self.error_log) > self.max_error_log_size:
                    self.error_log.pop(0)
                
                print(f"ERROR in {context}: {error}")
                if self.debug_enabled:
                    traceback.print_exc()
                    
        except Exception as e:
            print(f"ERROR logging error: {e}")
    
    def log_debug(self, message: str, data: Any = None):
        """Log a debug message"""
        try:
            if self.debug_enabled:
                timestamp = time.strftime("%H:%M:%S", time.localtime())
                debug_msg = f"[{timestamp}] DEBUG: {message}"
                
                if data is not None:
                    debug_msg += f" - Data: {data}"
                
                print(debug_msg)
                
        except Exception as e:
            print(f"ERROR logging debug message: {e}")
    
    def log_warning(self, message: str, data: Any = None):
        """Log a warning message"""
        try:
            timestamp = time.strftime("%H:%M:%S", time.localtime())
            warning_msg = f"[{timestamp}] WARNING: {message}"
            
            if data is not None:
                warning_msg += f" - Data: {data}"
            
            print(warning_msg)
            
        except Exception as e:
            print(f"ERROR logging warning message: {e}")
    
    def get_error_log(self) -> List[Dict]:
        """Get the error log"""
        return self.error_log.copy()
    
    def clear_error_log(self):
        """Clear the error log"""
        try:
            self.error_log.clear()
            print("DEBUG: Error log cleared")
        except Exception as e:
            print(f"ERROR clearing error log: {e}")
    
    def set_debug_enabled(self, enabled: bool):
        """Enable or disable debug output"""
        try:
            self.debug_enabled = enabled
            print(f"DEBUG: Debug output {'enabled' if enabled else 'disabled'}")
        except Exception as e:
            print(f"ERROR setting debug enabled: {e}")
    
    def print_game_state(self):
        """Print current game state for debugging"""
        try:
            if not self.debug_enabled:
                return
            
            print("\n=== GAME STATE DEBUG INFO ===")
            print(f"Window size: {self.game.width} x {self.game.height}")
            print(f"Physics objects: {len(self.game.physics_objects)}")
            print(f"Collision shapes: {len(self.game.collision_shapes)}")
            print(f"Current tool: {self.game.tools[self.game.current_tool_index].name if self.game.tools else 'None'}")
            print(f"Gravity: {self.game.current_gravity}")
            print(f"Wind: {self.game.current_wind_strength} @ {self.game.wind_direction}")
            print(f"Space held: {self.game.space_held}")
            print(f"Erase tool active: {self.game.erase_tool_active}")
            print("==============================\n")
            
        except Exception as e:
            print(f"ERROR printing game state: {e}")
    
    def print_performance_info(self):
        """Print performance information for debugging"""
        try:
            if not self.debug_enabled:
                return
            
            perf_summary = self.game.performance_monitor.get_performance_summary()
            
            print("\n=== PERFORMANCE DEBUG INFO ===")
            print(f"FPS: {perf_summary.get('fps', 0):.1f}")
            print(f"Avg FPS: {perf_summary.get('avg_fps', 0):.1f}")
            print(f"Draw time: {perf_summary.get('draw_time_ms', 0):.2f}ms")
            print(f"Update time: {perf_summary.get('update_time_ms', 0):.2f}ms")
            print(f"Physics objects: {perf_summary.get('physics_objects', 0)}")
            print(f"Collision shapes: {perf_summary.get('collision_shapes', 0)}")
            
            opt_stats = perf_summary.get('optimization_stats', {})
            print(f"Original shapes: {opt_stats.get('original_shapes', 0)}")
            print(f"Optimized shapes: {opt_stats.get('optimized_shapes', 0)}")
            print(f"Simplification ratio: {opt_stats.get('simplification_ratio', 0):.2f}%")
            print("==============================\n")
            
        except Exception as e:
            print(f"ERROR printing performance info: {e}")
    
    def validate_game_state(self) -> bool:
        """Validate the current game state for consistency"""
        try:
            if not self.debug_enabled:
                return True
            
            errors_found = []
            
            # Check physics objects consistency
            for obj in self.game.physics_objects:
                if not hasattr(obj, 'body') or not obj.body:
                    errors_found.append(f"Physics object missing body: {obj}")
                elif not hasattr(obj, 'shape') or not obj.shape:
                    errors_found.append(f"Physics object missing shape: {obj}")
            
            # Check collision shapes consistency
            for shape in self.game.collision_shapes:
                if not hasattr(shape, 'a') or not hasattr(shape, 'b'):
                    errors_found.append(f"Collision shape missing endpoints: {shape}")
            
            # Check tools consistency
            if not self.game.tools:
                errors_found.append("No tools available")
            elif self.game.current_tool_index >= len(self.game.tools):
                errors_found.append(f"Current tool index out of range: {self.game.current_tool_index}")
            
            if errors_found:
                print("=== GAME STATE VALIDATION ERRORS ===")
                for error in errors_found:
                    print(f"ERROR: {error}")
                print("=====================================")
                return False
            
            return True
            
        except Exception as e:
            print(f"ERROR validating game state: {e}")
            return False
