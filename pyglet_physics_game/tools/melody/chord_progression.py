"""
Chord Progression Melody Effect
Generates chord progressions and harmonic sequences
"""

class ChordProgression:
    """Generates chord progressions and harmonic sequences"""
    
    def __init__(self):
        self.name = "Chord Progression"
        self.category = "melody"
        
        # Default parameters
        self.root_note = 60        # MIDI note (C4)
        self.progression_type = "I-V-vi-IV"  # Chord progression
        self.chord_voicing = "triad"         # Chord voicing type
        self.velocity = 80         # Note velocity (0-127)
        self.duration = 1.0        # Chord duration in seconds
        
        # Chord definitions
        self.chord_types = {
            "triad": [0, 2, 4],           # Root, third, fifth
            "seventh": [0, 2, 4, 6],      # Root, third, fifth, seventh
            "ninth": [0, 2, 4, 6, 8],     # Root, third, fifth, seventh, ninth
            "sus2": [0, 1, 4],            # Root, second, fifth
            "sus4": [0, 3, 4]             # Root, fourth, fifth
        }
        
        # Scale intervals for chord building
        self.scale_intervals = [0, 2, 4, 5, 7, 9, 11]  # Major scale
        
        # Common progressions
        self.progressions = {
            "I-V-vi-IV": [0, 4, 5, 3],      # C-G-Am-F
            "ii-V-I": [1, 4, 0],             # Dm-G-C
            "vi-IV-I-V": [5, 3, 0, 4],      # Am-F-C-G
            "I-vi-IV-V": [0, 5, 3, 4],      # C-Am-F-G
            "circle_of_fifths": [0, 4, 1, 5, 2, 6, 3]  # C-G-D-A-E-B-F
        }
    
    def generate_progression(self):
        """Generate a chord progression"""
        if self.progression_type not in self.progressions:
            self.progression_type = "I-V-vi-IV"
            
        if self.chord_voicing not in self.chord_types:
            self.chord_voicing = "triad"
            
        progression_degrees = self.progressions[self.progression_type]
        chord_voicing = self.chord_types[self.chord_voicing]
        chords = []
        
        for degree in progression_degrees:
            chord_notes = []
            root_note = self.root_note + self.scale_intervals[degree]
            
            for interval in chord_voicing:
                note = root_note + interval
                chord_notes.append({
                    "note": note,
                    "velocity": self.velocity,
                    "duration": self.duration
                })
            
            chords.append({
                "chord": chord_notes,
                "root": root_note,
                "degree": degree
            })
        
        return chords
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Chord Progression",
            "type": "melody_effect",
            "params": [
                {
                    "id": "root_note",
                    "label": "Root Note",
                    "type": "int",
                    "min": 0,
                    "max": 127,
                    "step": 1,
                    "value": self.root_note,
                    "unit": "MIDI"
                },
                {
                    "id": "progression_type",
                    "label": "Progression",
                    "type": "choice",
                    "choices": ["I-V-vi-IV", "ii-V-I", "vi-IV-I-V", "I-vi-IV-V", "circle_of_fifths"],
                    "value": self.progression_type,
                    "unit": ""
                },
                {
                    "id": "chord_voicing",
                    "label": "Voicing",
                    "type": "choice",
                    "choices": ["triad", "seventh", "ninth", "sus2", "sus4"],
                    "value": self.chord_voicing,
                    "unit": ""
                },
                {
                    "id": "velocity",
                    "label": "Velocity",
                    "type": "int",
                    "min": 1,
                    "max": 127,
                    "step": 1,
                    "value": self.velocity,
                    "unit": ""
                },
                {
                    "id": "duration",
                    "label": "Duration",
                    "type": "float",
                    "min": 0.5,
                    "max": 4.0,
                    "step": 0.1,
                    "value": self.duration,
                    "unit": "s"
                }
            ]
        }
