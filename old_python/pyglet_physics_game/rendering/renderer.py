import pyglet
import traceback
import math
from pyglet import shapes
from pyglet import text
from typing import Tuple
import time
import pymunk

class Renderer:
	"""Main renderer that handles all drawing operations - Safe Performance Mode"""
	
	def __init__(self, game):
		self.game = game
		
		# Performance optimizations
		self.debug_mode = getattr(game, 'debug_mode', False)
		self.safe_performance_mode = getattr(game, 'safe_performance_mode', False)
		
		# SINGLE batch for maximum performance (Pyglet best practice)
		self.batch = pyglet.graphics.Batch()
		
		# Batch groups for proper layering (background -> auras -> grid -> bullets -> objects -> UI)
		self.background_group = pyglet.graphics.Group(order=0)  # Background and auras
		self.grid_group = pyglet.graphics.Group(order=1)        # Grid system
		self.bullets_group = pyglet.graphics.Group(order=2)     # Sound bullets (above grid)
		self.objects_group = pyglet.graphics.Group(order=3)     # Physics objects
		self.collision_group = pyglet.graphics.Group(order=4)   # Collision shapes
		self.ui_group = pyglet.graphics.Group(order=5)          # UI elements

		# Bullet-delete overlay shapes (created lazily)
		self._bd_circle = None
		self._bd_border = None
		
		# Pre-create background elements
		self._init_background()
		
		# Performance counters
		self.draw_calls = 0
		self.last_stats_time = 0
		
		if self.debug_mode:
			print("DEBUG: Renderer initialized - OPTIMIZED Batch Mode")
	
	def _label(self, value: str, *, font_size: int, x: int, y: int, color: Tuple[int, int, int], batch=None):
		"""Create a themed label using UITheme font fallbacks"""
		try:
			font_names = getattr(self.game.ui_theme, 'ui_font_names', ["Arial"]) if hasattr(self.game, 'ui_theme') else ["Arial"]
			return text.Label(value, font_size=font_size, x=x, y=y, color=color, font_name=font_names, batch=batch)
		except Exception:
			# Fallback without custom font
			return text.Label(value, font_size=font_size, x=x, y=y, color=color, batch=batch)

	def _init_background(self):
		"""Initialize background elements - will be updated each frame"""
		# Background elements are now created dynamically in _update_background()
		pass
	
	def _update_background(self):
		"""Update background elements with current theme colors - only when theme changes"""
		try:
			# Only update if theme has changed
			from pyglet_physics_game.ui.color_manager import get_color_manager
			color_mgr = get_color_manager()
			current_theme = color_mgr.get_current_theme()
			
			if hasattr(self, '_last_theme') and self._last_theme == current_theme:
				return  # No need to update
			
			self._last_theme = current_theme
			
			# Clear old background shapes
			if hasattr(self, '_background_shapes'):
				for shape in self._background_shapes:
					shape.delete()
			self._background_shapes = []
			
			# Create background rectangle
			background_rect = shapes.Rectangle(
				0, 0, self.game.width, self.game.height, 
				color=color_mgr.background_main_game, 
				batch=self.batch,
				group=self.background_group
			)
			self._background_shapes.append(background_rect)
			
			# Only create grid lines if enhanced grid system is not active
			if not (hasattr(self.game, 'grid_system') and self.game.grid_system.visible):
				# Create grid lines with current theme colors
				grid_spacing = 100
				grid_colors = color_mgr.get_grid_colors()
				grid_color = grid_colors['primary']
				
				# Vertical grid lines
				for x in range(0, self.game.width + 1, grid_spacing):
					line = shapes.Line(x, 0, x, self.game.height, color=grid_color, batch=self.batch, group=self.background_group)
					line.opacity = 200  # Make grid more visible
					self._background_shapes.append(line)
					
				# Horizontal grid lines
				for y in range(0, self.game.height + 1, grid_spacing):
					line = shapes.Line(0, y, self.game.width, y, color=grid_color, batch=self.batch, group=self.background_group)
					line.opacity = 200  # Make grid more visible
					self._background_shapes.append(line)
				
		except Exception as e:
			print(f"ERROR updating background: {e}")
			traceback.print_exc()
	
	def _create_background_grid(self):
		"""Create background grid once (not every frame)"""
		try:
			# Add background rectangle to the batch
			from pyglet_physics_game.ui.color_manager import get_color_manager
			color_mgr = get_color_manager()
			background_rect = shapes.Rectangle(
				0, 0, self.game.width, self.game.height, 
				color=color_mgr.background_main_game, 
				batch=self.background_batch
			)
			
			grid_spacing = 100
			grid_color = (30, 30, 30)
			
			# Create grid lines using pyglet shapes (safe approach)
			self.grid_lines = []
			
			# Vertical grid lines
			for x in range(0, self.game.width + 1, grid_spacing):
				line = shapes.Line(x, 0, x, self.game.height, color=grid_color, batch=self.background_batch)
				line.opacity = 100
				self.grid_lines.append(line)
				
			# Horizontal grid lines
			for y in range(0, self.game.height + 1, grid_spacing):
				line = shapes.Line(0, y, self.game.width, y, color=grid_color, batch=self.background_batch)
				line.opacity = 100
				self.grid_lines.append(line)
				
		except Exception as e:
			print(f"ERROR creating background grid: {e}")
			traceback.print_exc()
	
	def _create_ui_elements(self):
		"""Pre-create UI elements for performance"""
		try:
			# Create static UI text labels
			self.ui_labels = {}
			
			# Tool labels
			tool_names = ['C', 'W', 'M', 'T', 'F']
			tool_colors = [(255, 0, 0), (0, 0, 255), (128, 0, 128), (255, 192, 203), (139, 69, 19)]
			
			for i, (name, color) in enumerate(zip(tool_names, tool_colors)):
				label = self._label(name, font_size=16, x=20 + i * 50, y=self.game.height - 50, color=color, batch=self.ui_batch)
				self.ui_labels[f'tool_{i}'] = label
			
		except Exception as e:
			print(f"ERROR creating UI elements: {e}")
			traceback.print_exc()

	def draw(self):
		"""Draw the game - OPTIMIZED single batch rendering"""
		try:
			# Start performance monitoring for draw
			self.game.performance_monitor.start_draw()
			
			# Clear window
			self.game.window.clear()
			
			# Update background with current theme colors
			self._update_background()
			
			# Draw dynamic grid system using renderer's batch (under bullets)
			if hasattr(self.game, 'grid_system'):
				self.game.grid_system.draw_with_renderer_batch(self.batch, self.grid_group)
			
			# Update bullet-delete overlay before drawing the batch so it shows this frame
			self._update_bullet_delete_overlay()
			if hasattr(self.game, '_bullet_delete_pos') and self.game._bullet_delete_pos:
				print(f"[DELETE] overlay at {self.game._bullet_delete_pos}")
			
			# Draw everything with a single batch call (MAXIMUM PERFORMANCE)
			# This includes auras (background_group), grid (grid_group), and bullets (bullets_group)
			self.batch.draw()
			
			# Draw physics objects directly (after background)
			self._update_physics_objects()
			self._update_collision_shapes()
			
			# Draw UI elements that can't be batched (HUD, debug, etc.)
			self._draw_ui()
			self._draw_erase_radius_circle()
			self._draw_brush_radius_indicator()
			
			# Conditional drawing based on performance flags
			if not getattr(self.game, 'disable_particles', False):
				self._draw_wind_particles()
			if not getattr(self.game, 'disable_trails', False):
				self._draw_object_trails()
			
			# End performance monitoring for draw
			self.game.performance_monitor.end_draw()
			
		except Exception as e:
			print(f"ERROR in draw: {e}")
			traceback.print_exc()
	
	def _update_physics_objects(self):
		"""Draw physics objects directly - WORKING METHOD FROM BACKUP"""
		try:
			# Use direct drawing like the working backup
			for obj in self.game.physics_objects:
				if hasattr(obj, 'draw_direct'):
					obj.draw_direct()
				else:
					# Fallback direct drawing
					pos = obj.body.position
					
					if isinstance(obj.shape, pymunk.Circle):
						# Draw circle directly
						radius = obj.shape.radius
						circle = shapes.Circle(pos.x, pos.y, radius, color=obj.color)
						circle.opacity = 255
						circle.draw()
						
					elif isinstance(obj.shape, pymunk.Poly):
						# Draw polygon directly - use first 3 vertices as triangle
						vertices = obj.shape.get_vertices()
						if len(vertices) >= 3:
							# Convert to world coordinates
							world_vertices = []
							for vertex in vertices[:3]:  # Only first 3 vertices
								world_vertex = obj.body.local_to_world(vertex)
								world_vertices.append((world_vertex.x, world_vertex.y))
							
							# Draw triangle directly
							if len(world_vertices) >= 3:
								poly = shapes.Polygon(*world_vertices, color=obj.color)
								poly.opacity = 255
								poly.draw()
							
					elif isinstance(obj.shape, pymunk.Segment):
						# Draw segment directly
						a = obj.body.local_to_world(obj.shape.a)
						b = obj.body.local_to_world(obj.shape.b)
						
						line = shapes.Line(a.x, a.y, b.x, b.y, color=obj.color)
						line.opacity = 255
						line.draw()
					
		except Exception as e:
			print(f"ERROR updating physics objects: {e}")
			traceback.print_exc()
	
	def _update_collision_shapes(self):
		"""Draw collision shapes directly - WORKING METHOD FROM BACKUP"""
		try:
			# Draw collision shapes directly like the working backup
			for shape in self.game.collision_shapes:
				if hasattr(shape, 'a') and hasattr(shape, 'b') and hasattr(shape, 'radius'):
					a = shape.a
					b = shape.b
					
					# Draw collision segment with proper thickness
					thickness = shape.radius * 2  # pymunk.Segment radius is half the thickness
					
					# Calculate line properties for thick drawing
					dx = b.x - a.x
					dy = b.y - a.y
					length = math.sqrt(dx*dx + dy*dy)
					
					if length > 0:
						# Draw center line (red) - shows the collision
						from pyglet_physics_game.ui.color_manager import get_color_manager
						color_mgr = get_color_manager()
						center_line = shapes.Line(a.x, a.y, b.x, b.y, color=color_mgr.collision_center)
						center_line.opacity = 200
						center_line.draw()
						
						# Draw yellow outline for visibility
						offset = 10  # 10px offset for visibility
						dx_norm = dx / length
						dy_norm = dy / length
						outline_x = offset * dy_norm
						outline_y = -offset * dx_norm
						
						outline_line = shapes.Line(a.x + outline_x, a.y + outline_y, 
												 b.x + outline_x, b.y + outline_y, color=color_mgr.collision_outline)
						outline_line.opacity = 255
						outline_line.draw()
						
		except Exception as e:
			print(f"ERROR updating collision shapes: {e}")
			traceback.print_exc()
	
	def _draw_collision_segment_batch(self, a, b, thickness, batch):
		"""Draw collision segment using batch rendering for maximum performance"""
		try:
			# Calculate the perpendicular vector for thickness
			dx = b.x - a.x
			dy = b.y - a.y
			length = math.sqrt(dx*dx + dy*dy)
			
			if length > 0:
				# Normalize and get perpendicular vector
				dx_norm = dx / length
				dy_norm = dy / length
				
				# Draw the center line (red) - shows the collision
				from pyglet_physics_game.ui.color_manager import get_color_manager
				color_mgr = get_color_manager()
				center_line = shapes.Line(a.x, a.y, b.x, b.y, color=color_mgr.collision_center, batch=batch)
				center_line.opacity = 200
				
				# Draw the yellow outline (just one offset line for visibility)
				offset = 10  # 10px offset for visibility
				outline_x = offset * dy_norm
				outline_y = -offset * dx_norm
				
				outline_line = shapes.Line(a.x + outline_x, a.y + outline_y, 
										 b.x + outline_x, b.y + outline_y, color=color_mgr.collision_outline, batch=batch)
				outline_line.opacity = 255
				
		except Exception as e:
			print(f"ERROR in _draw_collision_segment_batch: {e}")
			traceback.print_exc()
	
	def _draw_collision_segment(self, a, b, thickness):
		"""Draw collision segment using safe rendering"""
		try:
			# Calculate the perpendicular vector for thickness
			dx = b.x - a.x
			dy = b.y - a.y
			length = math.sqrt(dx*dx + dy*dy)
			
			if length > 0:
				# Normalize and get perpendicular vector
				dx_norm = dx / length
				dy_norm = dy / length
				
				# Draw the center line (red) - shows the collision
				from pyglet_physics_game.ui.color_manager import get_color_manager
				color_mgr = get_color_manager()
				center_line = shapes.Line(a.x, a.y, b.x, b.y, color=color_mgr.collision_center)
				center_line.opacity = 200
				center_line.draw()
				
				# Draw the yellow outline (just one offset line for visibility)
				offset = 10  # 10px offset for visibility
				outline_x = offset * dy_norm
				outline_y = -offset * dx_norm
				
				outline_line = shapes.Line(a.x + outline_x, a.y + outline_y, 
									 b.x + outline_x, b.y + outline_y, color=color_mgr.collision_outline)
				outline_line.opacity = 255
				outline_line.draw()
				
		except Exception as e:
			print(f"ERROR drawing collision segment: {e}")
			traceback.print_exc()
	
	def _draw_tool_previews(self):
		"""Draw tool previews"""
		try:
			current_tool = self.game.tools[self.game.current_tool_index]
			if hasattr(current_tool, 'draw_preview_direct'):
				current_tool.draw_preview_direct()
				
		except Exception as e:
			print(f"ERROR drawing tool previews: {e}")
			traceback.print_exc()
	
	def _draw_tool_zones(self):
		"""Draw tool zones"""
		try:
			# Draw wind tool zones
			wind_tool = next((tool for tool in self.game.tools if hasattr(tool, 'draw_wind_zones_direct')), None)
			if wind_tool:
				wind_tool.draw_wind_zones_direct()
				
			# Draw magnet tool zones
			magnet_tool = next((tool for tool in self.game.tools if hasattr(tool, 'draw_magnet_zones_direct')), None)
			if magnet_tool:
				magnet_tool.draw_magnet_zones_direct()
				
			# Draw teleporter tool zones
			teleporter_tool = next((tool for tool in self.game.tools if hasattr(tool, 'draw_teleporters_direct')), None)
			if teleporter_tool:
				teleporter_tool.draw_teleporters_direct()
				
		except Exception as e:
			print(f"ERROR drawing tool zones: {e}")
			traceback.print_exc()
	
	def _draw_physics_indicators(self):
		"""Draw physics control indicators"""
		try:
			# Draw gravity indicator
			gravity_y = self.game.height - 60
			gravity_color = (255, 0, 0) if self.game.current_gravity[1] < 0 else (0, 255, 0)
			gravity_text = f"Gravity: {self.game.current_gravity[1]:.0f}"
			label = self._label(gravity_text, font_size=14, x=10, y=gravity_y, color=gravity_color)
			label.draw()
			
			# Draw wind indicator
			wind_y = self.game.height - 80
			wind_color = (0, 255, 255) if self.game.current_wind_strength > 0 else (150, 150, 150)
			wind_text = f"Wind: {self.game.current_wind_strength:.0f} @ {math.degrees(self.game.wind_direction):.0f}°"
			label = self._label(wind_text, font_size=14, x=10, y=wind_y, color=wind_color)
			label.draw()
			
		except Exception as e:
			print(f"ERROR drawing physics indicators: {e}")
			traceback.print_exc()
	
	def _draw_ui(self):
		"""Draw user interface elements"""
		try:
			# Draw always-on HUD
			if hasattr(self.game, 'hud'):
				self.game.hud.draw()
			
			# Draw visual feedback
			if hasattr(self.game, 'visual_feedback'):
				self.game.visual_feedback.draw()
			
			# Draw debug window
			if hasattr(self.game, 'debug_window'):
				self.game.debug_window.draw()
			
			# Audio settings menu visuals are updated during game.update(); batch draws them automatically
			
			# Draw command helper
			if hasattr(self.game, 'command_helper'):
				self.game.command_helper.draw()
			
			# Object count moved to debug window (F12)
			
			# Current tool moved to HUD
			
			# Brush width moved to HUD
			
			# Object positions moved to debug window (F12)
			
			# All controls moved to command helper (H)
				
		except Exception as e:
			print(f"ERROR drawing UI: {e}")
			traceback.print_exc()
	
	def _draw_collision_info(self):
		"""Draw collision optimization statistics"""
		try:
			# Position the info in the top-left corner
			x = 10
			y = self.game.height - 100
			
			opt_stats = self.game.performance_monitor.get_optimization_stats()
			
			# Draw original shapes count
			from pyglet_physics_game.ui.color_manager import get_color_manager
			color_mgr = get_color_manager()
			original_text = f"Original Shapes: {opt_stats.get('original_shapes', 0)}"
			label = self._label(original_text, font_size=14, x=x, y=y, color=color_mgr.debug_text)
			label.draw()
			y -= 20
			
			# Draw optimized shapes count
			optimized_text = f"Optimized Shapes: {opt_stats.get('optimized_shapes', 0)}"
			label = self._label(optimized_text, font_size=14, x=x, y=y, color=color_mgr.debug_text)
			label.draw()
			y -= 20
			
			# Draw simplification ratio
			ratio_text = f"Simplification Ratio: {opt_stats.get('simplification_ratio', 0):.2f}%"
			label = self._label(ratio_text, font_size=14, x=x, y=y, color=color_mgr.debug_text)
			label.draw()
			
		except Exception as e:
			print(f"ERROR drawing collision info: {e}")
			traceback.print_exc()
	

	def _draw_erase_radius_circle(self):
		"""Draw the erase or bullet-delete radius circle at the cursor."""
		try:
			# Bullet-delete overlay is managed in-batch via _update_bullet_delete_overlay
			# Legacy erase indicator
			if hasattr(self.game, 'erase_mouse_pos') and self.game.erase_mouse_pos:
				x, y = self.game.erase_mouse_pos
				if hasattr(self.game.tools[self.game.current_tool_index], 'thickness'):
					erase_radius = self.game.tools[self.game.current_tool_index].thickness / 2
				else:
					erase_radius = 25
				from pyglet_physics_game.ui.color_manager import get_color_manager
				color_mgr = get_color_manager()
				circle = shapes.Circle(x, y, erase_radius, color=color_mgr.erase_radius)
				circle.opacity = 100
				circle.draw()
				border = shapes.Circle(x, y, erase_radius, color=color_mgr.debug_text)
				border.opacity = 150
				border.draw()
		except Exception as e:
			print(f"ERROR drawing erase radius circle: {e}")
			traceback.print_exc()

	def _ensure_bd_shapes(self):
		if self._bd_circle is None or self._bd_border is None:
			from pyglet_physics_game.ui.color_manager import get_color_manager
			color_mgr = get_color_manager()
			self._bd_circle = shapes.Circle(0, 0, 1, color=color_mgr.erase_radius, batch=self.batch, group=self.ui_group)
			self._bd_circle.opacity = 0
			self._bd_border = shapes.Circle(0, 0, 1, color=color_mgr.debug_text, batch=self.batch, group=self.ui_group)
			self._bd_border.opacity = 0
		# Ensure failsafe progress visuals
		if not hasattr(self, '_fs_ring'):
			self._fs_ring = shapes.Circle(0, 0, 1, color=(255, 255, 0), batch=self.batch, group=self.ui_group)
			self._fs_ring.opacity = 0
		if not hasattr(self, '_fs_bar_bg'):
			self._fs_bar_bg = shapes.Rectangle(0, 0, 1, 6, color=(60, 60, 60), batch=self.batch, group=self.ui_group)
			self._fs_bar_bg.opacity = 0
		if not hasattr(self, '_fs_bar_fg'):
			self._fs_bar_fg = shapes.Rectangle(0, 0, 1, 6, color=(255, 200, 0), batch=self.batch, group=self.ui_group)
			self._fs_bar_fg.opacity = 0

	def _update_bullet_delete_overlay(self):
		try:
			self._ensure_bd_shapes()
			if hasattr(self.game, '_bullet_delete_pos') and self.game._bullet_delete_pos:
				x, y = self.game._bullet_delete_pos
				bd_radius = 60.0
				if hasattr(self.game, '_bullet_delete_tool') and self.game._bullet_delete_tool:
					bd_radius = float(getattr(self.game._bullet_delete_tool, 'radius', 60.0))
				# Force bright debug color and full opacity for visibility
				self._bd_circle.color = (255, 0, 255)
				self._bd_border.color = (255, 255, 255)
				self._bd_circle.x = x; self._bd_circle.y = y; self._bd_circle.radius = bd_radius; self._bd_circle.opacity = 200
				self._bd_border.x = x; self._bd_border.y = y; self._bd_border.radius = bd_radius; self._bd_border.opacity = 255
			else:
				self._bd_circle.opacity = 0
				self._bd_border.opacity = 0
			# Update failsafe progress at mouse
			if getattr(self.game, '_failsafe_active', False) and getattr(self.game, '_failsafe_mouse_pos', None):
				x, y = self.game._failsafe_mouse_pos
				progress = float(getattr(self.game, '_failsafe_progress', 0.0))
				radius = 30.0
				# ring
				self._fs_ring.x = x; self._fs_ring.y = y; self._fs_ring.radius = radius
				self._fs_ring.opacity = 200
				self._fs_ring.color = (255, int(200 * (1.0 - progress) + 0.0), 0)
				# bar background
				bar_w = 80
				self._fs_bar_bg.x = int(x - bar_w/2); self._fs_bar_bg.y = int(y - radius - 12)
				self._fs_bar_bg.width = bar_w; self._fs_bar_bg.opacity = 200
				# bar foreground
				self._fs_bar_fg.x = int(x - bar_w/2); self._fs_bar_fg.y = int(y - radius - 12)
				self._fs_bar_fg.width = int(bar_w * max(0.0, min(1.0, progress)))
				self._fs_bar_fg.opacity = 230
			else:
				self._fs_ring.opacity = 0
				self._fs_bar_bg.opacity = 0
				self._fs_bar_fg.opacity = 0
		except Exception as e:
			print(f"ERROR updating bullet delete overlay: {e}")
	
	def _draw_brush_radius_indicator(self):
		"""Draw the brush radius indicator for the FreeDraw tool"""
		try:
			if hasattr(self.game, 'current_mouse_pos') and self.game.current_mouse_pos:
				x, y = self.game.current_mouse_pos
				brush_radius = self.game.tools[self.game.current_tool_index].thickness / 2
				
				# Draw brush radius circle
				from pyglet_physics_game.ui.color_manager import get_color_manager
				color_mgr = get_color_manager()
				circle = shapes.Circle(x, y, brush_radius, color=color_mgr.brush_radius)
				circle.opacity = 100
				circle.draw()
				
				# Draw border
				border = shapes.Circle(x, y, brush_radius, color=color_mgr.debug_text)
				border.opacity = 150
				border.draw()
				
		except Exception as e:
			print(f"ERROR drawing brush radius indicator: {e}")
			traceback.print_exc()
	
	def _draw_fps_counter(self):
		"""Draw the FPS counter with real-time values"""
		try:
			# Get real-time FPS from performance monitor
			fps = self.game.performance_monitor.get_fps()
			
			# Get additional performance metrics
			update_time = getattr(self.game.performance_monitor, 'last_update_time', 0)
			draw_time = getattr(self.game.performance_monitor, 'last_draw_time', 0)
			
			# Position FPS counter in bottom right with detailed info
			from pyglet_physics_game.ui.color_manager import get_color_manager
			color_mgr = get_color_manager()
			fps_text = f"FPS: {fps:.0f}"
			label = self._label(fps_text, font_size=16, x=self.game.width - 120, y=10, color=color_mgr.debug_text)
			label.draw()
			
			# Show performance breakdown
			if update_time > 0:
				update_text = f"Update: {update_time*1000:.1f}ms"
				label = self._label(update_text, font_size=12, x=self.game.width - 120, y=30, color=color_mgr.debug_success)
				label.draw()
			
			if draw_time > 0:
				draw_text = f"Draw: {draw_time*1000:.1f}ms"
				label = self._label(draw_text, font_size=12, x=self.game.width - 120, y=50, color=color_mgr.debug_warning)
				label.draw()
			
			# Show enhanced object statistics
			enhanced_objects = sum(1 for obj in self.game.physics_objects if hasattr(obj, 'bullet_data'))
			total_objects = len(self.game.physics_objects)
			object_text = f"Enhanced: {enhanced_objects}/{total_objects}"
			label = self._label(object_text, font_size=12, x=self.game.width - 120, y=70, color=color_mgr.debug_info)
			label.draw()
		except Exception as e:
			print(f"ERROR drawing FPS counter: {e}")
			traceback.print_exc()
	
	def _draw_wind_particles(self):
		"""Draw wind particles for visual feedback"""
		try:
			if self.game.current_wind_strength > 0:
				for particle in self.game.particle_system.wind_particles:
					# Calculate opacity based on lifetime and wind strength
					opacity = int(255 * (particle['lifetime'] / 4.0) * (self.game.current_wind_strength / 1000))
					opacity = max(30, min(200, opacity))  # Clamp between 30-200
					
					# Draw particle as a small circle
					from pyglet_physics_game.ui.color_manager import get_color_manager
					color_mgr = get_color_manager()
					circle = shapes.Circle(particle['x'], particle['y'], particle['size'], 
									 color=color_mgr.particle_wind)
					circle.opacity = opacity
					circle.draw()
					
		except Exception as e:
			print(f"ERROR drawing wind particles: {e}")
			traceback.print_exc()
	
	def _draw_object_trails(self):
		"""Draw object trails - ULTRA-FAST for maximum performance"""
		try:
			# Get trail data directly (no copying)
			trail_data = self.game.trail_system.get_all_trails()
			
			# Skip if no trails (performance optimization)
			if not trail_data:
				return
			
			# ULTRA-FAST: Only 2 colors and fixed opacity for maximum performance
			colors = [(255, 255, 0), (200, 150, 0)]  # Just 2 colors
			opacity = 100  # Fixed low opacity for performance
			
			for obj_id, (x_array, y_array, count) in trail_data.items():
				if count < 2:
					continue
				
				# Draw trail segments with maximum performance
				for i in range(count - 1):
					start_x, start_y = x_array[i], y_array[i]
					end_x, end_y = x_array[i + 1], y_array[i + 1]
					
					# Use simple color based on segment index
					color = colors[i % len(colors)]
					
					# Draw trail segment with maximum performance
					line = shapes.Line(start_x, start_y, end_x, end_y, color=color)
					line.opacity = opacity
					line.draw()
						
		except Exception as e:
			if getattr(self.game, 'debug_mode', False):
				print(f"ERROR drawing object trails: {e}")
				traceback.print_exc()
