import pyglet
import traceback
import math
import time
from typing import Tuple
from pyglet_physics_game.physics.physics_tools import FreeDrawTool
from pyglet_physics_game.tools.bullets_handling.bullet_delete import BulletDeleteTool

class InputHandler:
	"""Handles all input events (keyboard, mouse)"""
	
	def __init__(self, game):
		self.game = game
		print("DEBUG: Input handler initialized")
	
	def handle_key_press(self, symbol, modifiers):
		"""Handle keyboard input"""
		try:
			if symbol == pyglet.window.key.DELETE:
				print("[DELETE] pressed: bullet delete enabled")
				return
			# BACKSPACE failsafe (hold 1s to stop all sounds and clear bullets)
			if symbol == pyglet.window.key.BACKSPACE:
				setattr(self.game, '_backspace_down', True)
				setattr(self.game, '_failsafe_active', True)
				setattr(self.game, '_failsafe_start_time', time.time())
				setattr(self.game, '_failsafe_progress', 0.0)
				setattr(self.game, '_failsafe_triggered', False)
				# initialize overlay position to current mouse if available
				if getattr(self.game, 'current_mouse_pos', None):
					setattr(self.game, '_failsafe_mouse_pos', self.game.current_mouse_pos)
				print("[FAILSAFE] BACKSPACE hold started")
				return
			# Preset recall/store (0-9): Shift+digit = store, digit = recall
			if pyglet.window.key._0 <= symbol <= pyglet.window.key._9:
				idx = symbol - pyglet.window.key._0
				if modifiers & pyglet.window.key.MOD_SHIFT:
					if hasattr(self.game, 'ui_manager'):
						self.game.ui_manager.store_preset(idx)
						print(f"DEBUG: Stored preset {idx}")
				else:
					if hasattr(self.game, 'ui_manager'):
						self.game.ui_manager.recall_preset(idx)
						print(f"DEBUG: Recalled preset {idx}")
				return
			
			if symbol == pyglet.window.key.SPACE:
				# Hold spacebar to control gravity/wind
				self.game.space_held = True
				print(f"DEBUG: SPACE pressed - gravity/wind control: ON")
				
			elif symbol == pyglet.window.key.TAB:
				# Switch tools
				old_tool_index = self.game.current_tool_index
				old_tool_name = self.game.tools[old_tool_index].name
				
				self.game.current_tool_index = (self.game.current_tool_index + 1) % len(self.game.tools)
				for i, tool in enumerate(self.game.tools):
					if i == self.game.current_tool_index:
						tool.activate()
					else:
						tool.deactivate()
				
				new_tool_name = self.game.tools[self.game.current_tool_index].name
				print(f"DEBUG: Switched to tool: {new_tool_name}")
				
				# Show visual feedback
				if hasattr(self.game, 'visual_feedback'):
					self.game.visual_feedback.show_tool_change(old_tool_name, new_tool_name)
				
			elif symbol == pyglet.window.key.C:
				# Clear all objects
				self.game.clear_objects()
				
			elif symbol == pyglet.window.key.R:
				# Reset physics
				self.game.reset_physics()
				
			# Performance tuning controls

			# Toggle Audio Settings menu (F11)
			elif symbol == pyglet.window.key.F11:
				print("[DEBUG][input_handler.py] F11 detected! Toggling UIManager audio settings menu.")
				if hasattr(self.game, 'ui_manager'):
					self.game.ui_manager.toggle_audio_settings()
					try:
						print(f"[DEBUG][input_handler.py] Menu visibility is now: {self.game.ui_manager.audio_settings_menu.visible}")
					except Exception:
						pass
				else:
					print("[DEBUG][input_handler.py] ERROR: self.game.ui_manager not found!")
				return
				
			elif symbol == pyglet.window.key.F2:
				# Cycle through color themes
				from pyglet_physics_game.ui.theme_switcher import get_theme_switcher
				from pyglet_physics_game.ui.color_manager import get_color_manager
				theme_switcher = get_theme_switcher()
				theme_name = theme_switcher.next_theme()
				theme_info = theme_switcher.get_current_theme_info()
				print(f"DEBUG: Switched to theme: {theme_info.get('name', theme_name)}")
				
				# Window clear color stays black - background is drawn by renderer
				
			elif symbol == pyglet.window.key.F3:
				# Toggle trails
				self.game.disable_trails = not self.game.disable_trails
				print(f"DEBUG: Trails: {'DISABLED' if self.game.disable_trails else 'ENABLED'}")
				
			elif symbol == pyglet.window.key.F4:
				# Increase object limit
				self.game.max_objects = min(500, self.game.max_objects + 50)
				print(f"DEBUG: Object limit increased to {self.game.max_objects}")
				
			elif symbol == pyglet.window.key.F5:
				# Decrease object limit
				self.game.max_objects = max(10, self.game.max_objects - 50)
				print(f"DEBUG: Object limit decreased to {self.game.max_objects}")
				
			# Object material controls
			elif symbol == pyglet.window.key.B:
				# Spawn metal object
				if self.game.current_mouse_pos:
					x, y = self.game.current_mouse_pos
					from systems.bullet_data import MaterialType
					self.game.spawn_object(x, y, MaterialType.METAL)
					print(f"DEBUG: Spawned metal object at ({x:.1f}, {y:.1f})")
						
			elif symbol == pyglet.window.key.N:
				# Spawn energy object
				if self.game.current_mouse_pos:
					x, y = self.game.current_mouse_pos
					from systems.bullet_data import MaterialType
					self.game.spawn_object(x, y, MaterialType.ENERGY)
					print(f"DEBUG: Spawned energy object at ({x:.1f}, {y:.1f})")
						
			elif symbol == pyglet.window.key.M:
				# Spawn plasma object
				if self.game.current_mouse_pos:
					x, y = self.game.current_mouse_pos
					from systems.bullet_data import MaterialType
					self.game.spawn_object(x, y, MaterialType.PLASMA)
					print(f"DEBUG: Spawned plasma object at ({x:.1f}, {y:.1f})")
			
			elif symbol == pyglet.window.key.SPACE:
				self.game.space_held = True
			
			# Preset keys (0-9)
			elif symbol >= pyglet.window.key._0 and symbol <= pyglet.window.key._9:
				preset_num = symbol - pyglet.window.key._0
				if hasattr(self.game, 'ui_manager'):
					if modifiers & pyglet.window.key.MOD_SHIFT:
						# Shift + number: Store current selection to preset
						self.game.ui_manager.store_preset(preset_num)
						print(f"DEBUG: Stored current selection to preset {preset_num}")
					else:
						# Number only: Recall preset
						self.game.ui_manager.recall_preset(preset_num)
						print(f"DEBUG: Recalled preset {preset_num}")
			
			# Debug window toggle (F12)
			elif symbol == pyglet.window.key.F12:
				if hasattr(self.game, 'debug_window'):
					self.game.debug_window.toggle()
			# Master debug dump (F10)
			elif symbol == pyglet.window.key.F10:
				try:
					from pyglet_physics_game.ipc.client import get_ipc_client
					ipc = get_ipc_client()
					ipc._send("/debug/dump_state")
					print("[DEBUG] Requested JUCE state dump via /debug/dump_state")
				except Exception as e:
					print(f"[DEBUG] Failed to request JUCE state dump: {e}")
				try:
					if hasattr(self.game, 'dumpCurrentStateToLog'):
						self.game.dumpCurrentStateToLog()
				except Exception as e:
					print(f"[DEBUG] Failed to dump Python state: {e}")
			# Grid system cycling (F6)
			elif symbol == pyglet.window.key.F6:
				if hasattr(self.game, 'grid_system'):
					self.game.grid_system.cycle_grid_level()
			
			# Command helper toggle (H)
			elif symbol == pyglet.window.key.H:
				if hasattr(self.game, 'command_helper'):
					self.game.command_helper.toggle()
			
		except Exception as e:
			print(f"ERROR in handle_key_press: {e}")
			traceback.print_exc()
			
	def handle_key_release(self, symbol, modifiers):
		"""Handle keyboard release"""
		try:
			if symbol == pyglet.window.key.DELETE:
				print("[DELETE] released: bullet delete disabled")
				if hasattr(self.game, '_bullet_delete_pos'):
					self.game._bullet_delete_pos = None
			if symbol == pyglet.window.key.BACKSPACE:
				# Cancel failsafe on release if not yet triggered
				setattr(self.game, '_backspace_down', False)
				setattr(self.game, '_failsafe_active', False)
				setattr(self.game, '_failsafe_progress', 0.0)
				# keep _failsafe_triggered until next press
				print("[FAILSAFE] BACKSPACE released")
			if symbol == pyglet.window.key.SPACE:
				# Release spacebar to stop gravity/wind control
				self.game.space_held = False
				print(f"DEBUG: SPACE released - gravity/wind control: OFF")
			# Shift release closes the experimental menu and commits selection
			if symbol == pyglet.window.key.LSHIFT or symbol == pyglet.window.key.RSHIFT:
				if hasattr(self.game, 'ui_manager'):
					self.game.ui_manager.close_menu_commit()
		except Exception as e:
			print(f"ERROR in handle_key_release: {e}")
			traceback.print_exc()
			
	def handle_mouse_press(self, x, y, button, modifiers):
		"""Handle mouse button press"""
		try:
			# Shift + Left/Right click opens experimental menu
			if modifiers & pyglet.window.key.MOD_SHIFT and hasattr(self.game, 'ui_manager'):
				if button == pyglet.window.mouse.LEFT:
					self.game.ui_manager.open_menu('left', x, y)
					return
				elif button == pyglet.window.mouse.RIGHT:
					self.game.ui_manager.open_menu('right', x, y)
					return
			
			# NEW CORE MECHANIC: Left/Right click fire sound bullets (with throttle and mutual exclusion)
			if button == pyglet.window.mouse.LEFT or button == pyglet.window.mouse.RIGHT:
				now = time.time()
				# Initialize throttle config if missing
				if not hasattr(self.game, 'spawn_throttle_secs'):
					self.game.spawn_throttle_secs = 0.10
				if not hasattr(self.game, '_next_spawn_allowed_time'):
					self.game._next_spawn_allowed_time = 0.0
				if not hasattr(self.game, '_spawn_lock_side'):
					self.game._spawn_lock_side = None
				current_side = 'left' if button == pyglet.window.mouse.LEFT else 'right'
				# Respect mutual exclusion: if another side is locked, ignore
				if self.game._spawn_lock_side is not None and self.game._spawn_lock_side != current_side:
					print(f"[SPAWN] blocked by lock: {self.game._spawn_lock_side}")
					return
				# Respect throttle
				if now < self.game._next_spawn_allowed_time:
					print("[SPAWN] throttled")
					return
				# Acquire lock and spawn
				self.game._spawn_lock_side = current_side
				selector = 'left' if button == pyglet.window.mouse.LEFT else 'right'
				self.game.fire_sound_bullet(x, y, selector)
				self.game._next_spawn_allowed_time = now + float(self.game.spawn_throttle_secs)
				return
			
			# NEW: Middle click for collision drawing (old left click behavior)
			if button == pyglet.window.mouse.MIDDLE:
				if modifiers & pyglet.window.key.MOD_ALT:
					# Alt + Middle click: Erase collisions (old right click behavior)
					self.game.erase_tool_active = True
					self.game.erase_mouse_pos = (x, y)  # Store mouse position for erase radius
				else:
					# Middle click: Draw collisions (old left click behavior)
					current_tool = self.game.tools[self.game.current_tool_index]
					if isinstance(current_tool, FreeDrawTool):
						current_tool.start_draw((x, y))
					elif hasattr(current_tool, 'start_draw'):
						current_tool.start_draw((x, y))
				
		except Exception as e:
			print(f"ERROR in handle_mouse_press: {e}")
			traceback.print_exc()
			
	def handle_mouse_release(self, x, y, button, modifiers):
		"""Handle mouse button release"""
		try:
			# release spawn lock when corresponding button is released
			if button == pyglet.window.mouse.LEFT and getattr(self.game, '_spawn_lock_side', None) == 'left':
				self.game._spawn_lock_side = None
			elif button == pyglet.window.mouse.RIGHT and getattr(self.game, '_spawn_lock_side', None) == 'right':
				self.game._spawn_lock_side = None
			# NEW: Middle click for collision drawing (old left click behavior)
			if button == pyglet.window.mouse.MIDDLE:
				if modifiers & pyglet.window.key.MOD_ALT:
					# Alt + Middle click: Erase collisions (old right click behavior)
					self.game.erase_tool_active = False
					self.game.erase_mouse_pos = None  # Clear mouse position
				else:
					# Middle click: Draw collisions (old left click behavior)
					current_tool = self.game.tools[self.game.current_tool_index]
					if isinstance(current_tool, FreeDrawTool):
						shapes_list = current_tool.finish_draw(self.game.space)
						if shapes_list:
							# Track optimization statistics
							original_points = len(current_tool.points) if hasattr(current_tool, 'points') else 0
							optimized_shapes = len(shapes_list)
							
							if original_points > 0:
								self.game.performance_monitor.update_optimization_stats(original_points, optimized_shapes)
							
							# Add shapes to collision system
							for shape in shapes_list:
								self.game.collision_system.add_collision_shape(shape)
								
					elif hasattr(current_tool, 'finish_draw'):
						shapes_list = current_tool.finish_draw(self.game.space)
						if shapes_list:
							for shape in shapes_list:
								self.game.collision_system.add_collision_shape(shape)
				
		except Exception as e:
			print(f"ERROR in handle_mouse_release: {e}")
			traceback.print_exc()
			
	def handle_mouse_motion(self, x, y, dx, dy):
		"""Handle mouse motion"""
		try:
			# Store current mouse position for brush indicator
			self.game.current_mouse_pos = (x, y)

			# If Delete is held, run bullet delete tool continuously and show circle
			delete_down = (hasattr(self.game, 'keys') and self.game.keys[pyglet.window.key.DELETE]) or getattr(self.game, '_delete_down', False)
			if delete_down:
				if not hasattr(self.game, '_bullet_delete_tool') or self.game._bullet_delete_tool is None:
					self.game._bullet_delete_tool = BulletDeleteTool(self.game)
				self.game._bullet_delete_tool.activate()
				self.game._bullet_delete_tool.update_draw((x, y))
				print(f"[DELETE] active: deleting at ({x:.1f}, {y:.1f}) radius={self.game._bullet_delete_tool.radius}")
				# Set explicit bullet delete position for renderer overlay
				self.game._bullet_delete_pos = (x, y)
				# Also set legacy erase overlay for visibility parity
				self.game.erase_mouse_pos = (x, y)
				try:
					if hasattr(self.game.tools[self.game.current_tool_index], 'thickness'):
						self.game.tools[self.game.current_tool_index].thickness = int(self.game._bullet_delete_tool.radius * 2)
				except Exception:
					pass
			else:
				# Clear overlay when Delete not held
				self.game._bullet_delete_pos = None
				self.game.erase_mouse_pos = None
			
			# Update mouse system position
			if hasattr(self.game, 'mouse_system'):
				self.game.mouse_system.set_mouse_position(x, y)
			
			# Forward to UI manager for hover handling
			if hasattr(self.game, 'ui_manager'):
				self.game.ui_manager.on_mouse_motion(x, y)
			
			if self.game.space_held:
				# Check if we're snapped to the physics center deadzone
				if hasattr(self.game, 'mouse_system'):
					current_snap = self.game.mouse_system.get_current_snap()
					if (current_snap and current_snap.snapped and 
						current_snap.zone and current_snap.zone.name == "physics_center"):
						
						# We're in the deadzone - set zero values
						self.game.current_gravity = (0, 0)
						self.game.space.gravity = self.game.current_gravity
						self.game.current_wind_strength = 0
						self.game.wind_direction = 0
						return
				
				# Normal physics control (outside deadzone)
				# Control gravity with mouse Y position
				gravity_strength = (y - self.game.height // 2) * 2
				self.game.current_gravity = (0, gravity_strength)
				self.game.space.gravity = self.game.current_gravity
				
				# Control wind with mouse X position relative to center (X-axis only)
				center_x = self.game.width // 2
				dx = x - center_x
				
				# Calculate wind strength based on distance from center (X-axis only)
				wind_strength = abs(dx) * 2.0  # Scale factor for stronger wind effect
				self.game.current_wind_strength = wind_strength
				
				# Wind direction: 0° = right, 180° = left (X-axis only)
				self.game.wind_direction = 0 if dx > 0 else math.pi
					
		except Exception as e:
			print(f"ERROR in handle_mouse_motion: {e}")
			traceback.print_exc()
			
	def handle_mouse_drag(self, x, y, dx, dy, buttons, modifiers):
		"""Handle mouse drag"""
		try:
			# NEW: Middle click for collision drawing (old left click behavior)
			if buttons & pyglet.window.mouse.MIDDLE:
				if modifiers & pyglet.window.key.MOD_ALT:
					# Alt + Middle click: Erase collisions (old right click behavior)
					self.game.erase_tool_active = True
					self.game.erase_mouse_pos = (x, y)
					self._erase_objects_at_position(x, y)
				else:
					# Middle click: Draw collisions (old left click behavior)
					current_tool = self.game.tools[self.game.current_tool_index]
					if isinstance(current_tool, FreeDrawTool):
						current_tool.update_draw((x, y))
					elif hasattr(current_tool, 'update_draw'):
						current_tool.update_draw((x, y))
					
		except Exception as e:
			print(f"ERROR in handle_mouse_drag: {e}")
			traceback.print_exc()
			
	def handle_mouse_scroll(self, x, y, scroll_x, scroll_y):
		"""Handle mouse scroll wheel"""
		try:
			# Forward scroll to experimental UI first - if consumed, don't process game logic
			if hasattr(self.game, 'ui_manager'):
				event_consumed = self.game.ui_manager.on_mouse_scroll(x, y, scroll_y)
				if event_consumed:
					return  # Menu consumed the event, don't process game scroll logic
			
			# Handle spacebar + center circle physics mode toggle
			if hasattr(self.game, 'space_held') and self.game.space_held:
				if self._is_mouse_over_physics_center_circle(x, y):
					# Toggle bullet physics mode
					self._toggle_bullet_physics_mode(scroll_y)
					return
			
			# Get current tool
			current_tool = self.game.tools[self.game.current_tool_index]
			
			# If FreeDraw tool is active, modify brush width
			if isinstance(current_tool, FreeDrawTool):
				# Scroll up (scroll_y > 0) increases brush width
				# Scroll down (scroll_y < 0) decreases brush width
				width_change = scroll_y * 2  # Adjust sensitivity
				new_width = current_tool.thickness + width_change
				
				# Clamp brush width between reasonable bounds
				new_width = max(1, min(50, new_width))
				
				if new_width != current_tool.thickness:
					old_width = current_tool.thickness
					current_tool.thickness = new_width
					print(f"DEBUG: FreeDraw brush width changed to {new_width}")
					
					# Show visual feedback
					if hasattr(self.game, 'visual_feedback'):
						self.game.visual_feedback.show_parameter_change("Brush Width", old_width, new_width, "px")
					
		except Exception as e:
			print(f"ERROR in handle_mouse_scroll: {e}")
			traceback.print_exc()
	
	def _is_mouse_over_physics_center_circle(self, x, y):
		"""Check if mouse is over the physics grid center circle"""
		try:
			# Get screen center and circle radius from physics grid config
			center_x = self.game.width / 2
			center_y = self.game.height / 2
			radius = 50  # From physics_grid.json
			
			# Calculate distance from mouse to center
			import math
			distance = math.sqrt((x - center_x)**2 + (y - center_y)**2)
			
			return distance <= radius
		except Exception as e:
			print(f"ERROR checking center circle: {e}")
			return False
	
	def _toggle_bullet_physics_mode(self, scroll_y):
		"""Toggle global bullet physics mode based on scroll direction"""
		try:
			# Toggle global physics state
			if scroll_y > 0:  # Scroll up = enable physics
				self.game.global_physics_enabled = True
			else:  # Scroll down = disable physics
				self.game.global_physics_enabled = False
			
			# Show visual feedback
			physics_status = "ON" if self.game.global_physics_enabled else "OFF"
			if hasattr(self.game, 'visual_feedback'):
				self.game.visual_feedback.show_parameter_change("Global Bullet Physics", "TOGGLE", physics_status, "")
			
			print(f"DEBUG: Global bullet physics mode toggled to {physics_status}")
			
		except Exception as e:
			print(f"ERROR toggling global bullet physics mode: {e}")
			import traceback
			traceback.print_exc()
	
	def _erase_objects_at_position(self, x, y):
		"""Erase objects within erase radius at position"""
		try:
			# Use the same logic as the erase function for consistency
			if isinstance(self.game.tools[self.game.current_tool_index], FreeDrawTool):
				erase_radius = self.game.tools[self.game.current_tool_index].thickness / 2
			else:
				erase_radius = 25  # Default radius for other tools
				
			erased_count = 0
			
			# Check physics objects
			objects_to_remove = []
			for obj in self.game.physics_objects:
				obj_pos = obj.body.position
				distance = math.sqrt((x - obj_pos.x)**2 + (y - obj_pos.y)**2)
				if distance <= erase_radius:
					objects_to_remove.append(obj)
					
			# Remove physics objects
			for obj in objects_to_remove:
				self.game.space.remove(obj.body, obj.shape)
				self.game.physics_objects.remove(obj)
				erased_count += 1
				
			# Check collision shapes (drawn strokes and boundary walls) - erase everything
			shapes_to_remove = []
			for shape in self.game.collision_shapes:
				if hasattr(shape, 'a') and hasattr(shape, 'b'):
					# Check if any point of the segment is within erase radius
					a = shape.a
					b = shape.b
					
					# Check distance from line segment to mouse position
					if self._point_to_line_distance(x, y, a.x, a.y, b.x, b.y) <= erase_radius:
						shapes_to_remove.append(shape)
						
			# Remove collision shapes
			for shape in shapes_to_remove:
				self.game.collision_system.remove_collision_shape(shape)
				erased_count += 1
					
			if erased_count > 0:
				print(f"DEBUG: Erased {erased_count} objects at ({x}, {y})")
				
		except Exception as e:
			print(f"ERROR in _erase_objects_at_position: {e}")
			traceback.print_exc()
			
	# Removed _is_boundary_wall method - now erasing everything
	
	def _point_to_line_distance(self, px, py, x1, y1, x2, y2):
		"""Calculate distance from point to line segment"""
		try:
			A = px - x1
			B = py - y1
			C = x2 - x1
			D = y2 - y1
			
			dot = A * C + B * D
			len_sq = C * C + D * D
			
			if len_sq == 0:
				return math.sqrt(A * A + B * B)
				
			param = dot / len_sq
			
			if param < 0:
				xx, yy = x1, y1
			elif param > 1:
				xx, yy = x2, y2
			else:
				xx = x1 + param * C
				yy = y1 + param * D
				
			dx = px - xx
			dy = py - yy
			
			return math.sqrt(dx * dx + dy * dy)
			
		except Exception as e:
			print(f"ERROR calculating point to line distance: {e}")
			return float('inf')
