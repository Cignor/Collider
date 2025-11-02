"""
Audio Processing Utilities

This module contains utilities for audio processing, channel conversion, and sample rate handling.
"""

from .channel_utils import (
    PedalboardChannelUtils,
    stereo_to_mono, mono_to_stereo, ensure_stereo, ensure_mono,
    apply_panning, split_stereo, ensure_multichannel,
    generate_tone, generate_silence, validate_audio_array
)
from .sample_rate_utils import (
    smart_audio_loader, detect_sample_rate, resample_audio,
    normalize_audio, get_audio_info
)

__all__ = [
    'PedalboardChannelUtils',
    'stereo_to_mono',
    'mono_to_stereo', 
    'ensure_stereo',
    'ensure_mono',
    'apply_panning',
    'split_stereo',
    'ensure_multichannel',
    'generate_tone',
    'generate_silence',
    'validate_audio_array',
    'smart_audio_loader',
    'detect_sample_rate',
    'resample_audio',
    'normalize_audio',
    'get_audio_info'
]
