import pyglet
import pymunk
import math
import traceback
from typing import List, Tuple, Optional
from pyglet import shapes
from pyglet import text

# Import our modules
# from .stroke_optimizer import StrokeOptimizer  # Implemented inline optimization instead

class PhysicsTool:
    """Base class for physics tools using Pyglet"""
    def __init__(self, name: str, color: Tuple[int, int, int], icon: str, category: str = "Physics"):
        self.name = name
        self.color = color
        self.icon = icon
        self.category = category
        self.active = False
        
    def activate(self):
        """Activate the tool"""
        self.active = True
        
    def deactivate(self):
        """Deactivate the tool"""
        self.active = False
        
    def draw_icon(self, x: int, y: int, batch):
        """Draw tool icon"""
        label = text.Label(self.icon, font_size=16, x=x, y=y, 
                          color=self.color, batch=batch)
        if self.active:
            # Draw selection box
            box = shapes.Rectangle(x-5, y-5, 30, 30, color=self.color, batch=batch)
            box.opacity = 100
            
    def draw_icon_direct(self, x: int, y: int):
        """Draw tool icon directly"""
        label = text.Label(self.icon, font_size=16, x=x, y=y, color=self.color)
        label.draw()
        if self.active:
            # Draw selection box
            box = shapes.Rectangle(x-5, y-5, 30, 30, color=self.color)
            box.opacity = 100
            box.draw()

class CollisionTool(PhysicsTool):
    """Tool for creating collision boundaries"""
    def __init__(self):
        super().__init__("Collision", (255, 0, 0), "C", "Boundary")
        self.drawing = False
        self.start_pos = None
        self.end_pos = None
        self.thickness = 8
        
    def start_draw(self, pos: Tuple[int, int]):
        """Start drawing collision boundary"""
        print(f"DEBUG: CollisionTool.start_draw called with pos={pos}")
        try:
            self.drawing = True
            self.start_pos = pos
            print(f"DEBUG: CollisionTool drawing started at {pos}")
        except Exception as e:
            print(f"ERROR in CollisionTool.start_draw: {e}")
            traceback.print_exc()
            raise
        
    def update_draw(self, pos: Tuple[int, int]):
        """Update drawing preview"""
        print(f"DEBUG: CollisionTool.update_draw called with pos={pos}")
        try:
            if self.drawing:
                self.end_pos = pos
                print(f"DEBUG: CollisionTool preview updated to {pos}")
        except Exception as e:
            print(f"ERROR in CollisionTool.update_draw: {e}")
            traceback.print_exc()
            raise
        
    def finish_draw(self, space: pymunk.Space) -> Optional[pymunk.Shape]:
        """Finish drawing and create collision boundary"""
        print("DEBUG: CollisionTool.finish_draw called")
        try:
            if not self.drawing or not self.start_pos or not self.end_pos:
                print("DEBUG: CollisionTool not drawing or missing positions")
                self.drawing = False
                self.start_pos = None
                self.end_pos = None
                return None
                
            print(f"DEBUG: Creating collision boundary from {self.start_pos} to {self.end_pos}")
            
            # Create static body for collision boundary
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            print("DEBUG: Created static body")
            
            # Create line shape
            start_vec = pymunk.Vec2d(self.start_pos[0], self.start_pos[1])
            end_vec = pymunk.Vec2d(self.end_pos[0], self.end_pos[1])
            shape = pymunk.Segment(body, start_vec, end_vec, self.thickness/2)  # radius = thickness/2
            print("DEBUG: Created segment shape")
            
            shape.friction = 0.7
            shape.elasticity = 0.5
            shape.collision_type = 1
            print("DEBUG: Set shape properties")
            
            space.add(body, shape)
            print("DEBUG: Added body and shape to space")
            
            self.drawing = False
            self.start_pos = None
            self.end_pos = None
            print("DEBUG: Reset drawing state")
            
            print("DEBUG: CollisionTool.finish_draw completed successfully")
            return shape
            
        except Exception as e:
            print(f"ERROR in CollisionTool.finish_draw: {e}")
            traceback.print_exc()
            raise
        
    def draw_preview(self, batch):
        """Draw preview of collision boundary"""
        print("DEBUG: CollisionTool.draw_preview called")
        try:
            if self.drawing and self.start_pos and self.end_pos:
                print(f"DEBUG: Drawing preview line from {self.start_pos} to {self.end_pos}")
                line = shapes.Line(self.start_pos[0], self.start_pos[1], 
                                  self.end_pos[0], self.end_pos[1], 
                                  color=self.color, batch=batch)
                line.opacity = 150
                print("DEBUG: Preview line drawn successfully")
            else:
                print(f"DEBUG: Not drawing preview - drawing={self.drawing}, start_pos={self.start_pos}, end_pos={self.end_pos}")
        except Exception as e:
            print(f"ERROR in CollisionTool.draw_preview: {e}")
            traceback.print_exc()
            raise

    def draw_preview_direct(self):
        """Draw preview of collision boundary directly"""
        try:
            if self.drawing and self.start_pos and self.end_pos:
                print(f"DEBUG: Drawing preview line directly from {self.start_pos} to {self.end_pos}")
                line = shapes.Line(self.start_pos[0], self.start_pos[1], 
                                  self.end_pos[0], self.end_pos[1], 
                                  color=self.color)
                line.opacity = 150
                line.draw()
                print("DEBUG: Preview line drawn directly successfully")
            else:
                print(f"DEBUG: Not drawing preview directly - drawing={self.drawing}, start_pos={self.start_pos}, end_pos={self.end_pos}")
        except Exception as e:
            print(f"ERROR in CollisionTool.draw_preview_direct: {e}")
            traceback.print_exc()

