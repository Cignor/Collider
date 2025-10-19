import os
import pyglet
from pyglet import shapes
import pymunk
import math
import random
import traceback
import time
from typing import List, Tuple, Optional
import queue

# CRITICAL: Disable vsync globally before any pyglet imports
os.environ['PYGLET_VSYNC'] = '0'
os.environ['PYGLET_GL_DEBUG'] = '0'  # Disable OpenGL debug
os.environ['PYGLET_GL_ERROR_CHECKING'] = '0'  # Disable error checking

# Import our modules
from pyglet_physics_game.physics.physics_objects import PhysicsObject
from pyglet_physics_game.physics.physics_tools import (
    PhysicsTool, CollisionTool, WindTool, 
    MagnetTool, TeleporterTool, FreeDrawTool
)
from pyglet_physics_game.physics.physics_manager import PhysicsManager
from pyglet_physics_game.rendering.renderer import Renderer
from pyglet_physics_game.input.input_handler import InputHandler
from pyglet_physics_game.systems.particle_system import ParticleSystem
from pyglet_physics_game.systems.collision_system import CollisionSystem
from pyglet_physics_game.systems.wind_system import WindSystem
from pyglet_physics_game.systems.trail_system import TrailSystem
from pyglet_physics_game.systems.bullet_manager import BulletManager
from pyglet_physics_game.systems.sound_bullet import SoundBullet
from pyglet_physics_game.utils.performance_monitor import PerformanceMonitor
from pyglet_physics_game.utils.debug_utils import DebugUtils
from pyglet_physics_game.ui.theme import UITheme
from pyglet_physics_game.ui.manager import UIManager
from pyglet_physics_game.ui.modular_hud import ModularHUD
from pyglet_physics_game.ui.visual_feedback import VisualFeedback
from pyglet_physics_game.ui.debug_window import DebugWindow
from pyglet_physics_game.ui.command_helper import CommandHelper
from pyglet_physics_game.ipc.server import OscServer
from pyglet_physics_game.ui.audio_settings_model import AudioSettingsModel
from pyglet_physics_game.ui.audio_settings_menu import AudioSettingsMenu
try:
    from pyglet_physics_game.audio_engine.core.audio_engine import UltraOptimizedEngine
    _HAVE_AUDIO_ENGINE = True
except Exception:
    UltraOptimizedEngine = None
    _HAVE_AUDIO_ENGINE = False

