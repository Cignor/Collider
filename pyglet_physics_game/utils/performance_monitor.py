import time
import traceback
from typing import List

class PerformanceMonitor:
    """Monitors and tracks performance metrics - Performance Optimized"""
    
    def __init__(self, game):
        self.game = game
        
        # Performance flags
        self.debug_mode = getattr(game, 'debug_mode', False)
        self.verbose_logging = True  # Enable verbose logging for debugging
        
        # FPS monitoring
        self.frame_count = 0
        self.last_fps_time = time.time()
        self.fps_history = []
        self.current_fps = 0
        
        # Timing metrics
        self.update_times = []
        self.draw_times = []
        
        # Performance statistics
        self.optimization_stats = {
            'original_shapes': 0,
            'optimized_shapes': 0,
            'simplification_ratio': 0.0
        }
        
        # Collision shape tracking
        self.collision_shape_count = 0
        
        # Performance tuning
        self.stats_interval = 120  # Print stats every 120 frames instead of 60
        self.max_history = 30  # Reduce history size for performance
        
        if self.debug_mode:
            print("DEBUG: Performance monitor initialized - Performance Mode")
    
    def start_update(self):
        """Start timing an update cycle"""
        if self.verbose_logging:
            self.update_start_time = time.time()
    
    def end_update(self):
        """End timing an update cycle"""
        try:
            if self.verbose_logging:
                update_time = time.time() - self.update_start_time
                self.update_times.append(update_time)
                if len(self.update_times) > self.max_history:
                    self.update_times.pop(0)
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR ending update timing: {e}")
    
    def start_draw(self):
        """Start timing a draw cycle"""
        if self.verbose_logging:
            self.draw_start_time = time.time()
    
    def end_draw(self):
        """End timing a draw cycle"""
        try:
            if self.verbose_logging:
                draw_time = time.time() - self.draw_start_time
                self.draw_times.append(draw_time)
                if len(self.draw_times) > self.max_history:
                    self.draw_times.pop(0)
            
            # Update FPS counter
            self._update_fps()
            
            # Print timing info every N frames (reduced frequency for performance)
            if len(self.draw_times) >= self.stats_interval:
                self._print_performance_stats()
                
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR ending draw timing: {e}")
    
    def _update_fps(self):
        """Update FPS calculation"""
        try:
            self.frame_count += 1
            current_time = time.time()
            
            if current_time - self.last_fps_time >= 1.0:  # Every second
                self.current_fps = self.frame_count / (current_time - self.last_fps_time)
                self.fps_history.append(self.current_fps)
                
                if len(self.fps_history) > 5:  # Reduced from 10 to 5
                    self.fps_history.pop(0)
                
                self.frame_count = 0
                self.last_fps_time = current_time
                
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR updating FPS: {e}")
    
    def _print_performance_stats(self):
        """Print performance statistics"""
        try:
            if not self.verbose_logging:
                return
                
            avg_draw_time = sum(self.draw_times) / len(self.draw_times) if self.draw_times else 0
            avg_update_time = sum(self.update_times) / len(self.update_times) if self.update_times else 0
            
            print(f"DEBUG: Draw Time = {avg_draw_time*1000:.2f}ms, Update Time = {avg_update_time*1000:.2f}ms")
            print(f"DEBUG: FPS = {self.current_fps:.1f}, Physics Objects = {len(self.game.physics_objects)}, Collision Shapes = {len(self.game.collision_shapes)}")
            
            # Clear timing arrays after printing
            self.draw_times.clear()
            self.update_times.clear()
            
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR printing performance stats: {e}")
    
    def update_optimization_stats(self, original_points: int, optimized_shapes: int):
        """Update collision optimization statistics"""
        try:
            if original_points > 0:
                self.optimization_stats['original_shapes'] += original_points
                self.optimization_stats['optimized_shapes'] += optimized_shapes
                
                # Calculate overall simplification ratio
                total_original = self.optimization_stats['original_shapes']
                total_optimized = self.optimization_stats['optimized_shapes']
                if total_original > 0:
                    self.optimization_stats['simplification_ratio'] = (total_optimized / total_original) * 100
                
                if self.debug_mode:
                    print(f"DEBUG: Optimization stats updated - Original: {original_points}, Optimized: {optimized_shapes}")
                
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR updating optimization stats: {e}")
    
    def get_fps(self) -> float:
        """Get current FPS"""
        return self.current_fps
    
    def get_avg_fps(self) -> float:
        """Get average FPS over recent history"""
        if self.fps_history:
            return sum(self.fps_history) / len(self.fps_history)
        return 0.0
    
    def get_optimization_stats(self) -> dict:
        """Get optimization statistics"""
        return self.optimization_stats.copy()
    
    def get_last_update_time(self) -> float:
        """Get the last update time in seconds"""
        if self.update_times:
            return self.update_times[-1]
        return 0.0
    
    def get_last_draw_time(self) -> float:
        """Get the last draw time in seconds"""
        if self.draw_times:
            return self.draw_times[-1]
        return 0.0
    
    def get_performance_summary(self) -> dict:
        """Get comprehensive performance summary"""
        try:
            avg_draw_time = sum(self.draw_times) / len(self.draw_times) if self.draw_times else 0
            avg_update_time = sum(self.update_times) / len(self.update_times) if self.update_times else 0
            
            return {
                'fps': self.current_fps,
                'avg_fps': self.get_avg_fps(),
                'draw_time_ms': avg_draw_time * 1000,
                'update_time_ms': avg_update_time * 1000,
                'physics_objects': len(self.game.physics_objects),
                'collision_shapes': len(self.game.collision_shapes),
                'optimization_stats': self.optimization_stats.copy()
            }
        except Exception as e:
            if self.debug_mode:
                print(f"ERROR getting performance summary: {e}")
            return {}
    
    def set_verbose_logging(self, enabled: bool):
        """Enable or disable verbose logging for performance tuning"""
        self.verbose_logging = enabled
        if self.debug_mode:
            print(f"DEBUG: Verbose logging {'enabled' if enabled else 'disabled'}")
    
    def set_debug_mode(self, enabled: bool):
        """Enable or disable debug mode"""
        self.debug_mode = enabled
        if enabled:
            print("DEBUG: Performance monitor debug mode enabled")
