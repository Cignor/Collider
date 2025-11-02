"""
Rhythm Pattern Melody Effect
Generates rhythmic patterns and timing sequences
"""

class RhythmPattern:
    """Generates rhythmic patterns and timing sequences"""
    
    def __init__(self):
        self.name = "Rhythm Pattern"
        self.category = "melody"
        
        # Default parameters
        self.pattern_type = "4/4"          # Time signature
        self.tempo = 120                   # BPM
        self.pattern_length = 16           # Pattern length in steps
        self.velocity = 80                 # Note velocity (0-127)
        self.swing = 0.0                   # Swing amount (0.0 = straight, 1.0 = full swing)
        
        # Pattern definitions
        self.patterns = {
            "4/4": [1, 0, 1, 0, 1, 0, 1, 0],                    # Basic 4/4
            "syncopated": [1, 0, 0, 1, 0, 1, 0, 0],             # Syncopated
            "shuffle": [1, 0, 0, 1, 0, 0, 1, 0],                # Shuffle
            "complex": [1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0],  # Complex
            "minimal": [1, 0, 0, 0, 1, 0, 0, 0],                # Minimal
            "polyrhythm": [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0]  # Polyrhythm
        }
    
    def generate_pattern(self):
        """Generate a rhythmic pattern"""
        if self.pattern_type not in self.patterns:
            self.pattern_type = "4/4"
            
        base_pattern = self.patterns[self.pattern_type]
        pattern = []
        
        # Extend or truncate pattern to desired length
        for i in range(self.pattern_length):
            step_value = base_pattern[i % len(base_pattern)]
            
            if step_value == 1:
                # Calculate timing with swing
                step_time = i / (self.pattern_length / 4)  # Convert to beats
                
                # Apply swing to off-beats
                if i % 2 == 1:  # Off-beat
                    step_time += self.swing * 0.1
                
                pattern.append({
                    "time": step_time,
                    "velocity": self.velocity,
                    "duration": 0.1
                })
        
        return pattern
    
    def get_parameters(self):
        """Return parameter configuration for UI"""
        return {
            "name": "Rhythm Pattern",
            "type": "melody_effect",
            "params": [
                {
                    "id": "pattern_type",
                    "label": "Pattern Type",
                    "type": "choice",
                    "choices": ["4/4", "syncopated", "shuffle", "complex", "minimal", "polyrhythm"],
                    "value": self.pattern_type,
                    "unit": ""
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
                    "id": "pattern_length",
                    "label": "Pattern Length",
                    "type": "int",
                    "min": 4,
                    "max": 32,
                    "step": 1,
                    "value": self.pattern_length,
                    "unit": "steps"
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
                    "id": "swing",
                    "label": "Swing",
                    "type": "float",
                    "min": 0.0,
                    "max": 1.0,
                    "step": 0.1,
                    "value": self.swing,
                    "unit": ""
                }
            ]
        }
