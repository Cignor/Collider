import pyglet
from .ui_audio_selection import AudioSelectionMenu
from .audio_settings_menu import AudioSettingsMenu


class UIManager:
	"""
	Definitive, unified UI manager for all modal components.
	Owns all menus and delegates all events.
	"""
	def __init__(self, game, model, renderer_batch, renderer_ui_group):
		self.game = game

		# Create and own BOTH menus
		self.audio_selection_menu = AudioSelectionMenu(game)
		self.audio_settings_menu = AudioSettingsMenu(model, renderer_batch, renderer_ui_group)

		# For backward compatibility with bullet firing logic
		self.menu = self.audio_selection_menu

	def toggle_audio_settings(self):
		# Ensure menus are mutually exclusive
		if getattr(self.audio_selection_menu, 'opened', False):
			self.audio_selection_menu.close_and_commit()
		self.audio_settings_menu.toggle()

	def update(self, dt):
		if getattr(self.audio_selection_menu, 'opened', False):
			self.audio_selection_menu.update(dt)
		if self.audio_settings_menu.visible:
			self.audio_settings_menu.update(dt)

	def draw(self):
		# This draw method is now the single source of truth for UI overlays
		if getattr(self.audio_selection_menu, 'opened', False):
			self.audio_selection_menu.draw()
		if self.audio_settings_menu.visible:
			self.audio_settings_menu.draw()

	# --- Event Delegation ---
	# Return True if an event was consumed by an active menu

	def on_mouse_press(self, x, y, button, modifiers):
		# Shift + Left/Right click opens AudioSelectionMenu (mutually exclusive with settings)
		try:
			if modifiers & pyglet.window.key.MOD_SHIFT:
				if button == pyglet.window.mouse.LEFT:
					if self.audio_settings_menu.visible:
						self.audio_settings_menu.toggle()
					self.audio_selection_menu.open('left', x, y)
					return True
				elif button == pyglet.window.mouse.RIGHT:
					if self.audio_settings_menu.visible:
						self.audio_settings_menu.toggle()
					self.audio_selection_menu.open('right', x, y)
					return True
		except Exception:
			pass
		if self.audio_settings_menu.visible:
			return self.audio_settings_menu.on_mouse_press(x, y, button, modifiers)
		# Add logic for audio_selection_menu if it has on_mouse_press
		return False

	def on_mouse_release(self, x, y, button, modifiers):
		# When selection menu is open, no special release handling here; fall through
		if self.audio_settings_menu.visible:
			return self.audio_settings_menu.on_mouse_release(x, y, button, modifiers)
		return False

	def on_mouse_scroll(self, x, y, scroll_y):
		if self.audio_settings_menu.visible:
			return self.audio_settings_menu.on_mouse_scroll(x, y, scroll_y)
		if getattr(self.audio_selection_menu, 'opened', False):
			self.audio_selection_menu.handle_scroll(x, y, scroll_y)
			return True
		return False

	def on_mouse_motion(self, x, y):
		if getattr(self.audio_selection_menu, 'opened', False):
			self.audio_selection_menu.handle_mouse_motion(x, y)
		if self.audio_settings_menu.visible and hasattr(self.audio_settings_menu, 'on_mouse_motion'):
			self.audio_settings_menu.on_mouse_motion(x, y)

	def on_key_release(self, symbol, modifiers):
		# Ensure Shift release closes the AudioSelectionMenu
		try:
			if symbol == pyglet.window.key.LSHIFT or symbol == pyglet.window.key.RSHIFT:
				if getattr(self.audio_selection_menu, 'opened', False):
					self.audio_selection_menu.close_and_commit()
					return True
		except Exception:
			pass
		# Forward to settings menu if it cares about key releases
		try:
			if self.audio_settings_menu.visible and hasattr(self.audio_settings_menu, 'on_key_release'):
				return bool(self.audio_settings_menu.on_key_release(symbol, modifiers))
		except Exception:
			pass
		return False

	# --- Pass-through methods for legacy AudioSelectionMenu ---

	def open_menu(self, selector, x, y):
		if self.audio_settings_menu.visible:
			self.audio_settings_menu.toggle()
		self.audio_selection_menu.open(selector, x, y)

	def close_menu_commit(self):
		self.audio_selection_menu.close_and_commit()

	def recall_preset(self, idx):
		self.audio_selection_menu.set_active_preset(idx)

	def store_preset(self, idx):
		self.audio_selection_menu.store_current_to_preset(idx)