class PhysicsGame:
    """Main physics game using Pyglet - ULTRA Performance Mode"""
    
    def __init__(self, width: int = 1920, height: int = 1080):
        self.width = width
        self.height = height
        
        # Centralized, thread-safe queue for all outgoing IPC commands
        self.ipc_command_queue = queue.Queue()
        
        # AGGRESSIVE Pyglet performance settings
        pyglet.options['vsync'] = False
        pyglet.options['gl_minimum_version'] = '2.0'
        pyglet.options['gl_maximum_version'] = '4.6'
        pyglet.options['gl_debug'] = False
        pyglet.options['gl_error_checking'] = False
        
        # Create Pyglet window with MAXIMUM performance
        self.window = pyglet.window.Window(
            width, height, 
            caption="Pyglet Physics Game - ULTRA Performance Mode", 
            vsync=False,
            resizable=False,
            fullscreen=False,
            config=pyglet.gl.Config(
                double_buffer=True,
                depth_size=0,  # No depth buffer needed for 2D
                stencil_size=0,  # No stencil buffer
                alpha_size=8,
                antialias=False  # Disable antialiasing for speed
            )
        )
        self.window.set_location(100, 100)
        
        # Set window clear color to black - background will be drawn by renderer
        pyglet.gl.glClearColor(0.0, 0.0, 0.0, 1.0)
        self.window.set_vsync(False)
        
        # Force disable vsync at system level
        try:
            import ctypes
            ctypes.windll.user32.SetProcessDPIAware()
            # Force disable vsync
            ctypes.windll.user32.SetWindowLongW(self.window._window, -20, 0x00000000)
        except:
            pass
        
        # Create rendering batch for maximum performance
        self.batch = pyglet.graphics.Batch()
        
        # Physics space - OPTIMIZED for speed
        self.space = pymunk.Space()
        self.space.gravity = (0, 0)  # Start with zero gravity
        
        # SMART physics tuning - balance between speed and stability
        self.space.iterations = 6  # Balanced: not too low (unstable) or high (slow)
        self.space.sleep_time_threshold = 0.5  # Balanced: bodies sleep when truly still
        self.space.collision_slop = 0.1  # Balanced: small tolerance for stability
        self.space.collision_bias = 0.1  # Balanced: small bias for stability
        
        # Physics timing control for stability
        self.fixed_timestep = True  # Use fixed timestep for stability
        self.physics_timestep = 1.0 / 60.0  # 60Hz physics updates
        self.max_substeps = 5  # Maximum substeps for variable timestep fallback
        self.accumulated_time = 0.0  # Accumulate time for fixed timestep
        
        # Game state
        self.physics_objects = []
        self.collision_shapes = []
        self.sound_bullets = []  # NEW: Sound bullets list
        self.active_voice_ids = set()
        self.global_physics_enabled = True  # Global physics state for all bullets
        # Listener radius for spatial audio (mouse-based listener)
        self.listener_radius = 0.35 * float(min(self.width, self.height))
        # Visualize listener radius (drawn each frame at mouse)
        self._listener_visual = shapes.Circle(0, 0, int(self.listener_radius), color=(0, 200, 255))
        self._listener_visual.opacity = 40
        # Authoritative set of active audio voiceIds (gate for IPC)
        self.active_voice_ids = set()
        self.tools = []
        self.current_tool_index = 0
        self.spawn_timer = 0
        self.spawn_interval = 300
        
        # Global physics control
        self.space_held = False
        self.base_gravity = (0, 0)  # Start with zero gravity
        self.current_gravity = (0, 0)  # Start with zero gravity
        self.base_wind_strength = 0
        self.current_wind_strength = 0
        self.wind_direction = 0
        
        # Erase tool
        self.erase_tool_active = False
        self.erase_radius = 50
        self.erase_mouse_pos = None
        
        # Mouse tracking
        self.current_mouse_pos = None
        
        # Performance flags - ULTRA performance mode
        self.debug_mode = False  # Disable ALL debug output
        self.max_fps = 1200  # Target FPS
        self.ultra_performance_mode = True
        self.max_objects = 100  # Maximum physics objects for performance
        
        # Additional performance controls
        self.frame_skip = False  # Skip frames for performance

        self.disable_particles = False  # Disable particle effects
        self.disable_trails = False  # Disable movement trails
        
        # Key state handler for combo detection (e.g., Delete + Middle)
        try:
            self.keys = pyglet.window.key.KeyStateHandler()
            self.window.push_handlers(self.keys)
        except Exception:
            self.keys = None
        # Explicit delete key latch (independent from KeyStateHandler)
        self._delete_down = False

        # Initialize subsystems
        self._init_subsystems()
        # Initialize failsafe and spawn throttle state
        self._backspace_down = False
        self._failsafe_active = False
        self._failsafe_triggered = False
        self._failsafe_start_time = 0.0
        self._failsafe_progress = 0.0
        self._failsafe_mouse_pos = None
        self.spawn_throttle_secs = getattr(self, 'spawn_throttle_secs', 0.10)
        self._next_spawn_allowed_time = getattr(self, '_next_spawn_allowed_time', 0.0)
        self._spawn_lock_side = getattr(self, '_spawn_lock_side', None)
        
        # Initialize UI theme (fonts, colors)
        self.ui_theme = UITheme()
        self.ui_theme.load_default_fonts()
        
        # Initialize enhanced grid system
        from pyglet_physics_game.ui.modular_grid_system import ModularGridSystem
        self.grid_system = ModularGridSystem(self.width, self.height)
        self.grid_system.game = self  # Pass game reference
        
        # Initialize global coordinate system with grid integration
        from pyglet_physics_game.ui.coordinate_system import initialize_coordinate_system
        self.coordinate_manager = initialize_coordinate_system(self.width, self.height, self.grid_system)
        
        # Initialize mouse system with dual-circle snapping
        from pyglet_physics_game.ui.mouse_system import MouseSystem
        from pyglet_physics_game.ui.snap_zones import SnapZoneManager
        self.mouse_system = MouseSystem(self)
        self.mouse_system.coordinate_manager = self.coordinate_manager  # Pass coordinate manager
        self.snap_zone_manager = SnapZoneManager(self)
        
        # Initialize UI manager (experimental menu host)
        # Pass model, renderer batch and UI group so the manager can wire menus properly
        # Create model first so manager can own the only AudioSettingsMenu instance
        self.audio_settings_model = AudioSettingsModel()
        self.ui_manager = UIManager(self, self.audio_settings_model, self.batch, self.renderer.ui_group if hasattr(self, 'renderer') else None)
        self.ui_manager.coordinate_manager = self.coordinate_manager  # Pass coordinate manager
        # Legacy selection menu removed; no menu attribute
        # IPC handshake (notify JUCE app that Python UI is alive)
        try:
            from pyglet_physics_game.ipc.client import get_ipc_client
            self.ipc_command_queue.put({"hello": {"from": "python", "ts": time.time()}})
            # Also push listener config from json and initial position
            try:
                import json, os
                cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config', 'audio_engine_config.json')
                if os.path.exists(cfg_path):
                    with open(cfg_path, 'r', encoding='utf-8') as f:
                        cfg = json.load(f)
                    listener = cfg.get('listener', {})
                    rad = float(listener.get('radius_px', float(self.listener_radius)))
                    near = float(listener.get('near_ratio', 0.12))
                    self.ipc_command_queue.put({"listener.set": {"radius_px": rad, "near_ratio": near}})
            except Exception:
                pass
        except Exception:
            pass
        
        # Initialize modular HUD
        self.hud = ModularHUD(self)
        self.hud.set_coordinate_manager(self.coordinate_manager)  # Pass coordinate manager
        
        # Initialize visual feedback system
        self.visual_feedback = VisualFeedback(self)
        
        # Initialize debug window
        self.debug_window = DebugWindow(self)
        
        # Initialize command helper
        self.command_helper = CommandHelper(self)

        # Effect zones disabled for debugging simplicity
        self.effect_zones = None
        
        # Audio engine dismantle: disable runtime audio and keep UI/game running
        self.audio_engine = None
        if _HAVE_AUDIO_ENGINE and False:
            # guarded off during dismantle
            try:
                self.audio_engine = UltraOptimizedEngine()
            except Exception as e:
                self.audio_engine = None
                print(f"ERROR initializing audio engine: {e}")
        
        # Initialize tools
        self._init_tools()
        
        # Create initial scene (clean start - no default objects)
        self._create_initial_scene()
        
        # Set up event handlers
        self._setup_events()
        
        # Start physics simulation - MAXIMUM FPS with custom timing
        self.last_update = time.time()
        self.target_dt = 1.0 / self.max_fps
        self.frame_counter = 0
        self._last_listener_ipc_time = 0.0
        pyglet.clock.schedule(self.update)

        # Audio settings UI + OSC server (JUCE -> Python)
        self.audio_osc_server = OscServer()
        self.audio_osc_server.start()
        self.audio_settings_model = AudioSettingsModel()
        self.ui_manager = UIManager(self, self.audio_settings_model, self.renderer.batch, self.renderer.ui_group)
        # Now that our server is running, ask JUCE to send us the device info.
        print("[GAME CORE] OSC server started. Requesting initial state from JUCE engine...")
        try:
            from pyglet_physics_game.ipc.client import get_ipc_client
            ipc_client = get_ipc_client()
            # Send explicit request to JUCE to resend all /info/... messages
            ipc_client._send("/settings/requestInfo")
        except Exception as e:
            print(f"[GAME CORE] Failed to send initial info request: {e}")
        
        
        print("DEBUG: PhysicsGame initialized - ULTRA Performance Mode")

    def dumpCurrentStateToLog(self) -> None:
        try:
            print("--- PYTHON STATE DUMP TRIGGERED ---")
            # Menu visibility
            try:
                vis = getattr(self, 'audio_settings_menu').visible if hasattr(self, 'audio_settings_menu') else None
                print(f"[UI] audio_settings_menu.visible={vis}")
            except Exception:
                print("[UI] audio_settings_menu.visible=<error>")

            # Model contents
            try:
                if hasattr(self, 'audio_settings_model') and self.audio_settings_model:
                    print(f"[MODEL] {vars(self.audio_settings_model)}")
                else:
                    print("[MODEL] <none>")
            except Exception:
                print("[MODEL] <error>")

            # Sound bullets count
            try:
                cnt = len(self.sound_bullets) if hasattr(self, 'sound_bullets') and self.sound_bullets is not None else 0
                print(f"[AUDIO] sound_bullets.count={cnt}")
            except Exception:
                print("[AUDIO] sound_bullets.count=<error>")

            # IPC client status (best-effort)
            try:
                from pyglet_physics_game.ipc.client import get_ipc_client
                ipc = get_ipc_client()
                print(f"[IPC] client target=127.0.0.1:9001 object={'ok' if ipc is not None else 'none'}")
            except Exception:
                print("[IPC] client=<error>")

            # OSC inbox size
            try:
                qsize = self.audio_osc_server.inbox.qsize() if hasattr(self, 'audio_osc_server') else -1
                print(f"[OSC] inbox.size={qsize}")
            except Exception:
                print("[OSC] inbox.size=<error>")
        except Exception as e:
            print(f"[DEBUG] dumpCurrentStateToLog failed: {e}")
    
    def fire_sound_bullet(self, x, y, selector):
        """Fire a sound bullet using properties from the specified selector"""
        try:
            print(f"DEBUG: fire_sound_bullet called for {selector} at ({x}, {y})")
            # Enforce spawn throttle here too (defense-in-depth)
            try:
                now = time.time()
                if now < getattr(self, '_next_spawn_allowed_time', 0.0):
                    print("[SPAWN] throttled at fire_sound_bullet")
                    return
            except Exception:
                pass
            
            # Check if ui_manager exists
            if not hasattr(self, 'ui_manager'):
                print(f"DEBUG: No ui_manager found")
                return
            
            # Check if menu exists
            if not hasattr(self.ui_manager, 'menu'):
                print(f"DEBUG: No menu found in ui_manager")
                return
            
            # Check active preset
            print(f"DEBUG: Active preset: {getattr(self.ui_manager.menu, 'active_preset', 'None')}")
            
            # Get properties from the audio bank preset
            properties = self._get_selector_properties(selector)
            if not properties:
                print(f"DEBUG: No properties found for {selector} selector")
                return
            # Also get file/preset info for audio source
            source_info = self._get_selector_source(selector)
            
            # Optional: quantize spawn to musical clock subdivision (non-blocking)
            def _spawn_now():
                # Create sound bullet with properties and source info
                # SoundBullet will use source_info to create audio
                try:
                    b = SoundBullet(x, y, {**properties, **(source_info or {})}, self)
                except TypeError:
                    b = SoundBullet(x, y, properties, self)
                # Track and update this bullet explicitly (backup manager API doesn't store SoundBullet)
                if not hasattr(self, 'sound_bullets'):
                    self.sound_bullets = []
                self.sound_bullets.append(b)
                print(f"DEBUG: Fired {selector} sound bullet at ({x}, {y}) with properties: {properties}")

            try:
                import json, os
                cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'audio_engine', 'config', 'audio_engine_config.json')
                quantize_enabled = False
                subdiv = '1/8'
                if os.path.exists(cfg_path):
                    with open(cfg_path, 'r', encoding='utf-8') as f:
                        cfg = json.load(f)
                    quantize_enabled = bool(cfg.get('clock_quantize_enabled', False))
                    subdiv = str(cfg.get('clock_quantize_subdiv', '1/8'))
                if quantize_enabled and hasattr(self, 'audio_engine') and self.audio_engine:
                    ts = self.audio_engine.get_clock_status()
                    bpm = float(ts.get('bpm', 120.0))
                    ppq = float(ts.get('ppq', 480.0))
                    pulse = float(ts.get('pulse', 0.0))
                    denom = 8
                    try:
                        if '/' in subdiv:
                            denom = int(subdiv.split('/')[-1])
                    except Exception:
                        denom = 8
                    pulses_per_second = (bpm / 60.0) * ppq
                    grid = ppq / float(denom)
                    if pulses_per_second > 0 and grid > 0:
                        import pyglet
                        next_pulse = (int(pulse / grid) + 1) * grid
                        delta_pulse = max(0.0, next_pulse - pulse)
                        delay = float(delta_pulse) / float(pulses_per_second)
                        pyglet.clock.schedule_once(lambda _dt: _spawn_now(), delay)
                        return
            except Exception:
                pass
            _spawn_now()
            # Update throttle window
            try:
                self._next_spawn_allowed_time = time.time() + float(getattr(self, 'spawn_throttle_secs', 0.10))
            except Exception:
                pass
            
        except Exception as e:
            print(f"ERROR firing sound bullet: {e}")
            traceback.print_exc()
    
    def _get_selector_properties(self, selector):
        """Get properties for the specified selector from audio bank preset"""
        try:
            print(f"DEBUG: _get_selector_properties called for {selector}")
            
            if not hasattr(self, 'ui_manager'):
                print(f"DEBUG: No ui_manager found")
                return None
                
            if not hasattr(self.ui_manager, 'menu'):
                print(f"DEBUG: No menu found in ui_manager")
                return None
                
            if self.ui_manager.menu.active_preset is None:
                print(f"DEBUG: No active preset (active_preset is {self.ui_manager.menu.active_preset})")
                return None
            
            # Load the audio bank preset
            import json
            import os
            root_dir = os.path.dirname(os.path.dirname(__file__))  # Go up one level from game/ to pyglet_physics_game/
            presets_file = os.path.join(root_dir, 'audio_bank_preset.json')
            print(f"DEBUG: Looking for presets file at: {presets_file}")
            
            with open(presets_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            print(f"DEBUG: Loaded presets data, looking for preset: {self.ui_manager.menu.active_preset}")
            preset_data = data.get(str(self.ui_manager.menu.active_preset), {})
            print(f"DEBUG: Preset data: {preset_data}")
            
            selector_key = f'{selector}_selector'
            print(f"DEBUG: Looking for selector key: {selector_key}")
            selector_data = preset_data.get(selector_key, {})
            print(f"DEBUG: Selector data: {selector_data}")
            
            properties = selector_data.get('properties', {})
            print(f"DEBUG: Properties: {properties}")
            return properties
            
        except Exception as e:
            print(f"ERROR getting selector properties: {e}")
            import traceback
            traceback.print_exc()
            return None

    def _get_selector_source(self, selector):
        """Get source info (file or preset path) for the specified selector from audio bank preset"""
        try:
            if not hasattr(self, 'ui_manager'):
                return None
            if not hasattr(self.ui_manager, 'menu') or self.ui_manager.menu.active_preset is None:
                return None
            import json, os
            root_dir = os.path.dirname(os.path.dirname(__file__))
            presets_file = os.path.join(root_dir, 'audio_bank_preset.json')
            with open(presets_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
            preset_data = data.get(str(self.ui_manager.menu.active_preset), {})
            selector_key = f'{selector}_selector'
            selector_data = preset_data.get(selector_key, {})
            # Normalize path to absolute
            src = {}
            if 'file' in selector_data and selector_data['file']:
                fp = selector_data['file']
                if not os.path.isabs(fp):
                    repo_root = os.path.dirname(root_dir)
                    fp = os.path.normpath(os.path.join(repo_root, fp))
                src['file'] = fp
                src['type'] = 'samples'
            elif 'preset' in selector_data and selector_data['preset']:
                pp = selector_data['preset']
                if not os.path.isabs(pp):
                    repo_root = os.path.dirname(root_dir)
                    pp = os.path.normpath(os.path.join(repo_root, pp))
                src['preset'] = pp
                src['type'] = selector_data.get('type', 'preset')
            return src
        except Exception as e:
            print(f"ERROR getting selector source: {e}")
            return None
    
    def _init_subsystems(self):
        """Initialize all game subsystems"""
        try:
            # Initialize physics manager
            self.physics_manager = PhysicsManager(self)
            
            # Initialize renderer
            self.renderer = Renderer(self)
            
            # Initialize input handler
            self.input_handler = InputHandler(self)
            
            # Initialize particle system
            self.particle_system = ParticleSystem(self)
            
            # Initialize collision system
            self.collision_system = CollisionSystem(self)
            
            # Initialize wind system
            self.wind_system = WindSystem(self)
            
            # Initialize trail system
            self.trail_system = TrailSystem(self)
            
            # Initialize bullet manager
            self.bullet_manager = BulletManager(self)
            self.bullet_manager.set_trail_system(self.trail_system)
            
            # Initialize performance monitor
            self.performance_monitor = PerformanceMonitor(self)
            
            # Initialize debug utilities
            self.debug_utils = DebugUtils(self)
            
        except Exception as e:
            print(f"ERROR initializing subsystems: {e}")
            traceback.print_exc()
    
    def _init_tools(self):
        """Initialize physics tools"""
        try:
            self.tools = [
                CollisionTool(),
                WindTool(),
                MagnetTool(),
                TeleporterTool(),
                FreeDrawTool()
            ]
            # Start with FreeDraw tool
            self.current_tool_index = 4
            self.tools[self.current_tool_index].activate()
            
        except Exception as e:
            print(f"ERROR initializing tools: {e}")
            traceback.print_exc()
    
    def _setup_events(self):
        """Set up Pyglet event handlers"""
        try:
            @self.window.event
            def on_draw():
                self.draw()
                # Removed obsolete brute-force menu update
                
            @self.window.event
            def on_key_press(symbol, modifiers):
                print(f"[DEBUG][game_core.py] on_key_press received symbol: {symbol}")
                # Give menu first chance to consume key events
                try:
                    if hasattr(self, 'ui_manager') and self.ui_manager.audio_settings_menu.visible and self.ui_manager.audio_settings_menu.on_key_press(symbol, modifiers):
                        return
                except Exception:
                    pass
                print(f"[DEBUG][game_core.py] Forwarding key press to InputHandler...")
                self.input_handler.handle_key_press(symbol, modifiers)
                if symbol == pyglet.window.key.DELETE:
                    try:
                        print("[DELETE] key event received by window")
                    except Exception:
                        pass
                    self._delete_down = True
                
            @self.window.event
            def on_key_release(symbol, modifiers):
                # Give UIManager first chance to consume key release (Shift closes selection menu)
                try:
                    if hasattr(self, 'ui_manager') and self.ui_manager.on_key_release(symbol, modifiers):
                        return
                except Exception:
                    pass
                self.input_handler.handle_key_release(symbol, modifiers)
                if symbol == pyglet.window.key.DELETE:
                    self._delete_down = False
                
            @self.window.event
            def on_mouse_press(x, y, button, modifiers):
                # Delegate to UIManager first (handles Shift+click menus and settings panel)
                try:
                    if hasattr(self, 'ui_manager') and self.ui_manager.on_mouse_press(x, y, button, modifiers):
                        return
                except Exception:
                    pass
                self.input_handler.handle_mouse_press(x, y, button, modifiers)
                
            @self.window.event
            def on_mouse_release(x, y, button, modifiers):
                try:
                    if hasattr(self, 'ui_manager') and self.ui_manager.on_mouse_release(x, y, button, modifiers):
                        return
                except Exception:
                    pass
                self.input_handler.handle_mouse_release(x, y, button, modifiers)
                
            @self.window.event
            def on_mouse_motion(x, y, dx, dy):
                # Menu currently has no motion handling; fall-through to game
                self.input_handler.handle_mouse_motion(x, y, dx, dy)
                if hasattr(self, '_bullet_delete_pos') and self._bullet_delete_pos:
                    try:
                        print(f"[DELETE] window motion at ({x:.1f},{y:.1f})")
                    except Exception:
                        pass
                # Fallback overlay drive directly from window events if input path missed
                if getattr(self, '_delete_down', False):
                    self._bullet_delete_pos = (x, y)
                # Stream listener position to JUCE for native spatialization
                try:
                    from pyglet_physics_game.ipc.client import get_ipc_client
                    self.ipc_command_queue.put({"listener.pos": {"x": float(x), "y": float(y)}})
                except Exception:
                    pass
                
            @self.window.event
            def on_mouse_drag(x, y, dx, dy, buttons, modifiers):
                self.input_handler.handle_mouse_drag(x, y, dx, dy, buttons, modifiers)
                
            @self.window.event
            def on_mouse_scroll(x, y, scroll_x, scroll_y):
                # Delegate to UIManager first (consumes when menus active)
                try:
                    if hasattr(self, 'ui_manager'):
                        consumed = self.ui_manager.on_mouse_scroll(x, y, scroll_y)
                        if consumed:
                            return
                except Exception:
                    pass
                self.input_handler.handle_mouse_scroll(x, y, scroll_x, scroll_y)
            
            @self.window.event
            def on_close():
                # Ensure audio engine is cleaned up to avoid leaks/crashes
                try:
                    if hasattr(self, 'audio_engine') and self.audio_engine:
                        self.audio_engine.cleanup()
                except Exception:
                    pass
                try:
                    pyglet.app.exit()
                except Exception:
                    pass
                
        except Exception as e:
            print(f"ERROR setting up events: {e}")
            traceback.print_exc()
    
    def _create_initial_scene(self):
        """Create initial scene - clean start with no default objects"""
        try:
            # Clean start - no default objects spawned
            # Only create boundary walls using collision system
            self.collision_system.create_boundary_walls()
            
        except Exception as e:
            print(f"ERROR in _create_initial_scene: {e}")
            traceback.print_exc()
    
    def _add_test_collision_shape(self):
        """Add a test collision shape to verify rendering works"""
        try:
            # Create a test static body
            body = pymunk.Body(body_type=pymunk.Body.STATIC)
            
            # Create a test line segment
            start_vec = pymunk.Vec2d(100, 100)
            end_vec = pymunk.Vec2d(300, 200)
            test_shape = pymunk.Segment(body, start_vec, end_vec, 20)  # thickness = 40
            
            # Add to physics space
            self.space.add(body, test_shape)
            
            # Add to collision shapes list for rendering
            self.collision_shapes.append(test_shape)
            
        except Exception as e:
            print(f"ERROR adding test collision shape: {e}")
            traceback.print_exc()
    
    def spawn_object(self, x: int, y: int, material_type=None):
        """Spawn a random physics object at position with optional material type"""
        try:
            # Ensure spawn position is within visible bounds
            x = max(100, min(x, self.width - 100))
            y = max(100, min(y, self.height - 200))
            
            # Choose random material if not specified
            if material_type is None:
                from systems.bullet_data import MaterialType
                material_type = random.choice([
                    MaterialType.METAL, MaterialType.ENERGY, 
                    MaterialType.PLASMA, MaterialType.CRYSTAL
                ])
            
            object_type = random.choice(['circle', 'rectangle', 'triangle'])
            
            if object_type == 'circle':
                body = pymunk.Body(1, pymunk.moment_for_circle(1, 0, 25))
                body.position = x, y
                shape = pymunk.Circle(body, 25)
                color = (255, 100, 100)
                mass = 1.0
                
            elif object_type == 'rectangle':
                body = pymunk.Body(1, pymunk.moment_for_box(1, (50, 30)))
                body.position = x, y
                shape = pymunk.Poly.create_box(body, (50, 30))
                color = (100, 255, 100)
                mass = 1.5
                
            else:  # triangle
                body = pymunk.Body(1, pymunk.moment_for_poly(1, [(-25, -15), (25, -15), (0, 25)]))
                body.position = x, y
                shape = pymunk.Poly(body, [(-25, -15), (25, -15), (0, 25)])
                color = (100, 100, 255)
                mass = 0.8
            
            shape.friction = 0.7
            shape.elasticity = 0.5
            
            self.space.add(body, shape)
            
            # Create physics object with bullet data
            physics_obj = PhysicsObject(body, shape, color, material_type, mass)
            self.physics_objects.append(physics_obj)
            
            # Integrate with trail system
            if hasattr(self, 'trail_system') and physics_obj.bullet_data.trail_enabled:
                self.trail_system._integrate_object_with_trails(physics_obj)
            
        except Exception as e:
            print(f"ERROR in spawn_object: {e}")
            traceback.print_exc()
    
    def update(self, dt):
        """Update physics simulation - SMART OPTIMIZED for stability + performance"""
        # Drain OSC inbox from JUCE → Python thread first to sync the model
        try:
            # Drain the OSC message inbox from the server thread
            # and apply updates to our data model.
            while not self.audio_osc_server.inbox.empty():
                try:
                    address, args = self.audio_osc_server.inbox.get_nowait()
                    print(f"[GAME CORE] Processing message from inbox: {address}")
                    self.audio_settings_model.apply_message(address, args)
                except queue.Empty:
                    break
        except queue.Empty:
            # This can happen in rare race conditions, it's safe to ignore.
            pass
        try:
            # Update performance monitoring
            self.performance_monitor.start_update()
            
            # SMART physics stepping - stable and performant
            if self.fixed_timestep:
                # Use fixed timestep for physics stability - accumulate time
                self.accumulated_time += dt
                
                # Step physics at fixed 60Hz intervals
                while self.accumulated_time >= self.physics_timestep:
                    self.space.step(self.physics_timestep)
                    self.accumulated_time -= self.physics_timestep
            else:
                # Fallback to variable timestep with substeps for stability
                if dt > 0:
                    # Limit maximum timestep to prevent large jumps
                    dt = min(dt, 0.1)  # Max 100ms
                    
                    # Use substeps for stability
                    substeps = min(int(dt / self.physics_timestep) + 1, self.max_substeps)
                    substep_dt = dt / substeps
                    
                    for _ in range(substeps):
                        self.space.step(substep_dt)
            
            # Update all subsystems
            self.physics_manager.update(dt)
            self.particle_system.update(dt)
            self.collision_system.update(dt)
            self.wind_system.update(dt)
            # TEMP: Menu visuals will be forced after rendering in on_draw()
            
            # Update bullet data for all physics objects
            for obj in self.physics_objects:
                obj.update_bullet_data()
            
            # Update trail system (now integrated with bullet data)
            self.trail_system.update(dt)
            
            # Update SoundBullets explicitly (visuals, physics sync, IPC)
            try:
                if hasattr(self, 'sound_bullets'):
                    inactive = []
                    for sb in list(self.sound_bullets):
                        if not sb.update(dt):
                            inactive.append(sb)
                    for sb in inactive:
                        try:
                            sb.remove()
                        except Exception:
                            pass
                    if inactive:
                        self.sound_bullets = [sb for sb in self.sound_bullets if sb.is_active]
            except Exception:
                pass

            # Update all other systems that might use bullet data
            self.physics_manager.update(dt)
            self.particle_system.update(dt)
            self.collision_system.update(dt)
            self.wind_system.update(dt)
            
            # BACKSPACE failsafe processing: update progress + trigger after 1s
            try:
                if getattr(self, '_failsafe_active', False):
                    # follow mouse
                    if self.current_mouse_pos is not None:
                        self._failsafe_mouse_pos = self.current_mouse_pos
                    # progress
                    t = time.time()
                    start = getattr(self, '_failsafe_start_time', t)
                    self._failsafe_progress = max(0.0, min(1.0, (t - start) / 1.0))
                    if self._failsafe_progress >= 1.0 and not getattr(self, '_failsafe_triggered', False):
                        # perform failsafe: clear all bullets (SoundBullets and manager bullets)
                        try:
                            if hasattr(self, 'sound_bullets'):
                                for sb in list(self.sound_bullets):
                                    try:
                                        sb.remove()
                                    except Exception:
                                        pass
                                self.sound_bullets.clear()
                        except Exception:
                            pass
                        try:
                            if hasattr(self, 'bullet_manager'):
                                self.bullet_manager.clear_all_bullets()
                        except Exception:
                            pass
                        try:
                            from pyglet_physics_game.ipc.client import get_ipc_client
                            self.ipc_command_queue.put({"engine.stopAll": {}})
                        except Exception:
                            pass
                        self._failsafe_triggered = True
                else:
                    self._failsafe_progress = 0.0
            except Exception:
                pass

            # Voice stealing: keep at most 100 most audible bullets unmuted with simple hysteresis
            try:
                if hasattr(self, 'audio_engine') and self.audio_engine:
                    listener_r = float(self.listener_radius)
                    # Read voice stealing config
                    try:
                        import json, os
                        cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'audio_engine', 'config', 'audio_engine_config.json')
                        with open(cfg_path, 'r', encoding='utf-8') as f:
                            cfg = json.load(f)
                        max_voices = int(cfg.get('max_audible_voices', 100))
                        low_thresh = float(cfg.get('voice_steal_low_thresh', 0.02))
                        high_thresh = float(cfg.get('voice_steal_high_thresh', 0.04))
                    except Exception:
                        max_voices = 100
                        low_thresh = 0.02
                        high_thresh = 0.04
                    # Score = 1.0 inside near field; falls off with distance and scaled by amplitude
                    scored = []
                    if self.current_mouse_pos is not None:
                        lx, ly = self.current_mouse_pos
                        for b in self.bullet_manager.sound_bullets: # Use bullet_manager.sound_bullets
                            dx = b.x - lx
                            dy = b.y - ly
                            dist = math.sqrt(dx * dx + dy * dy)
                            audible_r = max(8.0, listener_r + float(getattr(b, 'amplitude', 100.0)))
                            near_r = max(8.0, 0.12 * audible_r)
                            if dist <= near_r:
                                vol_pred = 1.0
                            else:
                                rel = max(0.0, dist - near_r)
                                range_r = max(1.0, audible_r - near_r)
                                nd = min(1.0, rel / range_r)
                                vol_pred = (1.0 - nd) ** 2
                            # Weight by amplitude to prioritize larger emitters when similar distance
                            weight = vol_pred * (1.0 + float(getattr(b, 'amplitude', 100.0)) / (listener_r + 1e-6))
                            scored.append((weight, b))
                        # Keep top-N (config)
                        scored.sort(key=lambda t: t[0], reverse=True)
                        keep = set(id(b) for _, b in scored[:max_voices])
                        for w, b in scored:
                            bid = id(b)
                            desired_mute = (bid not in keep)
                            # Track previous state on bullet to add hysteresis
                            prev_mute = getattr(b, '_muted_by_steal', False)
                            if desired_mute and not prev_mute:
                                # Only mute if weight clearly below low threshold
                                if w < low_thresh:
                                    try:
                                        self.audio_engine.update_player_mute(b._engine_player_id, True)
                                        b._muted_by_steal = True
                                    except Exception:
                                        pass
                            elif not desired_mute and prev_mute:
                                # Only unmute if clearly above high threshold
                                if w > high_thresh:
                                    try:
                                        self.audio_engine.update_player_mute(b._engine_player_id, False)
                                        b._muted_by_steal = False
                                    except Exception:
                                        pass
                            else:
                                # Set state directly when stable
                                try:
                                    self.audio_engine.update_player_mute(b._engine_player_id, desired_mute)
                                    b._muted_by_steal = desired_mute
                                except Exception:
                                    pass
            except Exception:
                pass
            
            # DISABLED: Auto-spawn objects (clean start)
            # self.spawn_timer += 1
            # if self.spawn_timer >= self.spawn_interval:
            #     self.spawn_timer = 0
            #     x = random.randint(100, self.width - 100)
            #     y = self.height - 100  # Spawn near top
            #     self.spawn_object(x, y)
            
            # Update visual feedback system
            self.visual_feedback.update(dt)

            # Effect zones disabled
            
            # Update mouse system
            if hasattr(self, 'mouse_system'):
                self.mouse_system.update(dt)

            # Stream listener position to JUCE at ~60 Hz
            try:
                if self.current_mouse_pos is not None:
                    now = time.time()
                    if now - getattr(self, '_last_listener_ipc_time', 0.0) >= (1.0/60.0):
                        x, y = self.current_mouse_pos
                        self.ipc_command_queue.put({"listener.pos": {"x": float(x), "y": float(y)}})
                        self._last_listener_ipc_time = now
            except Exception:
                pass
            
            # Update grid system based on spacebar state (only when state changes)
            if hasattr(self, 'grid_system'):
                if self.space_held and self.grid_system.current_grid != "physics_grid":
                    # Switch to physics grid when spacebar is held
                    self.grid_system.set_grid("physics_grid")
                    # Add physics snap zones
                    self._update_physics_snap_zones()
                elif not self.space_held and self.grid_system.current_grid != "design_grid":
                    # Switch to design grid when spacebar is released
                    self.grid_system.set_grid("design_grid")
                    # Remove physics snap zones
                    self._clear_physics_snap_zones()
            
            # Update debug window
            self.debug_window.update(dt)
            
            # Update command helper
            self.command_helper.update(dt)
            
            # Process and send all queued IPC commands
            self._drain_ipc_queue()
            
            # Update UI manager
            if hasattr(self, 'ui_manager'):
                self.ui_manager.update(dt)
            
            # Update performance monitoring
            self.performance_monitor.end_update()
            
            # Increment frame counter
            self.frame_counter += 1
            
        except Exception as e:
            print(f"ERROR in update: {e}")
            traceback.print_exc()
    
    def _drain_ipc_queue(self):
        """Drain the IPC command queue and send messages."""
        try:
            from pyglet_physics_game.ipc.client import get_ipc_client
            ipc = get_ipc_client()
            if ipc is None:
                # If client not available, clear queue to prevent buildup
                while not self.ipc_command_queue.empty():
                    try:
                        self.ipc_command_queue.get_nowait()
                    except queue.Empty:
                        break
                return
            
            # Send up to a max number of messages per frame to avoid blocking
            max_messages_per_frame = 100
            pending_positions = {}
            pending_listener = None
            for _ in range(max_messages_per_frame):
                try:
                    command = self.ipc_command_queue.get_nowait()
                    # Map legacy JSON messages to OSC calls
                    if isinstance(command, dict) and command:
                        key = next(iter(command.keys()))
                        payload = command[key]
                        if key == "listener.set":
                            # Rationale: Keep local overlay in sync with engine listener settings
                            rad_px = float(payload.get("radius_px", 0.0))
                            near_ratio = float(payload.get("near_ratio", 0.0))
                            try:
                                # Update local values for overlay
                                self.listener_radius = rad_px
                                self.listener_near_ratio = near_ratio if hasattr(self, 'listener_near_ratio') else near_ratio
                            except Exception:
                                pass
                            ipc.set_listener(rad_px, near_ratio)
                        elif key == "listener.pos":
                            pending_listener = (float(payload.get("x", 0.0)), float(payload.get("y", 0.0)))
                        elif key == "engine.stopAll":
                            ipc.stop_all()
                        elif key == "spawnBullet":
                            vid = int(payload.get("voiceId", 0))
                            src = payload.get("source", {}) or {}
                            stype = str(src.get("type", "")).lower()
                            # initial position and amplitude
                            pos = payload.get("position", {}) or {}
                            x0 = float(pos.get("x", 0.0)) if isinstance(pos, dict) else 0.0
                            y0 = float(pos.get("y", 0.0)) if isinstance(pos, dict) else 0.0
                            amp0 = 0.0
                            if "properties" in payload and isinstance(payload["properties"], dict):
                                a = payload["properties"].get("amplitude")
                                if a is not None:
                                    amp0 = float(a)
                            # Use extended create only (single message, atomic)
                            # derive flags from properties
                            props = payload.get("properties", {}) if isinstance(payload.get("properties", {}), dict) else {}
                            pitch_on_grid = None
                            if "pitch_on_grid" in props:
                                pitch_on_grid = str(props.get("pitch_on_grid", "")).lower() in ("on", "true", "1")
                            elif "pitchOnGrid" in props:
                                pitch_on_grid = bool(props.get("pitchOnGrid"))
                            looping = None
                            if "looping" in props:
                                v = props.get("looping")
                                looping = (str(v).lower() in ("on", "true", "1")) if isinstance(v, str) else bool(v)
                            volume = props.get("volume") if isinstance(props.get("volume"), (int, float)) else None

                            if stype == "sample":
                                ipc.create_voice_ex(vid, "sample", str(src.get("file", "")), x0, y0, amp0, pitch_on_grid=pitch_on_grid, looping=looping, volume=volume)
                            elif stype == "oscillator":
                                ipc.create_voice_ex(vid, "synth", str(payload.get("audio", {}).get("preset", src.get("preset", ""))), x0, y0, amp0, pitch_on_grid=pitch_on_grid, looping=looping, volume=volume)
                            elif stype == "noise":
                                ipc.create_voice_ex(vid, "noise", str(payload.get("audio", {}).get("preset", src.get("preset", ""))), x0, y0, amp0, pitch_on_grid=pitch_on_grid, looping=looping, volume=volume)
                        elif key == "voice.set":
                            vid = int(payload.get("voiceId", 0))
                            if "gain" in payload:
                                ipc.update_parameter(vid, "gain", float(payload.get("gain", 0.0)))
                            if "pan" in payload:
                                ipc.update_parameter(vid, "pan", float(payload.get("pan", 0.0)))
                            if "pitchSemitones" in payload:
                                ipc.update_parameter(vid, "pitchSemitones", float(payload.get("pitchSemitones", 0.0)))
                            if "position" in payload and isinstance(payload["position"], dict):
                                p = payload["position"]
                                px = float(p.get("x", 0.0)) if "x" in p else None
                                py = float(p.get("y", 0.0)) if "y" in p else None
                                if px is not None or py is not None:
                                    old = pending_positions.get(vid)
                                    if old is None:
                                        pending_positions[vid] = (px or 0.0, py or 0.0)
                                    else:
                                        pending_positions[vid] = (px if px is not None else old[0], py if py is not None else old[1])
                        elif key == "voice.remove":
                            vid = int(payload.get("voiceId", 0))
                            ipc.destroy_voice(vid)
                        else:
                            # fallback: print
                            try:
                                print(f"[IPC_LEGACY_DROP] {command}")
                            except Exception:
                                pass
                    else:
                        # Non-dict payloads are dropped
                        pass
                except queue.Empty:
                    break  # Queue is empty
                except Exception as e:
                    print(f"ERROR sending IPC command: {e}")
                    # Re-queue the command? For now, we drop it.
                    pass
            # Flush coalesced updates after draining frame budget
            try:
                if pending_listener is not None:
                    ipc.set_listener_pos(pending_listener[0], pending_listener[1])
                if pending_positions:
                    ipc.update_voice_positions([(vid, x, y) for vid, (x, y) in pending_positions.items()])
            except Exception:
                pass

        except Exception as e:
            print(f"ERROR in _drain_ipc_queue: {e}")

    def draw(self):
        """Draw the game - OPTIMIZED for maximum FPS"""
        try:
            # Start performance monitoring for draw
            self.performance_monitor.start_draw()
            
            # Clear window
            self.window.clear()
            
            # Use renderer to draw everything (including sound bullets via batch system)
            self.renderer.draw()
            
            # Draw UI overlays via unified UIManager
            if hasattr(self, 'ui_manager'):
                self.ui_manager.draw()

            # Draw effect zones overlay
            try:
                if hasattr(self, 'effect_zones'):
                    self.effect_zones.draw()
            except Exception:
                pass
            
            # Draw mouse system (dual-circle cursor)
            if hasattr(self, 'mouse_system'):
                self.mouse_system.draw()
            
            # Draw listener radius overlay at mouse position (visual debug)
            if self.current_mouse_pos is not None and hasattr(self, '_listener_visual'):
                lx, ly = self.current_mouse_pos
                self._listener_visual.x = int(lx)
                self._listener_visual.y = int(ly)
                self._listener_visual.radius = int(self.listener_radius)
                self._listener_visual.draw()
            
            # End performance monitoring for draw
            self.performance_monitor.end_draw()
            
        except Exception as e:
            print(f"ERROR in draw: {e}")
            traceback.print_exc()
    
    def clear_objects(self):
        """Clear all physics objects and sound bullets"""
        try:
            # Clear physics objects
            for obj in self.physics_objects:
                self.space.remove(obj.body, obj.shape)
            self.physics_objects.clear()
            
            # Clear collision shapes
            self.collision_system.clear_all_shapes()
            
            # Clear sound bullets via the manager
            self.bullet_manager.clear_all()
            
            # Clear tool zones
            for tool in self.tools:
                if isinstance(tool, WindTool):
                    tool.wind_zones.clear()
                elif isinstance(tool, MagnetTool):
                    tool.magnet_zones.clear()
                elif isinstance(tool, TeleporterTool):
                    tool.teleport_pairs.clear()
                    
        except Exception as e:
            print(f"ERROR in clear_objects: {e}")
            traceback.print_exc()
    
    def reset_physics(self):
        """Reset physics to default state"""
        try:
            self.space.gravity = self.base_gravity
            self.current_gravity = self.base_gravity
            self.current_wind_strength = 0
            self.wind_direction = 0
            
        except Exception as e:
            print(f"ERROR in reset_physics: {e}")
            traceback.print_exc()
    
    def _update_physics_snap_zones(self):
        """Add physics snap zones when spacebar is held"""
        if not hasattr(self, 'mouse_system') or not hasattr(self, 'snap_zone_manager'):
            return
            
        try:
            # Create physics center zone
            physics_zone = self.snap_zone_manager.create_physics_center_zone(self.width, self.height)
            self.mouse_system.add_snap_zone(physics_zone)
            print("DEBUG: Added physics snap zone")
        except Exception as e:
            print(f"ERROR adding physics snap zones: {e}")
    
    def _clear_physics_snap_zones(self):
        """Remove physics snap zones when spacebar is released"""
        if not hasattr(self, 'mouse_system'):
            return
            
        try:
            self.mouse_system.remove_snap_zone("physics_center")
            print("DEBUG: Removed physics snap zone")
        except Exception as e:
            print(f"ERROR removing physics snap zones: {e}")
