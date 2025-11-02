#!/usr/bin/env python3
"""
DSP Integration Example

This example demonstrates how to use the integrated DSP system
with the audio effects engine.
"""

import sys
import os
import numpy as np
import time

# Add paths for imports
sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

from tools.audio_effects.core.audio_engine import (
    init_audio_engine, create_dsp_player, play_audio, stop_audio,
    set_player_dsp_mode, set_player_pitch_shift, set_player_tempo_factor,
    set_player_dsp_volume, set_player_dsp_pan, get_player_dsp_parameters,
    cleanup_audio_engine
)

from tools.audio_effects.core.effect_processor import IntegratedAudioEngine

def create_test_audio(duration: float = 3.0, sample_rate: int = 48000) -> np.ndarray:
    """Create a musical test audio signal"""
    t = np.linspace(0, duration, int(sample_rate * duration))
    
    # Create a musical chord progression
    # C major chord: C4, E4, G4
    c4_freq = 261.63  # C4
    e4_freq = 329.63  # E4
    g4_freq = 392.00  # G4
    
    signal = (np.sin(2 * np.pi * c4_freq * t) +
              0.7 * np.sin(2 * np.pi * e4_freq * t) +
              0.5 * np.sin(2 * np.pi * g4_freq * t))
    
    # Add some envelope
    envelope = np.exp(-t * 0.3)  # Gentle decay
    signal *= envelope
    
    # Normalize
    signal = signal / np.max(np.abs(signal)) * 0.6
    
    return signal.astype(np.float32)

def example_basic_dsp_usage():
    """Example 1: Basic DSP usage with audio engine"""
    print("\n🎵 Example 1: Basic DSP Usage")
    print("-" * 40)
    
    try:
        # Initialize audio engine
        print("Initializing audio engine...")
        success = init_audio_engine(sample_rate=48000, buffer_size=192)
        if not success:
            print("❌ Failed to initialize audio engine")
            return False
        print("✅ Audio engine initialized")
        
        # Create test audio
        print("Creating test audio...")
        test_audio = create_test_audio(duration=2.0)
        print(f"✅ Test audio created: {len(test_audio)} samples")
        
        # Create DSP player with granular synthesis
        print("Creating DSP player (granular synthesis)...")
        success = create_dsp_player(
            player_id="music_player",
            audio_data=test_audio,
            processing_mode="granular",
            pitch_shift=7.0,  # Perfect fifth up
            tempo_factor=1.2,  # 20% faster
            volume=0.8,
            pan=0.0
        )
        if not success:
            print("❌ Failed to create DSP player")
            return False
        print("✅ DSP player created")
        
        # Play audio
        print("Playing audio with DSP processing...")
        success = play_audio("music_player", loop=True, volume=0.7, pan=0.0)
        if not success:
            print("❌ Failed to play audio")
            return False
        print("✅ Audio playing")
        
        # Let it play for a bit
        time.sleep(2.0)
        
        # Demonstrate real-time parameter changes
        print("\n🔄 Demonstrating real-time parameter changes...")
        
        # Change pitch
        print("Changing pitch: 0 → 7 → 12 → 0 semitones")
        for pitch in [0, 7, 12, 0]:
            set_player_pitch_shift("music_player", pitch)
            print(f"   Pitch: {pitch} semitones")
            time.sleep(1.0)
        
        # Change tempo
        print("Changing tempo: 0.8x → 1.0x → 1.5x → 1.0x")
        for tempo in [0.8, 1.0, 1.5, 1.0]:
            set_player_tempo_factor("music_player", tempo)
            print(f"   Tempo: {tempo}x")
            time.sleep(1.0)
        
        # Change panning
        print("Changing panning: center → left → right → center")
        for pan in [0.0, -1.0, 1.0, 0.0]:
            set_player_dsp_pan("music_player", pan)
            print(f"   Pan: {pan}")
            time.sleep(0.8)
        
        # Stop audio
        print("Stopping audio...")
        stop_audio("music_player")
        print("✅ Audio stopped")
        
        return True
        
    except Exception as e:
        print(f"❌ Error in basic DSP usage: {e}")
        return False
    finally:
        cleanup_audio_engine()

