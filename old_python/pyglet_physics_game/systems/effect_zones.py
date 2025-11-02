import math
from typing import List, Dict, Optional
from pyglet import shapes
import pyglet


class EffectZone:
    """Axis-aligned rectangular JUCE-native FX zone driven by effects.json specs.

    spec list example (normalized forms expected):
    [
      {"type": "reverb", "room_size": 0.8, "damping": 0.5, "mix": 0.7},
      {"type": "delay", "delay_ms": 380.0, "feedback": 0.4, "mix": 0.6},
      {"type": "highpass", "cutoff_hz": 800.0}
    ]
    """

    def __init__(self, game, x: float, y: float, w: float, h: float, spec: List[Dict], color=(255, 128, 0)):
        self.game = game
        self.x = float(x)
        self.y = float(y)
        self.w = float(w)
        self.h = float(h)
        self.spec = list(spec or [])
        # Visuals (attach to renderer batch so they are drawn by the main renderer)
        batch = getattr(game.renderer, 'batch', None)
        group = getattr(game.renderer, 'grid_group', None)
        self.rect = shapes.BorderedRectangle(
            int(self.x), int(self.y), int(self.w), int(self.h),
            border=4, color=(color[0] // 3, color[1] // 3, color[2] // 3), border_color=color,
            batch=batch, group=group
        )
        try:
            self.rect.opacity = 70
        except Exception:
            pass
        # Label showing effect names
        try:
            effects_names = ", ".join([str(s.get("type","")) for s in (self.spec or [])]) or "(none)"
            title = f"{effects_names}  (native-only)"
            self.label = getattr(pyglet, 'text', None).Label(
                title,
                font_name='Arial', font_size=10, x=int(self.x)+6, y=int(self.y)+int(self.h)-14,
                color=(255,255,255,200), anchor_x='left', anchor_y='top', batch=batch
            ) if hasattr(pyglet, 'text') else None
            # Parameter labels per effect (debug visibility)
            self.param_labels = []
            y_cursor = int(self.y) + int(self.h) - 28
            if hasattr(pyglet, 'text'):
                for s in (self.spec or []):
                    try:
                        typ = str(s.get('type', ''))
                        params = ", ".join([f"{k}={s[k]}" for k in sorted(s.keys()) if k != 'type'])
                        txt = f"{typ}: {params}" if params else f"{typ}"
                        lbl = pyglet.text.Label(
                            txt,
                            font_name='Arial', font_size=9, x=int(self.x)+6, y=y_cursor,
                            color=(200,200,200,200), anchor_x='left', anchor_y='top', batch=batch
                        )
                        self.param_labels.append(lbl)
                        y_cursor -= 12
                    except Exception:
                        pass
        except Exception:
            self.label = None

    def contains(self, px: float, py: float) -> bool:
        return (self.x <= px <= self.x + self.w) and (self.y <= py <= self.y + self.h)

    def draw(self):
        # Shapes are batched; no per-frame draw needed.
        try:
            if hasattr(self, 'label') and self.label is not None:
                # Keep label within rect if window resizes
                self.label.x = int(self.x) + 6
                self.label.y = int(self.y) + int(self.h) - 4
            if hasattr(self, 'param_labels') and isinstance(self.param_labels, list):
                y_cursor = int(self.y) + int(self.h) - 18
                for lbl in self.param_labels:
                    try:
                        y_cursor -= 12
                        lbl.x = int(self.x) + 6
                        lbl.y = y_cursor
                    except Exception:
                        pass
        except Exception:
            pass
        return


class EffectZoneManager:
    """Manages multiple overlapping effect zones and applies combined Pedalboard chains to bullets."""

    def __init__(self, game):
        self.game = game
        self.zones: List[EffectZone] = []
        # Per-bullet cache of current applied effect signature to avoid redundant updates
        self._bullet_fx_key: Dict[int, str] = {}
        # Load effects schema for basic normalization/clamping
        self._effects_schema = {}
        self._effects_schema_path = None
        self._effects_schema_mtime = 0.0
        try:
            import os, json
            cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config', 'effects.json')
            self._effects_schema_path = cfg_path
            if os.path.isfile(cfg_path):
                self._effects_schema_mtime = os.path.getmtime(cfg_path)
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    if isinstance(data, dict):
                        self._effects_schema = data.get('effects', {}) or {}
        except Exception:
            self._effects_schema = {}

    def create_demo_zones(self):
        """Create one zone per effect defined in config/effects.json, with audible defaults."""
        effects = list((self._effects_schema or {}).keys())
        # Only include effects currently supported by the native engine
        supported = {'reverb', 'delay', 'lowpass', 'highpass', 'svf', 'tremolo'}
        effects = [e for e in effects if e in supported]
        if not effects:
            # Fallback to old demos if schema missing
            W, H = int(self.game.width), int(self.game.height)
            pad = 80
            z1 = EffectZone(
                self.game, pad, pad, max(200, W * 0.25), max(200, H * 0.35),
                spec=[{"type": "reverb", "room_size": 0.85, "damping": 0.40, "mix": 0.65}],
                color=(120, 220, 255)
            )
            z2 = EffectZone(
                self.game, W * 0.30, H * 0.20, W * 0.40, H * 0.45,
                spec=[{"type": "delay", "delay_ms": 420.0, "feedback": 0.45, "mix": 0.55}],
                color=(255, 180, 60)
            )
            z3 = EffectZone(
                self.game, pad, pad, W - 2 * pad, max(120, H * 0.18),
                spec=[{"type": "highpass", "cutoff_hz": 900.0}, {"type": "lowpass", "cutoff_hz": 2800.0}],
                color=(180, 255, 120)
            )
            self.zones = [z1, z2, z3]
            return

        # Tile zones across canvas
        W, H = int(self.game.width), int(self.game.height)
        pad, gap = 40, 20
        n = len(effects)
        cols = max(1, int(math.ceil(math.sqrt(n))))
        rows = max(1, int(math.ceil(n / cols)))
        tile_w = max(140.0, (W - 2 * pad - (cols - 1) * gap) / float(cols))
        tile_h = max(100.0, (H - 2 * pad - (rows - 1) * gap) / float(rows))

        zones = []
        for idx, eff in enumerate(effects):
            r = idx // cols
            c = idx % cols
            x = pad + c * (tile_w + gap)
            y = pad + (rows - 1 - r) * (tile_h + gap)
            params_schema = (self._effects_schema.get(eff, {}) or {}).get('params', {})
            spec = self._spec_for_effect(eff, params_schema)
            color = self._zone_color_for_effect(eff)
            # Ensure list-of-dicts for EffectZone
            spec_list = spec if isinstance(spec, list) else [spec]
            zones.append(EffectZone(self.game, x, y, tile_w, tile_h, spec=spec_list, color=color))
        self.zones = zones

    def draw(self):
        for z in self.zones:
            z.draw()

    def _effect_key(self, specs: List[List[Dict]]) -> str:
        # Create a stable key for combined specs
        parts = []
        for arr in specs:
            for s in (arr or []):
                typ = str(s.get("type", ""));
                kv = ",".join(f"{k}={s[k]}" for k in sorted(s.keys()) if k != "type")
                parts.append(f"{typ}({kv})")
        return "+".join(parts)

    def update(self, bullets: List):
        if not bullets:
            return
        # Hot-reload effects schema on change
        try:
            import os, json
            if self._effects_schema_path and os.path.isfile(self._effects_schema_path):
                mt = os.path.getmtime(self._effects_schema_path)
                if mt != self._effects_schema_mtime:
                    with open(self._effects_schema_path, 'r', encoding='utf-8') as f:
                        data = json.load(f)
                        if isinstance(data, dict):
                            self._effects_schema = data.get('effects', {}) or {}
                            self._effects_schema_mtime = mt
        except Exception:
            pass
        # IPC migration: effects application is handled in JUCE. Emit IPC messages only.
        # Read musical clock (still used for gating updates, but effects are cleared)
        pulse = 0.0
        try:
            st = self.game.audio_engine.get_clock_status() if hasattr(self.game.audio_engine, 'get_clock_status') else {}
            pulse = float(st.get('pulse', 0.0)) if isinstance(st, dict) else 0.0
        except Exception:
            pulse = 0.0
        if not hasattr(self, '_last_pulse'):
            self._last_pulse = -1.0
        apply_now = (int(pulse) != int(getattr(self, '_last_pulse', -1.0)))
        self._last_pulse = pulse

        for b in bullets:
            if not getattr(b, '_audio_ready', False):
                continue
            bx, by = float(getattr(b, 'x', 0.0)), float(getattr(b, 'y', 0.0))
            active_specs = []
            for z in self.zones:
                if z.contains(bx, by):
                    active_specs.append(z.spec)
            key = self._effect_key(active_specs)
            bid = id(b)
            if key == self._bullet_fx_key.get(bid, "") and not apply_now:
                continue
            self._bullet_fx_key[bid] = key

            # Emit IPC: if no active zones, send a clear payload to restore dry state
            try:
                from pyglet_physics_game.ipc.client import get_ipc_client
                client = get_ipc_client()
                # Not yet mapped to OSC; keep as stub log until JUCE maps this path
                zones_payload = [self._normalize_specs(z.spec) for z in self.zones if z.contains(bx, by)]
                try:
                    print(f"[OSC_STUB] /zone/fx/apply voiceId={b._engine_player_id} zones={len(zones_payload)}")
                except Exception:
                    pass
            except Exception:
                pass

    # --- Helpers ---
    def _normalize_specs(self, spec_list: List[Dict]) -> List[Dict]:
        out: List[Dict] = []
        for s in (spec_list or []):
            try:
                typ = str(s.get('type',''))
                sn = dict(s)
                # Harmonize naming to JUCE mapping (accept both camelCase and snake_case)
                if typ == 'reverb':
                    if 'roomSize' in sn and 'room_size' not in sn:
                        sn['room_size'] = sn['roomSize']
                    if 'wet' in sn and 'mix' not in sn:
                        sn['mix'] = sn['wet']
                elif typ == 'delay':
                    if 'timeMs' in sn and 'delay_ms' not in sn:
                        sn['delay_ms'] = sn['timeMs']
                elif typ == 'svf':
                    # Convert generic svf to concrete mode for engine (lp/hp)
                    mode = str(sn.get('mode', 'lp')).lower()
                    cutoff = sn.get('cutoff_hz', sn.get('cutoffHz', 1200.0))
                    if mode in ('hp','highpass'):
                        sn = {'type': 'highpass', 'cutoff_hz': cutoff}
                    else:
                        sn = {'type': 'lowpass', 'cutoff_hz': cutoff}
                elif typ in ('lowpass','highpass'):
                    if 'cutoffHz' in sn and 'cutoff_hz' not in sn:
                        sn['cutoff_hz'] = sn['cutoffHz']
                # Clamp values per effects schema
                if isinstance(self._effects_schema, dict):
                    if typ == 'reverb':
                        sch = self._effects_schema.get('reverb', {}).get('params', {})
                        sn['room_size'] = _clamp_float(sn.get('room_size', sch.get('roomSize', {}).get('default', 0.7)), sch.get('roomSize', {}))
                        sn['damping'] = _clamp_float(sn.get('damping', sch.get('damping', {}).get('default', 0.5)), sch.get('damping', {}))
                        sn['mix'] = _clamp_float(sn.get('mix', sch.get('wet', {}).get('default', 0.25)), sch.get('wet', {}))
                    elif typ == 'delay':
                        sch = self._effects_schema.get('delay', {}).get('params', {})
                        sn['delay_ms'] = _clamp_float(sn.get('delay_ms', sch.get('timeMs', {}).get('default', 350.0)), sch.get('timeMs', {}))
                        sn['feedback'] = _clamp_float(sn.get('feedback', sch.get('feedback', {}).get('default', 0.35)), sch.get('feedback', {}))
                        sn['mix'] = _clamp_float(sn.get('mix', sch.get('mix', {}).get('default', 0.3)), sch.get('mix', {}))
                    elif typ in ('lowpass','highpass'):
                        # use svf cutoff bounds
                        sch = self._effects_schema.get('svf', {}).get('params', {})
                        sn['cutoff_hz'] = _clamp_float(sn.get('cutoff_hz', sch.get('cutoffHz', {}).get('default', 1200.0)), sch.get('cutoffHz', {}))
                out.append(sn)
            except Exception:
                out.append(s)
        return out

    # --- Zone factory helpers ---
    def _spec_for_effect(self, effect_name: str, params_schema: Dict) -> Dict:
        """Build a single-effect spec with loud, audible defaults derived from schema."""
        en = effect_name.lower()
        # Utility to read default with clamp and optional scale for audibility
        def df(name, scale=1.0):
            ps = params_schema.get(name, {}) if isinstance(params_schema, dict) else {}
            val = ps.get('default', ps.get('min', 0.0))
            try:
                v = float(val) * float(scale)
            except Exception:
                v = val
            if isinstance(ps, dict):
                # Clamp
                vmin = ps.get('min', v)
                vmax = ps.get('max', v)
                try:
                    v = max(float(vmin), min(float(vmax), float(v)))
                except Exception:
                    pass
            return v

        if en == 'svf':
            mode = params_schema.get('mode', {}).get('default', 'lp') if isinstance(params_schema, dict) else 'lp'
            cutoff = params_schema.get('cutoffHz', {}).get('default', 1200.0)
            if str(mode).lower() in ('hp','highpass'):
                return {'type': 'highpass', 'cutoff_hz': float(cutoff)}
            return {'type': 'lowpass', 'cutoff_hz': float(cutoff)}
        if en == 'delay':
            return {
                'type': 'delay',
                'delay_ms': float(df('timeMs', 1.0)),
                'feedback': float(df('feedback', 1.0)),
                'mix': float(max(0.5, df('mix', 1.0)))
            }
        if en == 'reverb':
            # Push wet higher for audibility
            wet = params_schema.get('wet', {}).get('default', 0.25)
            try:
                wet = min(1.0, float(wet) * 1.8)
            except Exception:
                wet = 0.7
            return {
                'type': 'reverb',
                'room_size': float(df('roomSize', 1.0)),
                'damping': float(df('damping', 1.0)),
                'mix': float(wet)
            }
        if en == 'chorus':
            return {
                'type': 'chorus',
                'rateHz': df('rateHz', 1.0),
                'depth': df('depth', 1.0),
                'centreMs': df('centreMs', 1.0),
                'feedback': df('feedback', 1.0),
                'mix': max(0.6, df('mix', 1.0))
            }
        if en == 'phaser':
            return {
                'type': 'phaser',
                'rateHz': df('rateHz', 1.0),
                'depth': df('depth', 1.0),
                'centreHz': df('centreHz', 1.0),
                'feedback': df('feedback', 1.0),
                'mix': max(0.6, df('mix', 1.0))
            }
        if en == 'waveshaper':
            # Avoid param name collision "type" by using "curve"
            curve = params_schema.get('type', {}).get('default', 'tanh') if isinstance(params_schema, dict) else 'tanh'
            return { 'type': 'waveshaper', 'curve': curve, 'driveDb': df('driveDb', 1.0) }
        if en == 'compressor':
            return {
                'type': 'compressor',
                'thresholdDb': df('thresholdDb', 1.0),
                'ratio': df('ratio', 1.0),
                'attackMs': df('attackMs', 1.0),
                'releaseMs': df('releaseMs', 1.0),
                'makeupDb': df('makeupDb', 1.0)
            }
        if en == 'limiter':
            return {
                'type': 'limiter',
                'ceilingDb': df('ceilingDb', 1.0),
                'releaseMs': df('releaseMs', 1.0)
            }
        if en == 'tremolo':
            return { 'type': 'tremolo', 'rateHz': float(df('rateHz', 1.0)), 'depth': float(max(0.9, df('depth', 1.0))) }
        # Autopan removed by design
        # Unknown -> pass-through with best effort
        return { 'type': effect_name }

    def _zone_color_for_effect(self, effect_name: str):
        m = {
            'svf': (120, 255, 180),
            'delay': (255, 180, 60),
            'reverb': (120, 220, 255),
            'chorus': (200, 160, 255),
            'phaser': (255, 120, 200),
            'waveshaper': (255, 120, 120),
            'compressor': (255, 220, 120),
            'limiter': (255, 255, 120),
            'tremolo': (120, 255, 255),
            'autopan': (160, 255, 200),
        }
        return m.get(effect_name.lower(), (200, 200, 200))


def _clamp_float(val, schema):
    try:
        v = float(val)
    except Exception:
        v = float(schema.get('default', 0.0))
    vmin = float(schema.get('min', v))
    vmax = float(schema.get('max', v))
    if vmin > vmax:
        vmin, vmax = vmax, vmin
    return max(vmin, min(vmax, v))