class WindTool(PhysicsTool):
    """Tool for creating wind zones"""
    def __init__(self):
        super().__init__("Wind", (0, 255, 255), "W", "Force")
        self.wind_zones = []
        self.wind_strength = 1000
        self.wind_radius = 100
        
    def create_wind_zone(self, pos: Tuple[int, int], direction: float):
        """Create a wind zone at position with direction"""
        wind_zone = {
            'pos': pos,
            'direction': direction,
            'strength': self.wind_strength,
            'radius': self.wind_radius
        }
        self.wind_zones.append(wind_zone)
        
    def apply_wind(self, space: pymunk.Space):
        """Apply wind forces to objects in wind zones"""
        for zone in self.wind_zones:
            for body in space.bodies:
                if body.body_type == pymunk.Body.DYNAMIC:
                    # Calculate distance to wind zone
                    body_pos = body.position
                    zone_pos = pymunk.Vec2d(zone['pos'][0], zone['pos'][1])
                    distance = body_pos.get_distance(zone_pos)
                    
                    if distance < zone['radius']:
                        # Calculate wind force based on distance
                        force_factor = 1.0 - (distance / zone['radius'])
                        wind_force = zone['strength'] * force_factor
                        
                        # Apply force in wind direction
                        force_x = math.cos(zone['direction']) * wind_force
                        force_y = math.sin(zone['direction']) * wind_force
                        
                        body.apply_force_at_local_point((force_x, force_y), (0, 0))
                        
    def draw_wind_zones(self, batch):
        """Draw wind zones on screen"""
        for zone in self.wind_zones:
            # Draw wind zone circle
            circle = shapes.Circle(zone['pos'][0], zone['pos'][1], zone['radius'], 
                                 color=self.color, batch=batch)
            circle.opacity = 50
            
            # Draw wind direction arrow
            end_x = zone['pos'][0] + math.cos(zone['direction']) * 50
            end_y = zone['pos'][1] + math.sin(zone['direction']) * 50
            arrow = shapes.Line(zone['pos'][0], zone['pos'][1], end_x, end_y, 
                               color=self.color, batch=batch)
            
            # Draw wind strength indicator
            strength_text = f"{zone['strength']:.0f}"
            label = text.Label(strength_text, font_size=16, 
                              x=zone['pos'][0] - 20, y=zone['pos'][1] - 10,
                              color=self.color, batch=batch)
                              
    def draw_wind_zones_direct(self):
        """Draw wind zones on screen directly"""
        for zone in self.wind_zones:
            # Draw wind zone circle
            circle = shapes.Circle(zone['pos'][0], zone['pos'][1], zone['radius'], 
                                 color=self.color)
            circle.opacity = 50
            circle.draw()
            
            # Draw wind direction arrow
            end_x = zone['pos'][0] + math.cos(zone['direction']) * 50
            end_y = zone['pos'][1] + math.sin(zone['direction']) * 50
            arrow = shapes.Line(zone['pos'][0], zone['pos'][1], end_x, end_y, 
                               color=self.color)
            arrow.draw()
            
            # Draw wind strength indicator
            strength_text = f"{zone['strength']:.0f}"
            label = text.Label(strength_text, font_size=16, 
                              x=zone['pos'][0] - 20, y=zone['pos'][1] - 10,
                              color=self.color)
            label.draw()

