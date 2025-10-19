import os
import json
import time
import traceback
from typing import List, Dict, Optional, Tuple

import pyglet
from pyglet import shapes, text
from pyglet.gl import glEnable, glDisable, glScissor, GL_SCISSOR_TEST

COLUMNS = 3
COL_WIDTH = 360  # enlarged for better readability
ROW_HEIGHT = 22
PADDING = 10
SLIDER_HEIGHT = 14
SLIDER_TRACK_HEIGHT = 6
SAVE_DEBOUNCE_S = 0.0  # make param saving immediate for precision
MARQUEE_SPEED_PX_S = 40.0
MARQUEE_GAP_PX = 30

class AudioSelectionMenu:
	"""Shift-hold modal, three-column audio selection menu for samples, frequencies, and noise parameters"""
	
	def __init__(self, game):
		self.game = game
		self.theme = getattr(game, 'ui_theme', None)
		self.coordinate_manager = None  # Will be set by game core
		# Initialize color manager
		from .color_manager import get_color_manager
		self.color_mgr = get_color_manager()
		

		self.opened: bool = False
		self.active_selector: Optional[str] = None  # 'left' or 'right'
		self.anchor: Tuple[int, int] = (200, 200)
		self.hover_col: int = -1
		self.hover_index: int = -1
		self.left_selection: Optional[Dict] = None
		self.right_selection: Optional[Dict] = None
		# Use shared audio bank file at project root
		root_dir = os.path.dirname(os.path.dirname(__file__))  # pyglet_physics_game
		self.presets_file = os.path.join(root_dir, 'audio_bank_preset.json')
		self.active_preset: Optional[int] = None
		self._last_save_time: float = 0.0
		self._scroll_accumulator: float = 0.0
		
		# Exponential scrolling system for frequency/noise parameters
		self._scroll_start_time: float = 0.0
		self._is_scrolling: bool = False
		self._current_scroll_param: Optional[str] = None  # param ID being scrolled
		
		# Cached listings
		self._col1_items = ['frequencies', 'noise', 'samples', 'properties', 'audiogroup', 'midi control']
		self._col2_items: List[str] = []
		self._col3_items: List[Dict] = []  # params or files
		self._selected_folder: Optional[str] = None  # remember last folder hovered in col 1
		self._selected_subfolder: Optional[str] = None  # remember last subfolder hovered in col 2 (for samples)
		self._selected_preset: Optional[str] = None  # remember last preset selected in col 2 (for frequencies/noise)
		self._selected_property: Optional[str] = None  # remember last property selected in col 2 (for properties)
		self._col3_offset: int = 0  # scroll offset for column 3
		
		# Performance optimization: cache selection states
		self._selection_cache: Dict[str, bool] = {}
		self._last_cache_update: float = 0.0
		
		# Ultra-performance: cache drawing objects
		self._cached_rectangles: Dict[str, shapes.Rectangle] = {}
		self._cached_labels: Dict[str, text.Label] = {}
		self._last_draw_time: float = 0.0
		
		# Ensure preset bank exists and pre-load active preset 0 if present
		self._ensure_presets_file()
		try:
			data = self._load_presets()
			if '0' in data:
				self.set_active_preset(0)
			
			# Load properties from current active preset (will be loaded when selector is set)
			if self.active_preset is not None:
				print(f"DEBUG: Active preset {self.active_preset} loaded, properties will be loaded when selector is set")

		except Exception:
			pass
	
	def set_coordinate_manager(self, coordinate_manager):
		"""Set the coordinate manager for this menu"""
		self.coordinate_manager = coordinate_manager
	
	# ----- Public API -----
	def open(self, selector: str, x: int, y: int):
		self.opened = True
		self.active_selector = selector  # 'left' or 'right'
		self.anchor = (x, y)
		self.hover_col = 0
		self.hover_index = -1
		self._selected_folder = None
		self._selected_subfolder = None
		self._col3_offset = 0
		self._populate_col2(None)
		self._col3_items = []
		
		# Load properties for this selector
		self._load_selector_properties()
		

	
	def close_and_commit(self):
		if not self.opened:
			return
		# Build selection from current hover
		selection = self._current_hover_selection()
		# Validate selection based on type and column rules
		is_valid = False
		if selection:
			sel_type = selection.get('type')
			if sel_type == 'samples':
				# Must have an actual file chosen in column 3
				is_valid = bool(selection.get('file'))
			elif sel_type in ('frequencies', 'noise'):
				# Must have a preset chosen from column 2
				is_valid = bool(selection.get('preset'))
			elif sel_type == 'properties':
				# Must have a property chosen from column 2 and value from column 3
				is_valid = bool(selection.get('property') and selection.get('value') is not None)
		# Commit only if valid per the above rules
		if selection and is_valid:
			# Normalize file and preset paths to project-relative if present
			try:
				root_dir = os.path.dirname(os.path.dirname(__file__))  # pyglet_physics_game
				repo_root = os.path.dirname(root_dir)
				def _relpath_inplace(sel: Dict):
					if not isinstance(sel, dict):
						return sel
					if 'file' in sel and sel['file']:
						fp = sel['file']
						try:
							if os.path.isabs(fp):
								sel['file'] = os.path.relpath(fp, start=repo_root).replace('\\', '/')
						except Exception:
							pass
					if 'preset' in sel and sel['preset']:
						pp = sel['preset']
						try:
							if os.path.isabs(pp):
								sel['preset'] = os.path.relpath(pp, start=repo_root).replace('\\', '/')
						except Exception:
							pass
					return sel
				selection = _relpath_inplace(selection)
			except Exception:
				pass

			if self.active_selector == 'left':
				self.left_selection = selection
			else:
				self.right_selection = selection

			# Emit IPC event for selection commit so JUCE can load sources/presets
			try:
				from pyglet_physics_game.ipc.client import get_ipc_client
				client = get_ipc_client()
				client.send({
					"menu.selection": {
						"selector": self.active_selector,
						"preset": int(self.active_preset) if self.active_preset is not None else None,
						"selection": selection
					}
				})
			except Exception:
				pass
			# Visual cue: briefly remember commit time
			self._last_commit_time = time.time()
			# Persist into the currently active preset slot, preserving existing properties
			try:
				if self.active_preset is not None:
					data = self._load_presets()
					preset_key = str(self.active_preset)
					preset_data = data.get(preset_key, {})
					# Helper to normalize and preserve properties
					def _merge_selector(sel_key: str, sel_value: Optional[Dict]) -> Dict:
						existing = preset_data.get(sel_key, {}) if isinstance(preset_data, dict) else {}
						props = existing.get('properties')
						merged = sel_value.copy() if isinstance(sel_value, dict) else existing.copy()
						# Normalize any absolute paths
						root_local = os.path.dirname(os.path.dirname(__file__))
						repo_root_local = os.path.dirname(root_local)
						if 'file' in merged and merged.get('file') and os.path.isabs(merged['file']):
							merged['file'] = os.path.relpath(merged['file'], start=repo_root_local).replace('\\', '/')
						if 'preset' in merged and merged.get('preset') and os.path.isabs(merged['preset']):
							merged['preset'] = os.path.relpath(merged['preset'], start=repo_root_local).replace('\\', '/')
						# Preserve properties if not explicitly provided in the new selection
						if props and 'properties' not in merged:
							merged['properties'] = props
						return merged
					# Build merged selectors
					left_merged = _merge_selector('left_selector', self.left_selection)
					right_merged = _merge_selector('right_selector', self.right_selection)
					data[preset_key] = {
						'left_selector': left_merged,
						'right_selector': right_merged
					}
					self._save_presets(data)
			except Exception as e:
				print(f"ERROR persisting active preset: {e}")
			
			# Save motion direction if it's a motion selection
			if selection.get('type') == 'motion' and selection.get('direction'):
				self._save_motion_direction(selection['direction'])
		# If committing a samples selection, ensure default looping property exists in preset if missing
		try:
			if selection and selection.get('type') == 'samples' and self.active_preset is not None and self.active_selector is not None:
				data = self._load_presets()
				pk = str(self.active_preset)
				sk = f"{self.active_selector}_selector"
				if pk in data and sk in data[pk]:
					props = data[pk][sk].get('properties', {})
					if 'looping' not in props:
						props['looping'] = 'on'
					data[pk][sk]['properties'] = props
					self._save_presets(data)
		except Exception:
			pass
		self.opened = False
		self.active_selector = None
		self.hover_col = -1
		self.hover_index = -1
		self._selected_folder = None
		self._selected_subfolder = None
		self._selected_direction = None
		self._col3_offset = 0
		

	
	def draw(self):
		if not self.opened:
			return
		try:

			x0, y0 = self.anchor
			# Clamp menu within window
			menu_w = COLUMNS * COL_WIDTH + (COLUMNS + 1) * PADDING
			menu_h = 14 * ROW_HEIGHT + 2 * PADDING
			x = max(PADDING, min(x0, self.game.width - menu_w - PADDING))
			y = max(PADDING, min(y0, self.game.height - menu_h - PADDING))
			
			# Panel background
			panel = shapes.Rectangle(x, y, menu_w, menu_h, color=self.color_mgr.background_ui_panel)
			panel.opacity = 230
			panel.draw()
			
			# Columns
			col_x = [x + PADDING + i * (COL_WIDTH + PADDING) for i in range(COLUMNS)]
			col_y = y + menu_h - PADDING - ROW_HEIGHT
			
			# Headings
			headings = ["Folders", "Contents", "Parameters / Files"]
			for i, title in enumerate(headings):
				self._label(title, 12, col_x[i], col_y + 12, self.color_mgr.text_secondary, emphasize=True)
			
			# Items
			self._draw_list(col_x[0], col_y - ROW_HEIGHT, self._col1_items, col_index=0)
			self._draw_list(col_x[1], col_y - ROW_HEIGHT, self._col2_items, col_index=1)
			self._draw_col3(col_x[2], col_y - ROW_HEIGHT, x, y, menu_h)
			
			# Active selector badge
			badge_text = f"Selecting: {'LEFT' if self.active_selector=='left' else 'RIGHT'}"
			self._label(badge_text, 12, x + menu_w - 200, y + 6, self.color_mgr.category_color('tools'), emphasize=True)
			
			# Selection path indicator - position it below the menu to avoid overlap
			path_text = self._get_selection_path_text()
			if path_text:
				# Position below the menu with some padding
				path_y = y - 25
				self._label(path_text, 10, x + 10, path_y, self.color_mgr.feedback_success, emphasize=True)
		except Exception as e:
			print(f"ERROR drawing experimental menu: {e}")
			traceback.print_exc()
	
	def handle_mouse_motion(self, mx: int, my: int):
		if not self.opened:
			return
		self.hover_col, self.hover_index = self._hit_test(mx, my)
		
		# Store previous column 3 items to detect changes
		prev_col3_items = self._col3_items.copy()
		
		# Populate dependent columns on hover change
		if self.hover_col == 0:
			folder = self._get_item(self._col1_items, self.hover_index)
			if folder != self._selected_folder:  # Only update if changed
				self._selected_folder = folder
				self._selected_preset = None  # Reset preset when changing folders
				# Only reset property if changing away from properties
				if self._selected_folder != 'properties':
					self._selected_property = None
				self._selection_cache.clear()  # Clear cache when selection changes
				self._populate_col2(folder)
				self._populate_col3(None)
		elif self.hover_col == 1:
			folder = self._selected_folder
			sub = self._get_item(self._col2_items, self.hover_index)
			selection_changed = False
			if folder == 'samples' and sub != self._selected_subfolder:
				self._selected_subfolder = sub
				selection_changed = True
			elif folder in ('frequencies', 'noise') and sub != self._selected_preset:
				self._selected_preset = sub
				selection_changed = True
			elif folder == 'properties' and sub != self._selected_property:
				self._selected_property = sub
				selection_changed = True
			
			if selection_changed:
				self._selection_cache.clear()  # Clear cache when selection changes
				self._populate_col3(sub, folder)
		else:
			# When not hovering in column 1, don't reset motion direction
			# This prevents the direction from being lost when hovering out
			pass
		
		# Hide inner cursor when hovering over sliders
		if (self.hover_col == 2 and self.hover_index >= 0 and 
			self.hover_index < len(self._col3_items) and 
			self._col3_items[self.hover_index].get('kind') == 'param'):
			# Hovering over a slider - hide inner cursor
			if hasattr(self.game, 'mouse_system'):
				self.game.mouse_system.set_hide_inner_cursor(True)
		else:
			# Not hovering over a slider - show inner cursor
			if hasattr(self.game, 'mouse_system'):
				self.game.mouse_system.set_hide_inner_cursor(False)
		

	
	def handle_scroll(self, mx: int, my: int, scroll_y: int):
		if not self.opened:
			return
		
		# Check if we're in the third panel area
		col, idx = self._hit_test(mx, my)

		# Panel 2 slider: audiogroup value 1..8 (placeholder routing group)
		if col == 1 and self._selected_folder == 'audiogroup':
			try:
				properties_config = self._load_properties_config()
				current_group = int(properties_config.get('audio_group', 1))
				step = 1 if scroll_y > 0 else -1
				new_val = max(1, min(8, current_group + step))
				if new_val != current_group:
					param = { 'id': 'audio_group', 'value': int(new_val), 'type': 'int', 'min': 1, 'max': 8 }
					self._save_properties_config(param)
			except Exception:
				pass
			return
		
		# Special handling for shape parameters - make entire third panel scrollable
		if col == 2 and self._selected_folder == 'properties' and self._selected_property == 'shape':
			# Find the shape parameter in col3_items
			shape_item = None
			for item in self._col3_items:
				if item.get('kind') == 'param' and item['data'].get('id') == 'shape':
					shape_item = item
					break
			
			if shape_item:
				param = shape_item['data']
				choices = param.get('choices', [])
				current_value = param.get('value', choices[0] if choices else '')
				current_index = choices.index(current_value) if current_value in choices else 0
				
				# Scroll through choices
				if scroll_y > 0:  # Scroll up - next choice
					new_index = (current_index + 1) % len(choices)
				else:  # Scroll down - previous choice
					new_index = (current_index - 1) % len(choices)
				
				param['value'] = choices[new_index]
				self._save_param_change_precise(shape_item['json_path'], param)
				return
		
		# Special handling for color parameters - make entire third panel scrollable
		if col == 2 and self._selected_folder == 'properties' and self._selected_property == 'color':
			# Find the color parameter being hovered
			if idx is not None and idx >= 0 and idx < len(self._col3_items):
				item = self._col3_items[idx]
				if item.get('kind') == 'param' and item['data'].get('id') in ['color_r', 'color_g', 'color_b']:
					param = item['data']
					param_type = param.get('type', 'int')
					param_id = param.get('id', '')
					
					# Handle RGB slider scrolling with exponential scaling
					base_step = float(param.get('step', 1.0))
					step = self._get_rgb_exponential_scroll_step(param_id, base_step)
					delta = step * (1 if scroll_y > 0 else -1)
					new_val = float(param.get('value', 0)) + delta
					new_val = max(float(param['min']), min(float(param['max']), new_val))
					param['value'] = int(round(new_val)) if param_type == 'int' else new_val
					
					self._save_param_change_precise(item['json_path'], param)
					return
		
		# If hovering column 3 and current folder is samples, scroll list
		if col == 2 and self._selected_folder == 'samples':
			visible_rows = 14
			total = len(self._col3_items)
			max_offset = max(0, total - visible_rows)
			# Normalize scroll direction to +/-1
			delta = 1 if scroll_y > 0 else -1
			self._col3_offset = max(0, min(max_offset, self._col3_offset - delta))
			return
		
		if col != 2 or idx is None or idx < 0:
			return
		# Translate visible index to actual index with offset
		actual_idx = idx + self._col3_offset
		if actual_idx >= len(self._col3_items):
			return
		item = self._col3_items[actual_idx]
		if item.get('kind') != 'param':
			return
		# Compute new value precisely based on type and step
		param = item['data']
		param_type = param.get('type', 'float')
		
		if param_type == 'choice':
			# Handle choice parameters (direction, pitch on grid)
			choices = param.get('choices', [])
			current_value = param.get('value', choices[0] if choices else '')
			current_index = choices.index(current_value) if current_value in choices else 0
			
			# Scroll through choices
			if scroll_y > 0:  # Scroll up - next choice
				new_index = (current_index + 1) % len(choices)
			else:  # Scroll down - previous choice
				new_index = (current_index - 1) % len(choices)
			
			param['value'] = choices[new_index]
		else:
			# Handle numeric parameters (speed, amplitude)
			base_step = float(param.get('step', 1.0))
			is_int = (param_type == 'int')
			
			# Use exponential scrolling for frequency/noise parameters and RGB color parameters
			if self._is_frequency_or_noise_param(param):
				param_id = param.get('id', 'unknown')
				step = self._get_exponential_scroll_step(param_id, base_step)
			elif self._is_rgb_color_param(param):
				param_id = param.get('id', 'unknown')
				step = self._get_rgb_exponential_scroll_step(param_id, base_step)
			else:
				step = base_step
			
			delta = step * (1 if scroll_y > 0 else -1)
			new_val = float(param.get('value', 0)) + delta
			new_val = max(float(param['min']), min(float(param['max']), new_val))
			param['value'] = int(round(new_val)) if is_int else new_val
		
		self._save_param_change_precise(item['json_path'], param)
	
	def set_active_preset(self, preset_idx: int):
		self.active_preset = preset_idx
		data = self._load_presets()
		preset = data.get(str(preset_idx))
		if preset:
			self.left_selection = preset.get('left_selector')
			self.right_selection = preset.get('right_selector')
			# Load properties configuration for this preset (will be loaded when selector is set)
			self._selected_property = None
	
	def store_current_to_preset(self, preset_idx: int):
		data = self._load_presets()
		preset_data = {
			'left_selector': self.left_selection,
			'right_selector': self.right_selection
		}
		data[str(preset_idx)] = preset_data
		self._save_presets(data)
		self.active_preset = preset_idx
	
	def update(self, dt: float):
		"""Update the menu"""
		# Update exponential scrolling state
		if self._is_scrolling and time.time() - self._scroll_start_time > 0.2:
			# Reset scrolling state after 0.2 seconds of inactivity
			self._is_scrolling = False
			self._current_scroll_param = None
	
	# ----- Exponential scrolling helpers -----
	def _get_exponential_scroll_step(self, param_id: str, base_step: float) -> float:
		"""Calculate exponential scroll step based on scroll duration for frequency/noise parameters"""
		current_time = time.time()
		
		if not self._is_scrolling or self._current_scroll_param != param_id:
			# Start new scroll session
			self._is_scrolling = True
			self._current_scroll_param = param_id
			self._scroll_start_time = current_time
			return base_step
		
		# Calculate scroll duration
		scroll_duration = current_time - self._scroll_start_time
		
		# Ultra aggressive exponential scaling: 0.1s -> 0.15s -> 0.2s -> 0.25s -> 0.25s+
		if scroll_duration < 0.1:
			multiplier = 1.0
		elif scroll_duration < 0.16:
			multiplier = 10.0
		elif scroll_duration < 0.22:
			multiplier = 50.0
		elif scroll_duration < 0.35:
			multiplier = 100
		elif scroll_duration < 0.4:
			multiplier = 500
		else:
			multiplier = 1000
		
		step = base_step * multiplier
		return step
	
	def _get_rgb_exponential_scroll_step(self, param_id: str, base_step: float) -> float:
		"""Calculate exponential scroll step for RGB color parameters with faster scaling"""
		current_time = time.time()
		
		if not self._is_scrolling or self._current_scroll_param != param_id:
			# Start new scroll session
			self._is_scrolling = True
			self._current_scroll_param = param_id
			self._scroll_start_time = current_time
			return base_step
		
		# Calculate scroll duration
		scroll_duration = current_time - self._scroll_start_time
		
		# RGB exponential scaling: 0.15s -> 1, 0.15-0.2s -> 10, 0.2-0.3s -> 50, 0.3s+ -> 100
		if scroll_duration < 0.15:
			multiplier = 1.0
		elif scroll_duration < 0.2:
			multiplier = 10.0
		elif scroll_duration < 0.3:
			multiplier = 50.0
		else:
			multiplier = 100.0
		
		step = base_step * multiplier
		return step
	
	def _is_frequency_or_noise_param(self, param: Dict) -> bool:
		"""Check if parameter is a frequency or noise parameter that should use exponential scrolling"""
		param_id = param.get('id', '').lower()
		param_label = param.get('label', '').lower()
		
		# Check for frequency-related keywords
		freq_keywords = ['freq', 'frequency', 'hz', 'hertz']
		noise_keywords = ['noise', 'bandwidth', 'cutoff']
		
		return (any(keyword in param_id for keyword in freq_keywords + noise_keywords) or
				any(keyword in param_label for keyword in freq_keywords + noise_keywords))
	
	def _is_rgb_color_param(self, param: Dict) -> bool:
		"""Check if parameter is an RGB color parameter that should use exponential scrolling"""
		param_id = param.get('id', '').lower()
		return param_id in ['color_r', 'color_g', 'color_b']
	
	# ----- Internal drawing helpers -----
	def _draw_shape_preview(self, shape_name: str, x: int, y: int, size: int = 16):
		"""Draw a preview of the selected shape using the exact same definitions as the JSON file"""
		try:
			# Import shape manager to get the exact shape definitions
			from shapes.shape_manager import get_shape_manager
			shape_manager = get_shape_manager()
			
			# Calculate center position
			center_x = x + size // 2
			center_y = y + size // 2
			
			# Scale factor to fit the preview size (size is the total preview area)
			# The JSON shapes are defined with a radius of ~8, so scale accordingly
			scale = (size - 4) / 16.0  # Leave 2px margin on each side
			
			# Create the shape using the same method as the actual game
			shape = shape_manager.create_visual_shape(shape_name, center_x, center_y, 
													color=(255, 255, 255), scale=scale)
			
			if shape:
				shape.opacity = 220
				shape.draw()
			else:
				# Fallback to simple circle if shape creation fails
				shape = shapes.Circle(center_x, center_y, size // 2 - 2, color=(255, 255, 255))
				shape.opacity = 220
				shape.draw()
				
		except Exception as e:
			# Fallback to simple circle
			shape = shapes.Circle(x + size // 2, y + size // 2, size // 2 - 2, color=(255, 255, 255))
			shape.opacity = 220
			shape.draw()
	
	def _draw_colored_shape_preview(self, shape_name: str, color: Tuple[int, int, int], x: int, y: int, size: int = 16):
		"""Draw a preview of the selected shape with the specified color"""
		try:
			# Import shape manager to get the exact shape definitions
			from shapes.shape_manager import get_shape_manager
			shape_manager = get_shape_manager()
			
			# Calculate center position
			center_x = x + size // 2
			center_y = y + size // 2
			
			# Scale factor to fit the preview size (size is the total preview area)
			# The JSON shapes are defined with a radius of ~8, so scale accordingly
			scale = (size - 4) / 16.0  # Leave 2px margin on each side
			
			# Create the shape using the same method as the actual game, but with the specified color
			shape = shape_manager.create_visual_shape(shape_name, center_x, center_y, 
													color=color, scale=scale)
			
			if shape:
				shape.opacity = 255  # Full opacity for color preview
				shape.draw()
			else:
				# Fallback to simple circle if shape creation fails
				shape = shapes.Circle(center_x, center_y, size // 2 - 2, color=color)
				shape.opacity = 255
				shape.draw()
				
		except Exception as e:
			# Fallback to simple circle
			shape = shapes.Circle(x + size // 2, y + size // 2, size // 2 - 2, color=color)
			shape.opacity = 255
			shape.draw()
	
	def _label(self, value: str, font_size: int, x: int, y: int, color: Tuple[int,int,int], emphasize: bool=False):
		"""Ultra-optimized label drawing"""
		try:
			# Direct creation and drawing - no intermediate variables
			if emphasize:
				text.Label(value, font_size=font_size + 1, x=x, y=y, 
					color=tuple(min(255, c + 30) for c in color), 
					font_name=self.theme.ui_font_names if self.theme else ["Arial"]).draw()
			else:
				text.Label(value, font_size=font_size, x=x, y=y, color=color, 
					font_name=self.theme.ui_font_names if self.theme else ["Arial"]).draw()
		except Exception:
			# Minimal fallback
			text.Label(value, font_size=font_size, x=x, y=y, color=color).draw()
	
	def _draw_list(self, x: int, y_top: int, items: List[str], col_index: int):
		for i, name in enumerate(items[:14]):
			row_y = y_top - i * ROW_HEIGHT
			is_hover = (self.hover_col == col_index and self.hover_index == i)
			is_selected = self._is_item_selected(col_index, i, name)
			
			# Special handling for properties item in column 1 - always show current property
			display_name = name
			if col_index == 0 and name == 'properties':
				if self._selected_property:
					display_name = f"properties: {self._selected_property}"
				else:
					# Show default
					display_name = "properties: direction"
			
			# Background highlight for hovered or selected
			if is_hover:
				bg_color = self.color_mgr.category_color('tools')
				bg_opacity = 220
			elif is_selected:
				bg_color = self.color_mgr.feedback_success
				bg_opacity = 180
			else:
				bg_color = self.color_mgr.background_ui_panel
				bg_opacity = 140
			
			bg = shapes.Rectangle(x, row_y - 2, COL_WIDTH, ROW_HEIGHT, color=bg_color)
			bg.opacity = bg_opacity
			bg.draw()
			
			# Text style
			if is_hover:
				color = self.color_mgr.text_primary
				emphasize = True
			elif is_selected:
				color = self.color_mgr.text_primary
				emphasize = True
			else:
				color = self.color_mgr.text_secondary
				emphasize = False
			
			self._label(str(display_name), 12, x + 8, row_y + 3, color, emphasize=emphasize)
			# Inline slider for audiogroup in panel 2
			if col_index == 1 and self._selected_folder == 'audiogroup' and name == 'group':
				try:
					properties_config = self._load_properties_config()
					val = int(properties_config.get('audio_group', 1))
					val = max(1, min(8, val))
					slider_track_width = COL_WIDTH - 120
					slider_track_x = x + 100
					track_y = row_y + 4
					track_rect = shapes.Rectangle(slider_track_x, track_y, slider_track_width, SLIDER_TRACK_HEIGHT, color=self.color_mgr.outline_default)
					track_rect.opacity = 220
					track_rect.draw()
					n = (val - 1) / 7.0
					knob_x = int(slider_track_x + n * slider_track_width)
					knob_rect = shapes.Rectangle(knob_x - 3, track_y - 2, 6, SLIDER_HEIGHT, color=(200, 200, 200))
					knob_rect.opacity = 240
					knob_rect.draw()
					self._label(f"{val}", 12, slider_track_x + slider_track_width + 8, row_y + 3, self.color_mgr.text_primary, emphasize=False)
				except Exception:
					pass
	
	def _draw_col3(self, x: int, y_top: int, panel_x: int, panel_y: int, panel_h: int):
		visible_rows = 14
		start = int(self._col3_offset)
		end = int(min(len(self._col3_items), start + visible_rows))
		
		# Early exit if no items to draw
		if start >= len(self._col3_items):
			return
		
		# Clip column 3 drawing so text doesn't escape boxes (temporarily disabled due to platform clipping glitches)
		# clip_x = x
		# clip_y = panel_y + PADDING
		# clip_w = COL_WIDTH
		# clip_h = panel_h - 2 * PADDING
		# glEnable(GL_SCISSOR_TEST)
		# glScissor(int(clip_x), int(clip_y), int(clip_w), int(clip_h))
		
		# Pre-calculate ALL common values once
		slider_track_width = COL_WIDTH - 16
		slider_track_x = x + 8
		value_display_x = x + COL_WIDTH - 60
		text_x = x + 8
		text_y_offset = 3
		track_y_offset = 4
		knob_size = 6
		knob_offset = 3
		
		# Pre-calculate colors
		tools_color = self.color_mgr.category_color('tools')
		success_color = self.color_mgr.feedback_success
		panel_color = self.color_mgr.background_ui_panel
		outline_color = self.color_mgr.outline_default
		text_primary = self.color_mgr.text_primary
		text_secondary = self.color_mgr.text_secondary
		
		# Ultra-optimized: reduce object creation by reusing shapes
		# Create reusable rectangle for backgrounds
		bg_rect = shapes.Rectangle(0, 0, COL_WIDTH, ROW_HEIGHT, color=panel_color)
		bg_rect.opacity = 140
		
		# Create reusable rectangle for slider track
		track_rect = shapes.Rectangle(0, 0, slider_track_width, SLIDER_TRACK_HEIGHT, color=outline_color)
		track_rect.opacity = 220
		
		# Create reusable rectangle for knob
		knob_rect = shapes.Rectangle(0, 0, knob_size, SLIDER_HEIGHT, color=text_secondary)
		knob_rect.opacity = 240
		
		# Special handling for color property - show large color preview
		if (self._selected_folder == 'properties' and self._selected_property == 'color' and 
			len(self._col3_items) >= 3):  # We have RGB sliders
			# Calculate center of the third panel
			panel_center_x = x + COL_WIDTH // 2
			panel_center_y = panel_y + panel_h // 2
			
			# Get current color and shape
			current_color = self._get_current_color()
			properties_config = self._load_properties_config()
			current_shape = properties_config.get('shape', 'circle')
			
			# Draw RGB sliders at the top of the panel
			slider_start_y = y_top
			for i, item in enumerate(self._col3_items[:3]):  # Only show first 3 (RGB)
				row_y = slider_start_y - i * ROW_HEIGHT
				
				# Check if this slider is hovered
				is_slider_hover = (self.hover_col == 2 and self.hover_index == i)
				
				# Background
				if is_slider_hover:
					bg_rect.color = tools_color
					bg_rect.opacity = 220
				else:
					bg_rect.color = panel_color
					bg_rect.opacity = 140
				bg_rect.x = x
				bg_rect.y = row_y - 2
				bg_rect.draw()
				
				# Draw the RGB slider with proper slider functionality
				p = item['data']
				param_type = p.get('type', 'int')
				
				# Text
				if is_slider_hover:
					self._label(p.get('label', 'Color'), 12, text_x, row_y + text_y_offset, text_primary, emphasize=True)
				else:
					self._label(p.get('label', 'Color'), 12, text_x, row_y + text_y_offset, text_secondary, emphasize=False)
				
				# Draw slider track
				track_y = row_y + track_y_offset
				track_rect.x = slider_track_x
				track_rect.y = track_y
				track_rect.draw()
				
				# Draw slider knob
				vmin = float(p['min'])
				vmax = float(p['max'])
				v = float(p.get('value', vmin))
				n = 0.0 if vmax == vmin else (v - vmin) / (vmax - vmin)
				knob_x = int(slider_track_x + n * slider_track_width)
				
				# Draw knob
				knob_rect.color = success_color if is_slider_hover else text_secondary
				knob_rect.x = knob_x - knob_offset
				knob_rect.y = track_y - 2
				knob_rect.draw()
				
				# Draw value
				value_text = str(int(p.get('value', 0)))
				self._label(value_text, 10, value_display_x, row_y + text_y_offset, text_primary)
			
			# Draw colored shape preview - same size and position as shape panel
			preview_size = 120  # Same size as shape panel
			preview_x = panel_center_x - preview_size // 2  # Center horizontally (same as shape panel)
			preview_y = panel_center_y - preview_size // 2  # Center vertically (same as shape panel)
			
			# Draw the large colored shape preview
			self._draw_colored_shape_preview(current_shape, current_color, preview_x, preview_y, preview_size)
			
			# Draw color info below the preview
			color_info = f"RGB({current_color[0]}, {current_color[1]}, {current_color[2]})"
			info_x = panel_center_x - (len(color_info) * 6) // 2
			self._label(color_info, 12, info_x, preview_y - 40, text_primary, emphasize=True)
		else:
			# Standard drawing for non-color properties
			# Single pass drawing with minimal object creation
			for vis_i, item in enumerate(self._col3_items[start:end]):
				row_y = y_top - vis_i * ROW_HEIGHT
				
				# Optimize state checks - only check hover once
				is_hover = (self.hover_col == 2 and self.hover_index == vis_i)
				
				# Background (ultra-optimized with reuse)
				if is_hover:
					bg_rect.color = tools_color
					bg_rect.opacity = 220
				else:
					# Only check selection if not hovered
					is_selected = self._is_item_selected(2, vis_i, item.get('name', ''))
					if is_selected:
						bg_rect.color = success_color
						bg_rect.opacity = 180
					else:
						bg_rect.color = panel_color
						bg_rect.opacity = 140
				
				bg_rect.x = x
				bg_rect.y = row_y - 2
				bg_rect.draw()

				if item['kind'] == 'param':
					p = item['data']
					# Special handling for shape parameters - show big preview instead of slider
					if p.get('id') == 'shape':
						panel_center_x = x + COL_WIDTH // 2
						panel_center_y = panel_y + panel_h // 2
						preview_size = 120
						preview_x = panel_center_x - preview_size // 2
						preview_y = panel_center_y - preview_size // 2
						self._draw_shape_preview(p.get('value', 'circle'), preview_x, preview_y, preview_size)
						shape_name = p.get('value', 'circle').title()
						name_x = panel_center_x - (len(shape_name) * 6) // 2
						self._label(shape_name, 12, name_x, preview_y - 40, text_primary, emphasize=True)
						self._label(p.get('label', 'Bullet Shape'), 12, text_x, row_y + text_y_offset, text_primary, emphasize=True)
					elif p.get('id') in ['color_r', 'color_g', 'color_b']:
						current_color = self._get_current_color()
						preview_size = 60
						preview_x = x + COL_WIDTH - preview_size - 10
						preview_y = row_y - 2
						color_rect = shapes.Rectangle(preview_x, preview_y, preview_size, ROW_HEIGHT, color=current_color)
						color_rect.opacity = 255
						color_rect.draw()
						border_rect = shapes.Rectangle(preview_x, preview_y, preview_size, ROW_HEIGHT, color=(100, 100, 100))
						border_rect.opacity = 200
						border_rect.draw()
						if is_hover:
							self._label(p.get('label', 'Color'), 12, text_x, row_y + text_y_offset, text_primary, emphasize=True)
						else:
							self._label(p.get('label', 'Color'), 12, text_x, row_y + text_y_offset, text_secondary, emphasize=False)
						value_text = str(int(p.get('value', 0)))
						self._label(value_text, 10, value_display_x, row_y + text_y_offset, text_primary)
					else:
						if is_hover:
							self._label(p.get('label', p.get('id','param')), 12, text_x, row_y + text_y_offset, text_primary, emphasize=True)
						else:
							self._label(p.get('label', p.get('id','param')), 12, text_x, row_y + text_y_offset, text_secondary, emphasize=False)
						if p.get('type') != 'choice':
							track_y = row_y + track_y_offset
							track_rect.x = slider_track_x
							track_rect.y = track_y
							track_rect.draw()
							vmin = float(p['min'])
							vmax = float(p['max'])
							v = float(p.get('value', vmin))
							n = 0.0 if vmax == vmin else (v - vmin) / (vmax - vmin)
							knob_x = int(slider_track_x + n * slider_track_width)
							knob_rect.color = success_color if is_hover else text_secondary
							knob_rect.x = knob_x - knob_offset
							knob_rect.y = track_y - 2
							knob_rect.draw()
						if p.get('type') == 'choice':
							value_text = str(p.get('value', ''))
						else:
							v = float(p.get('value', 0))
							value_text = f"{v:.2f}"
						self._label(value_text, 10, value_display_x, row_y + text_y_offset, text_primary)
				else:
					# File item truncated with ellipsis to avoid overlap
					name = item['name']
					file_color = text_primary if is_hover else text_secondary
					font_names = self.theme.ui_font_names if self.theme else ["Arial"]
					avail = COL_WIDTH - 16
					shown = name
					lbl = text.Label(shown, font_size=12, x=0, y=0, color=file_color, font_name=font_names)
					while lbl.content_width > avail and len(shown) > 4:
						shown = shown[:-4] + '...'
						lbl.text = shown
					lbl.x = text_x
					lbl.y = row_y + text_y_offset
					lbl.draw()
		
		# glDisable(GL_SCISSOR_TEST)
	
	# ----- Data population -----
	def _get_current_folder(self) -> Optional[str]:
		if self.hover_col == 0 and 0 <= self.hover_index < len(self._col1_items):
			return self._col1_items[self.hover_index]
		return self._selected_folder
	
	def _populate_col2(self, folder: Optional[str]):
		root = os.path.dirname(os.path.dirname(__file__))  # pyglet_physics_game
		audio_dir = os.path.join(os.path.dirname(root), 'audio')
		items: List[str] = []
		try:
			if folder == 'samples':
				# list subfolders in audio/samples
				samples_dir = os.path.join(audio_dir, 'samples')
				items = [d for d in os.listdir(samples_dir) if os.path.isdir(os.path.join(samples_dir, d))]
			elif folder == 'noise':
				noise_dir = os.path.join(audio_dir, 'noise')
				items = [f for f in os.listdir(noise_dir) if f.endswith('.py')]
			elif folder == 'frequencies':
				freq_dir = os.path.join(audio_dir, 'frequencies')
				items = [f for f in os.listdir(freq_dir) if f.endswith('.py')]
			elif folder == 'properties':
				# Properties options
				items = ['direction', 'speed', 'amplitude', 'pitch on grid', 'shape', 'color', 'looping']
			elif folder == 'audiogroup':
				# Single slider placeholder
				items = ['group']
			elif folder == 'midi control':
				items = ['fader']
		except Exception:
			items = []
		self._col2_items = sorted(items)
		self._col3_offset = 0
	
	def _populate_col3(self, item: Optional[str], folder: Optional[str] = None):
		self._col3_items = []
		self._col3_offset = 0
		if not folder:
			return
		root = os.path.dirname(os.path.dirname(__file__))
		audio_dir = os.path.join(os.path.dirname(root), 'audio')
		if folder == 'samples':
			if not item:
				return
			dir_path = os.path.join(audio_dir, 'samples', item)
			try:
				allowed = ('.wav', '.mp3', '.flac', '.ogg', '.aiff', '.aif', '.m4a')
				files = [f for f in os.listdir(dir_path) if f.lower().endswith(allowed) and not f.startswith('.')]
				self._col3_items = [{ 'kind': 'file', 'name': f, 'path': os.path.join(dir_path, f)} for f in sorted(files)]
			except Exception:
				self._col3_items = []
		elif folder in ('noise', 'frequencies'):
			if not item:
				return
			# sidecar json with same stem
			stem = os.path.splitext(item)[0]
			json_dir = os.path.join(audio_dir, folder)
			json_path = os.path.join(json_dir, stem + '.json')
			params = self._load_params(json_path)
			self._col3_items = [{ 'kind': 'param', 'data': p, 'json_path': json_path } for p in params]
		elif folder == 'properties':
			# Properties has different parameters based on the selected property
			if not item:
				return
			
			# Load existing properties configuration
			properties_config = self._load_properties_config()
			
			if item == 'direction':
				# Direction: horizontal/vertical (scrolling parameter)
				current_direction = properties_config.get('direction', 'horizontal')
				direction_param = {
					'id': 'direction',
					'label': 'Direction',
					'type': 'choice',
					'value': current_direction,
					'choices': ['horizontal', 'vertical']
				}
				self._col3_items = [{'kind': 'param', 'data': direction_param, 'json_path': 'properties_config'}]
			elif item == 'speed':
				# Speed: pixels/seconds (scrolling parameter)
				current_speed = properties_config.get('speed', 100.0)
				speed_param = {
					'id': 'speed',
					'label': 'Speed (px/s)',
					'type': 'float',
					'value': current_speed,
					'min': 0.0,
					'max': 1000.0,
					'step': 10.0
				}
				self._col3_items = [{'kind': 'param', 'data': speed_param, 'json_path': 'properties_config'}]
			elif item == 'amplitude':
				# Amplitude: 0-500px incremental by 10px
				current_amplitude = properties_config.get('amplitude', 100.0)
				amplitude_param = {
					'id': 'amplitude',
					'label': 'Amplitude (px)',
					'type': 'float',
					'value': current_amplitude,
					'min': 0.0,
					'max': 500.0,
					'step': 10.0
				}
				self._col3_items = [{'kind': 'param', 'data': amplitude_param, 'json_path': 'properties_config'}]
			elif item == 'pitch on grid':
				# Pitch on grid: on/off (scrolling parameter)
				current_pitch_on_grid = properties_config.get('pitch_on_grid', 'off')
				pitch_param = {
					'id': 'pitch_on_grid',
					'label': 'Pitch on Grid',
					'type': 'choice',
					'value': current_pitch_on_grid,
					'choices': ['on', 'off']
				}
				self._col3_items = [{'kind': 'param', 'data': pitch_param, 'json_path': 'properties_config'}]
			elif item == 'looping':
				# Looping: on/off (scrolling parameter)
				current_looping = properties_config.get('looping', 'on')
				loop_param = {
					'id': 'looping',
					'label': 'Looping',
					'type': 'choice',
					'value': current_looping,
					'choices': ['on', 'off']
				}
				self._col3_items = [{'kind': 'param', 'data': loop_param, 'json_path': 'properties_config'}]
			elif item == 'shape':
				# Shape: bullet shape selection (scrolling parameter)
				current_shape = properties_config.get('shape', 'circle')
				shape_param = {
					'id': 'shape',
					'label': 'Bullet Shape',
					'type': 'choice',
					'value': current_shape,
					'choices': ['circle', 'rectangle', 'square', 'triangle', 'star', 'ellipse', 'hexagon', 'diamond']
				}
				self._col3_items = [{'kind': 'param', 'data': shape_param, 'json_path': 'properties_config'}]
			elif item == 'color':
				# Color: RGB color selection with sliders
				current_color = properties_config.get('color', {'r': 255, 'g': 255, 'b': 255})
				if isinstance(current_color, list) and len(current_color) >= 3:
					# Convert list format to dict format
					current_color = {'r': current_color[0], 'g': current_color[1], 'b': current_color[2]}
				elif not isinstance(current_color, dict):
					# Default to white if format is unknown
					current_color = {'r': 255, 'g': 255, 'b': 255}
				
				# Create RGB slider parameters
				r_param = {
					'id': 'color_r',
					'label': 'Red',
					'type': 'int',
					'value': current_color.get('r', 255),
					'min': 0,
					'max': 255,
					'step': 1
				}
				g_param = {
					'id': 'color_g',
					'label': 'Green',
					'type': 'int',
					'value': current_color.get('g', 255),
					'min': 0,
					'max': 255,
					'step': 1
				}
				b_param = {
					'id': 'color_b',
					'label': 'Blue',
					'type': 'int',
					'value': current_color.get('b', 255),
					'min': 0,
					'max': 255,
					'step': 1
				}
				self._col3_items = [
					{'kind': 'param', 'data': r_param, 'json_path': 'properties_config'},
					{'kind': 'param', 'data': g_param, 'json_path': 'properties_config'},
					{'kind': 'param', 'data': b_param, 'json_path': 'properties_config'}
				]
		elif folder == 'midi control':
			# Placeholder MIDI control editor for fader assignment
			if not item:
				return
			properties_config = self._load_properties_config()
			midi_cfg = properties_config.get('midi', {}) if isinstance(properties_config, dict) else {}
			cc_val = int(midi_cfg.get('cc', 1))
			ch_val = int(midi_cfg.get('channel', 1))
			cc_param = {
				'id': 'midi_cc',
				'label': 'MIDI CC',
				'type': 'int',
				'value': max(0, min(127, cc_val)),
				'min': 0,
				'max': 127,
				'step': 1
			}
			ch_param = {
				'id': 'midi_channel',
				'label': 'MIDI Channel',
				'type': 'int',
				'value': max(1, min(16, ch_val)),
				'min': 1,
				'max': 16,
				'step': 1
			}
			self._col3_items = [
				{'kind': 'param', 'data': cc_param, 'json_path': 'properties_config'},
				{'kind': 'param', 'data': ch_param, 'json_path': 'properties_config'}
			]
	
	def _current_hover_selection(self) -> Optional[Dict]:
		# Build a selection description from current hover state
		folder = self._get_current_folder()
		if not folder:
			return None
		
		if folder == 'samples':
			# For samples: selection happens in column 3 (the actual audio files)
			sub = self._selected_subfolder or self._get_item(self._col2_items, self.hover_index if self.hover_col==1 else -1)
			file_item = None
			# translate visible hover to actual index
			if self.hover_col == 2 and 0 <= self.hover_index < 14:
				vis_idx = self.hover_index
				actual = self._col3_offset + vis_idx
				if 0 <= actual < len(self._col3_items):
					ci = self._col3_items[actual]
					if ci.get('kind') == 'file':
						file_item = ci['path']
			return { 'type': 'samples', 'folder': sub, 'file': file_item }
		elif folder == 'properties':
			# For properties: selection happens in column 2 (property type) and column 3 (property value)
			sub = self._get_item(self._col2_items, self.hover_index if self.hover_col==1 else -1)
			# Get property value from column 3 if hovering there
			property_value = None
			if self.hover_col == 2 and 0 <= self.hover_index < len(self._col3_items):
				item = self._col3_items[self.hover_index]
				if item.get('kind') == 'param':
					property_value = item['data'].get('value')
			return { 'type': 'properties', 'property': sub, 'value': property_value }
		else:
			# For frequencies/noise: selection happens in column 2 (the preset files)
			sub = self._get_item(self._col2_items, self.hover_index if self.hover_col==1 else -1)
			preset = None
			if sub and self.hover_col == 1:  # Only when hovering in column 2
				stem = os.path.splitext(sub)[0]
				root = os.path.dirname(os.path.dirname(__file__))
				audio_dir = os.path.join(os.path.dirname(root), 'audio')
				json_abs = os.path.join(audio_dir, folder, stem + '.json')
				# Emit relative path to keep presets portable
				repo_root = os.path.dirname(root)
				try:
					preset = os.path.relpath(json_abs, start=repo_root).replace('\\', '/')
				except Exception:
					preset = json_abs
			return { 'type': folder, 'preset': preset }
	
	# ----- Persistence helpers -----
	def _load_params(self, json_path: str) -> List[Dict]:
		try:
			with open(json_path, 'r', encoding='utf-8') as f:
				data = json.load(f)
				return data.get('params', [])
		except Exception:
			return []
	
	def _save_param_change_precise(self, json_path: str, changed_param: Dict):
		"""Save only the changed parameter immediately and precisely."""
		try:
			if json_path == 'properties_config':
				# Special handling for properties configuration - save to audio bank preset
				self._save_properties_config(changed_param)
			else:
				with open(json_path, 'r', encoding='utf-8') as f:
					data = json.load(f)
				for p in data.get('params', []):
					if p.get('id') == changed_param.get('id'):
						p['value'] = changed_param.get('value')
						break
				with open(json_path, 'w', encoding='utf-8') as f:
					json.dump(data, f, indent=2)
		except Exception as e:
			print(f"ERROR saving precise param: {e}")
	
	def _load_properties_config(self) -> Dict:
		"""Load properties configuration from the current active preset and selector."""
		try:
			if self.active_preset is None or self.active_selector is None:
				return {}
			data = self._load_presets()
			preset_data = data.get(str(self.active_preset), {})
			selector_data = preset_data.get(f'{self.active_selector}_selector', {})
			return selector_data.get('properties', {})
		except Exception as e:
			print(f"ERROR loading properties config: {e}")
			return {}
	
	def _save_properties_config(self, properties_param: Dict):
		"""Save properties configuration to the current active preset and selector."""
		try:
			if self.active_preset is None or self.active_selector is None:
				return
			# Load current presets
			data = self._load_presets()
			
			# Get or create preset data
			preset_key = str(self.active_preset)
			selector_key = f'{self.active_selector}_selector'
			if preset_key not in data:
				data[preset_key] = {}
			if selector_key not in data[preset_key]:
				data[preset_key][selector_key] = {}
			if 'properties' not in data[preset_key][selector_key]:
				data[preset_key][selector_key]['properties'] = {}
			
			# Update the properties parameter
			param_id = properties_param.get('id', '')
			param_value = properties_param.get('value')
			
			if param_id == 'direction':
				data[preset_key][selector_key]['properties']['direction'] = param_value
			elif param_id == 'speed':
				data[preset_key][selector_key]['properties']['speed'] = param_value
			elif param_id == 'amplitude':
				data[preset_key][selector_key]['properties']['amplitude'] = param_value
			elif param_id == 'pitch_on_grid':
				data[preset_key][selector_key]['properties']['pitch_on_grid'] = param_value
			elif param_id == 'shape':
				data[preset_key][selector_key]['properties']['shape'] = param_value
			elif param_id == 'looping':
				data[preset_key][selector_key]['properties']['looping'] = param_value
			elif param_id == 'audio_group':
				data[preset_key][selector_key]['properties']['audio_group'] = int(param_value)
			elif param_id in ['midi_cc', 'midi_channel']:
				if 'midi' not in data[preset_key][selector_key]['properties']:
					data[preset_key][selector_key]['properties']['midi'] = {}
				if param_id == 'midi_cc':
					data[preset_key][selector_key]['properties']['midi']['cc'] = int(param_value)
				else:
					data[preset_key][selector_key]['properties']['midi']['channel'] = int(param_value)
			elif param_id in ['color_r', 'color_g', 'color_b']:
				# Handle RGB color components
				if 'color' not in data[preset_key][selector_key]['properties']:
					data[preset_key][selector_key]['properties']['color'] = {'r': 255, 'g': 255, 'b': 255}
				
				if param_id == 'color_r':
					data[preset_key][selector_key]['properties']['color']['r'] = param_value
				elif param_id == 'color_g':
					data[preset_key][selector_key]['properties']['color']['g'] = param_value
				elif param_id == 'color_b':
					data[preset_key][selector_key]['properties']['color']['b'] = param_value
			
			# Save back to file
			self._save_presets(data)
		except Exception as e:
			print(f"ERROR saving properties config: {e}")
	
	def _load_selector_properties(self):
		"""Load properties for the current selector and set the selected property."""
		try:
			if self.active_preset is None or self.active_selector is None:
				return
			data = self._load_presets()
			preset_data = data.get(str(self.active_preset), {})
			selector_data = preset_data.get(f'{self.active_selector}_selector', {})
			properties_config = selector_data.get('properties', {})
			
			# Set the selected property based on what's available
			if 'direction' in properties_config:
				self._selected_property = 'direction'
			else:
				self._selected_property = None
				
			print(f"DEBUG: Loaded properties for {self.active_selector} selector in preset {self.active_preset}")
		except Exception as e:
			print(f"ERROR loading selector properties: {e}")
	

	
	def _ensure_presets_file(self):
		if not os.path.isfile(self.presets_file):
			try:
				with open(self.presets_file, 'w', encoding='utf-8') as f:
					json.dump({}, f)
			except Exception:
				pass
	
	def _load_presets(self) -> Dict:
		try:
			with open(self.presets_file, 'r', encoding='utf-8') as f:
				return json.load(f)
		except Exception:
			return {}
	
	def _save_presets(self, data: Dict):
		try:
			with open(self.presets_file, 'w', encoding='utf-8') as f:
				json.dump(data, f, indent=2)
		except Exception as e:
			print(f"ERROR saving presets: {e}")
	
	# ----- Utilities -----
	def _hit_test(self, mx: int, my: int) -> Tuple[int, int]:
		if not self.opened:
			return -1, -1
		x, y = self.anchor
		menu_w = COLUMNS * COL_WIDTH + (COLUMNS + 1) * PADDING
		menu_h = 14 * ROW_HEIGHT + 2 * PADDING
		x = max(PADDING, min(x, self.game.width - menu_w - PADDING))
		y = max(PADDING, min(y, self.game.height - menu_h - PADDING))
		
		# Check bounds
		if not (x <= mx <= x + menu_w and y <= my <= y + menu_h):
			return -1, -1
		# Determine column
		for col in range(COLUMNS):
			cx = x + PADDING + col * (COL_WIDTH + PADDING)
			if cx <= mx <= cx + COL_WIDTH:
				# Determine row
				rel_y = my - (y + menu_h - PADDING - ROW_HEIGHT)
				row = int(((-rel_y) // ROW_HEIGHT))
				return col, row if row >= 0 else -1
		return -1, -1
	
	def _get_item(self, items: List[str], index: int) -> Optional[str]:
		if index is None or index < 0 or index >= len(items):
			return None
		return items[index]
	
	def _is_param_row(self, idx: Optional[int]) -> bool:
		if idx is None or idx < 0:
			return False
		actual = idx + self._col3_offset
		if 0 <= actual < len(self._col3_items):
			return self._col3_items[actual].get('kind') == 'param'
		return False
	
	def _is_item_selected(self, col_index: int, row_index: int, item_name: str) -> bool:
		"""Check if an item is part of the current selection path (optimized with caching)"""
		# Create cache key
		cache_key = f"{col_index}_{row_index}_{item_name}_{self._selected_folder}_{self._selected_subfolder}_{self._selected_preset}_{self._selected_property}"
		
		# Check cache first
		if cache_key in self._selection_cache:
			return self._selection_cache[cache_key]
		
		# Calculate selection state
		is_selected = False
		if col_index == 0:
			# Column 1: Check if this folder is selected
			is_selected = item_name == self._selected_folder
		elif col_index == 1:
			# Column 2: Check if this item is selected based on current folder
			if self._selected_folder == 'samples':
				is_selected = item_name == self._selected_subfolder
			elif self._selected_folder in ('frequencies', 'noise'):
				is_selected = item_name == self._selected_preset
			elif self._selected_folder == 'properties':
				is_selected = item_name == self._selected_property
			else:
				is_selected = False
		elif col_index == 2:
			# Column 3: Check if this parameter/file is selected
			actual_idx = row_index + self._col3_offset
			if 0 <= actual_idx < len(self._col3_items):
				item = self._col3_items[actual_idx]
				if item.get('kind') == 'param':
					# For parameters, we could highlight based on current selection
					# For now, just return False as parameter selection is more complex
					is_selected = False
				elif item.get('kind') == 'file':
					# For files, check if this is the selected file
					is_selected = False  # File selection logic would go here
			else:
				is_selected = False
		
		# Cache the result
		self._selection_cache[cache_key] = is_selected
		return is_selected
	
	def _get_current_color(self) -> Tuple[int, int, int]:
		"""Get the current RGB color from the color parameters"""
		try:
			# Get current color from properties config
			properties_config = self._load_properties_config()
			current_color = properties_config.get('color', {'r': 255, 'g': 255, 'b': 255})
			
			if isinstance(current_color, list) and len(current_color) >= 3:
				# Convert list format to tuple
				return (int(current_color[0]), int(current_color[1]), int(current_color[2]))
			elif isinstance(current_color, dict):
				# Convert dict format to tuple
				return (int(current_color.get('r', 255)), int(current_color.get('g', 255)), int(current_color.get('b', 255)))
			else:
				# Default to white
				return (255, 255, 255)
		except Exception:
			# Default to white on error
			return (255, 255, 255)
	
	def _get_selection_path_text(self) -> str:
		"""Generate a text representation of the current selection path"""
		path_parts = []
		
		# Add folder selection
		if self._selected_folder:
			path_parts.append(self._selected_folder)
		
		# Add subfolder/preset selection
		if self._selected_folder == 'samples' and self._selected_subfolder:
			path_parts.append(self._selected_subfolder)
		elif self._selected_folder in ('frequencies', 'noise'):
			# For frequencies/noise, show the selected preset
			if self._selected_preset:
				path_parts.append(self._selected_preset.replace('.py', ''))
			elif self.hover_col == 1 and 0 <= self.hover_index < len(self._col2_items):
				# Show currently hovered preset if no selection yet
				preset_name = self._col2_items[self.hover_index]
				path_parts.append(preset_name.replace('.py', ''))
		elif self._selected_folder == 'properties':
			# For properties, show the selected property
			if self._selected_property:
				path_parts.append(self._selected_property)
			elif self.hover_col == 1 and 0 <= self.hover_index < len(self._col2_items):
				# Show currently hovered property if no selection yet
				property_name = self._col2_items[self.hover_index]
				path_parts.append(property_name)
		
		# Add parameter/file selection
		# Guard actual index against offset and visible rows window
		if self._col3_items and self.hover_col == 2 and 0 <= self.hover_index < 14:
			actual_idx = self._col3_offset + self.hover_index
			if 0 <= actual_idx < len(self._col3_items):
				item = self._col3_items[actual_idx]
				if item.get('kind') == 'param':
					param_name = item['data'].get('label', item['data'].get('id', 'param'))
					path_parts.append(param_name)
				elif item.get('kind') == 'file':
					file_name = item.get('name', 'file')
					path_parts.append(file_name)
		
		if path_parts:
			return " → ".join(path_parts)
		return ""
