"""
Intelligent Mouse System with Dual-Circle Snapping
==================================================

This system provides a dual-circle mouse cursor with intelligent snapping capabilities:
- Outer Circle: The actual mouse cursor that moves freely
- Inner Circle: Snaps to UI elements, grid intersections, or special zones
- Snap Zones: Contextual areas that attract the inner circle

The system is designed to work with dense UI layouts and provides smooth,
intuitive interactions even in complex interfaces.
"""

import math
import time
from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass
from enum import Enum

import pyglet
from pyglet import shapes, text
from pyglet.graphics import Batch, Group


class SnapState(Enum):
    """Different states of the snap system"""
    DEFAULT = "default"
    UI_SNAP = "ui_snap"
    PHYSICS_SNAP = "physics_snap"
    GRID_SNAP = "grid_snap"
    ACTIVE = "active"


@dataclass
class SnapZone:
    """Base class for snap zones"""
    name: str
    center_x: float
    center_y: float
    radius: float
    priority: int = 0
    active: bool = True
    snap_distance: float = 20.0
    visual_feedback: bool = True


@dataclass
class CircularSnapZone(SnapZone):
    """Circular snap zone with deadzone support"""
    deadzone_radius: float = 0.0
    zero_values: Optional[Dict[str, float]] = None
    max_distance: float = 100.0
    scaling_type: str = "linear"  # "linear", "exponential", "logarithmic"


@dataclass
class RectangularSnapZone(SnapZone):
    """Rectangular snap zone for UI elements"""
    width: float = 100.0
    height: float = 30.0
    element_type: str = "slider"  # "slider", "button", "menu_item"
    scroll_tolerance: float = 30.0
    snap_x_only: bool = False  # Only snap on X-axis to avoid hover conflicts


@dataclass
class SnapResult:
    """Result of snap zone detection"""
    snapped: bool
    zone: Optional[SnapZone]
    snap_x: float
    snap_y: float
    distance: float
    intensity: float = 1.0
    state: SnapState = SnapState.DEFAULT


