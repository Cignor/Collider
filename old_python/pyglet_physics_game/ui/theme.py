import os
import traceback
from typing import List, Dict, Optional
import pyglet

class FontManager:
	"""Registers and provides access to custom fonts"""

	def __init__(self):
		self._registered_files: Dict[str, str] = {}
		self._ui_font_names: List[str] = []

	def register_font_file(self, alias: str, file_path: str) -> bool:
		"""Register a TTF/OTF font file with pyglet and store by alias"""
		try:
			if not os.path.isfile(file_path):
				return False
			pyglet.font.add_file(file_path)
			self._registered_files[alias] = file_path
			return True
		except Exception:
			traceback.print_exc()
			return False

	def set_ui_font_fallbacks(self, possible_names: List[str]):
		"""Set preferred order of font family names to try for UI labels"""
		self._ui_font_names = possible_names[:]

	@property
	def ui_font_names(self) -> List[str]:
		"""Return list of fallback font names for UI labels"""
		return self._ui_font_names or ["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"]

class UITheme:
	"""Provides cohesive UI styling and access to fonts and colors"""

	def __init__(self):
		self.fonts = FontManager()
		# Colors (RGB)
		self.color_text_primary = (255, 255, 255)
		self.color_text_secondary = (200, 200, 200)
		self.color_accent = (255, 150, 255)
		self.color_ok = (150, 255, 150)
		self.color_warn = (255, 255, 150)
		self.color_info = (150, 200, 255)

		# Sizes
		self.size_fps = 16
		self.size_small = 10
		self.size_medium = 12
		self.size_large = 14

	def load_default_fonts(self):
		"""Register bundled fonts and configure fallbacks"""
		try:
			# Resolve path to project root then typography folder
			this_dir = os.path.dirname(os.path.abspath(__file__))
			project_root = os.path.abspath(os.path.join(this_dir, os.pardir, os.pardir))
			font_path = os.path.join(project_root, "typography", "spaceMono-Bold.ttf")

			# Register main UI font file if present
			self.fonts.register_font_file("space_mono_bold", font_path)

			# Set preferred font family names (pyglet matches by internal family name)
			self.fonts.set_ui_font_fallbacks(["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"])
		except Exception:
			traceback.print_exc()

	@property
	def ui_font_names(self) -> List[str]:
		return self.fonts.ui_font_names
