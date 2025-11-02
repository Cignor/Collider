"""
Centralized UI style helpers to keep the HUD and future panels consistent.

Rules this module enforces:
- Shared margins, paddings, gaps, and row heights
- Single source of truth for colors and font fallbacks
- Accurate text measurement with pyglet font metrics
- Standard box drawing with subtle outline and inner highlight
"""

from typing import List, Tuple
from pyglet import text, shapes
from .color_manager import get_color_manager


class UIStyle:
    def __init__(self, theme):
        self.theme = theme
        self.color_mgr = get_color_manager()

        # Spacing
        self.margin = 16
        self.padding = 12
        self.box_padding = 8
        self.gap = 8
        self.row_height = 24

        # Colors will be retrieved dynamically from color manager

        # Font sizes
        self.title_size = 16
        self.info_size = 13
        self.preset_size = 11

        # Opacity
        self.bg_opacity = self.color_mgr.background_ui_opacity
        
        # Background color - Light gray to complement menu grays
        self.background_color = self.color_mgr.background_main_game

        # Grid system (12-column, 1920 baseline)
        self.grid_columns = 12
        self.grid_gutter = 16
        self.grid_margin = 16
        self.baseline = 8  # vertical rhythm

    # --- Grid helpers ---
    def column_width(self, screen_width: int) -> int:
        total_gutter = (self.grid_columns - 1) * self.grid_gutter
        total_margin = 2 * self.grid_margin
        return max(1, int((screen_width - total_gutter - total_margin) / self.grid_columns))

    def col_x(self, screen_width: int, col_index: int) -> int:
        """Left x of a column (0-based)"""
        cw = self.column_width(screen_width)
        return self.grid_margin + col_index * (cw + self.grid_gutter)

    def span_width(self, screen_width: int, span: int) -> int:
        cw = self.column_width(screen_width)
        return cw * span + self.grid_gutter * (span - 1)

    def snap_y(self, y: int) -> int:
        """Snap a y coordinate to the vertical baseline grid"""
        b = self.baseline
        return (y // b) * b

    # --- Grid rendering (optional overlay) ---
    def draw_grid(self, screen_width: int, screen_height: int, batch=None):
        from pyglet import shapes
        # Columns
        cw = self.column_width(screen_width)
        from .color_manager import get_color_manager
        color_mgr = get_color_manager()
        x = self.grid_margin
        for i in range(self.grid_columns):
            col = shapes.Rectangle(x, 0, cw, screen_height, color=color_mgr.grid_secondary, batch=batch)
            col.opacity = 20
            col.draw()
            x += cw + self.grid_gutter
        # Baselines
        y = 0
        while y < screen_height:
            ln = shapes.Line(0, y, screen_width, y, width=1, color=color_mgr.grid_primary, batch=batch)
            ln.opacity = 20
            ln.draw()
            y += self.baseline

    @property
    def font_names(self) -> List[str]:
        if self.theme and hasattr(self.theme, 'ui_font_names'):
            return self.theme.ui_font_names
        return ["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"]

    def measure_text_width(self, value: str, font_size: int) -> int:
        """Measure text width with caching for performance"""
        # PERFORMANCE: Cache text measurements to avoid creating Label objects every frame
        cache_key = (value, font_size)
        if not hasattr(self, '_text_width_cache'):
            self._text_width_cache = {}
        
        if cache_key in self._text_width_cache:
            return self._text_width_cache[cache_key]
        
        try:
            lbl = text.Label(value, font_name=self.font_names, font_size=font_size, x=0, y=0)
            width = int(lbl.content_width) + 10
            self._text_width_cache[cache_key] = width
            return width
        except Exception:
            width = int(len(value) * (font_size * 0.6) + 10)
            self._text_width_cache[cache_key] = width
            return width

    def draw_box(self, x: int, y: int, width: int, height: int, batch=None,
                 bg_color: Tuple[int, int, int] = None,
                 outline_color: Tuple[int, int, int] = None,
                 opacity: int = None):
        """Draw a standard UI box with optional color overrides"""
        if bg_color is None:
            bg_color = self.color_mgr.background_ui_panel  # Get dynamically from color manager
        if outline_color is None:
            outline_color = self.color_mgr.outline_default  # Get dynamically from color manager
        if opacity is None:
            opacity = 200  # Fixed opacity for UI panels

        # Background
        bg = shapes.Rectangle(x, y, width, height, color=bg_color, batch=batch)
        bg.opacity = opacity
        bg.draw()

        # Outline
        outline = shapes.Rectangle(x, y, width, height, color=outline_color, batch=batch)
        outline.opacity = 80
        outline.draw()

        # Inner highlight remains neutral to preserve gray background feel
        from .color_manager import get_color_manager
        color_mgr = get_color_manager()
        hi = shapes.Rectangle(x + 1, y + 1, width - 2, height - 2, color=color_mgr.text_primary, batch=batch)
        hi.opacity = 18
        hi.draw()

    def category_color(self, category: str) -> Tuple[int, int, int]:
        return self.color_mgr.category_color(category)