class MagnetTool(PhysicsTool):
    """Tool for creating magnetic attraction/repulsion zones"""
    def __init__(self):
        super().__init__("Magnet", (100, 100, 255), "M", "Force")
        self.magnet_zones = []
        self.magnet_strength = 1000
        self.magnet_radius = 80
        
    def create_magnet_zone(self, pos: Tuple[int, int], attraction: bool = True):
        """Create a magnet zone (attraction or repulsion)"""
        magnet_zone = {
            'pos': pos,
            'attraction': attraction,
            'strength': self.magnet_strength,
            'radius': self.magnet_radius
        }
        self.magnet_zones.append(magnet_zone)
        
    def apply_magnetic_forces(self, space: pymunk.Space):
        """Apply magnetic forces to objects"""
        for zone in self.magnet_zones:
            for body in space.bodies:
                if body.body_type == pymunk.Body.DYNAMIC:
                    body_pos = body.position
                    zone_pos = pymunk.Vec2d(zone['pos'][0], zone['pos'][1])
                    distance = body_pos.get_distance(zone_pos)
                    
                    if distance < zone['radius'] and distance > 5:
                        # Calculate force based on distance
                        force_factor = 1.0 - (distance / zone['radius'])
                        magnetic_force = zone['strength'] * force_factor
                        
                        # Calculate direction (towards or away from magnet)
                        direction = (zone_pos - body_pos).normalized()
                        if not zone['attraction']:
                            direction = -direction
                            
                        # Apply force
                        force_x = direction.x * magnetic_force
                        force_y = direction.y * magnetic_force
                        body.apply_force_at_local_point((force_x, force_y), (0, 0))
                        
    def draw_magnet_zones(self, batch):
        """Draw magnet zones on screen"""
        for zone in self.magnet_zones:
            color = (0, 255, 0) if zone['attraction'] else (255, 0, 0)
            # Draw magnet zone
            circle = shapes.Circle(zone['pos'][0], zone['pos'][1], zone['radius'], 
                                 color=color, batch=batch)
            circle.opacity = 50
            
            # Draw attraction/repulsion indicator
            symbol = "+" if zone['attraction'] else "-"
            label = text.Label(symbol, font_size=24, 
                              x=zone['pos'][0] - 8, y=zone['pos'][1] - 12,
                              color=color, batch=batch)
                              
    def draw_magnet_zones_direct(self):
        """Draw magnet zones on screen directly"""
        for zone in self.magnet_zones:
            color = (0, 255, 0) if zone['attraction'] else (255, 0, 0)
            # Draw magnet zone
            circle = shapes.Circle(zone['pos'][0], zone['pos'][1], zone['radius'], 
                                 color=color)
            circle.opacity = 50
            circle.draw()
            
            # Draw attraction/repulsion indicator
            symbol = "+" if zone['attraction'] else "-"
            label = text.Label(symbol, font_size=24, 
                              x=zone['pos'][0] - 8, y=zone['pos'][1] - 12,
                              color=color)
            label.draw()