def example_processing_mode_comparison():
    """Example 2: Compare different processing modes"""
    print("\n🎵 Example 2: Processing Mode Comparison")
    print("-" * 40)
    
    try:
        # Initialize audio engine
        success = init_audio_engine(sample_rate=48000, buffer_size=192)
        if not success:
            return False
        
        # Create test audio
        test_audio = create_test_audio(duration=1.5)
        
        # Test different processing modes
        modes = [
            ("granular", "High-quality granular synthesis"),
            ("interpolation", "Fast interpolation-based processing"),
            ("stft_realtime", "Real-time STFT processing")
        ]
        
        for mode, description in modes:
            print(f"\n🔧 Testing {mode} mode: {description}")
            
            # Create player
            player_id = f"test_{mode}"
            success = create_dsp_player(
                player_id=player_id,
                audio_data=test_audio,
                processing_mode=mode,
                pitch_shift=12.0,  # Octave up
                tempo_factor=1.3,
                volume=0.6,
                pan=0.0
            )
            if not success:
                print(f"❌ Failed to create {mode} player")
                continue
            
            # Play audio
            play_audio(player_id, loop=True, volume=0.5)
            print(f"   Playing with {mode} processing...")
            time.sleep(2.0)
            
            # Stop audio
            stop_audio(player_id)
            print(f"   ✅ {mode} mode completed")
        
        return True
        
    except Exception as e:
        print(f"❌ Error in processing mode comparison: {e}")
        return False
    finally:
        cleanup_audio_engine()

def example_effect_processor_integration():
    """Example 3: Using DSP effects with the effect processor"""
    print("\n🎵 Example 3: Effect Processor Integration")
    print("-" * 40)
    
    try:
        # Create integrated audio engine
        print("Creating integrated audio engine...")
        engine = IntegratedAudioEngine(sample_rate=48000, buffer_size=192)
        print("✅ Integrated audio engine created")
        
        # List available DSP effects
        print("\nAvailable DSP effects:")
        effects = engine.list_available_effects()
        dsp_effects = [e for e in effects if e.startswith('dsp_')]
        for effect in dsp_effects:
            info = engine.get_effect_info(effect)
            print(f"   • {effect}: {info.get('description', 'No description')}")
        
        # Create test audio
        test_audio = create_test_audio(duration=2.0)
        
        # Test different DSP effects
        dsp_effects_to_test = ['dsp_granular', 'dsp_interpolation', 'dsp_stft_realtime']
        
        for effect_name in dsp_effects_to_test:
            print(f"\n🔧 Testing {effect_name}...")
            
            # Create player with DSP effect
            success = engine.create_audio_player(
                player_id=f"effect_test_{effect_name}",
                audio_data=test_audio,
                effect_name=effect_name,
                effect_power=0.8  # 80% effect strength
            )
            if not success:
                print(f"❌ Failed to create player with {effect_name}")
                continue
            
            # Play audio
            engine.play_audio(f"effect_test_{effect_name}", loop=True, volume=0.6)
            print(f"   Playing with {effect_name}...")
            time.sleep(2.0)
            
            # Stop audio
            engine.stop_audio(f"effect_test_{effect_name}")
            print(f"   ✅ {effect_name} completed")
        
        # Test effect power scaling
        print(f"\n🔧 Testing effect power scaling with dsp_custom...")
        for power in [0.2, 0.5, 0.8, 1.0]:
            print(f"   Effect power: {power}")
            engine.update_effect("effect_test_dsp_custom", "dsp_custom", power)
            engine.play_audio("effect_test_dsp_custom", loop=True, volume=0.4)
            time.sleep(1.0)
            engine.stop_audio("effect_test_dsp_custom")
        
        # Cleanup
        engine.cleanup()
        print("✅ Integrated audio engine cleaned up")
        
        return True
        
    except Exception as e:
        print(f"❌ Error in effect processor integration: {e}")
        return False

