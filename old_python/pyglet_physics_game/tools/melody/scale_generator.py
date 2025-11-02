"""
Scale Generator Melody Effect
Generates musical scales and note sequences
"""

class ScaleGenerator:
    """Generates musical scales and note sequences"""
    
    def __init__(self):
        self.name = "Scale Generator"
        self.category = "melody"
        
        # Default parameters
        self.root_note = 60        # MIDI note (C4)
        self.scale_type = "major"  # Scale type
        self.octave_range = 2      # Number of octaves
        self.velocity = 80         # Note velocity (0-127)
        self.duration = 0.5        # Note duration in seconds
        
        # Scale definitions
        self.scales = {
            "major": [0, 2, 4, 5, 7, 9, 11],
            "minor": [0, 2, 3, 5, 7, 8, 10],
            "pentatonic": [0, 2, 4, 7, 9],
            "blues": [0, 3, 5, 6, 7, 10],
            "chromatic": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
        }
    
    def generate_scale(self):
        """Generate a scale sequence"""
        if self.scale_type not in self.scales:
            self.scale_type = "major"
            
        scale_intervals = self.scales[self.scale_type]
        notes = []
        
        for octave in range(self.octave_range):
            for interval in scale_intervals:
                note = self.root_note + (octave * 12) + interval
                notes.append({
                    "note": note,
                    "velocity": self.velocity,
                    "duration": self.duration
                })
        
        return notes
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Scale Generator",
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
                    "id": "scale_type",
                    "label": "Scale Type",
                    "type": "choice",
                    "choices": ["major", "minor", "pentatonic", "blues", "chromatic"],
                    "value": self.scale_type,
                    "unit": ""
                },
                {
                    "id": "octave_range",
                    "label": "Octave Range",
                    "type": "int",
                    "min": 1,
                    "max": 5,
                    "step": 1,
                    "value": self.octave_range,
                    "unit": "octaves"
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
                    "min": 0.1,
                    "max": 2.0,
                    "step": 0.1,
                    "value": self.duration,
                    "unit": "s"
                }
            ]
        }
