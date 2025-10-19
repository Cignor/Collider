import math
import pymunk
import pyglet
from pyglet import shapes
from typing import Dict, Tuple
import json, os
import numpy as np
from pyglet_physics_game.shapes.shape_manager import get_shape_manager
from pyglet_physics_game.ipc.client import get_ipc_client
from pyglet_physics_game.systems.bullet_data import BulletData

class SoundBullet:
    """Sound bullet that moves on grid following properties from audio bank preset"""
    
    def __init__(self, x: float, y: float, properties: Dict, game):
        self.x = x
        self.y = y
        self.properties = properties
        self.game = game
        
        # Extract properties
        self.direction = properties.get('direction', 'horizontal')
        self.speed = properties.get('speed', 100.0)  # pixels per second
        self.amplitude = properties.get('amplitude', 100.0)  # amplitude in pixels
        self.pitch_on_grid = properties.get('pitch_on_grid', 'off')
        self.shape = properties.get('shape', 'circle')  # bullet shape
        self.looping = str(properties.get('looping', 'on')).lower() in ('on', 'true', '1', 'yes')
        
        # Extract color property
        color_prop = properties.get('color', {'r': 255, 'g': 255, 'b': 255})
        if isinstance(color_prop, list) and len(color_prop) >= 3:
            self.color = (int(color_prop[0]), int(color_prop[1]), int(color_prop[2]))
        elif isinstance(color_prop, dict):
            self.color = (int(color_prop.get('r', 255)), int(color_prop.get('g', 255)), int(color_prop.get('b', 255)))
        else:
            self.color = (255, 255, 255)  # Default to white
        # Use global physics state instead of individual bullet property
        self.physics_enabled = self.game.global_physics_enabled
        
        # Movement state
        self.is_active = True  # Bullet stays active until manually removed
        self._engine_player_id = f"sb_{id(self)}"
        self._audio_ready = False
        self._emission_radius = float(self.amplitude)
        # Load audio spatialization config (pan smoothing and near ratio)
        try:
            cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'audio_engine', 'config', 'audio_engine_config.json')
            self._pan_tau_s = 0.03
            self._near_ratio = 0.12
            if os.path.exists(cfg_path):
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    cfg = json.load(f)
                ms = float(cfg.get('pan_smoothing_ms', 30))
                self._pan_tau_s = max(0.001, ms / 1000.0)
                self._near_ratio = float(cfg.get('spatial_near_ratio', 0.12))
        except Exception:
            self._pan_tau_s = 0.03
            self._near_ratio = 0.12
        
        # Store original velocity for manual mode (based on direction and speed)
        if self.direction == 'horizontal':
            self.original_velocity_x = self.speed
            self.original_velocity_y = 0.0
        else:  # vertical
            self.original_velocity_x = 0.0
            self.original_velocity_y = self.speed
        
        # Always initialize current velocity attributes for manual mode
        self.velocity_x = self.original_velocity_x
        self.velocity_y = self.original_velocity_y
        
        # Initialize bullet data for trail system integration
        self.bullet_data = BulletData(
            bullet_id=id(self),
            x=x,
            y=y,
            velocity_x=self.velocity_x,
            velocity_y=self.velocity_y,
            trail_enabled=True,  # Enable trails for sound bullets
            trail_segments=3,    # Default trail segments
            trail_color=self.color  # Use bullet color for trail
        )
        # Native diagnostics: log voice info for first frames after spawn
        self._native_telemetry_frames = 0
        # IPC client state
        try:
            self._ipc_client = get_ipc_client()
        except Exception:
            self._ipc_client = None
        self._ipc_last = {'gain': None, 'pan': None, 'pitch': None}
        
        # Get shape manager
        self.shape_manager = get_shape_manager()
        
        # Create visual representation using game's batch system for proper depth sorting
        if hasattr(game, 'renderer') and hasattr(game.renderer, 'batch'):
            # Create bullet shape using ShapeManager with the specified color
            # print(f"DEBUG: Creating visual shape for {self.shape} at ({x}, {y})")
            self.visual = self.shape_manager.create_visual_shape(self.shape, x, y, 
                                                               game.renderer.batch, game.renderer.bullets_group,
                                                               color=self.color)
            if self.visual:
                self.visual.opacity = 255  # Full opacity for better visibility
            # print(f"DEBUG: Visual shape created: {self.visual}")
            
            # Create physics aura (visual indicator of physics state) - drawn UNDER grid
            # Use amplitude directly as scale - respect the full 500px range from JSON
            aura_scale = max(1.0, self.amplitude / 8.0)  # Scale based on amplitude, minimum 1.0
            aura_color = (0, 255, 0) if self.physics_enabled else (255, 0, 0)  # Green for ON, Red for OFF
            self.physics_aura = self.shape_manager.create_visual_shape(self.shape, x, y, 
                                                                     game.renderer.batch, game.renderer.background_group, 
                                                                     color=aura_color, scale=aura_scale)
            if self.physics_aura:
                self.physics_aura.opacity = 33  # 30% opacity
            
        else:
            # Fallback to direct drawing if batch system not available
            self.visual = self.shape_manager.create_visual_shape(self.shape, x, y, color=self.color)
            if self.visual:
                self.visual.opacity = 255
            
            # Create physics aura (visual indicator of physics state) - drawn UNDER grid
            # Use amplitude directly as radius - respect the full 500px range from JSON
            aura_radius = max(8, self.amplitude)  # Use full amplitude, minimum 8px radius
            aura_color = (0, 255, 0) if self.physics_enabled else (255, 0, 0)  # Green for ON, Red for OFF
            self.physics_aura = shapes.Circle(x, y, aura_radius, color=aura_color)
            self.physics_aura.opacity = 33  # 30% opacity (30% of 255)
        
        # Create physics body and shape based on physics mode
        if self.physics_enabled:
            # PHYSICS MODE: Full physics simulation with collision
            # Create appropriate collision shape using ShapeManager
            # print(f"DEBUG: Creating physics shape for '{self.shape}' at ({x}, {y})")
            physics_result = self.shape_manager.create_physics_shape(self.shape, x, y)
            if physics_result:
                self.body, self.physics_shape = physics_result
                # print(f"DEBUG: Successfully created physics body and shape for '{self.shape}'")
                self.physics_shape.friction = 0.7
                self.physics_shape.elasticity = 0.8
                self.physics_shape.collision_type = 2  # Bullet collision type
                
                # Set initial velocity based on original direction and speed
                self.body.velocity = (self.original_velocity_x, self.original_velocity_y)
                
                # Add some physics properties for more interesting behavior
                self.body.angular_velocity = 0.0  # No initial rotation
                self.body.angle = 0.0  # No initial angle
                
                # Set up collision handler for bullet interactions
                self._setup_collision_handler()
                
                # Add to physics space
                self.game.space.add(self.body, self.physics_shape)
                # print(f"DEBUG: Added physics body to space for '{self.shape}'")
                
            else:
                print(f"ERROR: Failed to create physics shape for '{self.shape}'")
                self.body = None
                self.physics_shape = None
        else:
            # MANUAL MODE: No physics body, manual movement only
            self.body = None
            self.physics_shape = None
        
        # Manual velocity is already set above using original_velocity
        
        # Initialize audio if engine is available and a source file is present
        try:
            self._init_audio()
        except Exception as e:
            pass
        print(f"DEBUG: Created sound bullet at ({x}, {y}) with direction={self.direction}, speed={self.speed}, physics={'ON' if self.physics_enabled else 'OFF'}, shape={self.physics_shape}")
        # Persist default looping in active preset if UI didn't already
        try:
            if hasattr(game, 'ui') and hasattr(game.ui, 'menu') and hasattr(game.ui.menu, '_save_presets'):
                # Reflect into current preset for selector
                menu = game.ui.menu
                data = menu._load_presets()
                if isinstance(menu.active_preset, int) and menu.active_selector in ('left','right'):
                    pk = str(menu.active_preset)
                    sk = f"{menu.active_selector}_selector"
                    if pk in data and sk in data[pk]:
                        props = data[pk][sk].get('properties', {})
                        if 'looping' not in props:
                            props['looping'] = 'on' if self.looping else 'off'
                            data[pk][sk]['properties'] = props
                            menu._save_presets(data)
        except Exception:
            pass
    
    
    
    
    
    def _update_visual_position(self):
        """Update the visual position based on the bullet's current position and shape type"""
        # For polygon shapes, we need to recreate the shape since vertices are absolute
        # For other shapes, we can just update the position
        if self.visual:
            if self.shape in ['triangle', 'star', 'ellipse', 'hexagon', 'diamond']:
                # Remove old visual shape
                if hasattr(self.game, 'renderer') and hasattr(self.game.renderer, 'batch'):
                    self.visual.delete()
                
                # Create new visual shape at new position
                if hasattr(self.game, 'renderer') and hasattr(self.game.renderer, 'batch'):
                    self.visual = self.shape_manager.create_visual_shape(self.shape, self.x, self.y, 
                                                                       self.game.renderer.batch, self.game.renderer.bullets_group,
                                                                       color=self.color)
                    if self.visual:
                        self.visual.opacity = 255
            else:
                # Use ShapeManager to update visual position for non-polygon shapes
                self.shape_manager.update_visual_shape_position(self.visual, self.shape, self.x, self.y)
        
        # Update physics aura position
        self._update_physics_aura_position()
    
    def _update_physics_aura_position(self):
        """Update the physics aura position based on the bullet's current position and shape type"""
        if not hasattr(self, 'physics_aura') or not self.physics_aura:
            return
        
        # For polygon shapes, we need to recreate the shape since vertices are absolute
        # For other shapes, we can just update the position
        if self.shape in ['triangle', 'star', 'ellipse', 'hexagon', 'diamond']:
            # Remove old physics aura shape
            if hasattr(self.game, 'renderer') and hasattr(self.game.renderer, 'batch'):
                self.physics_aura.delete()
            
            # Create new physics aura shape at new position with scaling
            aura_scale = max(1.0, self.amplitude / 8.0)  # Scale based on amplitude, minimum 1.0
            aura_color = (0, 255, 0) if self.physics_enabled else (255, 0, 0)  # Green for ON, Red for OFF
            if hasattr(self.game, 'renderer') and hasattr(self.game.renderer, 'batch'):
                self.physics_aura = self.shape_manager.create_visual_shape(self.shape, self.x, self.y, 
                                                                         self.game.renderer.batch, self.game.renderer.background_group, 
                                                                         color=aura_color, scale=aura_scale)
                if self.physics_aura:
                    self.physics_aura.opacity = 33  # 30% opacity
        else:
            # Use ShapeManager to update physics aura position for non-polygon shapes
            aura_scale = max(1.0, self.amplitude / 8.0)  # Scale based on amplitude, minimum 1.0
            self.shape_manager.update_visual_shape_position(self.physics_aura, self.shape, self.x, self.y, scale=aura_scale)
    
    
    def _setup_collision_handler(self):
        """Set up collision handler for bullet interactions"""
        try:
            # Create collision handler for bullet vs collision shapes
            handler = self.game.space.add_collision_handler(2, 1)  # bullet vs collision shape
            
            def on_collision(arbiter, space, data):
                # Bullet bounces off collision shapes
                return True  # Allow collision
            
            def on_separate(arbiter, space, data):
                # Bullet separated from collision shape
                pass
            
            handler.begin = on_collision
            handler.separate = on_separate
            
        except Exception as e:
            # Collision handler setup failed, but bullets will still work with basic physics
            pass
    
    def update(self, dt: float):
        """Update bullet position using physics or manual movement"""
        try:
            if not self.is_active:
                return False
            
            if self.physics_enabled:
                # PHYSICS MODE: Get position from physics body (physics simulation handles movement)
                self.x = self.body.position.x
                self.y = self.body.position.y
                
                # Handle screen wrapping (loop around edges)
                if self.x < 0:
                    self.x = self.game.width
                    self.body.position = (self.x, self.y)
                elif self.x > self.game.width:
                    self.x = 0
                    self.body.position = (self.x, self.y)
                    
                if self.y < 0:
                    self.y = self.game.height
                    self.body.position = (self.x, self.y)
                elif self.y > self.game.height:
                    self.y = 0
                    self.body.position = (self.x, self.y)
            else:
                # MANUAL MODE: Manual movement, phases through objects
                self.x += self.velocity_x * dt
                self.y += self.velocity_y * dt
                
                # Handle screen wrapping (loop around edges)
                if self.x < 0:
                    self.x = self.game.width
                elif self.x > self.game.width:
                    self.x = 0
                    
                if self.y < 0:
                    self.y = self.game.height
                elif self.y > self.game.height:
                    self.y = 0
            
            # Update visual position
            self._update_visual_position()
            
            # Update bullet data for trail system
            self.bullet_data.x = self.x
            self.bullet_data.y = self.y
            self.bullet_data.velocity_x = self.velocity_x
            self.bullet_data.velocity_y = self.velocity_y
            # Calculate speed from velocity for trail system
            self.bullet_data.speed = math.sqrt(self.velocity_x**2 + self.velocity_y**2)
            
            # Check if physics mode changed and handle transitions
            new_physics_enabled = self.game.global_physics_enabled
            if new_physics_enabled != self.physics_enabled:
                self._handle_physics_mode_change(new_physics_enabled)
            
            # Update physics aura color based on current physics state
            aura_color = (0, 255, 0) if self.physics_enabled else (255, 0, 0)
            self.physics_aura.color = aura_color

            # Update audio spatialization
            self._update_audio_spatial(dt)

            # Native pre-crash telemetry (first ~90 frames): stats + voice info
            try:
                if self._audio_ready and self._native_telemetry_frames > 0 and hasattr(self.game, 'audio_engine'):
                    eng = self.game.audio_engine
                    stats = {}
                    try:
                        if hasattr(eng, '_native') and eng._native is not None and hasattr(eng._native, 'get_stats'):
                            stats = eng._native.get_stats()
                    except Exception:
                        stats = {}
                    vinfo = {}
                    try:
                        if hasattr(eng, 'get_native_voice_info'):
                            vinfo = eng.get_native_voice_info(self._engine_player_id)
                    except Exception:
                        vinfo = {}
                    try:
                        cb_rate = float(getattr(eng, 'get_output_speed_hz', lambda: 0.0)())
                    except Exception:
                        cb_rate = 0.0
                    print(f"NATIVE_DIAG id={self._engine_player_id} frames_left={self._native_telemetry_frames} stats={stats} vinfo={vinfo} cb_est={cb_rate:.1f}")
                    self._native_telemetry_frames -= 1
            except Exception:
                pass

            # Observed speed telemetry: estimate playback speed vs device rate
            try:
                if self._audio_ready and hasattr(self, '_rate_probe') and self._rate_probe is not None:
                    vinfo2 = {}
                    try:
                        vinfo2 = self.game.audio_engine.get_native_voice_info(self._engine_player_id)
                    except Exception:
                        vinfo2 = {}
                    p = float(vinfo2.get('position_f', 0.0)) if isinstance(vinfo2, dict) else 0.0
                    self._rate_probe['frames'] += 1
                    # Every ~0.5s, print observed vs expected
                    if self._rate_probe['frames'] >= 30:
                        t_now = self.game.time
                        dt = float(t_now - self._rate_probe['t0']) if hasattr(self.game, 'time') else 0.5
                        dp = max(0.0, p - self._rate_probe['p0'])
                        dev_rate = 48000.0
                        try:
                            if hasattr(self.game.audio_engine, '_native') and self.game.audio_engine._native is not None and hasattr(self.game.audio_engine._native, 'get_device_rate'):
                                dev_rate = float(self.game.audio_engine._native.get_device_rate())
                        except Exception:
                            pass
                        observed_hz = dp / max(1e-6, dt)
                        ratio = observed_hz / max(1.0, dev_rate)
                        print(f"RATE_PROBE id={self._engine_player_id} observed={observed_hz:.1f}Hz dev={dev_rate:.0f}Hz ratio={ratio:.3f}")
                        # reset window
                        self._rate_probe['t0'] = t_now
                        self._rate_probe['p0'] = p
                        self._rate_probe['frames'] = 0
            except Exception:
                pass
            
            return True
            
        except Exception as e:
            print(f"ERROR updating sound bullet: {e}")
            self.remove()
            return False
    
    def _handle_physics_mode_change(self, new_physics_enabled):
        """Handle transition between physics and manual modes"""
        try:
            if new_physics_enabled and not self.physics_enabled:
                # Switching from manual to physics mode
                # Create physics body and shape using ShapeManager
                physics_result = self.shape_manager.create_physics_shape(self.shape, self.x, self.y)
                if physics_result:
                    self.body, self.physics_shape = physics_result
                    self.physics_shape.friction = 0.7
                    self.physics_shape.elasticity = 0.8
                    self.physics_shape.collision_type = 2
                    
                    # Set physics body velocity to original velocity (reset to creation values)
                    self.body.velocity = (self.original_velocity_x, self.original_velocity_y)
                    self.body.angular_velocity = 0.0
                    self.body.angle = 0.0
                    
                    # Set up collision handler
                    self._setup_collision_handler()
                    
                    # Add to physics space
                    self.game.space.add(self.body, self.physics_shape)
                    
                    print(f"DEBUG: Bullet switched to PHYSICS mode at ({self.x:.1f}, {self.y:.1f}) with velocity ({self.original_velocity_x:.1f}, {self.original_velocity_y:.1f})")
                else:
                    print(f"ERROR: Failed to create physics shape for '{self.shape}' during mode switch")
                
            elif not new_physics_enabled and self.physics_enabled:
                # Switching from physics to manual mode
                # Reset to original velocity (direction and speed from creation)
                self.velocity_x = self.original_velocity_x
                self.velocity_y = self.original_velocity_y
                
                # Remove from physics space
                if self.body and self.shape:
                    try:
                        self.game.space.remove(self.body, self.physics_shape)
                    except:
                        pass  # Already removed
                
                # Clear physics objects
                self.body = None
                self.physics_shape = None
                
                print(f"DEBUG: Bullet switched to MANUAL mode at ({self.x:.1f}, {self.y:.1f}) with velocity ({self.original_velocity_x:.1f}, {self.original_velocity_y:.1f})")
            
            # Update physics state
            self.physics_enabled = new_physics_enabled
            
        except Exception as e:
            print(f"ERROR handling physics mode change: {e}")
    
    def remove(self):
        """Remove bullet from game"""
        try:
            # Notify JUCE to remove voice
            try:
                if getattr(self, '_ipc_client', None) is not None:
                    try:
                        self._ipc_client.destroy_voice(self._osc_id())
                    except Exception:
                        pass
            except Exception:
                pass
            # Mark as inactive
            self.is_active = False
            # Safe audio removal
            # Python audio path is dismantled; no-op here
            
            # Remove from physics space (only if physics mode)
            if self.physics_enabled and hasattr(self, 'body') and hasattr(self, 'shape'):
                try:
                    self.game.space.remove(self.body, self.physics_shape)
                except:
                    pass  # Already removed
            
            # Remove from bullets list
            if self in self.game.sound_bullets:
                self.game.sound_bullets.remove(self)
                
            print(f"DEBUG: Removed sound bullet at ({self.x:.1f}, {self.y:.1f}) [Physics: {'ON' if self.physics_enabled else 'OFF'}]")
            
        except Exception as e:
            print(f"ERROR removing sound bullet: {e}")

    # --- Audio integration ---
    def _init_audio(self):
        # Try IPC spawn first (JUCE authority)
        try:
            if self._ipc_client is not None:
                # initial gain/pan computation (same as spatial logic but at spawn)
                initial_vol = 1.0
                initial_pan = 0.0
                if getattr(self.game, 'current_mouse_pos', None):
                    lx, ly = self.game.current_mouse_pos
                    dx = self.x - lx
                    dy = self.y - ly
                    dist = float((dx*dx + dy*dy) ** 0.5)
                    listener_r = float(getattr(self.game, 'listener_radius', 0.35 * float(min(self.game.width, self.game.height))))
                    emitter_r = float(self._emission_radius)
                    audible_r = max(8.0, listener_r + emitter_r)
                    near_r = max(8.0, float(self._near_ratio) * audible_r)
                    if dist <= near_r:
                        initial_vol = 1.0
                    else:
                        rel = max(0.0, dist - near_r)
                        range_r = max(1.0, audible_r - near_r)
                        nd = min(1.0, rel / range_r)
                        initial_vol = float((1.0 - nd) ** 2)
                    pan_ref = max(16.0, min(listener_r, emitter_r))
                    pan_norm = max(-1.0, min(1.0, dx / pan_ref))
                    initial_pan = float(pan_norm)
                src_file = self.properties.get('file')
                src_preset = self.properties.get('preset')
                src_type = str(self.properties.get('type', '')).lower()
                source = None
                if src_file:
                    source = {"type": "sample", "file": src_file}
                elif src_preset and src_type in ("frequencies", "noise"):
                    source = {"type": ("oscillator" if src_type == "frequencies" else "noise"), "preset": src_preset}
                pitch_semi = 0.0
                if str(self.pitch_on_grid).lower() == 'on':
                    pitch_semi = float(self._compute_quantized_pitch_semitones())
                # Send spawn with position and amplitude; omit gain/pan (computed natively)
                # Create and send initial params via OSC
                vid = self._osc_id()
                if source and source.get('type') == 'sample' and source.get('file'):
                    self._ipc_client.create_voice(vid, 'sample', str(source.get('file')))
                elif source and source.get('type') == 'oscillator':
                    self._ipc_client.create_voice(vid, 'synth', str(source.get('preset', '')))
                elif source and source.get('type') == 'noise':
                    self._ipc_client.create_voice(vid, 'noise', str(source.get('preset', '')))
                self._ipc_client.update_parameter(vid, 'pitchSemitones', float(pitch_semi))
                self._ipc_client.update_parameter(vid, 'amplitude', float(self.amplitude))
                self._ipc_client.update_parameter(vid, 'positionX', float(self.x))
                self._ipc_client.update_parameter(vid, 'positionY', float(self.y))
                self._ipc_last['gain'] = float(initial_vol)
                self._ipc_last['pan'] = float(initial_pan)
                self._ipc_last['pitch'] = float(pitch_semi)
                self._audio_ready = True
                return
        except Exception:
            pass
        if not hasattr(self.game, 'audio_engine'):
            return
        src_file = self.properties.get('file')
        src_preset = self.properties.get('preset')
        src_type = self.properties.get('type')

        audio = None
        # Compute initial spatial volume/pan consistently with update logic
        initial_vol = 1.0
        initial_pan = 0.0
        try:
            if getattr(self.game, 'current_mouse_pos', None):
                lx, ly = self.game.current_mouse_pos
                dx = self.x - lx
                dy = self.y - ly
                dist = float((dx*dx + dy*dy) ** 0.5)
                listener_r = float(getattr(self.game, 'listener_radius', 0.35 * float(min(self.game.width, self.game.height))))
                emitter_r = float(self._emission_radius)
                audible_r = max(8.0, listener_r + emitter_r)
                near_r = max(8.0, float(self._near_ratio) * audible_r)
                if dist <= near_r:
                    initial_vol = 1.0
                else:
                    rel = max(0.0, dist - near_r)
                    range_r = max(1.0, audible_r - near_r)
                    nd = min(1.0, rel / range_r)
                    initial_vol = float((1.0 - nd) ** 2)
                pan_ref = max(16.0, min(listener_r, emitter_r))
                pan_norm = max(-1.0, min(1.0, dx / pan_ref))
                initial_pan = float(pan_norm)
            # Apply a small floor so spawn is audible in debug even if far
            try:
                import json, os
                cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'audio_engine', 'config', 'audio_engine_config.json')
                spatial_floor = 0.05
                if os.path.exists(cfg_path):
                    with open(cfg_path, 'r', encoding='utf-8') as f:
                        cfg = json.load(f)
                    spatial_floor = float(cfg.get('spatial_init_floor', spatial_floor))
                initial_vol = max(float(spatial_floor), float(initial_vol))
            except Exception:
                initial_vol = max(0.05, float(initial_vol))
        except Exception:
            initial_vol = 1.0
            initial_pan = 0.0

        # Case 1: file-based sample
        if src_file and os.path.isfile(src_file):
            from pyglet_physics_game.audio_engine.processing.sample_rate_utils import smart_audio_loader
            audio, sr, ch = smart_audio_loader(src_file, target_sample_rate=self.game.audio_engine.sample_rate, target_channels=1)
            if audio is None or (isinstance(audio, np.ndarray) and audio.size == 0):
                return
            if isinstance(audio, np.ndarray) and audio.ndim == 2:
                audio = audio[0]
            # If pitch_on_grid is on, offline render pitched sample for better performance
            offline_pitched = False
            if str(self.pitch_on_grid).lower() == 'on':
                # Skip slow offline render when native device owns the clock (miniaudio/WASAPI native path)
                native_fast = False
                try:
                    eng = self.game.audio_engine
                    native_fast = bool(getattr(eng, '_native_output_only', False)) and (eng._native is not None)
                except Exception:
                    native_fast = False
                if not native_fast:
                    try:
                        ps = float(self._compute_quantized_pitch_semitones())
                        rendered = self.game.audio_engine.offline_render_sample_with_signalsmith(audio, pitch_shift=ps, tempo_factor=1.0)
                        if isinstance(rendered, np.ndarray) and rendered.ndim == 2 and rendered.shape[0] == 2:
                            audio = rendered
                            offline_pitched = True
                    except Exception:
                        pass
        # Case 2: generator preset (frequencies/noise)
        elif src_preset and os.path.isfile(src_preset) and src_type in ('frequencies', 'noise'):
            try:
                # Read preset and create native generator bound to this player id
                with open(src_preset, 'r', encoding='utf-8') as f:
                    cfg = json.load(f)
                params = {p['id']: p.get('value') for p in cfg.get('params', [])}
                gain_db = float(params.get('gain_db', 0.0))
                gain = float(10.0 ** (gain_db / 20.0))
                if src_type == 'frequencies':
                    kind = str(params.get('waveform', cfg.get('name','sine'))).strip().lower()
                    if str(self.pitch_on_grid).lower() == 'on':
                        semi = float(self._compute_quantized_pitch_semitones())
                        f = float(440.0 * (2.0 ** (semi / 12.0)))
                    else:
                        f = float(params.get('frequency_hz', 440.0))
                    gen_kind = 'sine'
                    if 'triangle' in kind:
                        gen_kind = 'triangle'
                    elif 'square' in kind:
                        gen_kind = 'square'
                    ok_native = self.game.audio_engine.create_native_generator_player(self._engine_player_id, gen_kind, frequency_hz=f, gain=gain, pan=initial_pan)
                    if not ok_native:
                        try:
                            print(f"WARN: native generator failed, falling back to Python generator for {self._engine_player_id} kind={gen_kind}")
                        except Exception:
                            pass
                        # Python fallback: synthesize a short loop and play
                        frames = int(max(2048, self.game.audio_engine.buffer_size * 16))
                        semi_override = float(self._compute_quantized_pitch_semitones()) if str(self.pitch_on_grid).lower() == 'on' else None
                        block = self.game.audio_engine._render_frequency_block(src_preset, frames, semitone_override=semi_override)
                        if isinstance(block, np.ndarray) and block.ndim == 2 and block.shape[0] == 2:
                            if self.game.audio_engine.create_audio_player(self._engine_player_id, block, effects=None):
                                self.game.audio_engine.play_audio(self._engine_player_id, loop=True, volume=float(initial_vol), pan=float(initial_pan))
                                self._audio_ready = True
                                return
                else:
                    kindn = str(cfg.get('name','white')).strip().lower()
                    if 'pink' in kindn:
                        ok_native = self.game.audio_engine.create_native_generator_player(self._engine_player_id, 'pink', gain=gain, pan=initial_pan)
                    elif 'brown' in kindn or 'red' in kindn:
                        ok_native = self.game.audio_engine.create_native_generator_player(self._engine_player_id, 'brown', gain=gain, pan=initial_pan)
                    else:
                        ok_native = self.game.audio_engine.create_native_generator_player(self._engine_player_id, 'white', gain=gain, pan=initial_pan)
                    if not ok_native:
                        try:
                            print(f"WARN: native noise generator failed, falling back to Python noise for {self._engine_player_id} kind={kindn}")
                        except Exception:
                            pass
                        frames = int(max(2048, self.game.audio_engine.buffer_size * 16))
                        block = self.game.audio_engine._render_noise_block(src_preset, frames)
                        if isinstance(block, np.ndarray) and block.ndim == 2 and block.shape[0] == 2:
                            if self.game.audio_engine.create_audio_player(self._engine_player_id, block, effects=None):
                                self.game.audio_engine.play_audio(self._engine_player_id, loop=True, volume=float(initial_vol), pan=float(initial_pan))
                                self._audio_ready = True
                                return
                # Apply initial volume
                try:
                    self.game.audio_engine.update_player_volume(self._engine_player_id, float(initial_vol))
                except Exception:
                    pass
                self._audio_ready = True
                return
            except Exception:
                return
        else:
            return
        # If audio is mono (N,), wrap to stereo for engine
        if isinstance(audio, np.ndarray) and audio.ndim == 1:
            audio = np.stack([audio, audio]).astype(np.float32)
        # For sample files with pitch-on-grid, create a DSP player so pitch is applied immediately
        use_dsp_pitch = (src_file is not None and os.path.isfile(src_file) and str(self.pitch_on_grid).lower() == 'on')
        # If native path is active, prefer instant native DSP over Python DSP player to avoid spawn latency
        try:
            eng = self.game.audio_engine
            if use_dsp_pitch and getattr(eng, '_native_output_only', False) and eng._native is not None:
                use_dsp_pitch = False
        except Exception:
            pass
        # If we already offline-rendered pitched audio, do NOT apply native DSP again
        try:
            if use_dsp_pitch and isinstance(audio, np.ndarray) and audio.ndim == 2 and audio.shape[0] == 2:
                # We consider this already pitched offline
                use_dsp_pitch = False
        except Exception:
            pass
        if use_dsp_pitch:
            try:
                ps = float(self._compute_quantized_pitch_semitones())
                ok = self.game.audio_engine.create_dsp_player(
                    self._engine_player_id,
                    audio,
                    processing_mode='signalsmith',
                    pitch_shift=ps,
                    tempo_factor=1.0,
                    volume=1.0,
                    pan=0.0
                )
            except Exception:
                ok = False
        else:
            # Generators and non-grid samples: use non-DSP player
            ok = self.game.audio_engine.create_audio_player(self._engine_player_id, audio, effects=None)
        if ok:
            # Start playback with initial volume/pan
            self.game.audio_engine.play_audio(self._engine_player_id, loop=True, volume=float(initial_vol), pan=float(initial_pan))
            # Debug: print native voice info once on spawn (helps catch src/device rate mismatches)
            try:
                vinfo = self.game.audio_engine.get_native_voice_info(self._engine_player_id)
                if vinfo:
                    print(f"NATIVE_DEBUG(spawn): id={self._engine_player_id} info={vinfo}")
            except Exception:
                pass
            # Native-only path: ensure native voice remains authoritative
            try:
                p = self.game.audio_engine.audio_players.get(self._engine_player_id)
                if isinstance(p, dict):
                    p['native_id'] = self._engine_player_id
                    p['native_stream'] = False
            except Exception:
                pass
            # If grid pitch is enabled and we didn't offline-render a pitched buffer, apply native DSP semitones immediately
            try:
                if str(self.pitch_on_grid).lower() == 'on':
                    # Detect offline pitched by checking use_dsp_pitch branch outcome: we neutralize when offline was used
                    # Here, conservatively apply native pitch unless offline path was taken above
                    ps_now = float(self._compute_quantized_pitch_semitones())
                    # Best-effort: forward to native (no-op if running in Python path)
                    if hasattr(self.game.audio_engine, 'set_player_native_dsp'):
                        self.game.audio_engine.set_player_native_dsp(self._engine_player_id, ps_now, 1.0)
                    # Schedule periodic re-quantization on musical clock boundaries (native-only)
                    try:
                        import pyglet
                        if not hasattr(self, '_pitch_quant_state'):
                            self._pitch_quant_state = {'last_pulse': -1.0}
                        if not hasattr(self, '_pitch_sched_attached') or not self._pitch_sched_attached:
                            pyglet.clock.schedule_interval(self._reapply_quantized_pitch, 1.0/60.0)
                            self._pitch_sched_attached = True
                    except Exception:
                        pass
            except Exception:
                pass
            # Observed speed probe: schedule a short measurement window
            try:
                self._rate_probe = {
                    't0': self.game.time,
                    'p0': float(vinfo.get('position_f', 0.0)) if isinstance(vinfo, dict) else 0.0,
                    'frames': 0
                }
            except Exception:
                self._rate_probe = None
        else:
            return
        # For frequencies, pitch_on_grid handled during render via semitone_override
        self._audio_ready = True

    def _reapply_quantized_pitch(self, dt: float = 0.0):
        try:
            if not self._audio_ready or str(self.pitch_on_grid).lower() != 'on':
                return
            ps_now = float(self._compute_quantized_pitch_semitones())
            if self._ipc_client is not None and (self._ipc_last.get('pitch') is None or abs(self._ipc_last['pitch'] - ps_now) > 1e-3):
                try:
                    self._ipc_client.update_parameter(self._osc_id(), "pitchSemitones", float(ps_now))
                except Exception:
                    pass
                self._ipc_last['pitch'] = ps_now
        except Exception:
            pass

    def _compute_quantized_pitch_semitones(self) -> float:
        # Map Y to [-24, +24]
        h = max(1.0, float(self.game.height))
        y_clamped = min(max(self.y, 0.0), h)
        unquant = -24.0 + (y_clamped / h) * 48.0
        # Load grid
        try:
            grid_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'ui', 'grids', 'pitch_grid.json')
            with open(grid_path, 'r', encoding='utf-8') as f:
                grid = json.load(f)
        except Exception:
            return float(unquant)
        base_key = str(grid.get('base_key', 'C')).upper()
        degrees = list(grid.get('degrees_semitones', [0,2,3,5,7,8,10]))
        octave_range = grid.get('octave_range', [-2, 2])
        qmode = str(grid.get('quantize_mode', 'nearest')).lower()
        transpose = int(grid.get('transpose_semitones', 0))
        key_to_semi = {"C":0,"C#":1,"DB":1,"D":2,"D#":3,"EB":3,"E":4,"F":5,"F#":6,"GB":6,"G":7,"G#":8,"AB":8,"A":9,"A#":10,"BB":10,"B":11}
        base = key_to_semi.get(base_key, 0)
        # Build candidate set
        candidates = []
        for octv in range(int(octave_range[0]), int(octave_range[1]) + 1):
            base_oct = base + 12 * octv
            for d in degrees:
                candidates.append(base_oct + int(d) + transpose)
        # Find best per mode
        if not candidates:
            return float(unquant)
        if qmode == 'floor':
            cands = [c for c in candidates if c <= unquant]
            return float(max(cands) if cands else min(candidates, key=lambda c: abs(c - unquant)))
        if qmode == 'ceil':
            cands = [c for c in candidates if c >= unquant]
            return float(min(cands) if cands else min(candidates, key=lambda c: abs(c - unquant)))
        # nearest
        return float(min(candidates, key=lambda c: abs(c - unquant)))

    def _update_audio_spatial(self, dt: float):
        if not self._audio_ready:
            return
        # Listener = mouse
        if not getattr(self.game, 'current_mouse_pos', None):
            return
        lx, ly = self.game.current_mouse_pos
        dx = self.x - lx
        dy = self.y - ly
        dist = float((dx*dx + dy*dy) ** 0.5)
        # Use game's listener radius if available for consistent spatialization
        listener_r = float(getattr(self.game, 'listener_radius', 0.35 * float(min(self.game.width, self.game.height))))
        emitter_r = float(self._emission_radius)
        # Core correlation: audible reach is the sum of listener and emitter radii
        audible_r = max(8.0, listener_r + emitter_r)
        # Near-field keeps full volume to prevent perceived flutter while tracking closely
        near_r = max(8.0, float(self._near_ratio) * audible_r)
        if dist <= near_r:
            vol_target = 1.0
        else:
            rel = max(0.0, dist - near_r)
            range_r = max(1.0, audible_r - near_r)
            nd = min(1.0, rel / range_r)
            # Smooth, perceptual falloff (squared)
            vol_target = float((1.0 - nd) ** 2)
        # Constant-power panning based on a stable local scale (min of radii)
        pan_ref = max(16.0, min(listener_r, emitter_r))
        pan_norm = max(-1.0, min(1.0, dx / pan_ref))
        pan_target = float(pan_norm)
        
        # Parameter smoothing to avoid zipper noise
        tau = float(self._pan_tau_s)
        alpha = 1.0 - math.exp(-max(1e-6, float(dt)) / tau)
        if not hasattr(self, '_smoothed_vol'):
            self._smoothed_vol = vol_target
        if not hasattr(self, '_smoothed_pan'):
            self._smoothed_pan = pan_target
        self._smoothed_vol = (1.0 - alpha) * float(self._smoothed_vol) + alpha * vol_target
        self._smoothed_pan = (1.0 - alpha) * float(self._smoothed_pan) + alpha * pan_target
        vol_out = float(max(0.0, min(1.0, self._smoothed_vol)))
        pan_out = float(max(-1.0, min(1.0, self._smoothed_pan)))
        
        try:
            # IPC: send position updates to let JUCE compute spatialization natively
            if self._ipc_client is not None:
                try:
                    vid = self._osc_id()
                    self._ipc_client.update_parameter(vid, "positionX", float(self.x))
                    self._ipc_client.update_parameter(vid, "positionY", float(self.y))
                except Exception:
                    pass
        except Exception:
            pass

    def _osc_id(self) -> int:
        try:
            return int(id(self) & 0x7fffffff)
        except Exception:
            return int(id(self))
    
    def draw(self):
        """Draw the bullet and its physics aura - now handled by batch system"""
        try:
            # Shapes are now drawn by the batch system automatically
            # The batch system handles proper depth sorting with groups
            # No need to draw manually - the renderer's batch.draw() handles everything
            pass
        except Exception as e:
            print(f"ERROR drawing sound bullet: {e}")
