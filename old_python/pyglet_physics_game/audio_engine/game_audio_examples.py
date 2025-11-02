"""
Game Audio Examples

This module provides practical examples of using Pedalboard effects
in the context of the physics-audio game.
"""

import numpy as np
import pedalboard
from .filters import FilterEffects
from .dynamics import DynamicsEffects
from .modulation import ModulationEffects
from .time_based import TimeBasedEffects
from .distortion import DistortionEffects
from .utility import UtilityEffects
from .compression import CompressionEffects
from .spatial import SpatialEffects
import sys
import os
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from .processing.channel_utils import PedalboardChannelUtils

class GameAudioExamples:
    """
    Practical examples of using Pedalboard effects in the game context.
    """
    
    @staticmethod
    def bullet_sound_generation(frequency: float, material: str = "metal",
                               distance: float = 1.0, velocity: float = 1.0,
                               sample_rate: int = 44100) -> np.ndarray:
        """
        Generate bullet sound based on physics properties.
        
        Args:
            frequency: Base frequency of the bullet
            material: Material type ("metal", "wood", "glass", "plastic")
            distance: Distance from listener (0.1 to 10.0)
            velocity: Bullet velocity (0.1 to 2.0)
            sample_rate: Sample rate in Hz
            
        Returns:
            Generated bullet sound
        """
        # Create base tone
        duration = 0.5 / velocity  # Faster bullets = shorter sound
        tone = PedalboardChannelUtils.create_tone(frequency, duration, sample_rate)
        
        # Apply material-based filtering
        if material == "metal":
            tone = FilterEffects.lowpass_filter(tone, frequency * 2, sample_rate)
            tone = DistortionEffects.overdrive(tone, 8.0, sample_rate)
        elif material == "wood":
            tone = FilterEffects.lowpass_filter(tone, frequency * 1.5, sample_rate)
            tone = FilterEffects.peak_filter(tone, frequency * 0.8, 2.0, -3.0, sample_rate)
        elif material == "glass":
            tone = FilterEffects.highpass_filter(tone, frequency * 0.5, sample_rate)
            tone = FilterEffects.peak_filter(tone, frequency * 2, 1.0, 6.0, sample_rate)
        elif material == "plastic":
            tone = FilterEffects.bandpass_filter(tone, frequency, 0.8, sample_rate)
            tone = DistortionEffects.bitcrush(tone, 12.0, sample_rate)
        
        # Apply distance-based effects
        if distance > 1.0:
            # Distance attenuation
            attenuation = 1.0 / (1.0 + distance * 0.3)
            tone = UtilityEffects.gain(tone, 20 * np.log10(attenuation), sample_rate)
            
            # High frequency rolloff for distance
            tone = FilterEffects.lowpass_filter(tone, 2000.0 / distance, sample_rate)
        
        # Add velocity-based modulation
        if velocity > 1.0:
            # Fast bullets get doppler-like pitch modulation
            tone = ModulationEffects.pitch_shift(tone, velocity * 2, sample_rate)
        
        return tone
    
    @staticmethod
    def spatial_audio_positioning(audio: np.ndarray, x: float, y: float,
                                 listener_x: float = 0.0, listener_y: float = 0.0,
                                 sample_rate: int = 44100) -> np.ndarray:
        """
        Position audio in 2D space relative to listener.
        
        Args:
            audio: Input audio array
            x: Source X position
            y: Source Y position
            listener_x: Listener X position
            listener_y: Listener Y position
            sample_rate: Sample rate in Hz
            
        Returns:
            Spatially positioned audio
        """
        # Calculate relative position
        rel_x = x - listener_x
        rel_y = y - listener_y
        
        # Calculate distance and angle
        distance = np.sqrt(rel_x**2 + rel_y**2)
        angle = np.arctan2(rel_y, rel_x)
        
        # Convert angle to pan (-1 to 1)
        pan = np.sin(angle)
        
        # Apply spatial positioning
        positioned = SpatialEffects.pan_audio(audio, pan, sample_rate)
        
        # Apply distance effects
        if distance > 0.1:
            # Distance attenuation
            attenuation = 1.0 / (1.0 + distance * 0.2)
            positioned = UtilityEffects.gain(positioned, 20 * np.log10(attenuation), sample_rate)
            
            # High frequency rolloff
            positioned = FilterEffects.lowpass_filter(positioned, 5000.0 / distance, sample_rate)
            
            # Add reverb for distant sounds
            if distance > 2.0:
                positioned = TimeBasedEffects.reverb(positioned, room_size=0.3, damping=0.7, wet_level=0.4, sample_rate=sample_rate)
        
        return positioned
    
    @staticmethod
    def material_based_effects(audio: np.ndarray, material: str = "default",
                              sample_rate: int = 44100) -> np.ndarray:
        """
        Apply material-based audio effects.
        
        Args:
            audio: Input audio array
            material: Material type
            sample_rate: Sample rate in Hz
            
        Returns:
            Material-processed audio
        """
        material_effects = {
            "metal": [
                FilterEffects.lowpass_filter(audio, 3000.0, sample_rate),
                DistortionEffects.overdrive(audio, 6.0, sample_rate)
            ],
            "wood": [
                FilterEffects.peak_filter(audio, 800.0, 2.0, -2.0, sample_rate),
                FilterEffects.lowpass_filter(audio, 2000.0, sample_rate)
            ],
            "glass": [
                FilterEffects.highpass_filter(audio, 1000.0, sample_rate),
                FilterEffects.peak_filter(audio, 3000.0, 1.0, 4.0, sample_rate)
            ],
            "plastic": [
                FilterEffects.bandpass_filter(audio, 1500.0, 1.0, sample_rate),
                DistortionEffects.bitcrush(audio, 10.0, sample_rate)
            ],
            "water": [
                FilterEffects.lowpass_filter(audio, 1500.0, sample_rate),
                ModulationEffects.chorus(audio, 0.5, 0.3, sample_rate)
            ],
            "stone": [
                FilterEffects.lowpass_filter(audio, 1000.0, sample_rate),
                DistortionEffects.distortion(audio, 12.0, sample_rate)
            ]
        }
        
        if material in material_effects:
            # Apply first effect
            processed = material_effects[material][0]
            # Apply second effect if available
            if len(material_effects[material]) > 1:
                processed = material_effects[material][1]
            return processed
        
        return audio
    
    @staticmethod
    def dynamic_effects_based_on_velocity(audio: np.ndarray, velocity: float,
                                        sample_rate: int = 44100) -> np.ndarray:
        """
        Apply dynamic effects based on velocity.
        
        Args:
            audio: Input audio array
            velocity: Velocity magnitude (0.0 to 2.0)
            sample_rate: Sample rate in Hz
            
        Returns:
            Velocity-processed audio
        """
        if velocity > 1.5:
            # High velocity - add distortion and compression
            audio = DistortionEffects.fuzz(audio, 20.0, sample_rate)
            audio = DynamicsEffects.fast_compression(audio, -8.0, 8.0, 1.0, 20.0, sample_rate)
        elif velocity > 1.0:
            # Medium velocity - add overdrive
            audio = DistortionEffects.overdrive(audio, 8.0, sample_rate)
            audio = DynamicsEffects.compressor(audio, -12.0, 4.0, 5.0, 50.0, sample_rate)
        else:
            # Low velocity - subtle compression
            audio = DynamicsEffects.compressor(audio, -20.0, 2.0, 10.0, 100.0, sample_rate)
        
        return audio
    
    @staticmethod
    def ambient_soundscape(base_audio: np.ndarray, environment: str = "forest",
                          sample_rate: int = 44100) -> np.ndarray:
        """
        Create ambient soundscape based on environment.
        
        Args:
            base_audio: Base audio array
            environment: Environment type
            sample_rate: Sample rate in Hz
            
        Returns:
            Ambient-processed audio
        """
        environment_effects = {
            "forest": [
                FilterEffects.lowpass_filter(base_audio, 2000.0, sample_rate),
                TimeBasedEffects.reverb(base_audio, room_size=0.8, damping=0.3, wet_level=0.4, sample_rate=sample_rate),
                ModulationEffects.chorus(base_audio, 0.2, 0.2, sample_rate)
            ],
            "cave": [
                FilterEffects.lowpass_filter(base_audio, 1500.0, sample_rate),
                TimeBasedEffects.reverb(base_audio, room_size=1.0, damping=0.1, wet_level=0.6, sample_rate=sample_rate),
                DistortionEffects.distortion(base_audio, 8.0, sample_rate)
            ],
            "underwater": [
                FilterEffects.bandpass_filter(base_audio, 800.0, 2.0, sample_rate),
                ModulationEffects.chorus(base_audio, 0.8, 0.5, sample_rate),
                TimeBasedEffects.delay(base_audio, 0.1, 0.3, 0.3, sample_rate)
            ],
            "city": [
                FilterEffects.highpass_filter(base_audio, 200.0, sample_rate),
                DistortionEffects.bitcrush(base_audio, 8.0, sample_rate),
                TimeBasedEffects.reverb(base_audio, room_size=0.3, damping=0.8, wet_level=0.2, sample_rate=sample_rate)
            ]
        }
        
        if environment in environment_effects:
            processed = base_audio
            for effect_func in environment_effects[environment]:
                processed = effect_func
            return processed
        
        return base_audio
    
    @staticmethod
    def collision_sound_generation(impact_force: float, material1: str, material2: str,
                                 sample_rate: int = 44100) -> np.ndarray:
        """
        Generate collision sound based on impact force and materials.
        
        Args:
            impact_force: Impact force magnitude (0.0 to 2.0)
            material1: First material
            material2: Second material
            sample_rate: Sample rate in Hz
            
        Returns:
            Generated collision sound
        """
        # Create impact sound based on force
        frequency = 200 + impact_force * 800  # Higher force = higher frequency
        duration = 0.1 + impact_force * 0.2   # Higher force = longer sound
        
        # Generate base impact sound
        impact = PedalboardChannelUtils.create_tone(frequency, duration, sample_rate)
        
        # Apply material-based effects
        impact = GameAudioExamples.material_based_effects(impact, material1, sample_rate)
        impact = GameAudioExamples.material_based_effects(impact, material2, sample_rate)
        
        # Apply force-based dynamics
        if impact_force > 1.0:
            impact = DistortionEffects.distortion(impact, impact_force * 15.0, sample_rate)
            impact = DynamicsEffects.compressor(impact, -10.0, 6.0, 1.0, 30.0, sample_rate)
        
        # Add reverb for impact
        impact = TimeBasedEffects.reverb(impact, room_size=0.4, damping=0.6, wet_level=0.3, sample_rate=sample_rate)
        
        return impact
    
    @staticmethod
    def create_audio_zone_effect(audio: np.ndarray, zone_type: str = "distortion",
                                intensity: float = 0.5, sample_rate: int = 44100) -> np.ndarray:
        """
        Create audio zone effects for painted zones in the game.
        
        Args:
            audio: Input audio array
            zone_type: Type of audio zone
            intensity: Effect intensity (0.0 to 1.0)
            sample_rate: Sample rate in Hz
            
        Returns:
            Zone-processed audio
        """
        zone_effects = {
            "distortion": DistortionEffects.distortion(audio, intensity * 30.0, sample_rate),
            "reverb": TimeBasedEffects.reverb(audio, room_size=intensity, damping=0.5, wet_level=intensity * 0.5, sample_rate=sample_rate),
            "chorus": ModulationEffects.chorus(audio, intensity * 2.0, intensity, sample_rate),
            "delay": TimeBasedEffects.delay(audio, intensity * 0.5, intensity * 0.5, intensity, sample_rate),
            "lowpass": FilterEffects.lowpass_filter(audio, 2000.0 * (1.0 - intensity), sample_rate),
            "highpass": FilterEffects.highpass_filter(audio, 500.0 * intensity, sample_rate),
            "pitch_shift": ModulationEffects.pitch_shift(audio, intensity * 12.0, sample_rate),
            "bitcrush": DistortionEffects.bitcrush(audio, 16.0 - intensity * 12.0, sample_rate)
        }
        
        if zone_type in zone_effects:
            return zone_effects[zone_type]
        
        return audio
