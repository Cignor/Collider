"""
Snap Zone Management System
===========================

This module handles loading, creating, and managing snap zones from JSON configurations.
It provides a bridge between the configuration files and the mouse system.
"""

import json
import os
from typing import Dict, List, Optional, Any
from .mouse_system import SnapZone, CircularSnapZone, RectangularSnapZone


class SnapZoneManager:
    """
    Manages snap zones loaded from JSON configurations
    """
    
    def __init__(self, game):
        self.game = game
        self.configs_dir = os.path.join(os.path.dirname(__file__), 'snap_configs')
        self.loaded_configs: Dict[str, Dict] = {}
        self.active_zones: List[SnapZone] = []
        
    def load_config(self, config_name: str) -> bool:
        """Load a snap configuration from JSON file"""
        config_path = os.path.join(self.configs_dir, f"{config_name}.json")
        
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)
                self.loaded_configs[config_name] = config
                print(f"Loaded snap config: {config.get('name', config_name)}")
                return True
        except Exception as e:
            print(f"ERROR loading snap config '{config_name}': {e}")
            return False
            
    def create_zones_from_config(self, config_name: str, screen_width: int, screen_height: int) -> List[SnapZone]:
        """Create snap zones from a loaded configuration"""
        if config_name not in self.loaded_configs:
            if not self.load_config(config_name):
                return []
                
        config = self.loaded_configs[config_name]
        zones = []
        
        for zone_name, zone_data in config.get('zones', {}).items():
            zone = self._create_zone_from_data(zone_name, zone_data, screen_width, screen_height)
            if zone:
                zones.append(zone)
                
        return zones
        
    def _create_zone_from_data(self, name: str, data: Dict, screen_width: int, screen_height: int) -> Optional[SnapZone]:
        """Create a specific snap zone from configuration data"""
        zone_type = data.get('type', 'circular')
        
        try:
            if zone_type == 'circular_deadzone':
                return CircularSnapZone(
                    name=name,
                    center_x=self._resolve_coordinate(data['center'][0], screen_width),
                    center_y=self._resolve_coordinate(data['center'][1], screen_height),
                    radius=data.get('radius', 50),
                    priority=data.get('priority', 0),
                    active=data.get('active', True),
                    snap_distance=data.get('snap_distance', 20),
                    visual_feedback=data.get('visual_feedback', True),
                    deadzone_radius=data.get('deadzone_radius', 0),
                    zero_values=data.get('zero_values'),
                    max_distance=data.get('max_distance', 100),
                    scaling_type=data.get('scaling_type', 'linear')
                )
            elif zone_type == 'rectangular':
                return RectangularSnapZone(
                    name=name,
                    center_x=self._resolve_coordinate(data['center'][0], screen_width),
                    center_y=self._resolve_coordinate(data['center'][1], screen_height),
                    radius=data.get('radius', 20),  # Used as snap distance
                    priority=data.get('priority', 0),
                    active=data.get('active', True),
                    snap_distance=data.get('snap_distance', 20),
                    visual_feedback=data.get('visual_feedback', True),
                    width=data.get('width', 100),
                    height=data.get('height', 30),
                    element_type=data.get('element_type', 'slider'),
                    scroll_tolerance=data.get('scroll_tolerance', 30)
                )
            else:
                # Default circular zone
                return SnapZone(
                    name=name,
                    center_x=self._resolve_coordinate(data['center'][0], screen_width),
                    center_y=self._resolve_coordinate(data['center'][1], screen_height),
                    radius=data.get('radius', 20),
                    priority=data.get('priority', 0),
                    active=data.get('active', True),
                    snap_distance=data.get('snap_distance', 20),
                    visual_feedback=data.get('visual_feedback', True)
                )
        except Exception as e:
            print(f"ERROR creating snap zone '{name}': {e}")
            return None
            
    def _resolve_coordinate(self, coord: Any, screen_dimension: int) -> float:
        """Resolve coordinate values that might be relative or absolute"""
        if isinstance(coord, (int, float)):
            # If it's a percentage (0.0 to 1.0), convert to screen coordinates
            if 0.0 <= coord <= 1.0:
                return coord * screen_dimension
            else:
                return float(coord)
        elif isinstance(coord, str):
            # Handle string coordinates like "center", "left", "right"
            if coord == "center":
                return screen_dimension / 2
            elif coord == "left":
                return 0
            elif coord == "right":
                return screen_dimension
            else:
                # Try to parse as float
                try:
                    return float(coord)
                except ValueError:
                    return screen_dimension / 2  # Default to center
        else:
            return screen_dimension / 2  # Default to center
            
    def create_physics_center_zone(self, screen_width: int, screen_height: int) -> CircularSnapZone:
        """Create the physics center deadzone for gravity/wind control"""
        return CircularSnapZone(
            name="physics_center",
            center_x=screen_width / 2,
            center_y=screen_height / 2,
            radius=50,
            deadzone_radius=30,
            snap_distance=80,
            priority=10,
            active=True,
            visual_feedback=True,
            zero_values={"gravity": 0, "wind": 0},
            max_distance=200,
            scaling_type="linear"
        )
        
    def create_ui_slider_zones(self, slider_rects: List[Dict]) -> List[RectangularSnapZone]:
        """Create snap zones for UI sliders"""
        zones = []
        
        for i, rect in enumerate(slider_rects):
            zone = RectangularSnapZone(
                name=f"slider_{i}",
                center_x=rect['x'] + rect['width'] / 2,
                center_y=rect['y'] + rect['height'] / 2,
                radius=15,  # Snap distance
                priority=5,
                active=True,
                snap_distance=15,
                visual_feedback=True,
                width=rect['width'],
                height=rect['height'],
                element_type="slider",
                scroll_tolerance=30
            )
            zones.append(zone)
            
        return zones
        
    def create_menu_item_zones(self, menu_items: List[Dict]) -> List[RectangularSnapZone]:
        """Create snap zones for menu items"""
        zones = []
        
        for i, item in enumerate(menu_items):
            zone = RectangularSnapZone(
                name=f"menu_item_{i}",
                center_x=item['x'] + item['width'] / 2,
                center_y=item['y'] + item['height'] / 2,
                radius=12,  # Snap distance
                priority=3,
                active=True,
                snap_distance=12,
                visual_feedback=True,
                width=item['width'],
                height=item['height'],
                element_type="menu_item",
                scroll_tolerance=0
            )
            zones.append(zone)
            
        return zones
        
    def get_config_settings(self, config_name: str) -> Dict:
        """Get settings from a loaded configuration"""
        if config_name in self.loaded_configs:
            return self.loaded_configs[config_name].get('settings', {})
        return {}
        
    def update_zone_positions(self, zones: List[SnapZone], screen_width: int, screen_height: int):
        """Update zone positions for new screen resolution"""
        for zone in zones:
            if hasattr(zone, 'center_x') and hasattr(zone, 'center_y'):
                # Update coordinates that might be relative
                zone.center_x = self._resolve_coordinate(zone.center_x, screen_width)
                zone.center_y = self._resolve_coordinate(zone.center_y, screen_height)
