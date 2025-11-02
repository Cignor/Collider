#!/usr/bin/env python3
"""
Audio Engine (DISMANTLED STUB)
All runtime audio responsibilities are moving to JUCE. This stub keeps call sites alive.
"""

import numpy as np  # kept for type signatures
import threading
import time
import queue
from typing import Optional, Dict, List
try:
    from ..native import NativeMixerShim
except Exception:
    NativeMixerShim = None

class UltraOptimizedEngine:
    """
    Ultra-optimized audio engine with proper producer-consumer architecture.
    The callback does absolutely minimal work - just copies pre-mixed audio.
    """
    
    def __init__(self, sample_rate: int = 48000, buffer_size: int = 192):
        # Dismantled: do not initialize any device or background audio
        try:
            import json, os
            # Config lives under pyglet_physics_game/audio_engine/config
            cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config', 'audio_engine_config.json')
            if os.path.exists(cfg_path):
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    cfg = json.load(f)
                sample_rate = int(cfg.get('sample_rate', sample_rate))
                buffer_size = int(cfg.get('buffer_size', buffer_size))
        except Exception as e:
            print(f"Could not read audio_engine_config.json: {e}")

        self.sample_rate = sample_rate
        self.buffer_size = buffer_size
        # Cached mixing parameters (configurable)
        self._mix_queue_target = 50
        self._mix_sleep_s = 0.001
        self.is_initialized = True
        self.is_playing = False
        self.audio_stream = None
        
        # Producer-consumer with large buffer
        self.audio_queue = queue.Queue(maxsize=100)  # Much larger queue for safety
        self.mixing_thread = None
        self.mixing_active = False
        
        # Audio players (managed by mixing thread)
        self.audio_players = {}
        self.players_lock = threading.Lock()
        
        # Pre-allocated buffers to avoid memory allocation; allow headroom (reservoir)
        self.temp_chunk_buffer = np.zeros((2, buffer_size), dtype=np.float32)
        self.temp_mixed_buffer = np.zeros((2, buffer_size), dtype=np.float32)
        
        # Output speed probe
        self._rt_frames_out_total = 0
        self._rt_start_time = time.time()
        self._probe_last_log = time.time()
        # Musical clock (Python path)
        self._clock_time_s = 0.0
        self._clock_bpm = 120.0
        self._clock_ppq = 480.0
        self._clock_pulse = 0.0

        # No native mixer in dismantle mode
        self._native = None
        self._prefer_native_neutral = False
        self._allow_native_in_python_path = False
        # Default safe-mix (device silence) disabled
        self._safe_mix_enabled = False
        if NativeMixerShim is not None:
            try:
                import json, os
                cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config', 'audio_engine_config.json')
                use_native = False
                if os.path.exists(cfg_path):
                    with open(cfg_path, 'r', encoding='utf-8') as f:
                        cfg = json.load(f)
                    use_native = bool(cfg.get('use_native_mixer', False))
                    self._use_miniaudio = bool(cfg.get('use_miniaudio', False))
                    self._prefer_native_neutral = bool(cfg.get('prefer_native_when_neutral', True))
                    self._native_stream_capacity = int(cfg.get('native_stream_capacity', 1 << 18))
                    self._native_stream_enable = bool(cfg.get('native_stream_enable', True))
                    self._stream_processed_to_native = bool(cfg.get('stream_processed_to_native', True))
                    # Limit streaming reservoir to improve responsiveness
                    self._native_stream_target_blocks = int(cfg.get('native_stream_target_blocks', 4))
                    self._native_output_only = bool(cfg.get('native_output_only', False))
                    self._native_dsp_enable = bool(cfg.get('native_dsp_enable', True))
                    self._allow_native_in_python_path = bool(cfg.get('allow_native_in_python_path', False))
                    self._safe_mix_enabled = bool(cfg.get('safe_mix_enabled', False))
                    # Musical clock from config (user-editable)
                    try:
                        self._clock_bpm = float(cfg.get('clock_bpm', self._clock_bpm))
                        self._clock_ppq = float(cfg.get('clock_ppq', self._clock_ppq))
                    except Exception:
                        pass
                    # Cache mixing loop parameters
                    try:
                        self._mix_queue_target = int(cfg.get('mix_queue_target', self._mix_queue_target))
                        self._mix_sleep_s = max(0.0, float(cfg.get('mix_sleep_ms', int(self._mix_sleep_s * 1000.0)))) / 1000.0
                    except Exception:
                        pass
                if use_native:
                    self._native = NativeMixerShim(self.sample_rate, self.buffer_size)
                    try:
                        # Redundant safety: set backend explicitly in case shim init missed
                        if hasattr(self._native, '_mixer') and hasattr(self._native._mixer, 'set_backend'):
                            self._native._mixer.set_backend(bool(getattr(self, '_use_miniaudio', False)))
                    except Exception:
                        pass
                    try:
                        if hasattr(self._native, 'set_clock'):
                            self._native.set_clock(float(self._clock_bpm), float(self._clock_ppq))
                    except Exception:
                        pass
                # If fully native output, reduce sleep and match device rate
                if getattr(self, '_native_output_only', False):
                    if self._mix_sleep_s > 0.0005:
                        self._mix_sleep_s = 0.0005
                    try:
                        if self._native is not None and hasattr(self._native, 'get_device_rate'):
                            dev_rate = int(self._native.get_device_rate())
                            if dev_rate > 0 and dev_rate != int(self.sample_rate):
                                self.sample_rate = dev_rate
                                # Recreate temp buffers at new rate/block size
                                self.temp_chunk_buffer = np.zeros((2, buffer_size), dtype=np.float32)
                                self.temp_mixed_buffer = np.zeros((2, buffer_size), dtype=np.float32)
                    except Exception:
                        pass
            except Exception:
                self._native = None
        else:
            self._native_dsp_enable = False
            self._allow_native_in_python_path = False
        
        # Set low latency; prepare WASAPI exclusive settings for Windows if configured
        self._sd_extra_settings = None
        self._wasapi_exclusive = False
        self._strict_native_only = False
        try:
            import json, os, platform
            cfg_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config', 'audio_engine_config.json')
            wasapi_excl = False
            if os.path.exists(cfg_path):
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    cfg = json.load(f)
                wasapi_excl = bool(cfg.get('wasapi_exclusive', False))
                # Whether to refuse Python fallback if native fails to start
                self._strict_native_only = bool(cfg.get('strict_native_only', False))
                self._wasapi_exclusive = bool(wasapi_excl)
            if platform.system().lower().startswith('win') and wasapi_excl:
                try:
                    self._sd_extra_settings = sd.WasapiSettings(exclusive=True)
                except Exception:
                    self._sd_extra_settings = None
            sd.default.latency = 'low'
        except Exception:
            sd.default.latency = 'low'
        
        # Initialize ParamBus
        try:
            import os
            cfg_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'config')
            self.params = ParamBus(cfg_dir)
        except Exception:
            self.params = None

        # Initialize Effects Preset Manager
        try:
            import os
            # repo_root = parent of pyglet_physics_game
            pyg_root = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
            repo_root = os.path.dirname(pyg_root)
            presets_dir = 'audio/audio_effects/presets'
            if self.params is not None:
                pd = self.params.get_path('effects', 'presets_dir')
                if isinstance(pd, str) and pd:
                    presets_dir = pd
            self._fx_presets = EffectsPresetManager(repo_root, presets_dir)
        except Exception:
            self._fx_presets = None
        
        # Initialize audio
        # Do not initialize audio devices in dismantle mode
        print("Audio engine stub active (no devices)")
        
        # On Windows, improve scheduling resolution
        self._win_timer_raised = False
        try:
            import platform, ctypes
            if platform.system().lower().startswith('win'):
                timeBeginPeriod = ctypes.windll.winmm.timeBeginPeriod
                if timeBeginPeriod(1) == 0:
                    self._win_timer_raised = True
        except Exception:
            self._win_timer_raised = False
    
    def _initialize_audio(self):
        self.is_initialized = True
        return
    
    # ----- Generator hooks -----
    def _render_frequency_block(self, preset_json_path: str, num_frames: int, semitone_override: Optional[float] = None) -> Optional[np.ndarray]:
        try:
            import json, math
            with open(preset_json_path, 'r', encoding='utf-8') as f:
                cfg = json.load(f)
            params = {p['id']: p.get('value') for p in cfg.get('params', [])}
            # Determine waveform: prefer explicit param, then JSON name, then filename stem
            wave = str(params.get('waveform', cfg.get('name', 'sine'))).strip().lower()
            if not wave or wave == '':
                try:
                    import os
                    wave = os.path.splitext(os.path.basename(preset_json_path))[0].lower()
                except Exception:
                    wave = 'sine'
            if semitone_override is not None:
                freq = float(440.0 * (2.0 ** (float(semitone_override) / 12.0)))
            else:
                freq = float(params.get('frequency_hz', 440.0))
            gain_db = float(params.get('gain_db', 0.0))
            amp = 10.0 ** (gain_db / 20.0)
            t = np.arange(num_frames, dtype=np.float32) / float(self.sample_rate)
            if not hasattr(self, '_gen_phase'):
                self._gen_phase = 0.0
            phase0 = float(self._gen_phase)
            phase = phase0 + 2.0 * np.pi * freq * t
            step = 2.0 * np.pi * freq / self.sample_rate
            self._gen_phase = float((phase0 + step * num_frames) % (2.0 * np.pi))
            # Waveform generation
            if 'square' in wave:
                mono = np.sign(np.sin(phase)).astype(np.float32) * amp
            elif 'triangle' in wave:
                # triangle from sine via arcsin
                mono = (2.0 / np.pi * np.arcsin(np.sin(phase))).astype(np.float32) * amp
            elif 'saw' in wave or 'sawtooth' in wave:
                # sawtooth centered [-1,1]
                # compute phase cycles
                cycles = phase / (2.0 * np.pi)
                frac = cycles - np.floor(cycles + 0.5)
                mono = (2.0 * frac).astype(np.float32) * amp
            else:
                mono = (np.sin(phase).astype(np.float32) * amp)
            return np.stack([mono, mono])
        except Exception:
            return None

    def _render_noise_block(self, preset_json_path: str, num_frames: int) -> Optional[np.ndarray]:
        try:
            import json
            with open(preset_json_path, 'r', encoding='utf-8') as f:
                cfg = json.load(f)
            params = {p['id']: p.get('value') for p in cfg.get('params', [])}
            gain_db = float(params.get('gain_db', 0.0))
            amp = 10.0 ** (gain_db / 20.0)
            # Determine noise type by name or filename
            noise_name = str(cfg.get('name', '')).lower()
            if not noise_name:
                try:
                    import os
                    noise_name = os.path.splitext(os.path.basename(preset_json_path))[0].lower()
                except Exception:
                    noise_name = 'white_noise'
            # White noise baseline
            if 'white' in noise_name:
                mono = np.random.uniform(-1.0, 1.0, size=(num_frames,)).astype(np.float32)
            elif 'pink' in noise_name:
                # Simple pink-ish noise via one-pole filtering of white noise
                white = np.random.uniform(-1.0, 1.0, size=(num_frames,)).astype(np.float32)
                if not hasattr(self, '_pink_state'):
                    self._pink_state = 0.0
                alpha = 0.98  # pole close to 1 for ~1/f spectrum
                y = np.empty_like(white)
                prev = float(self._pink_state)
                for i in range(num_frames):
                    prev = alpha * prev + (1.0 - alpha) * white[i]
                    y[i] = prev
                self._pink_state = float(prev)
                mono = y
            elif 'brown' in noise_name or 'brownian' in noise_name or 'red' in noise_name:
                # Brown noise: integrate white with leakage to bound
                white = np.random.uniform(-1.0, 1.0, size=(num_frames,)).astype(np.float32)
                if not hasattr(self, '_brown_state'):
                    self._brown_state = 0.0
                leak = 0.995
                step = 0.02
                y = np.empty_like(white)
                prev = float(self._brown_state)
                for i in range(num_frames):
                    prev = leak * prev + step * white[i]
                    # clamp softly
                    if prev > 1.0:
                        prev = 1.0
                    elif prev < -1.0:
                        prev = -1.0
                    y[i] = prev
                self._brown_state = float(prev)
                mono = y
            else:
                mono = np.random.uniform(-1.0, 1.0, size=(num_frames,)).astype(np.float32)
            mono *= amp
            return np.stack([mono, mono])
        except Exception:
            return None
    
    def create_audio_player(self, player_id: str, audio_data: np.ndarray, 
                           effects: Optional[List] = None) -> bool:
        """Create an audio player"""
        if not self.is_initialized:
            return False
        
        try:
            # If a player with this id already exists, remove it to avoid duplicates
            try:
                with self.players_lock:
                    if player_id in self.audio_players:
                        try:
                            # Remove native voice first
                            if self._native is not None:
                                self._native.remove_voice(player_id)
                        except Exception:
                            pass
                        try:
                            del self.audio_players[player_id]
                        except Exception:
                            self.audio_players.pop(player_id, None)
            except Exception:
                pass
            # Ensure correct format
            if audio_data.ndim == 1:
                audio_data = np.stack([audio_data, audio_data])
            elif audio_data.shape[0] != 2:
                audio_data = audio_data.T
            
            audio_data = audio_data.astype(np.float32)
            
            # Prepare seamless loop copy to avoid boundary clicks
            audio_data = self._prepare_seamless_loop(audio_data)
            
            # Create effect chain and optional DSP effect
            effect_chain = None
            dsp_effect = None
            if effects:
                plugin_effects = []
                for eff in effects:
                    # Detect our DSP wrapper by marker attribute to avoid tight coupling
                    if hasattr(eff, '_is_dsp_wrapper') and getattr(eff, '_is_dsp_wrapper') is True:
                        dsp_effect = eff
                    else:
                        plugin_effects.append(eff)
                if plugin_effects:
                    effect_chain = Pedalboard(plugin_effects)
            
            # Store player data
            with self.players_lock:
                self.audio_players[player_id] = {
                    'audio_data': audio_data,
                    'effect_chain': effect_chain,
                    'dsp_effect': dsp_effect,
                    'is_playing': False,
                    'volume': 1.0,
                    'pan': 0.0,
                    'position': 0,
                    'loop': False,
                    'mute': False,
                    'native_id': None
                }
                # Mirror into native if available and no DSP/effects (pilot path),
                # only when native path is active or explicitly allowed in Python path
                if self._native is not None and dsp_effect is None and effect_chain is None and (getattr(self, '_native_output_only', False) or getattr(self, '_allow_native_in_python_path', False)):
                    try:
                        self._native.add_voice(player_id, audio_data, False, 1.0, 0.0)
                        self.audio_players[player_id]['native_id'] = player_id
                        try:
                            # Force srcRate to device rate so baseScaleStatic = 1.0 at neutral
                            if hasattr(self._native, 'get_device_rate') and hasattr(self._native, 'set_src_rate'):
                                dev_rate = float(self._native.get_device_rate())
                                if dev_rate > 0:
                                    self._native.set_src_rate(player_id, dev_rate)
                            elif hasattr(self._native, 'set_src_rate'):
                                self._native.set_src_rate(player_id, float(self.sample_rate))
                        except Exception:
                            pass
                        # Debug: log native voice parameters
                        try:
                            if hasattr(self._native, 'get_voice_info'):
                                info = self._native.get_voice_info(player_id)
                                stats = getattr(self._native, 'get_stats', lambda: {})()
                                print(f"NATIVE_DEBUG(create): id={player_id} info={info} stats={stats}")
                        except Exception:
                            pass
                    except Exception:
                        pass
            
            print(f"[STUB] Created player: {player_id}")
            return True
            
        except Exception as e:
            print(f"Failed to create player: {e}")
            return False
    
    def update_player_effects(self, player_id: str, effects: Optional[List] = None) -> bool:
        """Dismantled: no-op stub (Pedalboard removed)."""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            self.audio_players[player_id]['effect_chain'] = None
            self.audio_players[player_id]['dsp_effect'] = None
        print(f"[STUB] update_player_effects ignored for {player_id}")
        return True
    
    def update_player_pedalboard_effects(self, player_id: str, pedalboard_effects: Optional[List] = None) -> bool:
        """Dismantled: no-op stub (Pedalboard removed)."""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            self.audio_players[player_id]['effect_chain'] = None
        print(f"[STUB] update_player_pedalboard_effects ignored for {player_id}")
        return True

    def apply_effects_preset(self, player_id: str, preset_name: str) -> bool:
        """Dismantled: no-op stub (presets applied in JUCE)."""
        print(f"[STUB] apply_effects_preset ignored for {player_id}:{preset_name}")
        return True
    
    def play_audio(self, player_id: str, loop: bool = False, 
                   volume: float = 1.0, pan: float = 0.0) -> bool:
        """Play audio"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            player['is_playing'] = True
            player['volume'] = volume
            player['pan'] = pan
            player['loop'] = loop
            player['position'] = 0
            # Flush queue for immediate response
            try:
                while not self.audio_queue.empty():
                    self.audio_queue.get_nowait()
            except Exception:
                pass
            # Update native mirror params if present
            if self._native is not None and player.get('native_id') is not None:
                try:
                    self._native.set_params(player_id, float(volume), float(pan))
                    self._native.set_loop(player_id, bool(loop))
                    # FORCE: ensure native baseScale uses engine.sample_rate for this buffer
                    if hasattr(self._native, 'set_src_rate'):
                        self._native.set_src_rate(player_id, float(self.sample_rate))
                    # Debug: log voice info after start
                    try:
                        if hasattr(self._native, 'get_voice_info'):
                            info = self._native.get_voice_info(player_id)
                            stats = getattr(self._native, 'get_stats', lambda: {})()
                            print(f"NATIVE_DEBUG(play): id={player_id} info={info} stats={stats}")
                    except Exception:
                        pass
                except Exception:
                    pass
        
        # Prefill native ring for processed voices to avoid initial underruns
        try:
            if self._native is not None and getattr(self, '_native_output_only', False):
                with self.players_lock:
                    player = self.audio_players.get(player_id)
                    if player is not None:
                        dsp = player.get('dsp_effect')
                        effect_chain = player.get('effect_chain')
                        if (dsp is not None or effect_chain is not None) and getattr(self, '_stream_processed_to_native', False):
                            self._prefill_native_stream(player_id, blocks=6)
                        # Initialize native DSP params to neutral
                        try:
                            if hasattr(self._native, 'set_dsp_params'):
                                self._native.set_dsp_params(player_id, 0.0, 1.0)
                        except Exception:
                            pass
        except Exception:
            pass
        
        # Start audio stream if not running
        if not self.is_playing:
            self.start_audio_stream()
        
        print(f"[STUB] Started playing: {player_id}")
        return True
    
    def stop_audio(self, player_id: str) -> bool:
        """Stop audio"""
        with self.players_lock:
            if player_id in self.audio_players:
                self.audio_players[player_id]['is_playing'] = False
                # Also mute native voice if present
                if self._native is not None and self.audio_players[player_id].get('native_id') is not None:
                    try:
                        self._native.set_params(player_id, 0.0, self.audio_players[player_id].get('pan', 0.0))
                    except Exception:
                        pass
                print(f"[STUB] Stopped: {player_id}")
                return True
        return False

    def remove_audio_player(self, player_id: str) -> bool:
        """Remove an audio player and cleanup its DSP to avoid leaks"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            pdata = self.audio_players[player_id]
            # Stop playback
            pdata['is_playing'] = False
            # Cleanup DSP wrapper if present
            dsp = pdata.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'cleanup'):
                try:
                    dsp.cleanup()
                except Exception:
                    pass
            # Remove entry
            try:
                del self.audio_players[player_id]
            except Exception:
                self.audio_players.pop(player_id, None)
            # Remove native mirror
            if self._native is not None:
                try:
                    self._native.remove_voice(player_id)
                except Exception:
                    pass
            print(f"[STUB] Removed player: {player_id}")
            return True
        
        
    
    def update_player_volume(self, player_id: str, volume: float) -> bool:
        """Update volume"""
        with self.players_lock:
            if player_id in self.audio_players:
                self.audio_players[player_id]['volume'] = volume
                if self._native is not None:
                    try:
                        eff_vol = 0.0 if self.audio_players[player_id].get('mute') else float(volume)
                        self._native.set_params(player_id, eff_vol, self.audio_players[player_id].get('pan', 0.0))
                        try:
                            if self.audio_players[player_id].get('native_id') is not None and hasattr(self._native, 'set_src_rate'):
                                # Keep src_rate consistent
                                self._native.set_src_rate(player_id, float(self.sample_rate))
                        except Exception:
                            pass
                    except Exception:
                        pass
                # Flush queue for faster response if using Python path
                try:
                    if not getattr(self, '_native_output_only', False):
                        while not self.audio_queue.empty():
                            self.audio_queue.get_nowait()
                except Exception:
                    pass
                return True
        return False
    
    def update_player_pan(self, player_id: str, pan: float) -> bool:
        """Update pan"""
        with self.players_lock:
            if player_id in self.audio_players:
                self.audio_players[player_id]['pan'] = pan
                if self._native is not None:
                    try:
                        eff_vol = 0.0 if self.audio_players[player_id].get('mute') else float(self.audio_players[player_id].get('volume', 1.0))
                        self._native.set_params(player_id, eff_vol, pan)
                    except Exception:
                        pass
                # Flush queue for faster response if using Python path
                try:
                    if not getattr(self, '_native_output_only', False):
                        while not self.audio_queue.empty():
                            self.audio_queue.get_nowait()
                except Exception:
                    pass
                return True
        return False

    def update_player_mute(self, player_id: str, mute: bool) -> bool:
        """Mute/unmute player without changing its logical volume."""
        with self.players_lock:
            if player_id in self.audio_players:
                self.audio_players[player_id]['mute'] = bool(mute)
                if self._native is not None:
                    try:
                        eff_vol = 0.0 if mute else float(self.audio_players[player_id].get('volume', 1.0))
                        self._native.set_params(player_id, eff_vol, self.audio_players[player_id].get('pan', 0.0))
                    except Exception:
                        pass
                return True
        return False
    
    def start_audio_stream(self):
        """Start audio stream and mixing thread"""
        if self.is_playing:
            return
        
        try:
            # Start mixing thread
            self.mixing_active = True
            self.mixing_thread = threading.Thread(target=self._mixing_loop, daemon=True)
            self.mixing_thread.start()
            
            if self._native is not None and getattr(self, '_native_output_only', False):
                # Start native WASAPI device
                try:
                    exclusive = bool(getattr(self, '_wasapi_exclusive', False))
                    started = self._native.start_device(exclusive) if hasattr(self._native, 'start_device') else False
                    if not started and exclusive:
                        # Retry in shared mode before giving up (still native-only)
                        started = self._native.start_device(False)
                    if started:
                        # Configure safe mix per config
                        try:
                            if hasattr(self._native, 'set_safe_mix'):
                                self._native.set_safe_mix(bool(getattr(self, '_safe_mix_enabled', False)))
                                if getattr(self, '_safe_mix_enabled', False):
                                    print("DBG: Safe mix enabled (device writes silence)")
                        except Exception:
                            pass
                        self.is_playing = True
                        print("Audio stream started (native)")
                        return
                    else:
                        raise RuntimeError("native device failed")
                except Exception:
                    if getattr(self, '_strict_native_only', False):
                        # Strict native-only: do not fallback to Python
                        print("ERROR: native device failed and strict_native_only is enabled; audio disabled.")
                        self.is_playing = False
                        return
                    # Fallback to sounddevice (Python path)
                    try:
                        self._native_output_only = False
                    except Exception:
                        pass
                    self.audio_stream = sd.OutputStream(
                        samplerate=self.sample_rate,
                        channels=2,
                        blocksize=self.buffer_size,
                        callback=self._ultra_minimal_callback,
                        dtype=np.float32,
                        extra_settings=self._sd_extra_settings
                    )
                    self.audio_stream.start()
                    self.is_playing = True
                    print("Audio stream started (python)")
                    return
            
            # Start audio stream (Python)
            self.audio_stream = sd.OutputStream(
                samplerate=self.sample_rate,
                channels=2,
                blocksize=self.buffer_size,
                callback=self._ultra_minimal_callback,
                dtype=np.float32,
                extra_settings=self._sd_extra_settings
            )
            self.audio_stream.start()
            self.is_playing = True
            print("[STUB] Audio stream start ignored")
        except Exception as e:
            print(f"[STUB] Failed to start stream (ignored): {e}")
    
    def stop_audio_stream(self):
        """Stop audio stream"""
        self.mixing_active = False
        if self.mixing_thread:
            self.mixing_thread.join(timeout=1.0)
        
        if self._native is not None and getattr(self, '_native_output_only', False):
            try:
                if hasattr(self._native, 'stop_device'):
                    self._native.stop_device()
            except Exception:
                pass
        
        if self.audio_stream:
            self.audio_stream.stop()
            self.audio_stream.close()
            self.audio_stream = None
        self.is_playing = False
        print("[STUB] Audio stream stop ignored")
    
    def _mixing_loop(self):
        """Producer: mixes all audio sources using queue size checking"""
        # Try to raise thread priority on Windows for smoother scheduling
        try:
            import platform, ctypes
            if platform.system().lower().startswith('win'):
                kernel32 = ctypes.windll.kernel32
                THREAD_PRIORITY_HIGHEST = 2
                kernel32.SetThreadPriority(kernel32.GetCurrentThread(), THREAD_PRIORITY_HIGHEST)
        except Exception:
            pass
        while self.mixing_active:
            try:
                # Fast path: if native output-only, bypass Python queue and just feed native streaming voices
                if self._native is not None and getattr(self, '_native_output_only', False):
                    self._service_native_streams()
                    time.sleep(self._mix_sleep_s)
                    continue
                # Configurable queue fill target (cached)
                fill_target = self._mix_queue_target

                # Check if the queue is getting low before putting more work in
                while self.audio_queue.qsize() < fill_target:
                    try:
                        mixed_chunk = self._mix_all_players()
                        self.audio_queue.put(mixed_chunk, block=False)
                    except queue.Full:
                        # This happens rarely with the new qsize() check
                        break
                
                # Once the queue is full, sleep for a short while (cached)
                time.sleep(self._mix_sleep_s)
                # Periodic probe (Python path only): log estimated callback rate and queue size
                try:
                    if not getattr(self, '_native_output_only', False):
                        now = time.time()
                        if now - float(self._probe_last_log) >= 2.0:
                            print(f"DBG_RATE: cb_est={self.get_output_speed_hz():.1f}Hz q={self.audio_queue.qsize()}")
                            self._probe_last_log = now
                except Exception:
                    pass
                
            except Exception as e:
                print(f"Error in mixing: {e}")
                time.sleep(0.01)
    
    def _mix_all_players(self) -> np.ndarray:
        """Mix all playing audio sources - optimized version"""
        self.temp_mixed_buffer.fill(0.0)
        
        with self.players_lock:
            # In-place accumulation to avoid per-voice list allocations
            mixbuf = self.temp_mixed_buffer
            mixbuf.fill(0.0)
            
            for player_id, player in self.audio_players.items():
                if not player['is_playing']:
                    continue
                
                audio_data = player['audio_data']
                # Skip native-only voices (no Python buffer); native mix is added later
                if audio_data is None:
                    continue
                position = player['position']
                dsp = player.get('dsp_effect')
                effect_chain = player.get('effect_chain')
                
                # Check bounds and loop/stop as needed
                if position >= audio_data.shape[1]:
                    if player['loop']:
                        # Seamless looping: wrap without gaps
                        position = position % audio_data.shape[1]
                        player['position'] = position
                    else:
                        # If mirrored to native, remove it as well
                        if self._native is not None and player.get('native_id') is not None:
                            try:
                                self._native.remove_voice(player_id)
                            except Exception:
                                pass
                            player['native_id'] = None
                        player['is_playing'] = False
                        continue
                
                # Prefer native for:
                # - any voice when native DSP is enabled and no Pedalboard effects
                # - pure static voices (no DSP, no Pedalboard)
                # - neutral DSP voices (pitch=0, tempo=1) when configured and no Pedalboard effects
                prefer_native = False
                if self._native is not None and effect_chain is None and (getattr(self, '_native_output_only', False) or getattr(self, '_allow_native_in_python_path', False)):
                    if getattr(self, '_native_dsp_enable', True):
                        prefer_native = True
                    elif dsp is None:
                        prefer_native = True
                    elif self._prefer_native_neutral:
                        try:
                            params = dsp.get_parameters() if hasattr(dsp, 'get_parameters') else {}
                            pitch = float(params.get('pitch_shift', 0.0))
                            tempo = float(params.get('tempo_factor', 1.0))
                            prefer_native = (abs(pitch) < 1e-6 and abs(tempo - 1.0) < 1e-6)
                        except Exception:
                            prefer_native = False
                
                if prefer_native:
                    # Ensure native voice exists and is updated
                    if player.get('native_id') is None:
                        try:
                            self._native.add_voice(player_id, audio_data, bool(player.get('loop', False)), float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                            player['native_id'] = player_id
                            # Tell native the source sample rate; our audio_data is resampled to engine rate already
                            try:
                                if hasattr(self._native, 'set_src_rate'):
                                    self._native.set_src_rate(player_id, float(self.sample_rate))
                            except Exception:
                                pass
                        except Exception:
                            pass
                    else:
                        try:
                            self._native.set_loop(player_id, bool(player.get('loop', False)))
                            self._native.set_params(player_id, float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                        except Exception:
                            pass
                    # Forward DSP params to native if enabled
                    if getattr(self, '_native_dsp_enable', True):
                        try:
                            pitch = 0.0
                            tempo = 1.0
                            if dsp is not None and hasattr(dsp, 'get_parameters'):
                                params = dsp.get_parameters()
                                pitch = float(params.get('pitch_shift', 0.0))
                                tempo = float(params.get('tempo_factor', 1.0))
                            if hasattr(self._native, 'set_dsp_params'):
                                self._native.set_dsp_params(player_id, pitch, tempo)
                        except Exception:
                            pass
                    # Advance position to keep timeline aligned and skip Python mixing for this voice
                    end_pos_native = position + self.buffer_size
                    player['position'] = end_pos_native % audio_data.shape[1]
                    continue
                else:
                    # If previously mirrored natively for bypass, remove it now to avoid double-summing
                    if self._native is not None and player.get('native_id') is not None:
                        try:
                            self._native.remove_voice(player_id)
                        except Exception:
                            pass
                        player['native_id'] = None

                # Early skip for silent voices with no processing
                vol_for_mix = 0.0 if player.get('mute') else float(player.get('volume', 1.0))
                if dsp is None and effect_chain is None and vol_for_mix <= 1e-7 and abs(float(player.get('pan', 0.0))) <= 1e-7:
                    end_pos = position + self.buffer_size
                    player['position'] = end_pos % audio_data.shape[1]
                    continue

                end_pos = position + self.buffer_size
                if end_pos <= audio_data.shape[1]:
                    chunk = audio_data[:, position:end_pos]
                else:
                    # Wrap-around chunk for perfect seamless loops (avoid allocate when possible)
                    first_len = audio_data.shape[1] - position
                    remaining = end_pos - audio_data.shape[1]
                    # Ensure work buffer is defined before use
                    work = self.temp_chunk_buffer
                    work[:, :first_len] = audio_data[:, position:]
                    work[:, first_len:self.buffer_size] = audio_data[:, :remaining]
                    chunk = work[:, :self.buffer_size]
                
                # Work buffer copy to avoid modifying source audio_data
                work = self.temp_chunk_buffer
                if chunk.base is not work:
                    work[:, :self.buffer_size] = chunk
                
                # Defer panning and volume until after DSP/effects to avoid being overwritten
                
                # Apply DSP effect first (outside Pedalboard), then Pedalboard chain
                processed = work
                if dsp is not None:
                    try:
                        # Feed mono to DSP (conversion centralized outside the wrapper)
                        mono_for_dsp = work[0] if work.ndim == 2 else work
                        processed_mono = dsp.process(mono_for_dsp, self.sample_rate)
                        # Ensure stereo for the rest of the pipeline
                        if processed_mono.ndim == 1:
                            processed = np.stack([processed_mono, processed_mono])
                        elif processed_mono.ndim == 2 and processed_mono.shape[0] == 1:
                            processed = np.vstack([processed_mono, processed_mono])
                        else:
                            processed = processed_mono
                        # Clamp to valid range to avoid accumulation clipping
                        np.clip(processed, -1.0, 1.0, out=processed)
                    except Exception as e:
                        print(f"DSP error: {e}")
                
                if player['effect_chain'] is not None:
                    try:
                        # Pedalboard expects shape (frames, channels)
                        chunk_pb = processed.T
                        # Process with explicit buffer size; normalize output to [buffer_size, 2]
                        pb_out = player['effect_chain'].process(
                            chunk_pb, self.sample_rate, buffer_size=self.buffer_size, reset=False
                        )
                        if not isinstance(pb_out, np.ndarray):
                            pb_out = chunk_pb
                        # Ensure 2D
                        if pb_out.ndim == 1:
                            pb_out = np.stack([pb_out, pb_out], axis=1)
                        # Ensure at least 2 channels
                        if pb_out.ndim == 2 and pb_out.shape[1] == 1:
                            pb_out = np.repeat(pb_out, 2, axis=1)
                        # Enforce exact frame count
                        if pb_out.shape[0] != self.buffer_size:
                            if pb_out.shape[0] > self.buffer_size:
                                pb_out = pb_out[:self.buffer_size, :]
                            else:
                                pad = np.zeros((self.buffer_size - pb_out.shape[0], pb_out.shape[1]), dtype=pb_out.dtype)
                                pb_out = np.concatenate([pb_out, pad], axis=0)
                        # Limit to 2 channels
                        if pb_out.shape[1] > 2:
                            pb_out = pb_out[:, :2]
                        elif pb_out.shape[1] < 2:
                            pad_ch = 2 - pb_out.shape[1]
                            pb_out = np.pad(pb_out, ((0,0),(0,pad_ch)), mode='constant')
                        # Back to (channels, frames)
                        processed = np.clip(pb_out.T.astype(np.float32, copy=False), -1.0, 1.0)
                    except Exception as e:
                        print(f"Effect error: {e}")
                
                # Optional: stream processed stereo to native mixer to offload Python summing
                # Only in native-output mode or when explicitly allowed in Python path
                if (self._native is not None and getattr(self, '_stream_processed_to_native', False) and (getattr(self, '_native_output_only', False) or getattr(self, '_allow_native_in_python_path', False))):
                    try:
                        # Ensure native streaming voice exists
                        if player.get('native_id') is None or player.get('native_stream', False) is False:
                            # Use a small ring capacity to minimize latency
                            cap_req = int(self.buffer_size * max(16, int(getattr(self, '_native_stream_target_blocks', 4)) * 4))
                            cap = int(min(int(getattr(self, '_native_stream_capacity', 1 << 18)), cap_req))
                            self._native.add_stream_voice(player_id, cap, float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                            player['native_id'] = player_id
                            player['native_stream'] = True
                            try:
                                if hasattr(self._native, 'set_src_rate'):
                                    self._native.set_src_rate(player_id, float(self.sample_rate))
                            except Exception:
                                pass
                        else:
                            # keep params in sync (volume, pan, mute)
                            eff_vol = 0.0 if player.get('mute') else float(player.get('volume', 1.0))
                            self._native.set_params(player_id, eff_vol, float(player.get('pan', 0.0)))
                        # Push un-panned, un-scaled processed stereo; native applies pan/vol
                        to_push = np.ascontiguousarray(processed)
                        self._native.push_stereo(player_id, to_push)
                        try:
                            st = self._native.get_stream_status(player_id) if hasattr(self._native, 'get_stream_status') else {}
                            cap = int(st.get('capacity', 0)); fill = int(st.get('fill', 0))
                            print(f"FX_STREAM_PUSH id={player_id} shape={processed.shape} fill={fill} cap={cap}")
                        except Exception:
                            pass
                        # Advance position and skip Python accumulation for this voice
                        player['position'] = end_pos % audio_data.shape[1]
                        continue
                    except Exception:
                        # Fallback to Python mixing on any issue
                        pass
                
                # Apply panning (constant power) AFTER DSP/effects
                pan = float(player['pan'])
                if abs(pan) > 1e-6:
                    pan_val = (pan + 1.0) / 2.0
                    left_gain = np.cos(pan_val * np.pi / 2.0)
                    right_gain = np.sin(pan_val * np.pi / 2.0)
                    processed[0, :] *= left_gain
                    processed[1, :] *= right_gain
                
                # Apply volume AFTER DSP/effects with mute gating
                vol = 0.0 if player.get('mute') else float(player['volume'])
                if abs(vol - 1.0) > 1e-6:
                    processed = (processed * vol).astype(np.float32, copy=False)
                
                # Accumulate in place to the mix buffer
                np.add(mixbuf, processed, out=mixbuf, casting='unsafe')
                player['position'] = end_pos % audio_data.shape[1]
            # temp_mixed_buffer already holds the sum
        
        # Optional native mixing: static voices + streamed processed blocks
        if self._native is not None and (getattr(self, '_native_output_only', False) or getattr(self, '_allow_native_in_python_path', False)):
            try:
                native_mix = self._native.render()
                # Sum native result with Python-mixed content
                if native_mix.shape == self.temp_mixed_buffer.shape:
                    np.add(self.temp_mixed_buffer, native_mix, out=self.temp_mixed_buffer, casting='unsafe')
            except Exception:
                pass
        # Apply clipping protection in-place and return copy
        np.clip(self.temp_mixed_buffer, -1.0, 1.0, out=self.temp_mixed_buffer)
        return self.temp_mixed_buffer.copy()

    def _prepare_seamless_loop(self, audio_data: np.ndarray) -> np.ndarray:
        """Create a per-player copy with end crossfaded into start to avoid clicks at loop seam."""
        try:
            # Ensure stereo [2, N]
            if audio_data.ndim != 2 or audio_data.shape[0] != 2:
                return audio_data
            n = audio_data.shape[1]
            if n < 64:
                return audio_data.copy()
            out = audio_data.copy()
            xf_len = max(32, min(512, n // 16))
            # Linear ramp
            ramp = np.linspace(0.0, 1.0, xf_len, dtype=np.float32)
            # Blend last xf_len samples towards the start to smooth seam
            tail_idx_start = n - xf_len
            out[:, tail_idx_start:] = (
                audio_data[:, tail_idx_start:] * (1.0 - ramp)[None, :] +
                audio_data[:, :xf_len] * ramp[None, :]
            ).astype(np.float32, copy=False)
            return out
        except Exception:
            return audio_data

    def offline_render_sample_with_signalsmith(self, mono_audio: np.ndarray, *, pitch_shift: float = 0.0, tempo_factor: float = 1.0) -> Optional[np.ndarray]:
        """Offline-render a pitched/time-stretched stereo buffer for a mono sample using Signalsmith.
        Returns (2, N) float32 stereo with a seamless loop crossfade applied, or None on failure.
        """
        try:
            if mono_audio is None or not isinstance(mono_audio, np.ndarray) or mono_audio.size == 0:
                return None
            mono = mono_audio.astype(np.float32, copy=False)
            # Create a non-async wrapper (lighter for offline use)
            try:
                from ..processing.python_stretch_wrapper import create_signalsmith_stretch_dsp
                dsp = create_signalsmith_stretch_dsp(pitch_shift=float(pitch_shift), tempo_factor=float(tempo_factor))
            except Exception:
                dsp = None
            if dsp is None:
                # Fallback: simple stereo copy if DSP unavailable
                return np.stack([mono, mono])
            # Process in large blocks for throughput
            bs = int(self.buffer_size)
            if bs <= 0:
                bs = 512
            chunks = []
            w = 0
            L = mono.size
            # Prime with one neutral chunk to warm internal buffers if needed
            while w < L:
                n = min(bs, L - w)
                block = mono[w:w+n]
                out = dsp.process(block, self.sample_rate)
                if out is None or (isinstance(out, np.ndarray) and out.size == 0):
                    break
                if out.ndim == 1:
                    out_st = np.stack([out, out])
                elif out.ndim == 2 and out.shape[0] == 1:
                    out_st = np.vstack([out, out])
                else:
                    out_st = out
                chunks.append(out_st.astype(np.float32, copy=False))
                w += n
            # Try to read a short tail to flush the reservoir
            try:
                tail = dsp.process(np.zeros(bs, dtype=np.float32), self.sample_rate)
                if isinstance(tail, np.ndarray) and tail.size > 0:
                    if tail.ndim == 1:
                        tail = np.stack([tail, tail])
                    elif tail.ndim == 2 and tail.shape[0] == 1:
                        tail = np.vstack([tail, tail])
                    chunks.append(tail.astype(np.float32, copy=False))
            except Exception:
                pass
            # Concatenate
            if not chunks:
                return None
            out = np.concatenate(chunks, axis=1)
            np.clip(out, -1.0, 1.0, out=out)
            # Seamless loop preparation
            out = self._prepare_seamless_loop(out)
            # Cleanup wrapper if available
            try:
                if hasattr(dsp, 'cleanup'):
                    dsp.cleanup()
            except Exception:
                pass
            return out
        except Exception as e:
            print(f"offline_render_sample_with_signalsmith failed: {e}")
            return None
    
    def _ultra_minimal_callback(self, outdata, frames, time, status):
        """Ultra-minimal callback - just copies pre-mixed audio"""
        # Avoid any printing/logging here; keep RT path silent
        
        try:
            if self._native is not None and getattr(self, '_native_output_only', False):
                # Render entirely from native mixer for maximum performance
                native_mix = self._native.render()
                outdata[:, :] = native_mix.T
            else:
                # Get pre-mixed audio (non-blocking)
                chunk = self.audio_queue.get_nowait()
                # Copy to output (transpose for sounddevice)
                outdata[:, :] = chunk.T
            
        except queue.Empty:
            # No audio available - fill with silence
            outdata.fill(0.0)
        # Probe: count frames written to device
        try:
            self._rt_frames_out_total += int(frames)
        except Exception:
            pass
        # Advance Python-side clock only when Python owns output
        try:
            if not getattr(self, '_native_output_only', False):
                dt = float(frames) / float(self.sample_rate)
                self._clock_time_s += dt
                pulses_per_second = (self._clock_bpm / 60.0) * self._clock_ppq
                self._clock_pulse += dt * pulses_per_second
        except Exception:
            pass
    
    def cleanup(self):
        """Cleanup resources"""
        self.stop_audio_stream()
        # Restore Windows timer resolution if we changed it
        try:
            import platform, ctypes
            if self._win_timer_raised and platform.system().lower().startswith('win'):
                ctypes.windll.winmm.timeEndPeriod(1)
        except Exception:
            pass
        with self.players_lock:
            # Ensure all DSP wrappers are explicitly cleaned up to avoid native leaks
            try:
                for _pid, pdata in list(self.audio_players.items()):
                    dsp = pdata.get('dsp_effect')
                    if dsp is not None and hasattr(dsp, 'cleanup'):
                        try:
                            dsp.cleanup()
                        except Exception:
                            pass
            except Exception:
                pass
            self.audio_players.clear()
        print("[STUB] Engine cleaned up")

    # ---- Diagnostics ----
    def get_output_speed_hz(self) -> float:
        """Return estimated device output rate based on frames written since engine init."""
        try:
            elapsed = max(1e-6, time.time() - float(self._rt_start_time))
            return float(self._rt_frames_out_total) / elapsed
        except Exception:
            return 0.0

    # ---- Musical clock (Python path and native proxy) ----
    def set_clock_bpm(self, bpm: float) -> None:
        try:
            self._clock_bpm = max(1.0, float(bpm))
            if self._native is not None and hasattr(self._native, 'set_clock'):
                self._native.set_clock(float(self._clock_bpm), float(self._clock_ppq))
        except Exception:
            pass

    def get_clock_status(self) -> dict:
        # Prefer native transport when native device owns the clock
        try:
            if self._native is not None and getattr(self, '_native_output_only', False) and hasattr(self._native, 'get_transport'):
                return dict(self._native.get_transport())
        except Exception:
            pass
        try:
            return {
                'time_s': float(self._clock_time_s),
                'bpm': float(self._clock_bpm),
                'ppq': float(self._clock_ppq),
                'pulse': float(self._clock_pulse),
                'device_rate': int(self.sample_rate),
            }
        except Exception:
            return {}

    def get_native_voice_info(self, player_id: str) -> dict:
        """Return native voice telemetry if available (src_rate, device_rate, last_rate, position)."""
        if self._native is None:
            return {}
        try:
            if hasattr(self._native, 'get_voice_info'):
                return dict(self._native.get_voice_info(player_id))
        except Exception:
            return {}
        return {}
    
    # DSP Integration Methods
    def create_dsp_player(self, player_id: str, audio_data: np.ndarray, 
                         processing_mode: str = 'granular', pitch_shift: float = 0.0, 
                         tempo_factor: float = 1.0, volume: float = 1.0, pan: float = 0.0) -> bool:
        """Create a player with DSP processing"""
        if not self.is_initialized:
            return False
        
        try:
            # Prefer native DSP path: create regular player without Python DSP, then set native params
            ok = self.create_audio_player(player_id, audio_data, effects=None)
            if not ok:
                return False
            # Set initial native DSP params if native is present
            try:
                if self._native is not None and getattr(self, '_native_dsp_enable', True):
                    self.set_player_native_dsp(player_id, float(pitch_shift), float(max(0.1, tempo_factor)))
            except Exception:
                pass
            # Initialize volume/pan
            self.play_audio(player_id, loop=False, volume=float(volume), pan=float(pan))
            return True
            
        except Exception as e:
            print(f"Failed to create DSP player: {e}")
            return False
    
    def set_player_dsp_mode(self, player_id: str, mode: str) -> bool:
        """Change DSP processing mode for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            # Prefer DSP effect stored outside Pedalboard
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'set_processing_mode'):
                dsp.set_processing_mode(mode)
                print(f"Updated DSP mode for {player_id}: {mode}")
                return True
            # Fallback to chain scan
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'set_processing_mode'):
                        effect.set_processing_mode(mode)
                        print(f"Updated DSP mode for {player_id}: {mode}")
                        return True
        return False
    
    def set_player_pitch_shift(self, player_id: str, semitones: float) -> bool:
        """Set pitch shift for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            # Prefer DSP effect if present
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'set_pitch_shift'):
                dsp.set_pitch_shift(semitones)
                # Flush queue for immediate response
                try:
                    while not self.audio_queue.empty():
                        self.audio_queue.get_nowait()
                except Exception:
                    pass
                print(f"Updated pitch shift for {player_id}: {semitones} semitones")
                return True
            # Fallback to chain scan
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'set_pitch_shift'):
                        effect.set_pitch_shift(semitones)
                        print(f"Updated pitch shift for {player_id}: {semitones} semitones")
                        return True
        return False
    
    def set_player_tempo_factor(self, player_id: str, factor: float) -> bool:
        """Set tempo factor for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            # Prefer DSP effect if present
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'set_tempo_factor'):
                dsp.set_tempo_factor(factor)
                # Flush queue for immediate response
                try:
                    while not self.audio_queue.empty():
                        self.audio_queue.get_nowait()
                except Exception:
                    pass
                print(f"Updated tempo factor for {player_id}: {factor}")
                return True
            # Fallback to chain scan
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'set_tempo_factor'):
                        effect.set_tempo_factor(factor)
                        print(f"Updated tempo factor for {player_id}: {factor}")
                        return True
        return False

    def set_player_dsp_params(self, player_id: str, *, pitch_shift: Optional[float] = None, tempo_factor: Optional[float] = None, flush: bool = True) -> bool:
        """Atomically update DSP pitch/tempo for a player with a single optional queue flush.
        Returns True if any parameter was applied.
        """
        applied = False
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            player = self.audio_players[player_id]
            dsp = player.get('dsp_effect')
            # Prefer DSP effect if present
            if dsp is not None:
                try:
                    if pitch_shift is not None and hasattr(dsp, 'set_pitch_shift'):
                        dsp.set_pitch_shift(float(pitch_shift))
                        applied = True
                    if tempo_factor is not None and hasattr(dsp, 'set_tempo_factor'):
                        dsp.set_tempo_factor(float(tempo_factor))
                        applied = True
                except Exception:
                    pass
                # Fallback scan if no DSP methods
                if not applied and player['effect_chain']:
                    for effect in player['effect_chain']:
                        if pitch_shift is not None and hasattr(effect, 'set_pitch_shift'):
                            effect.set_pitch_shift(float(pitch_shift))
                            applied = True
                        if tempo_factor is not None and hasattr(effect, 'set_tempo_factor'):
                            effect.set_tempo_factor(float(tempo_factor))
                            applied = True
            else:
                # No DSP stored separately; try effect chain
                if player['effect_chain']:
                    for effect in player['effect_chain']:
                        if pitch_shift is not None and hasattr(effect, 'set_pitch_shift'):
                            effect.set_pitch_shift(float(pitch_shift))
                            applied = True
                        if tempo_factor is not None and hasattr(effect, 'set_tempo_factor'):
                            effect.set_tempo_factor(float(tempo_factor))
                            applied = True
        # Optional queue flush to minimize latency in Python path
        if applied and flush:
            try:
                if not getattr(self, '_native_output_only', False):
                    while not self.audio_queue.empty():
                        self.audio_queue.get_nowait()
            except Exception:
                pass
        return applied

    def flush_audio_queue(self):
        """Public: Flush the Python audio queue for immediate feedback after batched param changes."""
        try:
            while not self.audio_queue.empty():
                self.audio_queue.get_nowait()
        except Exception:
            pass
    
    def set_player_dsp_volume(self, player_id: str, volume: float) -> bool:
        """Set DSP volume for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'set_volume'):
                dsp.set_volume(volume)
                print(f"Updated DSP volume for {player_id}: {volume}")
                return True
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'set_volume'):
                        effect.set_volume(volume)
                        print(f"Updated DSP volume for {player_id}: {volume}")
                        return True
        return False
    
    def set_player_dsp_pan(self, player_id: str, pan: float) -> bool:
        """Set DSP panning for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return False
            
            player = self.audio_players[player_id]
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'set_pan'):
                dsp.set_pan(pan)
                print(f"Updated DSP pan for {player_id}: {pan}")
                return True
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'set_pan'):
                        effect.set_pan(pan)
                        print(f"Updated DSP pan for {player_id}: {pan}")
                        return True
        return False
    
    def get_player_dsp_parameters(self, player_id: str) -> dict:
        """Get DSP parameters for a player"""
        with self.players_lock:
            if player_id not in self.audio_players:
                return {}
            
            player = self.audio_players[player_id]
            dsp = player.get('dsp_effect')
            if dsp is not None and hasattr(dsp, 'get_parameters'):
                return dsp.get_parameters()
            if player['effect_chain']:
                for effect in player['effect_chain']:
                    if hasattr(effect, 'get_parameters'):
                        return effect.get_parameters()
        return {}

    def set_player_native_rate(self, player_id: str, rate: float) -> bool:
        """If a voice is in the native mixer (static/neutral path), set its playback rate (varispeed)."""
        if self._native is None:
            return False
        try:
            return self._native.set_rate(player_id, float(max(0.01, rate)))
        except Exception:
            return False

    def _service_native_streams(self):
        """When native_output_only is enabled, push processed chunks for streaming voices directly to native.
        Avoid building Python mix buffers/queue to reduce latency/CPU.
        """
        try:
            with self.players_lock:
                for player_id, player in self.audio_players.items():
                    if not player.get('is_playing'):
                        continue
                    audio_data = player['audio_data']
                    position = player['position']
                    dsp = player.get('dsp_effect')
                    effect_chain = player.get('effect_chain')
                    # Static neutral or no processing: ensure static native voice
                    if effect_chain is None and dsp is None:
                        if audio_data is None:
                            # Native generator voice, nothing to push; keep params in sync
                            try:
                                self._native.set_params(player_id, 0.0 if player.get('mute') else float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                            except Exception:
                                pass
                            continue
                        if player.get('native_id') is None:
                            try:
                                self._native.add_voice(player_id, audio_data, bool(player.get('loop', False)), float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                                player['native_id'] = player_id
                            except Exception:
                                pass
                        else:
                            try:
                                self._native.set_loop(player_id, bool(player.get('loop', False)))
                                eff_vol = 0.0 if player.get('mute') else float(player.get('volume', 1.0))
                                self._native.set_params(player_id, eff_vol, float(player.get('pan', 0.0)))
                            except Exception:
                                pass
                        # advance position to keep logical timeline
                        end_pos = position + self.buffer_size
                        player['position'] = end_pos % audio_data.shape[1]
                        continue
                    # Processed voice: push processed stereo to native streaming voice
                    end_pos = position + self.buffer_size
                    if end_pos <= audio_data.shape[1]:
                        chunk = audio_data[:, position:end_pos]
                    else:
                        first_len = audio_data.shape[1] - position
                        remaining = end_pos - audio_data.shape[1]
                        work = self.temp_chunk_buffer
                        work[:, :first_len] = audio_data[:, position:]
                        work[:, first_len:self.buffer_size] = audio_data[:, :remaining]
                        chunk = work[:, :self.buffer_size]
                    work = self.temp_chunk_buffer
                    if chunk.base is not work:
                        work[:, :self.buffer_size] = chunk
                    processed = work
                    if dsp is not None:
                        try:
                            mono_for_dsp = work[0] if work.ndim == 2 else work
                            processed_mono = dsp.process(mono_for_dsp, self.sample_rate)
                            if processed_mono.ndim == 1:
                                processed = np.stack([processed_mono, processed_mono])
                            elif processed_mono.ndim == 2 and processed_mono.shape[0] == 1:
                                processed = np.vstack([processed_mono, processed_mono])
                            else:
                                processed = processed_mono
                            np.clip(processed, -1.0, 1.0, out=processed)
                        except Exception as e:
                            print(f"DSP error(native stream): {e}")
                    if effect_chain is not None:
                        try:
                            pb_in = processed.T  # (frames, channels)
                            pb_out = player['effect_chain'].process(pb_in, self.sample_rate, buffer_size=self.buffer_size, reset=False)
                            if not isinstance(pb_out, np.ndarray):
                                pb_out = pb_in
                            # Ensure 2D
                            if pb_out.ndim == 1:
                                pb_out = np.stack([pb_out, pb_out], axis=1)
                            # Ensure at least 2 channels
                            if pb_out.ndim == 2 and pb_out.shape[1] == 1:
                                pb_out = np.repeat(pb_out, 2, axis=1)
                            # Enforce exact frame count
                            if pb_out.shape[0] != self.buffer_size:
                                if pb_out.shape[0] > self.buffer_size:
                                    pb_out = pb_out[:self.buffer_size, :]
                                else:
                                    pad = np.zeros((self.buffer_size - pb_out.shape[0], pb_out.shape[1]), dtype=pb_out.dtype)
                                    pb_out = np.concatenate([pb_out, pad], axis=0)
                            # Limit to 2 channels
                            if pb_out.shape[1] > 2:
                                pb_out = pb_out[:, :2]
                            elif pb_out.shape[1] < 2:
                                pad_ch = 2 - pb_out.shape[1]
                                pb_out = np.pad(pb_out, ((0,0),(0,pad_ch)), mode='constant')
                            processed = np.clip(pb_out.T.astype(np.float32, copy=False), -1.0, 1.0)
                        except Exception as e:
                            print(f"Effect error(native stream): {e}")
                    try:
                        if player.get('native_id') is None or player.get('native_stream', False) is False:
                            self._native.add_stream_voice(player_id, int(getattr(self, '_native_stream_capacity', 1 << 18)), float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                            player['native_id'] = player_id
                            player['native_stream'] = True
                        else:
                            eff_vol = 0.0 if player.get('mute') else float(player.get('volume', 1.0))
                            self._native.set_params(player_id, eff_vol, float(player.get('pan', 0.0)))
                        self._native.push_stereo(player_id, processed)
                        # Throttle reservoir to small target for responsiveness
                        st = self._native.get_stream_status(player_id) if hasattr(self._native, 'get_stream_status') else {}
                        cap = max(1, int(st.get('capacity', getattr(self, '_native_stream_capacity', 1 << 18))))
                        fill = int(st.get('fill', 0))
                        target = max(1, int(getattr(self, '_native_stream_target_blocks', 4))) * int(self.buffer_size)
                        if fill < target:
                            self._native.push_stereo(player_id, processed)
                        try:
                            print(f"FX_NATIVE_STREAM_PUSH id={player_id} fill={fill} cap={cap}")
                        except Exception:
                            pass
                        player['position'] = end_pos % audio_data.shape[1]
                    except Exception:
                        pass
        except Exception as e:
            print(f"_service_native_streams error: {e}")

    def _prefill_native_stream(self, player_id: str, blocks: int = 4):
        """Generate and push a few processed blocks to native streaming ring to build reservoir."""
        if blocks <= 0 or self._native is None:
            return
        try:
            with self.players_lock:
                player = self.audio_players.get(player_id)
                if player is None:
                    return
                audio_data = player['audio_data']
                position = player['position']
                dsp = player.get('dsp_effect')
                effect_chain = player.get('effect_chain')
                # Ensure native streaming voice exists
                if player.get('native_id') is None or player.get('native_stream', False) is False:
                    cap_req = int(self.buffer_size * max(16, int(getattr(self, '_native_stream_target_blocks', 4)) * 4))
                    cap = int(min(int(getattr(self, '_native_stream_capacity', 1 << 18)), cap_req))
                    self._native.add_stream_voice(player_id, cap, float(player.get('volume', 1.0)), float(player.get('pan', 0.0)))
                    player['native_id'] = player_id
                    player['native_stream'] = True
                    try:
                        if hasattr(self._native, 'set_src_rate'):
                            self._native.set_src_rate(player_id, float(self.sample_rate))
                    except Exception:
                        pass
                for _ in range(int(blocks)):
                    end_pos = position + self.buffer_size
                    if end_pos <= audio_data.shape[1]:
                        chunk = audio_data[:, position:end_pos]
                    else:
                        first_len = audio_data.shape[1] - position
                        remaining = end_pos - audio_data.shape[1]
                        work = self.temp_chunk_buffer
                        work[:, :first_len] = audio_data[:, position:]
                        work[:, first_len:self.buffer_size] = audio_data[:, :remaining]
                        chunk = work[:, :self.buffer_size]
                    work = self.temp_chunk_buffer
                    if chunk.base is not work:
                        work[:, :self.buffer_size] = chunk
                    processed = work
                    if dsp is not None:
                        try:
                            mono_for_dsp = work[0] if work.ndim == 2 else work
                            processed_mono = dsp.process(mono_for_dsp, self.sample_rate)
                            if processed_mono.ndim == 1:
                                processed = np.stack([processed_mono, processed_mono])
                            elif processed_mono.ndim == 2 and processed_mono.shape[0] == 1:
                                processed = np.vstack([processed_mono, processed_mono])
                            else:
                                processed = processed_mono
                            np.clip(processed, -1.0, 1.0, out=processed)
                        except Exception:
                            pass
                    if effect_chain is not None:
                        try:
                            pb_in = processed.T
                            pb_out = player['effect_chain'].process(pb_in, self.sample_rate, buffer_size=self.buffer_size, reset=False)
                            processed = np.clip(pb_out.T, -1.0, 1.0)
                        except Exception:
                            pass
                    try:
                        self._native.push_stereo(player_id, processed)
                    except Exception:
                        break
                    position = end_pos % audio_data.shape[1]
                player['position'] = position
        except Exception:
            pass

    def create_native_generator_player(self, player_id: str, kind: str, *, frequency_hz: float = 440.0, gain: float = 1.0, pan: float = 0.0) -> bool:
        """Create a native generator (sine/triangle/square/white/pink/brown) as a looped static voice."""
        if self._native is None:
            try:
                print(f"ERROR: native mixer not enabled; cannot create native generator for {player_id} kind={kind}")
            except Exception:
                pass
            return False
        try:
            # Create native voice bound to player_id
            kind_l = str(kind).lower()
            ok = False
            if kind_l == 'sine':
                ok = self._native.add_sine_voice(player_id, float(frequency_hz), float(gain), float(pan), 2.0)
            elif kind_l == 'triangle':
                ok = self._native.add_triangle_voice(player_id, float(frequency_hz), float(gain), float(pan), 2.0)
            elif kind_l == 'square':
                ok = self._native.add_square_voice(player_id, float(frequency_hz), float(gain), float(pan), 2.0)
            elif kind_l == 'white' or kind_l == 'white_noise':
                ok = self._native.add_white_noise_voice(player_id, float(gain), float(pan), 2.0)
            elif kind_l == 'pink' or kind_l == 'pink_noise':
                ok = self._native.add_pink_noise_voice(player_id, float(gain), float(pan), 2.0)
            elif kind_l == 'brown' or kind_l == 'brown_noise' or kind_l == 'red':
                ok = self._native.add_brown_noise_voice(player_id, float(gain), float(pan), 2.0)
            else:
                ok = False
            if not ok:
                try:
                    print(f"ERROR: native generator creation failed for {player_id} kind={kind}")
                except Exception:
                    pass
                return False
            # Register minimal player entry so control APIs work
            with self.players_lock:
                self.audio_players[player_id] = {
                    'audio_data': None,
                    'effect_chain': None,
                    'dsp_effect': None,
                    'is_playing': True,
                    'volume': 1.0,
                    'pan': float(pan),
                    'position': 0,
                    'loop': True,
                    'mute': False,
                    'native_id': player_id,
                    'native_stream': False,
                }
            # Ensure native device is running or Python fallback is active
            if not self.is_playing:
                self.start_audio_stream()
            # Apply initial params
            try:
                self._native.set_params(player_id, float(1.0), float(pan))
            except Exception:
                pass
            return True
        except Exception as e:
            try:
                print(f"ERROR: create_native_generator_player exception: {e}")
            except Exception:
                pass
            return False

    def set_player_native_dsp(self, player_id: str, semitones: float, tempo: float) -> bool:
        """Set native DSP pitch/tempo for streaming/static native voices (smoothed in C++)."""
        if self._native is None:
            return False
        try:
            return self._native.set_dsp_params(player_id, float(semitones), float(tempo))
        except Exception:
            return False

    def set_player_native_src_rate(self, player_id: str, src_rate: float) -> bool:
        """Set native src sample rate for a static/streaming voice to ensure correct base scaling."""
        if self._native is None:
            return False
        try:
            if hasattr(self._native, 'set_src_rate'):
                return bool(self._native.set_src_rate(player_id, float(max(1.0, src_rate))))
        except Exception:
            return False
        return False

# Global engine instance
_engine = None

def init_audio_engine(sample_rate: int = 48000, buffer_size: int = 192) -> bool:
    """Initialize the global engine"""
    global _engine
    _engine = UltraOptimizedEngine(sample_rate, buffer_size)
    return _engine.is_initialized

def create_audio_player(player_id: str, audio_data: np.ndarray, 
                       effects: Optional[List] = None) -> bool:
    """Create an audio player"""
    if _engine:
        return _engine.create_audio_player(player_id, audio_data, effects)
    return False

def play_audio(player_id: str, loop: bool = False, 
               volume: float = 1.0, pan: float = 0.0) -> bool:
    """Play audio"""
    if _engine:
        return _engine.play_audio(player_id, loop, volume, pan)
    return False

def stop_audio(player_id: str) -> bool:
    """Stop audio"""
    if _engine:
        return _engine.stop_audio(player_id)
    return False

def update_player_volume(player_id: str, volume: float) -> bool:
    """Update volume"""
    if _engine:
        return _engine.update_player_volume(player_id, volume)
    return False

def update_player_pan(player_id: str, pan: float) -> bool:
    """Update pan"""
    if _engine:
        return _engine.update_player_pan(player_id, pan)
    return False

def update_player_mute(self, player_id: str, mute: bool) -> bool:
    """Mute/unmute player without changing its logical volume."""
    with self.players_lock:
        if player_id in self.audio_players:
            self.audio_players[player_id]['mute'] = bool(mute)
            if self._native is not None:
                try:
                    eff_vol = 0.0 if mute else float(self.audio_players[player_id].get('volume', 1.0))
                    self._native.set_params(player_id, eff_vol, self.audio_players[player_id].get('pan', 0.0))
                except Exception:
                    pass
            return True
    return False

def start_audio_stream(self):
    """Start audio stream and mixing thread"""
    if self.is_playing:
        return
    
    try:
        # Start mixing thread
        self.mixing_active = True
        self.mixing_thread = threading.Thread(target=self._mixing_loop, daemon=True)
        self.mixing_thread.start()
        
        if self._native is not None and getattr(self, '_native_output_only', False):
            # Start native WASAPI device
            try:
                exclusive = bool(getattr(self, '_wasapi_exclusive', False))
                started = self._native.start_device(exclusive) if hasattr(self._native, 'start_device') else False
                if not started and exclusive:
                    # Retry in shared mode before giving up (still native-only)
                    started = self._native.start_device(False)
                if started:
                    self.is_playing = True
                    print("Audio stream started (native)")
                    return
                else:
                    raise RuntimeError("native device failed")
            except Exception:
                if getattr(self, '_strict_native_only', False):
                    # Strict native-only: do not fallback to Python
                    print("ERROR: native device failed and strict_native_only is enabled; audio disabled.")
                    self.is_playing = False
                    return
                # Fallback to sounddevice (Python path)
                try:
                    self._native_output_only = False
                except Exception:
                    pass
                self.audio_stream = sd.OutputStream(
                    samplerate=self.sample_rate,
                    channels=2,
                    blocksize=self.buffer_size,
                    callback=self._ultra_minimal_callback,
                    dtype=np.float32,
                    extra_settings=self._sd_extra_settings
                )
                self.audio_stream.start()
                self.is_playing = True
                print("Audio stream started (python)")
                return
        
        # Start audio stream (Python)
        self.audio_stream = sd.OutputStream(
            samplerate=self.sample_rate,
            channels=2,
            blocksize=self.buffer_size,
            callback=self._ultra_minimal_callback,
            dtype=np.float32,
            extra_settings=self._sd_extra_settings
        )
        self.audio_stream.start()
        self.is_playing = True
        print("Audio stream started")
    except Exception as e:
        print(f"Failed to start stream: {e}")

def stop_audio_stream(self):
    """Stop audio stream"""
    self.mixing_active = False
    if self.mixing_thread:
        self.mixing_thread.join(timeout=1.0)
    
    if self._native is not None and getattr(self, '_native_output_only', False):
        try:
            if hasattr(self._native, 'stop_device'):
                self._native.stop_device()
        except Exception:
            pass
    
    if self.audio_stream:
        self.audio_stream.stop()
        self.audio_stream.close()
        self.audio_stream = None
    self.is_playing = False
    print("Audio stream stopped")

def cleanup_audio_engine():
    """Cleanup the engine"""
    global _engine
    if _engine:
        _engine.cleanup()
        _engine = None

# DSP Integration Methods
def create_dsp_player(player_id: str, audio_data: np.ndarray, 
                     processing_mode: str = 'granular', pitch_shift: float = 0.0, 
                     tempo_factor: float = 1.0, volume: float = 1.0, pan: float = 0.0) -> bool:
    """Create a player with DSP processing"""
    if _engine:
        return _engine.create_dsp_player(player_id, audio_data, processing_mode, 
                                       pitch_shift, tempo_factor, volume, pan)
    return False

def set_player_dsp_mode(player_id: str, mode: str) -> bool:
    """Change DSP processing mode for a player"""
    if _engine:
        return _engine.set_player_dsp_mode(player_id, mode)
    return False

def set_player_pitch_shift(player_id: str, semitones: float) -> bool:
    """Set pitch shift for a player"""
    if _engine:
        return _engine.set_player_pitch_shift(player_id, semitones)
    return False

def set_player_tempo_factor(player_id: str, factor: float) -> bool:
    """Set tempo factor for a player"""
    if _engine:
        return _engine.set_player_tempo_factor(player_id, factor)
    return False

def set_player_dsp_volume(player_id: str, volume: float) -> bool:
    """Set DSP volume for a player"""
    if _engine:
        return _engine.set_player_dsp_volume(player_id, volume)
    return False

def set_player_dsp_pan(player_id: str, pan: float) -> bool:
    """Set DSP panning for a player"""
    if _engine:
        return _engine.set_player_dsp_pan(player_id, pan)
    return False

def get_player_dsp_parameters(player_id: str) -> dict:
    """Get DSP parameters for a player"""
    if _engine:
        return _engine.get_player_dsp_parameters(player_id)
    return {}


def set_player_dsp_params(player_id: str, pitch_shift: Optional[float] = None, tempo_factor: Optional[float] = None, flush: bool = True) -> bool:
    """Atomically update DSP params (pitch/tempo) for a player."""
    if _engine:
        return _engine.set_player_dsp_params(player_id, pitch_shift=pitch_shift, tempo_factor=tempo_factor, flush=flush)
    return False


def flush_audio_queue():
    """Flush Python audio queue to hear recent changes immediately."""
    if _engine:
        _engine.flush_audio_queue()
        return True
    return False

def set_player_native_rate(player_id: str, rate: float) -> bool:
    """Set native varispeed rate for a voice when applicable."""
    if _engine:
        return _engine.set_player_native_rate(player_id, rate)
    return False

def set_player_native_dsp(player_id: str, semitones: float, tempo: float) -> bool:
    """Set native DSP pitch/tempo for a voice."""
    if _engine:
        return _engine.set_player_native_dsp(player_id, semitones, tempo)
    return False

def set_player_native_src_rate(player_id: str, src_rate: float) -> bool:
    """Set native src sample rate for a voice."""
    if _engine:
        return _engine.set_player_native_src_rate(player_id, src_rate)
    return False