class TeleporterTool(PhysicsTool):
    """Tool for creating teleportation zones"""
    def __init__(self):
        super().__init__("Teleporter", (255, 0, 255), "T", "Transport")
        self.teleport_pairs = []
        self.teleport_radius = 60
        
    def create_teleport_pair(self, pos1: Tuple[int, int], pos2: Tuple[int, int]):
        """Create a teleporter pair"""
        teleport_pair = {
            'pos1': pos1,
            'pos2': pos2,
            'radius': self.teleport_radius
        }
        self.teleport_pairs.append(teleport_pair)
        
    def check_teleportation(self, space: pymunk.Space):
        """Check and perform teleportation"""
        for pair in self.teleport_pairs:
            for body in space.bodies:
                if body.body_type == pymunk.Body.DYNAMIC:
                    body_pos = body.position
                    pos1 = pymunk.Vec2d(pair['pos1'][0], pair['pos1'][1])
                    pos2 = pymunk.Vec2d(pair['pos2'][0], pair['pos2'][1])
                    
                    # Check if object is in teleporter 1
                    if body_pos.get_distance(pos1) < pair['radius']:
                        # Teleport to position 2
                        body.position = pos2
                        print(f"DEBUG: Object teleported from {pos1} to {pos2}")
                        
                    # Check if object is in teleporter 2
                    elif body_pos.get_distance(pos2) < pair['radius']:
                        # Teleport to position 1
                        body.position = pos1
                        print(f"DEBUG: Object teleported from {pos2} to {pos1}")
                        
    def draw_teleporters(self, batch):
        """Draw teleporter zones"""
        for pair in self.teleport_pairs:
            # Draw teleporter 1
            circle1 = shapes.Circle(pair['pos1'][0], pair['pos1'][1], pair['radius'], 
                                   color=self.color, batch=batch)
            circle1.opacity = 80
            
            # Draw teleporter 2
            circle2 = shapes.Circle(pair['pos2'][0], pair['pos2'][1], pair['radius'], 
                                   color=self.color, batch=batch)
            circle2.opacity = 80
            
            # Draw connection line
            line = shapes.Line(pair['pos1'][0], pair['pos1'][1], 
                              pair['pos2'][0], pair['pos2'][1], 
                              color=self.color, batch=batch)
            line.opacity = 150
            
    def draw_teleporters_direct(self):
        """Draw teleporter zones directly"""
        for pair in self.teleport_pairs:
            # Draw teleporter 1
            circle1 = shapes.Circle(pair['pos1'][0], pair['pos1'][1], pair['radius'], 
                                   color=self.color)
            circle1.opacity = 80
            circle1.draw()
            
            # Draw teleporter 2
            circle2 = shapes.Circle(pair['pos2'][0], pair['pos2'][1], pair['radius'], 
                                   color=self.color)
            circle2.opacity = 80
            circle2.draw()
            
            # Draw connection line
            line = shapes.Line(pair['pos1'][0], pair['pos1'][1], 
                              pair['pos2'][0], pair['pos2'][1], 
                              color=self.color)
            line.opacity = 150
            line.draw()