class MouseSystem:
    """
    Main mouse system that manages dual-circle rendering and snap zone detection
    """
    
    def __init__(self, game):
        self.game = game
        self.screen_width = game.width
        self.screen_height = game.height
        self.coordinate_manager = None  # Will be set by game core
        
        # Mouse position tracking
        self.mouse_x = 0
        self.mouse_y = 0
        self.last_mouse_x = 0
        self.last_mouse_y = 0
        
        # Circle properties
        self.outer_circle_radius = 12
        self.inner_circle_radius = 6
        self.circle_thickness = 2
        
        # Snap system
        self.snap_zones: List[SnapZone] = []
        self.current_snap: Optional[SnapResult] = None
        self.snap_state = SnapState.DEFAULT
        
        self.active_zone: Optional[SnapZone] = None
        
        # Visual feedback
        self.snap_animation_time = 0.0
        self.snap_animation_duration = 0.2
        
        # Hide inner cursor when hovering over sliders
        self.hide_inner_cursor = False
        
        # Rendering
        self.batch = Batch()
        self.mouse_group = Group(order=1000)  # Always on top
        self._create_circles()
        
        # Color management
        from .color_manager import get_color_manager
        self.color_mgr = get_color_manager()
        
        # Performance
        self._last_snap_check = 0.0
        self.snap_check_interval = 1.0 / 60.0  # 60 FPS max
        
    def _create_circles(self):
        """Create the dual-circle visual elements"""
        # Outer circle (cursor)
        self.outer_circle = shapes.Circle(
            0, 0, self.outer_circle_radius,
            color=(255, 255, 255),
            batch=self.batch,
            group=self.mouse_group
        )
        self.outer_circle.opacity = 180
        
        # Inner circle (snap indicator)
        self.inner_circle = shapes.Circle(
            0, 0, self.inner_circle_radius,
            color=(0, 255, 255),  # Cyan for snap indication
            batch=self.batch,
            group=self.mouse_group
        )
        self.inner_circle.opacity = 200
        
        # Snap zone indicator (when active)
        self.snap_zone_indicator = shapes.Circle(
            0, 0, 20,
            color=(255, 255, 0),  # Yellow for active zone
            batch=self.batch,
            group=self.mouse_group
        )
        self.snap_zone_indicator.opacity = 0  # Hidden by default
        
    def update(self, dt: float):
        """Update the mouse system"""
        # Update animation time
        self.snap_animation_time += dt
        
        # Update circle positions
        self._update_circle_positions()
        
        # Update snap zone detection (throttled for performance)
        current_time = time.time()
        if current_time - self._last_snap_check >= self.snap_check_interval:
            self._update_snap_detection()
            self._last_snap_check = current_time
            
        # Update visual feedback
        self._update_visual_feedback()
        
    def _update_circle_positions(self):
        """Update the positions of both circles"""
        # Outer circle follows mouse
        self.outer_circle.x = self.mouse_x
        self.outer_circle.y = self.mouse_y
        
        # Inner circle position - simple snap to zones or follow mouse
        if self.current_snap and self.current_snap.snapped:
            # Normal snap mode
            target_x = self.current_snap.snap_x
            target_y = self.current_snap.snap_y
            
            # Faster animation for sliders, normal for others
            if (self.current_snap.zone and 
                isinstance(self.current_snap.zone, RectangularSnapZone) and 
                self.current_snap.zone.element_type == "slider"):
                # Very fast snap for sliders - almost instant
                lerp_factor = min(1.0, self.snap_animation_time / 0.05)  # 50ms animation
            else:
                # Normal animation for other zones
                lerp_factor = min(1.0, self.snap_animation_time / self.snap_animation_duration)
            
            self.inner_circle.x = self._lerp(self.inner_circle.x, target_x, lerp_factor)
            self.inner_circle.y = self._lerp(self.inner_circle.y, target_y, lerp_factor)
            
            # Reset color for normal snap mode
            self.inner_circle.color = self.color_mgr.accent_cyan
        else:
            # Inner circle follows mouse when not snapping
            self.inner_circle.x = self.mouse_x
            self.inner_circle.y = self.mouse_y
            
            # Reset color for normal mode
            self.inner_circle.color = self.color_mgr.accent_cyan
            
    def _update_snap_detection(self):
        """Detect and update snap zones"""
        if not self.snap_zones:
            self.current_snap = None
            return
            
        # Find the best snap zone
        best_snap = None
        best_distance = float('inf')
        best_priority = -1
        
        for zone in self.snap_zones:
            if not zone.active:
                continue
                
            distance = self._calculate_distance_to_zone(zone)
            
            if distance <= zone.snap_distance:
                # Consider both distance and priority
                # Higher priority wins, but if priorities are equal, closer distance wins
                if (zone.priority > best_priority or 
                    (zone.priority == best_priority and distance < best_distance)):
                    snap_x, snap_y = self._calculate_snap_position(zone)
                    intensity = self._calculate_snap_intensity(zone, distance)
                    
                    best_snap = SnapResult(
                        snapped=True,
                        zone=zone,
                        snap_x=snap_x,
                        snap_y=snap_y,
                        distance=distance,
                        intensity=intensity,
                        state=self._get_snap_state(zone)
                    )
                    best_distance = distance
                    best_priority = zone.priority
                
        # Update current snap
        if best_snap != self.current_snap:
            self.current_snap = best_snap
            self.active_zone = best_snap.zone if best_snap else None
            self.snap_animation_time = 0.0  # Reset animation
            
    def _calculate_distance_to_zone(self, zone: SnapZone) -> float:
        """Calculate distance from mouse to snap zone"""
        if isinstance(zone, CircularSnapZone):
            return math.sqrt(
                (self.mouse_x - zone.center_x) ** 2 + 
                (self.mouse_y - zone.center_y) ** 2
            )
        elif isinstance(zone, RectangularSnapZone):
            if zone.snap_x_only:
                # For X-only snapping, check if mouse is within the row area first
                y_distance = abs(self.mouse_y - zone.center_y)
                if y_distance > zone.height / 2:
                    # Mouse is outside the row area, don't snap
                    return float('inf')
                # Only calculate X distance if within row bounds
                return abs(self.mouse_x - zone.center_x)
            else:
                # Distance to rectangle edge
                dx = max(0, abs(self.mouse_x - zone.center_x) - zone.width / 2)
                dy = max(0, abs(self.mouse_y - zone.center_y) - zone.height / 2)
                return math.sqrt(dx * dx + dy * dy)
        else:
            # Default to center distance
            return math.sqrt(
                (self.mouse_x - zone.center_x) ** 2 + 
                (self.mouse_y - zone.center_y) ** 2
            )
            
    def _calculate_snap_position(self, zone: SnapZone) -> Tuple[float, float]:
        """Calculate the snap position for a zone"""
        if isinstance(zone, CircularSnapZone):
            # For circular zones, snap to the edge or center
            distance = self._calculate_distance_to_zone(zone)
            if distance <= zone.deadzone_radius:
                return zone.center_x, zone.center_y
            else:
                # Snap to the edge of the deadzone
                angle = math.atan2(
                    self.mouse_y - zone.center_y,
                    self.mouse_x - zone.center_x
                )
                return (
                    zone.center_x + zone.deadzone_radius * math.cos(angle),
                    zone.center_y + zone.deadzone_radius * math.sin(angle)
                )
        elif isinstance(zone, RectangularSnapZone) and zone.snap_x_only:
            # For X-only snapping, only snap X coordinate, keep Y as mouse position
            return zone.center_x, self.mouse_y
        else:
            # For other zones, snap to center
            return zone.center_x, zone.center_y
            
    def _calculate_snap_intensity(self, zone: SnapZone, distance: float) -> float:
        """Calculate the intensity of the snap (0.0 to 1.0)"""
        if distance >= zone.snap_distance:
            return 0.0
            
        # Linear falloff
        return 1.0 - (distance / zone.snap_distance)
        
    def _get_snap_state(self, zone: SnapZone) -> SnapState:
        """Determine the snap state based on zone type"""
        if isinstance(zone, CircularSnapZone) and zone.zero_values:
            return SnapState.PHYSICS_SNAP
        elif isinstance(zone, RectangularSnapZone):
            if zone.element_type == "slider":
                return SnapState.UI_SNAP
            else:
                return SnapState.UI_SNAP
        else:
            return SnapState.GRID_SNAP
            
    def _update_visual_feedback(self):
        """Update visual feedback based on snap state"""
        if not self.current_snap:
            # Default state
            self.inner_circle.color = self.color_mgr.accent_cyan
            self.inner_circle.opacity = 200
            self.snap_zone_indicator.opacity = 0
            return
            
        # Update based on snap state
        if self.current_snap.state == SnapState.PHYSICS_SNAP:
            # Check if we're actually in the deadzone
            if isinstance(self.current_snap.zone, CircularSnapZone):
                distance_to_center = math.sqrt(
                    (self.mouse_x - self.current_snap.zone.center_x) ** 2 + 
                    (self.mouse_y - self.current_snap.zone.center_y) ** 2
                )
                
                if distance_to_center <= self.current_snap.zone.deadzone_radius:
                    # We're IN the deadzone - show zero state
                    self.inner_circle.color = self.color_mgr.feedback_success  # Green for zero
                    self.inner_circle.opacity = 255
                    # Show deadzone indicator with pulsing effect
                    self.snap_zone_indicator.x = self.current_snap.zone.center_x
                    self.snap_zone_indicator.y = self.current_snap.zone.center_y
                    self.snap_zone_indicator.radius = self.current_snap.zone.deadzone_radius
                    # Pulsing opacity based on animation time
                    pulse = 0.5 + 0.5 * math.sin(self.snap_animation_time * 8)  # Fast pulse
                    self.snap_zone_indicator.opacity = int(150 * pulse)
                else:
                    # We're near the deadzone but not in it
                    self.inner_circle.color = self.color_mgr.accent_cyan  # Cyan for near
                    self.inner_circle.opacity = 200
                    # Show deadzone indicator with subtle highlight
                    self.snap_zone_indicator.x = self.current_snap.zone.center_x
                    self.snap_zone_indicator.y = self.current_snap.zone.center_y
                    self.snap_zone_indicator.radius = self.current_snap.zone.deadzone_radius
                    self.snap_zone_indicator.opacity = 80
        elif self.current_snap.state == SnapState.UI_SNAP:
            self.inner_circle.color = self.color_mgr.category_color('tools')
            self.inner_circle.opacity = 255
            self.snap_zone_indicator.opacity = 0
        else:
            self.inner_circle.color = self.color_mgr.accent_cyan
            self.inner_circle.opacity = 200
            self.snap_zone_indicator.opacity = 0
            
    def _lerp(self, a: float, b: float, t: float) -> float:
        """Linear interpolation"""
        return a + (b - a) * t
        
    def set_mouse_position(self, x: float, y: float):
        """Update mouse position"""
        self.last_mouse_x = self.mouse_x
        self.last_mouse_y = self.mouse_y
        self.mouse_x = x
        self.mouse_y = y
        
        # Debug: Print mouse position occasionally
        if hasattr(self, '_debug_counter'):
            self._debug_counter += 1
        else:
            self._debug_counter = 0
        
        if self._debug_counter % 60 == 0:  # Print every 60 frames
            print(f"DEBUG: Mouse position - x: {x}, y: {y}, screen: {self.screen_width}x{self.screen_height}")
        

        
    def add_snap_zone(self, zone: SnapZone):
        """Add a snap zone to the system"""
        self.snap_zones.append(zone)
        
    def remove_snap_zone(self, name: str):
        """Remove a snap zone by name"""
        self.snap_zones = [z for z in self.snap_zones if z.name != name]
        
    def clear_snap_zones(self):
        """Clear all snap zones"""
        self.snap_zones.clear()
        
    def get_current_snap(self) -> Optional[SnapResult]:
        """Get the current snap result"""
        return self.current_snap
        
    def is_snapped(self) -> bool:
        """Check if currently snapped to any zone"""
        return self.current_snap is not None and self.current_snap.snapped
    
    def set_hide_inner_cursor(self, hide: bool):
        """Hide or show the inner cursor"""
        self.hide_inner_cursor = hide
        
    def draw(self):
        """Draw the mouse system"""
        # Draw both circles
        self.outer_circle.draw()
        # Only draw inner circle if not hidden
        if not self.hide_inner_cursor:
            self.inner_circle.draw()
        
    def update_resolution(self, width: int, height: int):
        """Update screen resolution"""
        self.screen_width = width
        self.screen_height = height
