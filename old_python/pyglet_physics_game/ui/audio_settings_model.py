from __future__ import annotations

from dataclasses import dataclass, field
from typing import List


@dataclass
class AudioSettingsModel:
    input_devices: List[str] = field(default_factory=list)
    output_devices: List[str] = field(default_factory=list)
    midi_devices: List[str] = field(default_factory=list)
    current_input: str = ""
    current_output: str = ""
    sample_rate: float = 0.0
    buffer_size: int = 0
    master_gain: float = 0.7
    cpu_load: float = 0.0

    def apply_message(self, address: str, args: tuple) -> None:
        try:
            if address == "/info/audioDeviceList":
                dev_type = str(args[0]) if len(args) > 0 else ""
                names = [str(a) for a in args[1:]] if len(args) > 1 else []
                if dev_type == "input":
                    self.input_devices = names
                elif dev_type == "output":
                    self.output_devices = names
            elif address == "/info/midiDeviceList":
                self.midi_devices = [str(a) for a in args] if args else []
            elif address == "/info/currentSettings":
                self.current_input = str(args[0]) if len(args) > 0 else ""
                self.current_output = str(args[1]) if len(args) > 1 else ""
                self.sample_rate = float(args[2]) if len(args) > 2 else 0.0
                try:
                    self.buffer_size = int(args[3]) if len(args) > 3 else 0
                except Exception:
                    self.buffer_size = 0
            elif address == "/info/masterGain":
                self.master_gain = float(args[0]) if len(args) > 0 else self.master_gain
            elif address == "/info/cpuLoad":
                try:
                    self.cpu_load = max(0.0, min(1.0, float(args[0]) if len(args) > 0 else 0.0))
                except Exception:
                    self.cpu_load = 0.0
        except Exception:
            pass


