from typing import Tuple, List
import math

class BulletDeleteTool:
	"""Temporary tool to delete sound bullets within a radius.
	Activated while the user holds Delete; deletion happens continuously on mouse move.
	The tool exposes a `thickness` property so the renderer will draw a circle
	using the existing brush indicator logic (radius = thickness/2).
	"""

	def __init__(self, game, radius: float = 40.0):
		self.game = game
		self.name = "BulletDelete"
		self.color = (255, 80, 80)
		self.radius = radius
		self.thickness = int(radius * 2)
		self.active = False
		self._last_pos: Tuple[float, float] | None = None

	# Standard tool interface used by InputHandler
	def activate(self):
		self.active = True

	def deactivate(self):
		self.active = False
		self._last_pos = None

	# For the new design we perform deletion on motion when Delete is held.
	# start_draw/update_draw/finish_draw remain for compatibility with tool API but are not required.
	def start_draw(self, pos: Tuple[float, float]):
		self.active = True
		self._last_pos = pos
		self._delete_at(pos)

	def update_draw(self, pos: Tuple[float, float]):
		self._last_pos = pos
		self._delete_at(pos)

	def finish_draw(self, space=None):
		self.active = False
		self._last_pos = None
		return None

	# Internal helpers
	def _delete_at(self, pos: Tuple[float, float]):
		try:
			if not hasattr(self.game, 'sound_bullets') or not self.game.sound_bullets:
				return
			x, y = pos
			r = self.radius
			to_remove: List = []
			for b in list(self.game.sound_bullets):
				bx, by = getattr(b, 'x', None), getattr(b, 'y', None)
				if bx is None or by is None:
					continue
				d = math.hypot(x - bx, y - by)
				if d <= r:
					to_remove.append(b)
			# Debug visibility
			if to_remove:
				print(f"[DELETE] removing {len(to_remove)} bullets at ({x:.1f},{y:.1f})")
			for b in to_remove:
				try:
					b.remove()
				except Exception:
					pass
		except Exception as e:
			print(f"ERROR in BulletDeleteTool._delete_at: {e}")
