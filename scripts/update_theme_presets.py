import json
from pathlib import Path


def ensure_rgba(value, default=(0.0, 0.0, 0.0, 1.0)):
    if not isinstance(value, (list, tuple)) or len(value) < 4:
        return list(default)
    return [float(value[i]) for i in range(4)]


def clamp(x: float) -> float:
    return max(0.0, min(1.0, x))


def mix(a, b, t: float):
    return [
        clamp(a[i] * (1.0 - t) + b[i] * t)
        for i in range(3)
    ] + [clamp(a[3] * (1.0 - t) + b[3] * t)]


def lighten(color, amount: float):
    return [clamp(c + (1.0 - c) * amount) for c in color[:3]] + [color[3]]


def darken(color, amount: float):
    return [clamp(c * (1.0 - amount)) for c in color[:3]] + [color[3]]


def set_alpha(color, alpha: float):
    return color[:3] + [clamp(alpha)]


def soften(base, toward, amount: float):
    return mix(base, toward, amount)


def tidy_list(values):
    return [round(float(v), 4) for v in values]


def process_preset(path: Path):
    data = json.loads(path.read_text())

    style = data.get("style", {})
    colors = style.get("Colors", {})

    window_bg = ensure_rgba(colors.get("WindowBg", [0.1, 0.1, 0.1, 1.0]))
    child_bg = ensure_rgba(colors.get("ChildBg", window_bg))
    border_col = ensure_rgba(colors.get("Border", [0.4, 0.4, 0.4, 1.0]))

    accent = ensure_rgba(data.get("accent", [0.3, 0.6, 1.0, 1.0]))

    text = data.get("text", {})
    section = ensure_rgba(text.get("section_header", accent))
    success = ensure_rgba(text.get("success", [0.2, 0.8, 0.35, 1.0]))
    warning = ensure_rgba(text.get("warning", [0.98, 0.76, 0.22, 1.0]))
    error = ensure_rgba(text.get("error", [0.95, 0.3, 0.2, 1.0]))
    disabled = ensure_rgba(text.get("disabled", [0.5, 0.5, 0.5, 1.0]))
    active = ensure_rgba(text.get("active", accent))

    modulation = data.get("modulation", {})
    freq_mod = ensure_rgba(modulation.get("frequency", accent))
    timbre_mod = ensure_rgba(modulation.get("timbre", warning))
    amp_mod = ensure_rgba(modulation.get("amplitude", success))
    filter_mod = ensure_rgba(modulation.get("filter", active))

    background_soft = soften(window_bg, child_bg, 0.35)
    background_deep = darken(window_bg, 0.15)
    grid_base = soften(border_col, accent, 0.25)
    label_color = soften(disabled, section, 0.3)
    peak_color = soften(warning, accent, 0.35)
    live_color = soften(accent, success, 0.2)
    border_color = soften(border_col, window_bg, 0.4)
    threshold_color = soften(error, accent, 0.2)

    modules = data.setdefault("modules", {})

    freq_graph = modules.setdefault("frequency_graph", {})
    freq_graph["background"] = tidy_list(background_deep)
    freq_graph["grid"] = tidy_list(set_alpha(grid_base, 0.95))
    freq_graph["label"] = tidy_list(set_alpha(label_color, 0.95))
    freq_graph["peak_line"] = tidy_list(set_alpha(peak_color, 0.75))
    freq_graph["live_line"] = tidy_list(set_alpha(live_color, 0.9))
    freq_graph["border"] = tidy_list(set_alpha(border_color, 1.0))
    freq_graph["threshold"] = tidy_list(set_alpha(threshold_color, 0.7))

    physics = modules.setdefault("physics", {})

    sky_mix = soften(accent, success, 0.35)
    forest_mix = soften(success, filter_mod, 0.4)
    sand_mix = soften(warning, disabled, 0.3)
    ember_mix = soften(error, timbre_mod, 0.2)
    ice_mix = soften(freq_mod, accent, 0.25)
    dusk_mix = soften(timbre_mod, window_bg, 0.2)
    night_mix = soften(filter_mod, window_bg, 0.45)

    physics["sandbox_title"] = tidy_list(set_alpha(section, 1.0))
    physics["stroke_label"] = tidy_list(set_alpha(soften(disabled, section, 0.5), 1.0))
    physics["physics_section"] = tidy_list(set_alpha(forest_mix, 1.0))
    physics["spawn_section"] = tidy_list(set_alpha(ice_mix, 1.0))
    physics["count_ok"] = tidy_list(set_alpha(success, 1.0))
    physics["count_warn"] = tidy_list(set_alpha(warning, 1.0))
    physics["count_alert"] = tidy_list(set_alpha(error, 1.0))

    physics["stroke_metal"] = tidy_list(set_alpha(soften(accent, freq_mod, 0.4), 1.0))
    physics["stroke_wood"] = tidy_list(set_alpha(soften(warning, sand_mix, 0.3), 1.0))
    physics["stroke_soil"] = tidy_list(set_alpha(soften(forest_mix, sand_mix, 0.25), 1.0))
    physics["stroke_conveyor"] = tidy_list(set_alpha(soften(filter_mod, accent, 0.35), 1.0))
    physics["stroke_bouncy"] = tidy_list(set_alpha(soften(success, accent, 0.25), 1.0))
    physics["stroke_sticky"] = tidy_list(set_alpha(soften(sand_mix, disabled, 0.25), 1.0))
    physics["stroke_emitter"] = tidy_list(set_alpha(soften(ember_mix, accent, 0.3), 1.0))

    physics["spawn_ball"] = tidy_list(set_alpha(ember_mix, 1.0))
    physics["spawn_square"] = tidy_list(set_alpha(success, 1.0))
    physics["spawn_triangle"] = tidy_list(set_alpha(freq_mod, 1.0))
    physics["spawn_vortex"] = tidy_list(set_alpha(soften(accent, filter_mod, 0.4), 1.0))
    physics["spawn_clear"] = tidy_list(set_alpha(soften(ember_mix, disabled, 0.4), 0.8))
    physics["spawn_clear_hover"] = tidy_list(set_alpha(soften(ember_mix, accent, 0.25), 1.0))
    physics["spawn_clear_active"] = tidy_list(set_alpha(soften(ember_mix, filter_mod, 0.35), 1.0))

    physics["canvas_background"] = tidy_list(set_alpha(background_soft, 1.0))
    physics["canvas_border"] = tidy_list(set_alpha(border_color, 0.9))
    physics["drag_indicator_fill"] = tidy_list(set_alpha(soften(accent, warning, 0.35), 0.4))
    physics["drag_indicator_outline"] = tidy_list(set_alpha(soften(accent, warning, 0.15), 0.85))
    physics["eraser_fill"] = tidy_list(set_alpha(soften(error, window_bg, 0.25), 0.25))
    physics["eraser_outline"] = tidy_list(set_alpha(soften(error, border_col, 0.15), 0.7))
    physics["crosshair_idle"] = tidy_list(set_alpha(soften(disabled, accent, 0.3), 0.5))
    physics["crosshair_active"] = tidy_list(set_alpha(soften(soften(accent, warning, 0.15), success, 0.25), 1.0))

    physics["magnet_north"] = tidy_list(set_alpha(soften(ember_mix, accent, 0.2), 0.8))
    physics["magnet_south"] = tidy_list(set_alpha(soften(ice_mix, accent, 0.35), 0.8))
    physics["magnet_link"] = tidy_list(set_alpha(soften(accent, warning, 0.3), 0.8))
    physics["vector_outline"] = tidy_list(set_alpha(soften(disabled, accent, 0.25), 0.8))
    physics["vector_fill"] = tidy_list(set_alpha(soften(disabled, accent, 0.15), 0.6))
    physics["soil_detail"] = tidy_list(set_alpha(soften(sand_mix, forest_mix, 0.4), 0.7))
    physics["overlay_text"] = tidy_list(set_alpha(soften(section, window_bg, 0.6), 0.8))
    physics["overlay_line"] = tidy_list(set_alpha(soften(border_col, accent, 0.2), 0.5))
    physics["separator_line"] = tidy_list(set_alpha(soften(warning, accent, 0.25), 0.78))

    path.write_text(json.dumps(data, indent=4))


def main():
    preset_dir = (
        Path(__file__).resolve().parents[1]
        / "juce" / "Source" / "preset_creator" / "theme" / "presets"
    )
    if not preset_dir.exists():
        raise SystemExit("Preset directory not found: " + str(preset_dir))

    for preset_file in sorted(preset_dir.glob("*.json")):
        process_preset(preset_file)


if __name__ == "__main__":
    main()

