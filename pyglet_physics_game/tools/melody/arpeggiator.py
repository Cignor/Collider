"""
Arpeggiator Melody Effect
Generates arpeggiated note sequences from chords
"""

class Arpeggiator:
    """Generates arpeggiated note sequences from chords"""
    
    def __init__(self):
        self.name = "Arpeggiator"
        self.category = "melody"
        
        # Default parameters
        self.root_note = 60        # MIDI note (C4)
        self.chord_type = "major"  # Chord type
        self.pattern = "up"        # Arpeggio pattern
        self.octave_range = 2      # Number of octaves
        self.tempo = 120           # BPM
        self.velocity = 80         # Note velocity (0-127)
        self.duration = 0.2        # Note duration in seconds
        
        # Chord definitions
        self.chord_types = {
            "major": [0, 4, 7],           # Root, major third, fifth
            "minor": [0, 3, 7],           # Root, minor third, fifth
            "augmented": [0, 4, 8],       # Root, major third, augmented fifth
            "diminished": [0, 3, 6],      # Root, minor third, diminished fifth
            "major7": [0, 4, 7, 11],      # Root, major third, fifth, major seventh
            "minor7": [0, 3, 7, 10],      # Root, minor third, fifth, minor seventh
            "dominant7": [0, 4, 7, 10]    # Root, major third, fifth, minor seventh
        }
        
        # Arpeggio patterns
        self.patterns = {
            "up": [0, 1, 2, 3],           # Ascending
            "down": [3, 2, 1, 0],         # Descending
            "updown": [0, 1, 2, 3, 2, 1], # Up then down
            "downup": [3, 2, 1, 0, 1, 2], # Down then up
            "random": [0, 2, 1, 3],       # Random order
            "chord": [0, 1, 2, 3, 0, 1, 2, 3]  # Chord-like
        }
    
    def generate_arpeggio(self):
        """Generate an arpeggiated sequence"""
        if self.chord_type not in self.chord_types:
            self.chord_type = "major"
            
        if self.pattern not in self.patterns:
            self.pattern = "up"
            
        chord_intervals = self.chord_types[self.chord_type]
        pattern_indices = self.patterns[self.pattern]
        notes = []
        
        # Generate notes for each octave
        for octave in range(self.octave_range):
            for pattern_index in pattern_indices:
                if pattern_index < len(chord_intervals):
                    interval = chord_intervals[pattern_index]
                    note = self.root_note + (octave * 12) + interval
                    
                    notes.append({
                        "note": note,
                        "velocity": self.velocity,
                        "duration": self.duration,
                        "time": len(notes) * (60.0 / self.tempo / 4)  # Quarter note timing
                    })
        
        return notes
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Arpeggiator",
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
                    "id": "chord_type",
                    "label": "Chord Type",
                    "type": "choice",
                    "choices": ["major", "minor", "augmented", "diminished", "major7", "minor7", "dominant7"],
                    "value": self.chord_type,
                    "unit": ""
                },
                {
                    "id": "pattern",
                    "label": "Pattern",
                    "type": "choice",
                    "choices": ["up", "down", "updown", "downup", "random", "chord"],
                    "value": self.pattern,
                    "unit": ""
                },
                {
                    "id": "octave_range",
                    "label": "Octave Range",
                    "type": "int",
                    "min": 1,
                    "max": 4,
                    "step": 1,
                    "value": self.octave_range,
                    "unit": "octaves"
                },
                {
                    "id": "tempo",
                    "label": "Tempo",
                    "type": "int",
                    "min": 60,
                    "max": 200,
                    "step": 5,
                    "value": self.tempo,
                    "unit": "BPM"
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
                    "max": 1.0,
                    "step": 0.1,
                    "value": self.duration,
                    "unit": "s"
                }
            ]
        }
