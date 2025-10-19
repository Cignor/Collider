# UI Architecture and Theme

This project uses a small UI theme layer to centralize fonts, colors, and sizing for a cohesive interface across panels and menus.

## Components

- FontManager: Registers TTF/OTF files and exposes a list of preferred font family names (fallbacks) for pyglet labels
- UITheme: Holds colors, sizes, and provides `ui_font_names` for consistent text rendering

## Font Registration

- Default font file: `typography/spaceMono-Bold.ttf`
- On startup, `UITheme.load_default_fonts()` registers the font with pyglet and sets fallbacks: `["Space Mono", "SpaceMono", "Space Mono Bold", "Arial"]`
- Labels should use `font_name=game.ui_theme.ui_font_names` to get proper fallback behavior

## Using Themed Labels

A small helper is provided in `Renderer._label()` to consistently create labels with the theme fonts and colors without repeating boilerplate.

## Cohesive UI Across Panels

To keep UI consistent as features grow:

1. Centralize theme variables
   - Colors, font sizes, spacing, paddings live in `UITheme`
2. Use composition
   - Create small UI modules per area (e.g., `ui/hud.py`, `ui/panels/inventory.py`)
3. Provide drawing contracts
   - Each panel exposes `draw(batch|direct)` and receives `game` and `theme`
4. Avoid logic in rendering
   - Panels should render given state; state lives in game/systems
5. Keep input separate
   - Route input via `InputHandler` and dispatch to active panel or tools

## Suggested Directory Layout

- `ui/theme.py`: Fonts, colors, sizes
- `ui/hud.py`: HUD components (FPS, controls, counters)
- `ui/panels/`: Future panels (settings, instruments, mixer)
- `ui/widgets/`: Reusable widgets (buttons, sliders, toggles)

## Future Extensions

- Multiple font weights (Regular, Bold, Mono) with names registered in `FontManager`
- Theme switching (dark/light/high-contrast)
- Layout system for consistent spacing and alignment
- Widget library (buttons, sliders) with keyboard/gamepad navigation
- Panel manager to control active views and transitions

This structure keeps the UI visually cohesive, easy to theme, and scalable as we add more panels and menus.