class FreeDrawTool(PhysicsTool):
    """Tool for freehand drawing collision boundaries"""
    def __init__(self):
        super().__init__("FreeDraw", (255, 150, 50), "F", "Boundary")
        self.drawing = False
        self.points = []
        self.thickness = 8
        self.min_distance = 10  # Minimum distance between points
        
    def start_draw(self, pos: Tuple[int, int]):
        """Start freehand drawing"""
        self.drawing = True
        self.points = [pos]
        
    def update_draw(self, pos: Tuple[int, int]):
        """Update drawing with new point"""
        if self.drawing:
            # Only add point if it's far enough from last point
            if not self.points or self._distance(pos, self.points[-1]) > self.min_distance:
                self.points.append(pos)
                
    def finish_draw(self, space: pymunk.Space) -> Optional[List[pymunk.Shape]]:
        """Finish drawing and create optimized collision boundaries"""
        if not self.drawing or len(self.points) < 2:
            self.drawing = False
            self.points = []
            return None
        
        print(f"DEBUG: Creating optimized collision shapes from {len(self.points)} points")
        
        # Simple optimization: reduce points by skipping some based on distance
        optimized_points = self._optimize_points(self.points)
        print(f"DEBUG: Optimized {len(self.points)} points to {len(optimized_points)} points")
        
        shapes_list = []
        # Create line segments between consecutive optimized points
        for i in range(len(optimized_points) - 1):
            start = optimized_points[i]
            end = optimized_points[i + 1]
            
            # Create static body for collision boundary
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            
            # Calculate line segment
            start_vec = pymunk.Vec2d(start[0], start[1])
            end_vec = pymunk.Vec2d(end[0], end[1])
            
            # Create line shape
            shape = pymunk.Segment(body, start_vec, end_vec, self.thickness/2)  # radius = thickness/2
            shape.friction = 0.7
            shape.elasticity = 0.5
            shape.collision_type = 1
            
            # Don't add to space here - let the collision system handle it
            # space.add(body, shape)
            shapes_list.append(shape)
        
        print(f"DEBUG: Created {len(shapes_list)} optimized collision shapes")
        
        self.drawing = False
        self.points = []
        
        return shapes_list
    
    def _optimize_points(self, points: List[Tuple[int, int]]) -> List[Tuple[int, int]]:
        """Ultra-aggressive point optimization - keep minimal points for maximum performance"""
        if len(points) <= 2:
            return points
        
        # Keep start point
        optimized = [points[0]]
        
        # Ultra-aggressive: aim for only 3-4 points maximum for complex paths
        # This will give you the 200+ FPS performance you had before
        step = max(1, len(points) // 4)  # Reduced from 6 to 4 for maximum performance
        
        for i in range(step, len(points) - 1, step):
            optimized.append(points[i])
        
        # Always keep the end point
        if points[-1] != optimized[-1]:
            optimized.append(points[-1])
        
        print(f"DEBUG: ULTRA-AGGRESSIVE optimization: {len(points)} -> {len(optimized)} points")
        return optimized
        
    def _distance(self, pos1: Tuple[int, int], pos2: Tuple[int, int]) -> float:
        """Calculate distance between two points"""
        return math.sqrt((pos1[0] - pos2[0])**2 + (pos1[1] - pos2[1])**2)
        
    def draw_preview(self, batch):
        """Draw preview of freehand drawing"""
        if self.drawing and len(self.points) > 1:
            print(f"DEBUG: Drawing preview with {len(self.points)} points")
            # Draw the path so far
            for i in range(len(self.points) - 1):
                start = self.points[i]
                end = self.points[i + 1]
                
                # Draw main line
                line = shapes.Line(start[0], start[1], end[0], end[1], 
                                  color=self.color, batch=batch)
                line.opacity = 200
                
                # Draw thicker background line for better visibility
                from pyglet_physics_game.ui.color_manager import get_color_manager
                color_mgr = get_color_manager()
                bg_line = shapes.Line(start[0], start[1], end[0], end[1], 
                                     color=color_mgr.debug_text, batch=batch)
                bg_line.opacity = 150
                
            # Draw current point
            if self.points:
                current_pos = self.points[-1]
                
                circle = shapes.Circle(current_pos[0], current_pos[1], 8, 
                                     color=color_mgr.brush_radius, batch=batch)
                circle.opacity = 200
                
                # Draw border for current point
                border = shapes.Circle(current_pos[0], current_pos[1], 8, 
                                      color=color_mgr.debug_text, batch=batch)
                border.opacity = 150
                
            print(f"DEBUG: Preview drawn successfully")
            
    def draw_preview_direct(self):
        """Draw preview of freehand drawing directly"""
        if self.drawing and len(self.points) > 1:
            print(f"DEBUG: Drawing preview with {len(self.points)} points")
            
            # Draw the path with simple circles
            for i in range(len(self.points) - 1):
                start = self.points[i]
                end = self.points[i + 1]
                
                # Draw simple line with circles
                thickness = self.thickness
                dx = end[0] - start[0]
                dy = end[1] - start[1]
                length = math.sqrt(dx*dx + dy*dy)
                
                if length > 0:
                    # Draw a few circles along the line
                    num_circles = max(2, min(4, int(length / (thickness / 2))))
                    
                    for j in range(num_circles + 1):
                        t = j / num_circles if num_circles > 0 else 0
                        circle_x = start[0] + t * dx
                        circle_y = start[1] + t * dy
                        
                        circle_radius = thickness / 2
                        circle = shapes.Circle(circle_x, circle_y, circle_radius, color=self.color)
                        circle.opacity = 200
                        circle.draw()
                
            # Draw current point indicator
            if self.points:
                current_pos = self.points[-1]
                
                from pyglet_physics_game.ui.color_manager import get_color_manager
                color_mgr = get_color_manager()
                circle = shapes.Circle(current_pos[0], current_pos[1], 8, 
                                     color=color_mgr.brush_radius)
                circle.opacity = 200
                circle.draw()
                
                # Draw border for current point
                border = shapes.Circle(current_pos[0], current_pos[1], 8, 
                                       color=color_mgr.debug_text)
                border.opacity = 150
                border.draw()
                
            print(f"DEBUG: Preview drawn successfully")