def example_game_integration():
    """Example 4: Game integration scenario"""
    print("\n🎵 Example 4: Game Integration Scenario")
    print("-" * 40)
    
    try:
        # Initialize audio engine
        success = init_audio_engine(sample_rate=48000, buffer_size=192)
        if not success:
            return False
        
        # Create different audio sources for game
        background_music = create_test_audio(duration=4.0)
        sound_effect = create_test_audio(duration=0.5) * 0.3  # Shorter, quieter
        
        # Background music with DSP
        print("Creating background music player...")
        success = create_dsp_player(
            player_id="background_music",
            audio_data=background_music,
            processing_mode="granular",
            pitch_shift=0.0,
            tempo_factor=1.0,
            volume=0.6,
            pan=0.0
        )
        if success:
            play_audio("background_music", loop=True, volume=0.5)
            print("✅ Background music playing")
        
        # Sound effect with different DSP settings
        print("Creating sound effect player...")
        success = create_dsp_player(
            player_id="sound_effect",
            audio_data=sound_effect,
            processing_mode="interpolation",  # Faster processing for effects
            pitch_shift=0.0,
            tempo_factor=1.0,
            volume=0.8,
            pan=0.0
        )
        
        # Simulate game events
        print("\n🎮 Simulating game events...")
        
        # Play sound effect multiple times with different pitches
        for i in range(5):
            pitch = i * 2  # 0, 2, 4, 6, 8 semitones
            set_player_pitch_shift("sound_effect", pitch)
            play_audio("sound_effect", loop=False, volume=0.7)
            print(f"   Sound effect played at {pitch} semitones")
            time.sleep(0.8)
        
        # Simulate collision - change background music
        print("\n💥 Simulating collision event...")
        set_player_dsp_mode("background_music", "stft_realtime")  # Switch to higher quality
        set_player_pitch_shift("background_music", -2.0)  # Lower pitch
        set_player_tempo_factor("background_music", 0.8)  # Slower tempo
        print("   Background music changed for collision effect")
        time.sleep(2.0)
        
        # Reset background music
        set_player_pitch_shift("background_music", 0.0)
        set_player_tempo_factor("background_music", 1.0)
        set_player_dsp_mode("background_music", "granular")
        print("   Background music reset")
        time.sleep(1.0)
        
        # Stop all audio
        stop_audio("background_music")
        stop_audio("sound_effect")
        print("✅ All audio stopped")
        
        return True
        
    except Exception as e:
        print(f"❌ Error in game integration: {e}")
        return False
    finally:
        cleanup_audio_engine()

def main():
    """Run all examples"""
    print("🚀 DSP Integration Examples")
    print("=" * 50)
    
    examples = [
        ("Basic DSP Usage", example_basic_dsp_usage),
        ("Processing Mode Comparison", example_processing_mode_comparison),
        ("Effect Processor Integration", example_effect_processor_integration),
        ("Game Integration Scenario", example_game_integration)
    ]
    
    for example_name, example_func in examples:
        print(f"\n{'='*20} {example_name} {'='*20}")
        try:
            success = example_func()
            if success:
                print(f"✅ {example_name} completed successfully")
            else:
                print(f"❌ {example_name} failed")
        except Exception as e:
            print(f"❌ {example_name} error: {e}")
        
        print("\n" + "-" * 50)
        time.sleep(1)  # Brief pause between examples
    
    print("\n🎉 All examples completed!")
    print("\nThe DSP integration provides:")
    print("• Three processing modes (granular, interpolation, stft_realtime)")
    print("• Real-time parameter control (pitch, tempo, volume, pan)")
    print("• Seamless integration with existing audio engine")
    print("• Effect processor compatibility")
    print("• Game-ready performance")

if __name__ == "__main__":
    main()
