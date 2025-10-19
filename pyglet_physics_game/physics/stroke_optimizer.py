"""
Stroke Optimization Module
Converts complex drawn strokes into lightweight collision shapes
"""

import math
import pymunk
from typing import List, Tuple, Optional
import numpy as np

class StrokeOptimizer:
    """Optimizes complex drawn strokes into lightweight collision shapes"""
    
    def __init__(self, simplification_tolerance: float = 5.0):
        self.simplification_tolerance = simplification_tolerance
        
    def simplify_stroke(self, points: List[Tuple[float, float]]) -> List[Tuple[float, float]]:
        """
        Simplify stroke using Douglas-Peucker algorithm
        Reduces point count while maintaining shape accuracy
        """
        if len(points) <= 2:
            return points
            
        # Find the point with maximum distance from line segment
        max_distance = 0
        max_index = 0
        
        start = points[0]
        end = points[-1]
        
        for i in range(1, len(points) - 1):
            distance = self._point_to_line_distance(points[i], start, end)
            if distance > max_distance:
                max_distance = distance
                max_index = i
        
        # If max distance is greater than tolerance, recursively simplify
        if max_distance > self.simplification_tolerance:
            # Recursively simplify the two sub-paths
            left_points = self.simplify_stroke(points[:max_index + 1])
            right_points = self.simplify_stroke(points[max_index:])
            
            # Combine results (avoid duplicate middle point)
            return left_points[:-1] + right_points
        else:
            # All points are within tolerance, return endpoints
            return [start, end]
    
    def _point_to_line_distance(self, point: Tuple[float, float], 
                               line_start: Tuple[float, float], 
                               line_end: Tuple[float, float]) -> float:
        """Calculate perpendicular distance from point to line segment"""
        px, py = point
        x1, y1 = line_start
        x2, y2 = line_end
        
        # Calculate line length
        line_length = math.sqrt((x2 - x1)**2 + (y2 - y1)**2)
        
        if line_length == 0:
            return math.sqrt((px - x1)**2 + (py - y1)**2)
        
        # Calculate perpendicular distance
        t = max(0, min(1, ((px - x1) * (x2 - x1) + (py - y1) * (y2 - y1)) / (line_length**2)))
        
        # Closest point on line
        closest_x = x1 + t * (x2 - x1)
        closest_y = y1 + t * (y2 - y1)
        
        return math.sqrt((px - closest_x)**2 + (py - closest_y)**2)
    
    def detect_straight_sections(self, points: List[Tuple[float, float]], 
                                angle_threshold: float = 15.0) -> List[bool]:
        """
        Detect which sections are straight vs curved
        Returns list of booleans indicating straight sections
        """
        if len(points) < 3:
            return [True] * len(points)
        
        straight_sections = [True]  # First point is always straight
        
        for i in range(1, len(points) - 1):
            prev = points[i - 1]
            curr = points[i]
            next_point = points[i + 1]
            
            # Calculate angles
            angle1 = math.atan2(curr[1] - prev[1], curr[0] - prev[0])
            angle2 = math.atan2(next_point[1] - curr[1], next_point[0] - curr[0])
            
            # Normalize angles
            angle_diff = abs(angle1 - angle2)
            if angle_diff > math.pi:
                angle_diff = 2 * math.pi - angle_diff
            
            # Convert to degrees
            angle_diff_deg = math.degrees(angle_diff)
            
            # Check if section is straight
            is_straight = angle_diff_deg < angle_threshold
            straight_sections.append(is_straight)
        
        straight_sections.append(True)  # Last point is always straight
        return straight_sections
    
    def create_optimized_collision_shapes(self, points: List[Tuple[float, float]], 
                                        thickness: float) -> List[pymunk.Shape]:
        """
        Create optimized collision shapes from stroke points
        Returns list of pymunk shapes for physics
        """
        if len(points) < 2:
            return []
        
        # Step 1: Simplify the stroke
        simplified_points = self.simplify_stroke(points)
        print(f"DEBUG: Simplified {len(points)} points to {len(simplified_points)} points")
        
        # Step 2: Detect straight vs curved sections
        straight_sections = self.detect_straight_sections(simplified_points)
        
        # Step 3: Generate collision shapes
        shapes = []
        
        for i in range(len(simplified_points) - 1):
            start = simplified_points[i]
            end = simplified_points[i + 1]
            is_straight = straight_sections[i] and straight_sections[i + 1]
            
            if is_straight:
                # Use Segment for straight sections (fastest)
                shape = self._create_segment_shape(start, end, thickness)
            else:
                # Use Poly for curved sections
                shape = self._create_poly_shape(start, end, thickness)
            
            if shape:
                shapes.append(shape)
        
        print(f"DEBUG: Created {len(shapes)} optimized collision shapes")
        return shapes
    
    def _create_segment_shape(self, start: Tuple[float, float], 
                             end: Tuple[float, float], 
                             thickness: float) -> Optional[pymunk.Shape]:
        """Create a pymunk Segment shape"""
        try:
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            start_vec = pymunk.Vec2d(start[0], start[1])
            end_vec = pymunk.Vec2d(end[0], end[1])
            
            # Segment radius is half the thickness
            shape = pymunk.Segment(body, start_vec, end_vec, thickness / 2)
            shape.friction = 0.7
            shape.elasticity = 0.5
            shape.collision_type = 1
            
            return shape
        except Exception as e:
            print(f"ERROR creating segment shape: {e}")
            return None
    
    def _create_poly_shape(self, start: Tuple[float, float], 
                          end: Tuple[float, float], 
                          thickness: float) -> Optional[pymunk.Shape]:
        """Create a pymunk Poly shape for curved sections"""
        try:
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            
            # Create a simple rectangle between start and end points
            # This is a compromise between accuracy and performance
            dx = end[0] - start[0]
            dy = end[1] - start[1]
            length = math.sqrt(dx*dx + dy*dy)
            
            if length == 0:
                return None
            
            # Calculate perpendicular vector for thickness
            perp_x = -dy / length * thickness / 2
            perp_y = dx / length * thickness / 2
            
            # Create rectangle vertices
            vertices = [
                (start[0] - perp_x, start[1] - perp_y),
                (start[0] + perp_x, start[1] + perp_y),
                (end[0] + perp_x, end[1] + perp_y),
                (end[0] - perp_x, end[1] - perp_y)
            ]
            
            # Convert to pymunk Vec2d
            vec_vertices = [pymunk.Vec2d(x, y) for x, y in vertices]
            
            shape = pymunk.Poly(body, vec_vertices)
            shape.friction = 0.7
            shape.elasticity = 0.5
            shape.collision_type = 1
            
            return shape
        except Exception as e:
            print(f"ERROR creating poly shape: {e}")
            return None
    
    def create_convex_hull_collision(self, points: List[Tuple[float, float]], 
                                   thickness: float) -> Optional[pymunk.Shape]:
        """
        Create a single convex hull collision shape for very complex strokes
        Use this as a fallback for extremely complex doodles
        """
        if len(points) < 3:
            return None
        
        try:
            # Simple convex hull algorithm
            hull_points = self._graham_scan(points)
            
            if len(hull_points) < 3:
                return None
            
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            
            # Convert to pymunk Vec2d
            vec_vertices = [pymunk.Vec2d(x, y) for x, y in hull_points]
            
            shape = pymunk.Poly(body, vec_vertices)
            shape.friction = 0.7
            shape.elasticity = 0.5
            shape.collision_type = 1
            
            return shape
        except Exception as e:
            print(f"ERROR creating convex hull shape: {e}")
            return None
    
    def _graham_scan(self, points: List[Tuple[float, float]]) -> List[Tuple[float, float]]:
        """Graham scan algorithm for convex hull"""
        if len(points) < 3:
            return points
        
        # Find the point with the lowest y-coordinate (and leftmost if tied)
        lowest = min(points, key=lambda p: (p[1], p[0]))
        
        # Sort points by polar angle with respect to lowest point
        def polar_angle(p):
            return math.atan2(p[1] - lowest[1], p[0] - lowest[0])
        
        sorted_points = sorted(points, key=polar_angle)
        
        # Remove duplicates
        unique_points = []
        for p in sorted_points:
            if not unique_points or p != unique_points[-1]:
                unique_points.append(p)
        
        if len(unique_points) < 3:
            return unique_points
        
        # Graham scan
        hull = [unique_points[0], unique_points[1]]
        
        for i in range(2, len(unique_points)):
            while len(hull) > 1 and self._cross_product(hull[-2], hull[-1], unique_points[i]) <= 0:
                hull.pop()
            hull.append(unique_points[i])
        
        return hull
    
    def _cross_product(self, o: Tuple[float, float], a: Tuple[float, float], 
                      b: Tuple[float, float]) -> float:
        """Calculate cross product of vectors OA and OB"""
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
