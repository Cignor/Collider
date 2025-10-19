import traceback
from typing import List, Tuple, Dict
import array

class TrailSystem:
    """Ultra-optimized trail system for maximum performance"""
    
    def __init__(self, game):
        self.game = game
        
        # ULTRA-AGGRESSIVE performance settings
        self.max_trail_segments = 3  # Reduced to 3 for maximum performance
        self.update_frequency = 6  # Update trails every 6 frames for maximum performance
        self.frame_counter = 0
        
        # Performance flags - DISABLE ALL EXPENSIVE OPERATIONS
        self.enable_distance_validation = False  # Disable expensive distance checks
        self.enable_trail_validation = False  # Disable expensive trail validation
        
        # Pre-allocated data structures to avoid memory allocation
        self.trail_data = {}  # obj_id -> (x_array, y_array, count)
        self.object_positions = {}  # obj_id -> (x, y) for quick access
        
        # Pre-allocate arrays for maximum performance
        self._preallocate_trail_arrays()
        
        if getattr(game, 'debug_mode', False):
            print("DEBUG: ULTRA-AGGRESSIVE trail system initialized")
    
    def _integrate_object_with_trails(self, physics_obj):
        """Integrate a physics object with the trail system"""
        try:
            obj_id = id(physics_obj)
            
            # Check if object has bullet data and trails are enabled
            if hasattr(physics_obj, 'bullet_data') and physics_obj.bullet_data.trail_enabled:
                # Get position (handle both physics objects and sound bullets)
                if hasattr(physics_obj, 'body') and hasattr(physics_obj.body, 'position'):
                    # Physics object
                    pos_x, pos_y = physics_obj.body.position.x, physics_obj.body.position.y
                elif hasattr(physics_obj, 'x') and hasattr(physics_obj, 'y'):
                    # Sound bullet
                    pos_x, pos_y = physics_obj.x, physics_obj.y
                else:
                    return  # Skip objects without position data
                
                # Create trail entry for this object
                self.trail_data[obj_id] = (
                    self.x_array.copy(),
                    self.y_array.copy(),
                    1
                )
                # Set initial position
                self.trail_data[obj_id][0][0] = pos_x
                self.trail_data[obj_id][1][0] = pos_y
                # Store position for tracking
                self.object_positions[obj_id] = (pos_x, pos_y)
                
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR integrating object with trails: {e}")
    
    def _preallocate_trail_arrays(self):
        """Pre-allocate arrays to avoid memory allocation during runtime"""
        # Create reusable arrays for trail data
        self.x_array = array.array('f', [0.0] * self.max_trail_segments)
        self.y_array = array.array('f', [0.0] * self.max_trail_segments)
    
    def update(self, dt):
        """Update trail system - ULTRA-OPTIMIZED for maximum performance"""
        try:
            # Only update every N frames for performance
            self.frame_counter += 1
            if self.frame_counter % self.update_frequency != 0:
                return
            
            self._update_object_trails_ultra_fast()
            
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR updating trail system: {e}")
                traceback.print_exc()
    
    def _update_object_trails_ultra_fast(self):
        """ULTRA-FAST trail update - maximum performance, no validation"""
        try:
            # Get current objects once (both physics objects and sound bullets)
            current_objects = self.game.physics_objects + getattr(self.game, 'sound_bullets', [])
            
            # Skip if no objects (performance optimization)
            if not current_objects:
                return
            
            # Clean up trails for objects that no longer exist
            current_obj_ids = {id(obj) for obj in current_objects}
            
            # Remove dead trails
            dead_trails = [obj_id for obj_id in self.trail_data if obj_id not in current_obj_ids]
            for obj_id in dead_trails:
                del self.trail_data[obj_id]
                if obj_id in self.object_positions:
                    del self.object_positions[obj_id]
            
            # Update trails for existing objects - ULTRA-FAST path
            for obj in current_objects:
                obj_id = id(obj)
                
                # Get current position (handle both physics objects and sound bullets)
                if hasattr(obj, 'body') and hasattr(obj.body, 'position'):
                    # Physics object
                    current_x, current_y = obj.body.position.x, obj.body.position.y
                elif hasattr(obj, 'x') and hasattr(obj, 'y'):
                    # Sound bullet
                    current_x, current_y = obj.x, obj.y
                else:
                    continue  # Skip objects without position data
                
                # CRITICAL: Always check for gigantic movements to prevent big trails
                if obj_id in self.object_positions:
                    old_x, old_y = self.object_positions[obj_id]
                    dx = current_x - old_x
                    dy = current_y - old_y
                    distance_squared = dx*dx + dy*dy
                    
                    # Check for gigantic movements that would create huge trails
                    if distance_squared > 2500.0:  # 50 pixels threshold (reduced from 100)
                        # Object moved too far - clear its trail to prevent gigantic trails
                        if obj_id in self.trail_data:
                            del self.trail_data[obj_id]
                        self.object_positions[obj_id] = (current_x, current_y)
                        continue
                    
                    # Only update if movement is significant (performance optimization)
                    if distance_squared < 1.0:  # 1 pixel threshold for reasonable trail appearance
                        continue
                
                # Update stored position
                self.object_positions[obj_id] = (current_x, current_y)
                
                # Check if object has bullet data and should have trails
                if hasattr(obj, 'bullet_data') and obj.bullet_data.trail_enabled:
                    # ALWAYS create trail data immediately for new objects
                    if obj_id not in self.trail_data:
                        # Create new trail with pre-allocated arrays
                        x_array = array.array('f', [0.0] * self.max_trail_segments)
                        y_array = array.array('f', [0.0] * self.max_trail_segments)
                        x_array[0] = current_x
                        y_array[0] = current_y
                        self.trail_data[obj_id] = (x_array, y_array, 1)
                    else:
                        # Update existing trail - ULTRA-FAST path
                        x_array, y_array, count = self.trail_data[obj_id]
                        
                        # Use bullet data for trail segments
                        max_segments = min(obj.bullet_data.trail_segments, self.max_trail_segments)
                        
                        # ULTRA-FAST: Just shift and update without any validation
                        for i in range(max_segments - 1, 0, -1):
                            x_array[i] = x_array[i-1]
                            y_array[i] = y_array[i-1]
                        
                        x_array[0] = current_x
                        y_array[0] = current_y
                        count = min(count + 1, max_segments)
                        
                        self.trail_data[obj_id] = (x_array, y_array, count)
                else:
                    # For objects without bullet data, integrate them immediately
                    if obj_id not in self.trail_data:
                        self._integrate_object_with_trails(obj)
                    
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR in ultra-fast trail update: {e}")
                traceback.print_exc()
    
    def get_trail_data(self, obj_id):
        """Get trail data for rendering - returns (x_array, y_array, count)"""
        return self.trail_data.get(obj_id, (None, None, 0))
    
    def get_all_trails(self) -> Dict:
        """Get all trail data for rendering - optimized to avoid copying"""
        return self.trail_data
    
    def clear_trail(self, obj_id):
        """Clear trail for a specific object"""
        try:
            if obj_id in self.trail_data:
                del self.trail_data[obj_id]
            if obj_id in self.object_positions:
                del self.object_positions[obj_id]
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR clearing trail: {e}")
    
    def clear_all_trails(self):
        """Clear all object trails"""
        try:
            self.trail_data.clear()
            self.object_positions.clear()
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR clearing all trails: {e}")
    
    def set_trail_segments(self, segments: int):
        """Set the number of trail segments to keep"""
        try:
            self.max_trail_segments = max(1, min(segments, 5))  # Cap at 5 for performance
            self._preallocate_trail_arrays()
        except Exception as e:
            if getattr(self.game, 'debug_mode', False):
                print(f"ERROR setting trail segments: {e}")
    
    def set_update_frequency(self, frequency: int):
        """Set how often trails update (higher = better performance)"""
        self.update_frequency = max(1, frequency)
