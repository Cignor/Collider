"""
Audio Engine (package alias)

This package name replaces the former tools.audio_effects.
It prefers local modules (after the move), but falls back to the
old tools.audio_effects package to maintain compatibility during migration.
"""

from .core.audio_engine import UltraOptimizedEngine
from .core.effect_processor import IntegratedAudioEngine, EffectsLibrary, EffectPreset
from .processing.sample_rate_utils import (
    smart_audio_loader, detect_sample_rate, resample_audio,
    normalize_audio, get_audio_info,
)
from .processing.python_stretch_wrapper import (
    create_signalsmith_stretch_dsp,
)
from .processing.channel_utils import (
    PedalboardChannelUtils,
    stereo_to_mono, mono_to_stereo, ensure_stereo, ensure_mono,
    apply_panning, split_stereo, ensure_multichannel,
    generate_tone, generate_silence, validate_audio_array,
)

__all__ = [
    # Core engine
    'UltraOptimizedEngine', 'IntegratedAudioEngine', 'EffectsLibrary', 'EffectPreset',
    # Utilities
    'smart_audio_loader', 'detect_sample_rate', 'resample_audio', 'normalize_audio', 'get_audio_info',
    'create_signalsmith_stretch_dsp',
    'PedalboardChannelUtils', 'stereo_to_mono', 'mono_to_stereo', 'ensure_stereo', 'ensure_mono',
    'apply_panning', 'split_stereo', 'ensure_multichannel', 'generate_tone', 'generate_silence', 'validate_audio_array',
]
