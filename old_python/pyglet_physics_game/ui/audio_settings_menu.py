from __future__ import annotations

import pyglet
from pyglet import shapes, text
from typing import Optional, List

from pyglet_physics_game.ui.audio_settings_model import AudioSettingsModel
from pyglet_physics_game.ipc.client import get_ipc_client


COLUMNS = 3
COL1_WIDTH = 180
COL2_WIDTH_MIN = 220
ROW_HEIGHT = 22
PADDING = 12
VISIBLE_ROWS = 14


class AudioSettingsMenu:
	"""Three-panel audio settings menu modeled on ui_audio_selection.py.

	- Panel 1: Folders (Outputs, Inputs, Buffer Sizes, MIDI, Master Gain)
	- Panel 2: Contents (devices/buffer sizes or a single param row)
	- Panel 3: Parameters / Active (shows hovered/current item)
	- Scroll in Panel 2 to change selection and immediately commit via OSC
	- Click in Panel 2 to commit the clicked item
	- Scroll over Master Gain slider to adjust
	"""

	def __init__(self, model: AudioSettingsModel, renderer_batch=None, renderer_ui_group=None):
		self.visible = False
		self.model: Optional[AudioSettingsModel] = model
		# Position and sizing
		self.x = 60
		self.y = 80
		self.panel_w = 800
		self.panel_h = 420

		# Cached state
		self._selected_folder_idx: int = 0  # default to Outputs
		self._hover_col2_row: int = -1
		self._col2_offset: int = 0
		# Hover for column 1
		self._hover_col1_row: int = -1

		# Primitives
		self._bg: Optional[shapes.Rectangle] = None
		self._gain_track: Optional[shapes.Rectangle] = None
		self._gain_thumb: Optional[shapes.Rectangle] = None
		self._p1_bg: Optional[shapes.Rectangle] = None
		self._p2_bg: Optional[shapes.Rectangle] = None
		self._p3_bg: Optional[shapes.Rectangle] = None
		# Reusable row backgrounds
		self._row_bg: Optional[shapes.Rectangle] = None

		# Static data
		self._folders: List[str] = ["Outputs", "Inputs", "Buffer Sizes", "MIDI Devices", "Master Gain"]
		self._buffer_sizes: List[int] = [128, 256, 480, 512, 1024, 2048]

	def toggle(self) -> None:
		self.visible = not self.visible
		if self.visible:
			self._ensure_primitives()
		else:
			self._teardown_primitives()

	def _ensure_primitives(self) -> None:
		if self._bg is not None:
			return
		self._bg = shapes.Rectangle(self.x, self.y, self.panel_w, self.panel_h, color=(20, 28, 34))
		self._bg.opacity = 230
		# Master gain slider track/thumb
		gx = self.x + PADDING + COL1_WIDTH + PADDING
		gy = self.y + self.panel_h - 80
		gw = self.panel_w - (gx - self.x) - PADDING
		self._gain_track = shapes.Rectangle(gx, gy, gw, 6, color=(85, 85, 85))
		self._gain_thumb = shapes.Rectangle(gx, gy - 6, 10, 18, color=(0, 200, 255))
		self._update_gain_thumb()
		# Create panel backgrounds and reusable row bg
		col1_x, col1_w, col2_x, col2_w, col3_x, col3_w = self._layout_cols()
		top_y = self.y + self.panel_h - 140
		list_h = VISIBLE_ROWS * ROW_HEIGHT + 12
		self._p1_bg = shapes.Rectangle(col1_x - 4, top_y - list_h, col1_w + 8, list_h + 6, color=(18, 24, 32))
		self._p1_bg.opacity = 220
		self._p2_bg = shapes.Rectangle(col2_x - 4, top_y - list_h, col2_w + 8, list_h + 6, color=(18, 24, 32))
		self._p2_bg.opacity = 220
		self._p3_bg = shapes.Rectangle(col3_x - 4, top_y - ROW_HEIGHT - 8, col3_w + 8, ROW_HEIGHT + 18, color=(18, 24, 32))
		self._p3_bg.opacity = 220
		self._row_bg = shapes.Rectangle(0, 0, 0, ROW_HEIGHT, color=(40, 90, 70))
		self._row_bg.opacity = 170

	def _teardown_primitives(self) -> None:
		if self._bg:
			try: self._bg.delete()
			except Exception: pass
		self._bg = None
		for shp in (self._gain_track, self._gain_thumb):
			if shp:
				try: shp.delete()
				except Exception: pass
		self._gain_track = None
		self._gain_thumb = None

	def _update_gain_thumb(self) -> None:
		if not self._gain_track or not self._gain_thumb or not self.model:
			return
		g = max(0.0, min(1.0, float(getattr(self.model, 'master_gain', 0.0))))
		self._gain_thumb.x = self._gain_track.x + int(g * (self._gain_track.width - self._gain_thumb.width))

	def _layout_cols(self):
		col1_x = self.x + PADDING
		col1_w = COL1_WIDTH
		col2_x = col1_x + col1_w + PADDING
		col3_min_w = 200
		col3_x = self.x + self.panel_w - PADDING - col3_min_w
		if col3_x < col2_x + COL2_WIDTH_MIN:
			col3_x = col2_x + COL2_WIDTH_MIN
		col2_w = max(COL2_WIDTH_MIN, col3_x - col2_x - PADDING)
		col3_w = self.x + self.panel_w - PADDING - col3_x
		return col1_x, col1_w, col2_x, col2_w, col3_x, col3_w

	def update(self, dt: float) -> None:
		if not self.visible or not self.model:
			return
		self._update_gain_thumb()

	def draw(self) -> None:
		if not self.visible:
			return
		if self._bg: self._bg.draw()
		if self._gain_track: self._gain_track.draw()
		if self._gain_thumb: self._gain_thumb.draw()
		if self._p1_bg: self._p1_bg.draw()
		if self._p2_bg: self._p2_bg.draw()
		if self._p3_bg: self._p3_bg.draw()

		# Headings
		text.Label("Audio Settings (F11 to close)", x=self.x + PADDING, y=self.y + self.panel_h - 30, color=(255, 255, 255, 255)).draw()
		if hasattr(self.model, 'sample_rate'):
			text.Label(f"Sample rate: {float(self.model.sample_rate):.1f} Hz", x=self.x + PADDING, y=self.y + self.panel_h - 60, color=(200, 200, 200, 255)).draw()
		text.Label("Master gain (scroll over slider)", x=self.x + PADDING, y=self.y + self.panel_h - 100, color=(200, 200, 200, 255)).draw()

		col1_x, col1_w, col2_x, col2_w, col3_x, col3_w = self._layout_cols()
		top_y = self.y + self.panel_h - 140

		# Panel 1: Folders
		text.Label("Folders", x=col1_x, y=top_y, color=(180, 220, 255, 255)).draw()
		for i, name in enumerate(self._folders):
			row_y = top_y - 22 - i * ROW_HEIGHT
			is_selected = (i == self._selected_folder_idx)
			is_hover = (i == self._hover_col1_row)
			# Draw background highlight like selection menu
			if self._row_bg:
				self._row_bg.x = col1_x
				self._row_bg.y = row_y - 2
				self._row_bg.width = COL1_WIDTH - 4
				self._row_bg.color = (60, 170, 90) if is_selected else ((0, 160, 160) if is_hover else (35, 45, 55))
				self._row_bg.opacity = 220 if (is_selected or is_hover) else 140
				self._row_bg.draw()
			text.Label(name, x=col1_x + 8, y=row_y + 2, color=((20, 20, 20, 255) if is_selected else (220, 220, 220, 255))).draw()

		# Panel 2: Contents
		text.Label("Contents", x=col2_x, y=top_y, color=(180, 220, 255, 255)).draw()
		items = self._get_panel2_items()
		start = self._col2_offset
		end = min(len(items), start + VISIBLE_ROWS)
		for vis_i, idx in enumerate(range(start, end)):
			row_y = top_y - 22 - vis_i * ROW_HEIGHT
			is_hover = (vis_i == self._hover_col2_row)
			is_selected = self._is_panel2_row_selected(idx)
			if self._row_bg:
				self._row_bg.x = col2_x
				self._row_bg.y = row_y - 2
				self._row_bg.width = col2_w - 8
				self._row_bg.color = (60, 170, 90) if is_selected else ((0, 160, 160) if is_hover else (35, 45, 55))
				self._row_bg.opacity = 220 if (is_selected or is_hover) else 140
				self._row_bg.draw()
			text.Label(items[idx], x=col2_x + 8, y=row_y + 2, color=((20, 20, 20, 255) if is_selected else (220, 220, 220, 255))).draw()

		# Panel 3: Parameters / Files (active/hovered)
		text.Label("Parameters / Files", x=col3_x, y=top_y, color=(180, 220, 255, 255)).draw()
		active_line = self._panel3_text(start)
		text.Label(active_line, x=col3_x + 8, y=top_y - 22, color=(255, 255, 0, 255)).draw()

	def _get_panel2_items(self) -> List[str]:
		if not self.model:
			return []
		folder = self._folders[self._selected_folder_idx]
		if folder == "Outputs":
			return list(getattr(self.model, 'output_devices', []) or [])
		if folder == "Inputs":
			return list(getattr(self.model, 'input_devices', []) or [])
		if folder == "Buffer Sizes":
			return [f"{v} samples" for v in self._buffer_sizes]
		if folder == "MIDI Devices":
			return list(getattr(self.model, 'midi_devices', []) or [])
		if folder == "Master Gain":
			g = float(getattr(self.model, 'master_gain', 0.0))
			return [f"Gain: {g:.3f}"]
		return []

	def _is_panel2_row_selected(self, absolute_idx: int) -> bool:
		folder = self._folders[self._selected_folder_idx]
		if folder == "Outputs":
			current = getattr(self.model, 'current_output', '')
			items = getattr(self.model, 'output_devices', []) or []
			return absolute_idx < len(items) and items[absolute_idx] == current
		if folder == "Inputs":
			current = getattr(self.model, 'current_input', '')
			items = getattr(self.model, 'input_devices', []) or []
			return absolute_idx < len(items) and items[absolute_idx] == current
		if folder == "Buffer Sizes":
			try:
				bs = int(getattr(self.model, 'buffer_size', 0))
				return absolute_idx < len(self._buffer_sizes) and self._buffer_sizes[absolute_idx] == bs
			except Exception:
				return False
		return False

	def _panel3_text(self, start_index: int) -> str:
		folder = self._folders[self._selected_folder_idx]
		items = self._get_panel2_items()
		if not items:
			return "-"
		# Prefer hovered row; fall back to selected/current
		hover_abs = start_index + self._hover_col2_row if self._hover_col2_row >= 0 else -1
		if 0 <= hover_abs < len(items):
			return items[hover_abs]
		# Selected
		if folder == "Outputs":
			return getattr(self.model, 'current_output', '-') or '-'
		if folder == "Inputs":
			return getattr(self.model, 'current_input', '-') or '-'
		if folder == "Buffer Sizes":
			bs = int(getattr(self.model, 'buffer_size', 0) or 0)
			return f"{bs} samples" if bs > 0 else "-"
		if folder == "MIDI Devices":
			md = getattr(self.model, 'midi_devices', []) or []
			return md[0] if md else '-'
		if folder == "Master Gain":
			g = float(getattr(self.model, 'master_gain', 0.0))
			return f"Gain: {g:.3f}"
		return "-"

	# ---------- Input ----------
	def on_mouse_motion(self, x: int, y: int) -> None:
		if not self.visible:
			return
		col1_x, col1_w, col2_x, col2_w, _, _ = self._layout_cols()
		top_y = self.y + self.panel_h - 140
		# Panel1 hover selects folder
		col1_y_top = top_y - 22
		if col1_x <= x <= col1_x + col1_w and self.y <= y <= col1_y_top + ROW_HEIGHT:
			idx = int((col1_y_top - y) // ROW_HEIGHT)
			if 0 <= idx < len(self._folders):
				if idx != self._selected_folder_idx:
					self._selected_folder_idx = idx
					self._col2_offset = 0
					self._hover_col2_row = -1
			self._hover_col1_row = idx
		# Panel2 hover row
		col2_y_top = top_y - 22
		if col2_x <= x <= col2_x + col2_w and col2_y_top - (VISIBLE_ROWS - 1) * ROW_HEIGHT - ROW_HEIGHT <= y <= col2_y_top + ROW_HEIGHT:
			row = int((col2_y_top - y) // ROW_HEIGHT)
			self._hover_col2_row = row if 0 <= row < VISIBLE_ROWS else -1
		else:
			self._hover_col2_row = -1
			self._hover_col1_row = -1

	def on_mouse_scroll(self, x: int, y: int, scroll_y: float) -> bool:
		if not self.visible:
			return False
		# Gain slider scroll
		if self._gain_track and (self._gain_track.x <= x <= self._gain_track.x + self._gain_track.width) and (self._gain_track.y - 10 <= y <= self._gain_track.y + 22):
			self._adjust_gain_scroll(scroll_y)
			return True
		# Panel 2 scroll cycles selection and commits
		col1_x, col1_w, col2_x, col2_w, _, _ = self._layout_cols()
		top_y = self.y + self.panel_h - 140
		col2_y_top = top_y - 22
		col2_y_bottom = col2_y_top - (VISIBLE_ROWS - 1) * ROW_HEIGHT
		in_col2 = (col2_x <= x <= col2_x + col2_w and col2_y_bottom - ROW_HEIGHT <= y <= col2_y_top + ROW_HEIGHT)
		if not in_col2:
			return False
		items = self._get_panel2_items()
		if not items:
			return False
		# Current absolute index
		if self._hover_col2_row >= 0:
			abs_idx = self._col2_offset + self._hover_col2_row
		else:
			abs_idx = self._current_selected_abs_index(items)
		if abs_idx < 0:
			abs_idx = 0
		delta = 1 if scroll_y > 0 else -1
		abs_idx = max(0, min(len(items) - 1, abs_idx + delta))
		# Keep visible
		if abs_idx < self._col2_offset:
			self._col2_offset = abs_idx
		elif abs_idx >= self._col2_offset + VISIBLE_ROWS:
			self._col2_offset = max(0, abs_idx - VISIBLE_ROWS + 1)
		self._hover_col2_row = min(VISIBLE_ROWS - 1, max(0, abs_idx - self._col2_offset))
		self._commit_by_folder(abs_idx, items)
		return True

	def _current_selected_abs_index(self, items: List[str]) -> int:
		folder = self._folders[self._selected_folder_idx]
		if folder == "Outputs":
			cur = getattr(self.model, 'current_output', '')
			return items.index(cur) if cur in items else -1
		if folder == "Inputs":
			cur = getattr(self.model, 'current_input', '')
			return items.index(cur) if cur in items else -1
		if folder == "Buffer Sizes":
			try:
				bs = int(getattr(self.model, 'buffer_size', 0) or 0)
				return self._buffer_sizes.index(bs) if bs in self._buffer_sizes else -1
			except Exception:
				return -1
		return -1

	def on_mouse_press(self, x: int, y: int, button: int, modifiers: int) -> bool:
		if not self.visible:
			return False
		# Click in panel 2 commits
		col1_x, col1_w, col2_x, col2_w, _, _ = self._layout_cols()
		top_y = self.y + self.panel_h - 140
		col2_y_top = top_y - 22
		if not (col2_x <= x <= col2_x + col2_w):
			return False
		row = int((col2_y_top - y) // ROW_HEIGHT)
		if 0 <= row < VISIBLE_ROWS:
			items = self._get_panel2_items()
			abs_idx = self._col2_offset + row
			if 0 <= abs_idx < len(items):
				self._hover_col2_row = row
				self._commit_by_folder(abs_idx, items)
				return True
		return False

	def _adjust_gain_scroll(self, scroll_y: float) -> None:
		if not self.model:
			return
		g = max(0.0001, min(1.0, float(getattr(self.model, 'master_gain', 0.0))))
		factor = 1.12 if scroll_y > 0 else (1.0 / 1.12)
		steps = int(abs(scroll_y)) if abs(scroll_y) >= 1 else 1
		for _ in range(steps):
			g *= factor
			g = max(0.0001, min(1.0, g))
		setattr(self.model, 'master_gain', g)
		self._update_gain_thumb()
		try:
			get_ipc_client()._send("/settings/setMasterGain", float(g))
		except Exception:
			pass

	def _commit_by_folder(self, abs_idx: int, items: List[str]) -> None:
		folder = self._folders[self._selected_folder_idx]
		try:
			if folder == "Outputs":
				get_ipc_client()._send("/settings/setDevice", "output", items[abs_idx])
			elif folder == "Inputs":
				get_ipc_client()._send("/settings/setDevice", "input", items[abs_idx])
			elif folder == "Buffer Sizes":
				val = int(self._buffer_sizes[abs_idx])
				get_ipc_client()._send("/settings/setBufferSize", val)
			# MIDI Devices has no commit action here
		except Exception:
			pass

	def on_mouse_release(self, x: int, y: int, button: int, modifiers: int) -> bool:
		return False

	def on_key_press(self, symbol: int, modifiers: int) -> bool:
		return False

	def on_key_release(self, symbol: int, modifiers: int) -> bool:
		return False


